/**
 * @file segment_store.cpp
 * @brief 实现固定小端二进制 Segment、摘要校验和原子目录发布。
 */

#include "logtrace/indexing/segment_store.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "logtrace/common/sha256.h"
#include "logtrace/common/time_utils.h"
#include "logtrace/indexing/original_log_reader.h"
#include "logtrace/indexing/term_tokenizer.h"

namespace smt {
namespace logtrace {
namespace {

const std::uint32_t kSegmentFormatVersion = 1;
const char kFilesMagic[] = "LTFIL001";
const char kDocumentsMagic[] = "LTDOC001";
const char kPostingsMagic[] = "LTPST001";
const char kTermsMagic[] = "LTTRM001";
const std::uint64_t kManifestLimit = 1024ULL * 1024ULL;
const std::uint64_t kBinaryLimit = 2ULL * 1024ULL * 1024ULL * 1024ULL;
const std::uint32_t kStringLimit = 1024 * 1024;

std::string segmentName(std::uint64_t batch_id) { return "segment_" + std::to_string(batch_id); }

bool isDirectory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void ensureDirectory(const std::string& path) {
    if (::mkdir(path.c_str(), 0750) != 0 && errno != EEXIST) {
        throw std::runtime_error("cannot create segment directory: " +
                                 std::string(std::strerror(errno)));
    }
    if (!isDirectory(path)) {
        throw std::runtime_error("segment path is not a directory: " + path);
    }
}

void syncDirectory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("cannot open segment directory: " +
                                 std::string(std::strerror(errno)));
    }
    if (::fsync(fd) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::runtime_error("cannot fsync segment directory: " +
                                 std::string(std::strerror(saved_errno)));
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close segment directory: " +
                                 std::string(std::strerror(errno)));
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void writeDurableFile(const std::string& path, const std::string& content) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640);
    if (fd < 0) {
        throw std::runtime_error("cannot create segment file: " +
                                 std::string(std::strerror(errno)));
    }
    std::size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t count = ::write(fd, content.data() + offset, content.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            ::close(fd);
            throw std::runtime_error("cannot write segment file: " +
                                     std::string(std::strerror(saved_errno)));
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::fsync(fd) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::runtime_error("cannot fsync segment file: " +
                                 std::string(std::strerror(saved_errno)));
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close segment file: " + std::string(std::strerror(errno)));
    }
}

std::string readRegularFile(const std::string& path, std::uint64_t limit) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > limit) {
        throw std::runtime_error("segment file is missing, oversized or not regular: " + path);
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("cannot open segment file: " + std::string(std::strerror(errno)));
    }
    std::string content(static_cast<std::size_t>(info.st_size), '\0');
    std::size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t count = ::read(fd, &content[offset], content.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            ::close(fd);
            throw std::runtime_error("cannot read segment file: " +
                                     std::string(std::strerror(saved_errno)));
        }
        if (count == 0) {
            ::close(fd);
            throw std::runtime_error("segment file was truncated while reading");
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close segment file: " + std::string(std::strerror(errno)));
    }
    return content;
}

std::string sha256Hex(const std::string& content) {
    Sha256 sha256;
    sha256.update(content.data(), content.size());
    return sha256.finishHex();
}

bool validSha256(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!((*it >= '0' && *it <= '9') || (*it >= 'a' && *it <= 'f'))) {
            return false;
        }
    }
    return true;
}

