/**
 * @file parsed_batch_reader.cpp
 * @brief 实现 PARSED manifest、归档 JSONL 和文档 JSONL 的严格加载。
 */

#include "logtrace/indexing/parsed_batch_reader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
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

namespace smt {
namespace logtrace {
namespace {

const std::uint64_t kManifestLimit = 1024ULL * 1024ULL;
const std::uint64_t kJsonLinesLimit = 1024ULL * 1024ULL * 1024ULL;

std::string readRegularFile(const std::string& path, std::uint64_t size_limit) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > size_limit) {
        throw std::runtime_error("parsed artifact file is invalid: " + path);
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("cannot open parsed artifact file: " +
                                 std::string(std::strerror(errno)));
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
            throw std::runtime_error("cannot read parsed artifact file: " +
                                     std::string(std::strerror(saved_errno)));
        }
        if (count == 0) {
            ::close(fd);
            throw std::runtime_error("parsed artifact file was truncated while reading");
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close parsed artifact file: " +
                                 std::string(std::strerror(errno)));
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

void requireKeys(const nlohmann::json& value, const std::set<std::string>& expected) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw std::runtime_error("parsed artifact JSON object fields are invalid");
    }
    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (expected.count(it.key()) == 0) {
            throw std::runtime_error("parsed artifact JSON contains an unknown field");
        }
    }
}

std::vector<nlohmann::json> parseJsonLines(const std::string& content) {
    if (content.empty() || content[content.size() - 1] != '\n') {
        throw std::runtime_error("parsed artifact JSONL must end with LF");
    }
    std::vector<nlohmann::json> rows;
    std::size_t begin = 0;
    while (begin < content.size()) {
        const std::size_t end = content.find('\n', begin);
        if (end == begin || end == std::string::npos) {
            throw std::runtime_error("parsed artifact JSONL contains an empty or partial row");
        }
        rows.push_back(nlohmann::json::parse(content.substr(begin, end - begin)));
        begin = end + 1;
    }
    return rows;
}

std::uint32_t toUint32(std::uint64_t value, const char* field) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string("parsed artifact field exceeds uint32: ") + field);
    }
    return static_cast<std::uint32_t>(value);
}

ArchiveRecord parseArchive(const nlohmann::json& row, ParserProfile* profile,
                           std::size_t* document_count) {
    static const std::set<std::string> keys = {
        "archive_id",     "line_id",       "station_id",  "device_id",         "collector_id",
        "work_order",     "product_sn",    "file_type",   "original_filename", "relative_path",
        "file_size",      "file_sha256",   "produced_at", "archived_at",       "parser_profile",
        "parser_version", "document_count"};
    requireKeys(row, keys);
    ArchiveRecord archive;
    archive.archive_id = row.at("archive_id").get<std::uint64_t>();
    archive.line_id = row.at("line_id").get<std::string>();
    archive.station_id = row.at("station_id").get<std::string>();
    archive.device_id = row.at("device_id").get<std::string>();
    archive.collector_id = row.at("collector_id").get<std::string>();
    archive.work_order = row.at("work_order").get<std::string>();
    archive.product_sn = row.at("product_sn").get<std::string>();
    archive.file_type = row.at("file_type").get<std::string>();
    archive.original_filename = row.at("original_filename").get<std::string>();
    archive.relative_path = row.at("relative_path").get<std::string>();
    archive.file_size = row.at("file_size").get<std::uint64_t>();
    archive.file_sha256 = row.at("file_sha256").get<std::string>();
    archive.produced_at = row.at("produced_at").get<std::string>();
    archive.archived_at = row.at("archived_at").get<std::string>();
    profile->name = row.at("parser_profile").get<std::string>();
    profile->version = row.at("parser_version").get<unsigned int>();
    *document_count = row.at("document_count").get<std::size_t>();
    std::int64_t timestamp = 0;
    if (archive.archive_id == 0 || archive.file_size == 0 || archive.line_id.empty() ||
        archive.station_id.empty() || archive.device_id.empty() || archive.collector_id.empty() ||
        archive.relative_path.empty() || archive.relative_path.size() > 512 ||
        !validSha256(archive.file_sha256) || profile->name.empty() || profile->version == 0 ||
        !parseIso8601Milliseconds(archive.produced_at, &timestamp) ||
        !parseIso8601Milliseconds(archive.archived_at, &timestamp)) {
        throw std::runtime_error("parsed artifact archive fields are invalid");
    }
    return archive;
}