void appendUint32(std::string* output, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output->push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void appendUint64(std::string* output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output->push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void appendInt64(std::string* output, std::int64_t value) {
    appendUint64(output, static_cast<std::uint64_t>(value));
}

void appendString(std::string* output, const std::string& value) {
    if (value.size() > kStringLimit) {
        throw std::runtime_error("segment string exceeds format limit");
    }
    appendUint32(output, static_cast<std::uint32_t>(value.size()));
    output->append(value);
}

void appendHeader(std::string* output, const char* magic, std::uint64_t count) {
    output->append(magic, 8);
    appendUint32(output, kSegmentFormatVersion);
    appendUint64(output, count);
}

class BinaryReader {
   public:
    explicit BinaryReader(const std::string& content) : content_(content), offset_(0) {}

    void requireMagic(const char* expected) {
        if (remaining() < 8 || content_.compare(offset_, 8, expected, 8) != 0) {
            throw std::runtime_error("segment binary magic is invalid");
        }
        offset_ += 8;
    }

    std::uint32_t readUint32() {
        require(4);
        std::uint32_t value = 0;
        for (unsigned int index = 0; index < 4; ++index) {
            value |=
                static_cast<std::uint32_t>(static_cast<unsigned char>(content_[offset_ + index]))
                << (index * 8);
        }
        offset_ += 4;
        return value;
    }

    std::uint64_t readUint64() {
        require(8);
        std::uint64_t value = 0;
        for (unsigned int index = 0; index < 8; ++index) {
            value |=
                static_cast<std::uint64_t>(static_cast<unsigned char>(content_[offset_ + index]))
                << (index * 8);
        }
        offset_ += 8;
        return value;
    }

    std::int64_t readInt64() { return static_cast<std::int64_t>(readUint64()); }

    std::string readString() {
        const std::uint32_t size = readUint32();
        if (size > kStringLimit) {
            throw std::runtime_error("segment string exceeds format limit");
        }
        require(size);
        std::string value = content_.substr(offset_, size);
        offset_ += size;
        return value;
    }

    std::string readBytes(std::size_t size) {
        require(size);
        std::string value = content_.substr(offset_, size);
        offset_ += size;
        return value;
    }

    void finish() const {
        if (offset_ != content_.size()) {
            throw std::runtime_error("segment binary contains trailing bytes");
        }
    }

   private:
    void require(std::size_t size) const {
        if (size > remaining()) {
            throw std::runtime_error("segment binary is truncated");
        }
    }

    std::size_t remaining() const { return content_.size() - offset_; }

    const std::string& content_;
    std::size_t offset_;
};

void appendSha256Bytes(std::string* output, const std::string& hex) {
    if (!validSha256(hex)) {
        throw std::runtime_error("segment source SHA-256 is invalid");
    }
    for (std::size_t index = 0; index < hex.size(); index += 2) {
        const std::string byte = hex.substr(index, 2);
        output->push_back(static_cast<char>(std::strtoul(byte.c_str(), nullptr, 16)));
    }
}

std::string bytesToSha256(const std::string& bytes) {
    static const char hex[] = "0123456789abcdef";
    if (bytes.size() != 32) {
        throw std::runtime_error("segment source SHA-256 bytes are invalid");
    }
    std::string result(64, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const unsigned char value = static_cast<unsigned char>(bytes[index]);
        result[index * 2] = hex[value >> 4];
        result[index * 2 + 1] = hex[value & 0x0F];
    }
    return result;
}

void removeFile(const std::string& path) {
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        throw std::runtime_error("cannot remove segment file: " +
                                 std::string(std::strerror(errno)));
    }
}

void removeSegmentDirectory(const std::string& path) {
    removeFile(path + "/manifest.json");
    removeFile(path + "/terms.bin");
    removeFile(path + "/postings.bin");
    removeFile(path + "/documents.bin");
    removeFile(path + "/files.bin");
    if (::rmdir(path.c_str()) != 0 && errno != ENOENT) {
        throw std::runtime_error("cannot remove segment directory: " +
                                 std::string(std::strerror(errno)));
    }
}

bool validSegmentName(const std::string& name) {
    const std::string prefix = "segment_";
    if (name.compare(0, prefix.size(), prefix) != 0 || name.size() == prefix.size()) {
        return false;
    }
    for (std::size_t index = prefix.size(); index < name.size(); ++index) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
    }
    return true;
}

std::vector<std::string> directoryEntries(const std::string& path) {
    DIR* directory = ::opendir(path.c_str());
    if (directory == nullptr) {
        throw std::runtime_error("cannot open segment directory: " +
                                 std::string(std::strerror(errno)));
    }
    std::vector<std::string> entries;
    errno = 0;
    while (dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            entries.push_back(name);
        }
        errno = 0;
    }
    const int read_errno = errno;
    if (::closedir(directory) != 0 && read_errno == 0) {
        throw std::runtime_error("cannot close segment directory: " +
                                 std::string(std::strerror(errno)));
    }
    if (read_errno != 0) {
        throw std::runtime_error("cannot read segment directory: " +
                                 std::string(std::strerror(read_errno)));
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

void requireExactSegmentFiles(const std::string& path) {
    static const std::vector<std::string> expected = {"documents.bin", "files.bin", "manifest.json",
                                                      "postings.bin", "terms.bin"};
    if (directoryEntries(path) != expected) {
        throw std::runtime_error("segment directory layout is invalid");
    }
}

void requireKeys(const nlohmann::json& value, const std::set<std::string>& expected) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw std::runtime_error("segment manifest object fields are invalid");
    }
    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (expected.count(it.key()) == 0) {
            throw std::runtime_error("segment manifest contains an unknown field");
        }
    }
}

struct ArtifactFact {
    std::uint64_t size;
    std::string sha256;
};

std::map<std::string, ArtifactFact> parseArtifactFacts(const nlohmann::json& value) {
    static const std::set<std::string> names = {"terms.bin", "postings.bin", "documents.bin",
                                                "files.bin"};
    requireKeys(value, names);
    std::map<std::string, ArtifactFact> facts;
    for (std::set<std::string>::const_iterator name = names.begin(); name != names.end(); ++name) {
        static const std::set<std::string> fact_keys = {"size", "sha256"};
        const nlohmann::json& fact = value.at(*name);
        requireKeys(fact, fact_keys);
        ArtifactFact parsed{fact.at("size").get<std::uint64_t>(),
                            fact.at("sha256").get<std::string>()};
        if (!validSha256(parsed.sha256) || parsed.size > kBinaryLimit) {
            throw std::runtime_error("segment manifest artifact fact is invalid");
        }
        facts[*name] = parsed;
    }
    return facts;
}

std::string serializeFiles(const std::vector<SegmentFileRecord>& files) {
    std::string output;
    appendHeader(&output, kFilesMagic, files.size());
    for (std::vector<SegmentFileRecord>::const_iterator file = files.begin(); file != files.end();
         ++file) {
        appendUint64(&output, file->archive_id);
        appendUint64(&output, file->file_size);
        appendSha256Bytes(&output, file->file_sha256);
        appendString(&output, file->relative_path);
    }
    return output;
}

std::string serializeDocuments(const std::vector<SegmentDocumentRecord>& documents) {
    std::string output;
    appendHeader(&output, kDocumentsMagic, documents.size());
    for (std::vector<SegmentDocumentRecord>::const_iterator document = documents.begin();
         document != documents.end(); ++document) {
        appendUint64(&output, document->doc_id);
        appendUint32(&output, document->local_id);
        appendUint32(&output, document->file_ordinal);
        appendUint64(&output, document->archive_id);
        appendUint64(&output, document->byte_offset);
        appendUint64(&output, document->byte_length);
        appendInt64(&output, document->occurred_at_ms);
        appendInt64(&output, document->archived_at_ms);
        appendUint32(&output, document->term_count);
        appendString(&output, document->line_id);
        appendString(&output, document->station_id);
        appendString(&output, document->device_id);
        appendString(&output, document->collector_id);
        appendString(&output, document->work_order);
        appendString(&output, document->product_sn);
        appendString(&output, document->source_type);
        appendString(&output, document->level);
        appendString(&output, document->module_name);
        appendString(&output, document->error_code);
        appendString(&output, document->event_name);
    }
    return output;
}

std::string serializePostings(const std::vector<SegmentPosting>& postings) {
    std::string output;
    appendHeader(&output, kPostingsMagic, postings.size());
    for (std::vector<SegmentPosting>::const_iterator posting = postings.begin();
         posting != postings.end(); ++posting) {
        appendUint32(&output, posting->local_id);
        appendUint32(&output, posting->term_frequency);
    }
    return output;
}

std::string serializeTerms(const std::vector<SegmentTermRecord>& terms) {
    std::string output;
    appendHeader(&output, kTermsMagic, terms.size());
    for (std::vector<SegmentTermRecord>::const_iterator term = terms.begin(); term != terms.end();
         ++term) {
        appendString(&output, term->term);
        appendUint32(&output, term->document_frequency);
        appendUint64(&output, term->posting_begin);
        appendUint32(&output, term->posting_count);
    }
    return output;
}