ParsedArtifactDocument parseDocument(const nlohmann::json& row) {
    static const std::set<std::string> keys = {
        "local_id",    "archive_id", "byte_offset", "byte_length",  "occurred_at", "archived_at",
        "line_id",     "station_id", "device_id",   "collector_id", "work_order",  "product_sn",
        "source_type", "level",      "module_name", "error_code",   "event_name",  "term_count"};
    requireKeys(row, keys);
    ParsedArtifactDocument parsed;
    parsed.local_id = toUint32(row.at("local_id").get<std::uint64_t>(), "local_id");
    ParsedDocument& document = parsed.document;
    document.archive_id = row.at("archive_id").get<std::uint64_t>();
    document.byte_offset = row.at("byte_offset").get<std::uint64_t>();
    document.byte_length = row.at("byte_length").get<std::uint64_t>();
    document.occurred_at = row.at("occurred_at").get<std::string>();
    document.archived_at = row.at("archived_at").get<std::string>();
    document.line_id = row.at("line_id").get<std::string>();
    document.station_id = row.at("station_id").get<std::string>();
    document.device_id = row.at("device_id").get<std::string>();
    document.collector_id = row.at("collector_id").get<std::string>();
    document.work_order = row.at("work_order").get<std::string>();
    document.product_sn = row.at("product_sn").get<std::string>();
    document.source_type = row.at("source_type").get<std::string>();
    document.level = row.at("level").get<std::string>();
    document.module_name = row.at("module_name").get<std::string>();
    document.error_code = row.at("error_code").get<std::string>();
    document.event_name = row.at("event_name").get<std::string>();
    document.term_count = row.at("term_count").get<std::size_t>();
    std::int64_t timestamp = 0;
    if (document.archive_id == 0 || document.byte_length == 0 || document.line_id.empty() ||
        document.station_id.empty() || document.device_id.empty() ||
        document.collector_id.empty() || document.source_type.empty() || document.level.empty() ||
        document.module_name.empty() ||
        document.term_count > std::numeric_limits<std::uint32_t>::max() ||
        !parseIso8601Milliseconds(document.occurred_at, &timestamp) ||
        !parseIso8601Milliseconds(document.archived_at, &timestamp)) {
        throw std::runtime_error("parsed artifact document fields are invalid");
    }
    return parsed;
}

}  // namespace

ParsedBatchReader::ParsedBatchReader(const std::string& index_root) : index_root_(index_root) {}

ParsedBatchData ParsedBatchReader::load(const ParsedBatchDescriptor& descriptor) const {
    const std::string expected_path = "parsed/batch_" + std::to_string(descriptor.batch_id);
    if (descriptor.batch_id == 0 || descriptor.parsed_path != expected_path ||
        !validSha256(descriptor.parsed_sha256)) {
        throw std::runtime_error("parsed batch database descriptor is invalid");
    }
    const std::string root = index_root_ + "/" + expected_path;
    const std::string manifest_content = readRegularFile(root + "/manifest.json", kManifestLimit);
    if (sha256Hex(manifest_content) != descriptor.parsed_sha256) {
        throw std::runtime_error("parsed batch manifest SHA-256 mismatch");
    }

    try {
        const nlohmann::json manifest = nlohmann::json::parse(manifest_content);
        static const std::set<std::string> manifest_keys = {
            "format_version",    "batch_id",          "first_archive_id",  "last_archive_id",
            "source_file_count", "parsed_file_count", "failed_file_count", "document_count",
            "created_at",        "archives_sha256",   "documents_sha256"};
        requireKeys(manifest, manifest_keys);
        const std::string archives_sha256 = manifest.at("archives_sha256").get<std::string>();
        const std::string documents_sha256 = manifest.at("documents_sha256").get<std::string>();
        std::int64_t created_at = 0;
        if (manifest.at("format_version").get<unsigned int>() != 1) {
            throw std::runtime_error("parsed batch manifest format version is invalid");
        }
        if (manifest.at("batch_id").get<std::uint64_t>() != descriptor.batch_id ||
            manifest.at("first_archive_id").get<std::uint64_t>() != descriptor.first_archive_id ||
            manifest.at("last_archive_id").get<std::uint64_t>() != descriptor.last_archive_id) {
            throw std::runtime_error("parsed batch manifest identity differs from database");
        }
        const std::size_t manifest_source_count =
            manifest.at("source_file_count").get<std::size_t>();
        const std::size_t manifest_document_count =
            manifest.at("document_count").get<std::size_t>();
        if (manifest_source_count != descriptor.source_file_count ||
            manifest_document_count != descriptor.document_count) {
            throw std::runtime_error("parsed batch manifest counts differ from database: source=" +
                                     std::to_string(manifest_source_count) + "/" +
                                     std::to_string(descriptor.source_file_count) +
                                     " documents=" + std::to_string(manifest_document_count) + "/" +
                                     std::to_string(descriptor.document_count));
        }
        if (!validSha256(archives_sha256) || !validSha256(documents_sha256)) {
            throw std::runtime_error("parsed batch child SHA-256 field is invalid");
        }
        if (!parseIso8601Milliseconds(manifest.at("created_at").get<std::string>(), &created_at)) {
            throw std::runtime_error("parsed batch creation time is invalid");
        }

        const std::string archives_content =
            readRegularFile(root + "/archives.jsonl", kJsonLinesLimit);
        const std::string documents_content =
            readRegularFile(root + "/documents.jsonl", kJsonLinesLimit);
        if (sha256Hex(archives_content) != archives_sha256 ||
            sha256Hex(documents_content) != documents_sha256) {
            throw std::runtime_error("parsed batch child SHA-256 mismatch");
        }

        ParsedBatchData result;
        result.descriptor = descriptor;
        result.parsed_file_count = manifest.at("parsed_file_count").get<std::size_t>();
        result.failed_file_count = manifest.at("failed_file_count").get<std::size_t>();
        if (result.parsed_file_count == 0 ||
            result.parsed_file_count + result.failed_file_count != descriptor.source_file_count) {
            throw std::runtime_error("parsed batch file counts are invalid");
        }

        const std::vector<nlohmann::json> archive_rows = parseJsonLines(archives_content);
        const std::vector<nlohmann::json> document_rows = parseJsonLines(documents_content);
        if (archive_rows.size() != result.parsed_file_count ||
            document_rows.size() != descriptor.document_count) {
            throw std::runtime_error("parsed batch JSONL row counts are invalid");
        }

        std::map<std::uint64_t, std::size_t> file_ordinals;
        std::vector<std::size_t> expected_documents;
        for (std::vector<nlohmann::json>::const_iterator row = archive_rows.begin();
             row != archive_rows.end(); ++row) {
            ParsedArtifactFile file;
            file.archive = parseArchive(*row, &file.profile, &file.document_count);
            if (file.archive.archive_id < descriptor.first_archive_id ||
                file.archive.archive_id > descriptor.last_archive_id ||
                file_ordinals.count(file.archive.archive_id) != 0) {
                throw std::runtime_error("parsed batch archive range or uniqueness is invalid");
            }
            file_ordinals[file.archive.archive_id] = result.files.size();
            expected_documents.push_back(file.document_count);
            result.files.push_back(file);
        }

        std::vector<std::size_t> actual_documents(result.files.size(), 0);
        for (std::size_t index = 0; index < document_rows.size(); ++index) {
            ParsedArtifactDocument document = parseDocument(document_rows[index]);
            if (document.local_id != index) {
                throw std::runtime_error("parsed batch local document ids are not contiguous");
            }
            const std::map<std::uint64_t, std::size_t>::const_iterator file =
                file_ordinals.find(document.document.archive_id);
            if (file == file_ordinals.end()) {
                throw std::runtime_error("parsed batch document references an unknown archive");
            }
            const ArchiveRecord& archive = result.files[file->second].archive;
            if (document.document.byte_offset > archive.file_size ||
                document.document.byte_length > archive.file_size - document.document.byte_offset ||
                document.document.line_id != archive.line_id ||
                document.document.station_id != archive.station_id ||
                document.document.device_id != archive.device_id ||
                document.document.collector_id != archive.collector_id ||
                document.document.source_type != archive.file_type ||
                document.document.archived_at != archive.archived_at) {
                throw std::runtime_error("parsed batch document metadata is inconsistent");
            }
            ++actual_documents[file->second];
            result.documents.push_back(document);
        }
        if (actual_documents != expected_documents) {
            throw std::runtime_error("parsed batch per-file document counts are inconsistent");
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(std::string("parsed batch JSON is invalid: ") + error.what());
    }
}

}  // namespace logtrace
}  // namespace smt