std::vector<SegmentFileRecord> loadFiles(const std::string& content, std::uint64_t expected_count) {
    BinaryReader reader(content);
    reader.requireMagic(kFilesMagic);
    if (reader.readUint32() != kSegmentFormatVersion || reader.readUint64() != expected_count) {
        throw std::runtime_error("files.bin header is inconsistent");
    }
    std::vector<SegmentFileRecord> files;
    std::set<std::uint64_t> archive_ids;
    for (std::uint64_t index = 0; index < expected_count; ++index) {
        SegmentFileRecord file;
        file.archive_id = reader.readUint64();
        file.file_size = reader.readUint64();
        file.file_sha256 = bytesToSha256(reader.readBytes(32));
        file.relative_path = reader.readString();
        if (file.archive_id == 0 || file.file_size == 0 || file.relative_path.empty() ||
            file.relative_path.size() > 512 || !archive_ids.insert(file.archive_id).second) {
            throw std::runtime_error("files.bin record is invalid");
        }
        files.push_back(file);
    }
    reader.finish();
    return files;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::vector<SegmentDocumentRecord> loadDocuments(const std::string& content,
                                                 std::uint64_t expected_count,
                                                 std::uint64_t batch_id,
                                                 const std::vector<SegmentFileRecord>& files) {
    BinaryReader reader(content);
    reader.requireMagic(kDocumentsMagic);
    if (reader.readUint32() != kSegmentFormatVersion || reader.readUint64() != expected_count) {
        throw std::runtime_error("documents.bin header is inconsistent");
    }
    std::vector<SegmentDocumentRecord> documents;
    for (std::uint64_t index = 0; index < expected_count; ++index) {
        SegmentDocumentRecord document;
        document.doc_id = reader.readUint64();
        document.local_id = reader.readUint32();
        document.file_ordinal = reader.readUint32();
        document.archive_id = reader.readUint64();
        document.byte_offset = reader.readUint64();
        document.byte_length = reader.readUint64();
        document.occurred_at_ms = reader.readInt64();
        document.archived_at_ms = reader.readInt64();
        document.term_count = reader.readUint32();
        document.line_id = reader.readString();
        document.station_id = reader.readString();
        document.device_id = reader.readString();
        document.collector_id = reader.readString();
        document.work_order = reader.readString();
        document.product_sn = reader.readString();
        document.source_type = reader.readString();
        document.level = reader.readString();
        document.module_name = reader.readString();
        document.error_code = reader.readString();
        document.event_name = reader.readString();
        const std::uint64_t expected_doc_id =
            (batch_id << 32) | static_cast<std::uint64_t>(document.local_id);
        if (document.local_id != index || document.doc_id != expected_doc_id ||
            document.file_ordinal >= files.size() || document.byte_length == 0 ||
            document.archive_id != files[document.file_ordinal].archive_id ||
            document.byte_offset > files[document.file_ordinal].file_size ||
            document.byte_length > files[document.file_ordinal].file_size - document.byte_offset ||
            document.line_id.empty() || document.station_id.empty() || document.device_id.empty() ||
            document.collector_id.empty() || document.source_type.empty() ||
            document.level.empty() || document.module_name.empty()) {
            throw std::runtime_error("documents.bin record is invalid");
        }
        documents.push_back(document);
    }
    reader.finish();
    return documents;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::vector<SegmentPosting> loadPostings(const std::string& content, std::uint64_t expected_count,
                                         std::size_t document_count) {
    BinaryReader reader(content);
    reader.requireMagic(kPostingsMagic);
    if (reader.readUint32() != kSegmentFormatVersion || reader.readUint64() != expected_count) {
        throw std::runtime_error("postings.bin header is inconsistent");
    }
    std::vector<SegmentPosting> postings;
    for (std::uint64_t index = 0; index < expected_count; ++index) {
        SegmentPosting posting{reader.readUint32(), reader.readUint32()};
        if (posting.local_id >= document_count || posting.term_frequency == 0) {
            throw std::runtime_error("postings.bin record is invalid");
        }
        postings.push_back(posting);
    }
    reader.finish();
    return postings;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

std::vector<SegmentTermRecord> loadTerms(const std::string& content, std::uint64_t expected_count,
                                         const std::vector<SegmentPosting>& postings,
                                         std::map<std::string, std::size_t>* lookup) {
    BinaryReader reader(content);
    reader.requireMagic(kTermsMagic);
    if (reader.readUint32() != kSegmentFormatVersion || reader.readUint64() != expected_count) {
        throw std::runtime_error("terms.bin header is inconsistent");
    }
    std::vector<SegmentTermRecord> terms;
    std::uint64_t expected_begin = 0;
    std::string previous;
    for (std::uint64_t index = 0; index < expected_count; ++index) {
        SegmentTermRecord term;
        term.term = reader.readString();
        term.document_frequency = reader.readUint32();
        term.posting_begin = reader.readUint64();
        term.posting_count = reader.readUint32();
        if (term.term.empty() || (!previous.empty() && term.term <= previous) ||
            term.document_frequency == 0 || term.document_frequency != term.posting_count ||
            term.posting_begin != expected_begin || term.posting_begin > postings.size() ||
            term.posting_count > postings.size() - term.posting_begin) {
            throw std::runtime_error("terms.bin record is invalid");
        }
        std::uint32_t previous_local = 0;
        for (std::uint32_t posting_index = 0; posting_index < term.posting_count; ++posting_index) {
            const std::uint32_t local_id =
                postings[static_cast<std::size_t>(term.posting_begin + posting_index)].local_id;
            if (posting_index != 0 && local_id <= previous_local) {
                throw std::runtime_error("posting list document ids are not strictly increasing");
            }
            previous_local = local_id;
        }
        (*lookup)[term.term] = terms.size();
        previous = term.term;
        expected_begin += term.posting_count;
        terms.push_back(term);
    }
    if (expected_begin != postings.size()) {
        throw std::runtime_error("terms.bin does not cover all postings");
    }
    reader.finish();
    return terms;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
LoadedSegment loadAtPath(const std::string& path, const std::string& expected_name,
                         std::uint64_t expected_batch_id,
                         const std::string& expected_manifest_sha) {
    requireExactSegmentFiles(path);
    const std::string manifest_content = readRegularFile(path + "/manifest.json", kManifestLimit);
    const std::string manifest_sha = sha256Hex(manifest_content);
    if (!expected_manifest_sha.empty() && manifest_sha != expected_manifest_sha) {
        throw std::runtime_error("segment manifest SHA-256 differs from database");
    }
    try {
        const nlohmann::json manifest = nlohmann::json::parse(manifest_content);
        static const std::set<std::string> keys = {"format_version",
                                                   "batch_id",
                                                   "first_archive_id",
                                                   "last_archive_id",
                                                   "segment_name",
                                                   "source_file_count",
                                                   "parsed_file_count",
                                                   "document_count",
                                                   "term_count",
                                                   "posting_count",
                                                   "average_document_length",
                                                   "min_occurred_at",
                                                   "max_occurred_at",
                                                   "created_at",
                                                   "artifacts"};
        requireKeys(manifest, keys);
        const std::uint32_t format_version = manifest.at("format_version").get<std::uint32_t>();
        const std::uint64_t batch_id = manifest.at("batch_id").get<std::uint64_t>();
        const std::uint64_t first_archive_id = manifest.at("first_archive_id").get<std::uint64_t>();
        const std::uint64_t last_archive_id = manifest.at("last_archive_id").get<std::uint64_t>();
        const std::string name = manifest.at("segment_name").get<std::string>();
        const std::uint64_t file_count = manifest.at("parsed_file_count").get<std::uint64_t>();
        const std::uint64_t source_file_count =
            manifest.at("source_file_count").get<std::uint64_t>();
        const std::uint64_t document_count = manifest.at("document_count").get<std::uint64_t>();
        const std::uint64_t term_count = manifest.at("term_count").get<std::uint64_t>();
        const std::uint64_t posting_count = manifest.at("posting_count").get<std::uint64_t>();
        const double average = manifest.at("average_document_length").get<double>();
        std::int64_t min_time = 0;
        std::int64_t max_time = 0;
        std::int64_t created_at = 0;
        if (format_version != kSegmentFormatVersion || batch_id != expected_batch_id ||
            batch_id > std::numeric_limits<std::uint32_t>::max() || first_archive_id == 0 ||
            last_archive_id < first_archive_id || name != expected_name ||
            source_file_count < file_count || file_count == 0 || document_count == 0 ||
            term_count == 0 || posting_count == 0 || !std::isfinite(average) || average <= 0.0 ||
            !parseIso8601Milliseconds(manifest.at("min_occurred_at").get<std::string>(),
                                      &min_time) ||
            !parseIso8601Milliseconds(manifest.at("max_occurred_at").get<std::string>(),
                                      &max_time) ||
            !parseIso8601Milliseconds(manifest.at("created_at").get<std::string>(), &created_at) ||
            min_time > max_time) {
            throw std::runtime_error("segment manifest fields are invalid");
        }
        const std::map<std::string, ArtifactFact> facts =
            parseArtifactFacts(manifest.at("artifacts"));
        std::map<std::string, std::string> contents;
        for (std::map<std::string, ArtifactFact>::const_iterator fact = facts.begin();
             fact != facts.end(); ++fact) {
            const std::string content = readRegularFile(path + "/" + fact->first, kBinaryLimit);
            if (content.size() != fact->second.size || sha256Hex(content) != fact->second.sha256) {
                throw std::runtime_error("segment artifact size or SHA-256 mismatch");
            }
            contents[fact->first] = content;
        }

        LoadedSegment segment;
        segment.format_version = format_version;
        segment.batch_id = batch_id;
        segment.first_archive_id = first_archive_id;
        segment.last_archive_id = last_archive_id;
        segment.source_file_count = static_cast<std::size_t>(source_file_count);
        segment.parsed_file_count = static_cast<std::size_t>(file_count);
        segment.segment_name = name;
        segment.manifest_sha256 = manifest_sha;
        segment.average_document_length = average;
        segment.min_occurred_at_ms = min_time;
        segment.max_occurred_at_ms = max_time;
        segment.files = loadFiles(contents["files.bin"], file_count);
        segment.documents =
            loadDocuments(contents["documents.bin"], document_count, batch_id, segment.files);
        segment.postings =
            loadPostings(contents["postings.bin"], posting_count, segment.documents.size());
        segment.terms =
            loadTerms(contents["terms.bin"], term_count, segment.postings, &segment.term_lookup);

        std::uint64_t total_terms = 0;
        std::int64_t actual_min = std::numeric_limits<std::int64_t>::max();
        std::int64_t actual_max = std::numeric_limits<std::int64_t>::min();
        for (std::vector<SegmentDocumentRecord>::const_iterator document =
                 segment.documents.begin();
             document != segment.documents.end(); ++document) {
            total_terms += document->term_count;
            actual_min = std::min(actual_min, document->occurred_at_ms);
            actual_max = std::max(actual_max, document->occurred_at_ms);
        }
        const double actual_average =
            static_cast<double>(total_terms) / static_cast<double>(segment.documents.size());
        if (std::fabs(actual_average - average) > 1e-9 || actual_min != min_time ||
            actual_max != max_time) {
            throw std::runtime_error("segment manifest statistics are inconsistent");
        }
        return segment;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("segment manifest JSON is invalid: ") + error.what());
    }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

SegmentStore::SegmentStore(const StoragePaths& storage) : storage_(storage) {}

SegmentBuildResult SegmentStore::build(const ParsedBatchData& batch) const {
    if (batch.descriptor.batch_id > std::numeric_limits<std::uint32_t>::max() ||
        batch.files.empty() || batch.documents.empty()) {
        throw std::runtime_error("parsed batch cannot be represented by segment format v1");
    }
    std::vector<SegmentFileRecord> files;
    std::map<std::uint64_t, std::uint32_t> file_ordinals;
    for (std::size_t index = 0; index < batch.files.size(); ++index) {
        const ArchiveRecord& archive = batch.files[index].archive;
        SegmentFileRecord file{archive.archive_id, archive.file_size, archive.file_sha256,
                               archive.relative_path};
        verifyOriginalArchive(storage_, file);
        file_ordinals[file.archive_id] = static_cast<std::uint32_t>(index);
        files.push_back(file);
    }

    std::vector<SegmentDocumentRecord> documents;
    std::map<std::string, std::map<std::uint32_t, std::uint32_t> > inverted;
    std::uint64_t total_terms = 0;
    std::int64_t min_time = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_time = std::numeric_limits<std::int64_t>::min();
    for (std::vector<ParsedArtifactDocument>::const_iterator parsed = batch.documents.begin();
         parsed != batch.documents.end(); ++parsed) {
        const std::map<std::uint64_t, std::uint32_t>::const_iterator file_ordinal =
            file_ordinals.find(parsed->document.archive_id);
        if (file_ordinal == file_ordinals.end()) {
            throw std::runtime_error("parsed document references unknown segment file");
        }
        const SegmentFileRecord& file = files[file_ordinal->second];
        const std::string raw = readOriginalRecord(storage_, file, parsed->document.byte_offset,
                                                   parsed->document.byte_length);
        const std::vector<std::string> terms = tokenizeTerms(raw);
        if (terms.size() != parsed->document.term_count ||
            terms.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("parsed document term count changed before segment build");
        }
        std::map<std::string, std::uint32_t> frequencies;
        for (std::vector<std::string>::const_iterator term = terms.begin(); term != terms.end();
             ++term) {
            std::uint32_t& frequency = frequencies[*term];
            if (frequency == std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("term frequency exceeds segment format limit");
            }
            ++frequency;
        }
        for (std::map<std::string, std::uint32_t>::const_iterator frequency = frequencies.begin();
             frequency != frequencies.end(); ++frequency) {
            inverted[frequency->first][parsed->local_id] = frequency->second;
        }

        std::int64_t occurred_at = 0;
        std::int64_t archived_at = 0;
        if (!parseIso8601Milliseconds(parsed->document.occurred_at, &occurred_at) ||
            !parseIso8601Milliseconds(parsed->document.archived_at, &archived_at)) {
            throw std::runtime_error("parsed document time cannot enter segment");
        }
        SegmentDocumentRecord document;
        document.doc_id = (batch.descriptor.batch_id << 32) | parsed->local_id;
        document.local_id = parsed->local_id;
        document.file_ordinal = file_ordinal->second;
        document.archive_id = parsed->document.archive_id;
        document.byte_offset = parsed->document.byte_offset;
        document.byte_length = parsed->document.byte_length;
        document.occurred_at_ms = occurred_at;
        document.archived_at_ms = archived_at;
        document.term_count = static_cast<std::uint32_t>(terms.size());
        document.line_id = parsed->document.line_id;
        document.station_id = parsed->document.station_id;
        document.device_id = parsed->document.device_id;
        document.collector_id = parsed->document.collector_id;
        document.work_order = parsed->document.work_order;
        document.product_sn = parsed->document.product_sn;
        document.source_type = parsed->document.source_type;
        document.level = parsed->document.level;
        document.module_name = parsed->document.module_name;
        document.error_code = parsed->document.error_code;
        document.event_name = parsed->document.event_name;
        documents.push_back(document);
        total_terms += document.term_count;
        min_time = std::min(min_time, occurred_at);
        max_time = std::max(max_time, occurred_at);
    }

    std::vector<SegmentPosting> postings;
    std::vector<SegmentTermRecord> term_records;
    for (std::map<std::string, std::map<std::uint32_t, std::uint32_t> >::const_iterator term =
             inverted.begin();
         term != inverted.end(); ++term) {
        if (term->second.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("term document frequency exceeds segment format limit");
        }
        SegmentTermRecord record{term->first, static_cast<std::uint32_t>(term->second.size()),
                                 postings.size(), static_cast<std::uint32_t>(term->second.size())};
        for (std::map<std::uint32_t, std::uint32_t>::const_iterator posting = term->second.begin();
             posting != term->second.end(); ++posting) {
            postings.push_back(SegmentPosting{posting->first, posting->second});
        }
        term_records.push_back(record);
    }
    if (term_records.empty() || postings.empty()) {
        throw std::runtime_error("segment contains no searchable terms");
    }

    const std::string files_content = serializeFiles(files);
    const std::string documents_content = serializeDocuments(documents);
    const std::string postings_content = serializePostings(postings);
    const std::string terms_content = serializeTerms(term_records);
    const std::string name = segmentName(batch.descriptor.batch_id);
    const double average = static_cast<double>(total_terms) / static_cast<double>(documents.size());
    const nlohmann::json artifacts = {
        {"files.bin", {{"size", files_content.size()}, {"sha256", sha256Hex(files_content)}}},
        {"documents.bin",
         {{"size", documents_content.size()}, {"sha256", sha256Hex(documents_content)}}},
        {"postings.bin",
         {{"size", postings_content.size()}, {"sha256", sha256Hex(postings_content)}}},
        {"terms.bin", {{"size", terms_content.size()}, {"sha256", sha256Hex(terms_content)}}}};
    const nlohmann::json manifest = {{"format_version", kSegmentFormatVersion},
                                     {"batch_id", batch.descriptor.batch_id},
                                     {"first_archive_id", batch.descriptor.first_archive_id},
                                     {"last_archive_id", batch.descriptor.last_archive_id},
                                     {"segment_name", name},
                                     {"source_file_count", batch.descriptor.source_file_count},
                                     {"parsed_file_count", files.size()},
                                     {"document_count", documents.size()},
                                     {"term_count", term_records.size()},
                                     {"posting_count", postings.size()},
                                     {"average_document_length", average},
                                     {"min_occurred_at", formatUtcMilliseconds(min_time)},
                                     {"max_occurred_at", formatUtcMilliseconds(max_time)},
                                     {"created_at", currentUtcMilliseconds()},
                                     {"artifacts", artifacts}};
    const std::string manifest_content = manifest.dump(2) + "\n";
    const std::string manifest_sha = sha256Hex(manifest_content);

    const std::string segments_root = storage_.indexRoot() + "/segments";
    const std::string building_root = segments_root + "/.building";
    const std::string building_path = building_root + "/" + name;
    const std::string final_path = segments_root + "/" + name;
    ensureDirectory(segments_root);
    ensureDirectory(building_root);
    removeSegmentDirectory(building_path);
    removeSegmentDirectory(final_path);
    ensureDirectory(building_path);
    try {
        writeDurableFile(building_path + "/files.bin", files_content);
        writeDurableFile(building_path + "/documents.bin", documents_content);
        writeDurableFile(building_path + "/postings.bin", postings_content);
        writeDurableFile(building_path + "/terms.bin", terms_content);
        writeDurableFile(building_path + "/manifest.json", manifest_content);
        syncDirectory(building_path);
        loadAtPath(building_path, name, batch.descriptor.batch_id, manifest_sha);
        if (::rename(building_path.c_str(), final_path.c_str()) != 0) {
            throw std::runtime_error("cannot publish segment directory: " +
                                     std::string(std::strerror(errno)));
        }
        syncDirectory(segments_root);
        return SegmentBuildResult{
            batch.descriptor.batch_id, name,           manifest_sha, documents.size(),
            term_records.size(),       postings.size()};
    } catch (const std::exception&) {
        removeSegmentDirectory(building_path);
        throw;
    }
}

LoadedSegment SegmentStore::load(const ReadySegmentDescriptor& descriptor) const {
    if (descriptor.batch_id == 0 || descriptor.segment_name != segmentName(descriptor.batch_id) ||
        !validSha256(descriptor.segment_sha256)) {
        throw std::runtime_error("READY segment database descriptor is invalid");
    }
    return loadAtPath(storage_.indexRoot() + "/segments/" + descriptor.segment_name,
                      descriptor.segment_name, descriptor.batch_id, descriptor.segment_sha256);
}

LoadedSegment SegmentStore::loadUnpublished(std::uint64_t batch_id) const {
    const std::string name = segmentName(batch_id);
    return loadAtPath(storage_.indexRoot() + "/segments/" + name, name, batch_id, "");
}

bool SegmentStore::publishedExists(std::uint64_t batch_id) const {
    return isDirectory(storage_.indexRoot() + "/segments/" + segmentName(batch_id));
}

void SegmentStore::remove(std::uint64_t batch_id) const {
    const std::string name = segmentName(batch_id);
    removeSegmentDirectory(storage_.indexRoot() + "/segments/.building/" + name);
    removeSegmentDirectory(storage_.indexRoot() + "/segments/" + name);
}

void SegmentStore::cleanupBuilding() const {
    const std::string segments_root = storage_.indexRoot() + "/segments";
    const std::string building_root = segments_root + "/.building";
    ensureDirectory(segments_root);
    ensureDirectory(building_root);
    const std::vector<std::string> entries = directoryEntries(building_root);
    for (std::vector<std::string>::const_iterator entry = entries.begin(); entry != entries.end();
         ++entry) {
        if (!validSegmentName(*entry) || !isDirectory(building_root + "/" + *entry)) {
            throw std::runtime_error("unexpected entry exists in segment building directory");
        }
        removeSegmentDirectory(building_root + "/" + *entry);
    }
    syncDirectory(building_root);
}

std::vector<std::string> SegmentStore::listPublishedNames() const {
    const std::string segments_root = storage_.indexRoot() + "/segments";
    ensureDirectory(segments_root);
    std::vector<std::string> names;
    const std::vector<std::string> entries = directoryEntries(segments_root);
    for (std::vector<std::string>::const_iterator entry = entries.begin(); entry != entries.end();
         ++entry) {
        if (*entry == ".building") {
            continue;
        }
        if (!validSegmentName(*entry) || !isDirectory(segments_root + "/" + *entry)) {
            throw std::runtime_error("unexpected entry exists in segments directory");
        }
        names.push_back(*entry);
    }
    return names;
}

}  // namespace logtrace
}  // namespace smt
