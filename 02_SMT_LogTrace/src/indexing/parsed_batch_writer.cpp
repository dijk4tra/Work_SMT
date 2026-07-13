/**
 * @file parsed_batch_writer.cpp
 * @brief 实现解析批次 JSONL 工件的完整写入和原子发布。
 */

#include "logtrace/indexing/parsed_batch_writer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "logtrace/common/sha256.h"
#include "logtrace/common/time_utils.h"

namespace smt {
namespace logtrace {
namespace {

/// @brief 管理单个解析工件文件的写入、摘要和持久化。
class DurableFile {
   public:
    explicit DurableFile(const std::string& path)
        : fd_(::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0640)), closed_(false) {
        if (fd_ < 0) {
            throw std::runtime_error("cannot create parsed artifact file " + path + ": " +
                                     std::strerror(errno));
        }
    }

    ~DurableFile() {
        if (!closed_) {
            ::close(fd_);
        }
    }

    void append(const std::string& value) {
        const char* data = value.data();
        std::size_t remaining = value.size();
        while (remaining != 0) {
            const ssize_t count = ::write(fd_, data, remaining);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("cannot write parsed artifact: " +
                                         std::string(std::strerror(errno)));
            }
            data += count;
            remaining -= static_cast<std::size_t>(count);
        }
        sha256_.update(value.data(), value.size());
    }

    std::string finish() {
        if (::fsync(fd_) != 0) {
            throw std::runtime_error("cannot fsync parsed artifact: " +
                                     std::string(std::strerror(errno)));
        }
        if (::close(fd_) != 0) {
            closed_ = true;
            throw std::runtime_error("cannot close parsed artifact: " +
                                     std::string(std::strerror(errno)));
        }
        closed_ = true;
        return sha256_.finishHex();
    }

   private:
    int fd_;
    bool closed_;
    Sha256 sha256_;
};

bool isDirectory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void ensureDirectory(const std::string& path) {
    if (::mkdir(path.c_str(), 0750) != 0 && errno != EEXIST) {
        throw std::runtime_error("cannot create parsed artifact directory " + path + ": " +
                                 std::strerror(errno));
    }
    if (!isDirectory(path)) {
        throw std::runtime_error("parsed artifact path is not a directory: " + path);
    }
}

void syncDirectory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("cannot open parsed artifact directory: " +
                                 std::string(std::strerror(errno)));
    }
    if (::fsync(fd) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::runtime_error("cannot fsync parsed artifact directory: " +
                                 std::string(std::strerror(saved_errno)));
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("cannot close parsed artifact directory: " +
                                 std::string(std::strerror(errno)));
    }
}

void removeFileIfPresent(const std::string& path) {
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        throw std::runtime_error("cannot remove parsed artifact file " + path + ": " +
                                 std::strerror(errno));
    }
}

void removeDirectoryIfPresent(const std::string& path) {
    removeFileIfPresent(path + "/manifest.json");
    removeFileIfPresent(path + "/archives.jsonl");
    removeFileIfPresent(path + "/documents.jsonl");
    if (::rmdir(path.c_str()) != 0 && errno != ENOENT) {
        throw std::runtime_error("cannot remove parsed artifact directory " + path + ": " +
                                 std::strerror(errno));
    }
}

nlohmann::json archiveJson(const ParsedArchive& parsed) {
    const ArchiveRecord& archive = parsed.archive;
    return nlohmann::json{{"archive_id", archive.archive_id},
                          {"line_id", archive.line_id},
                          {"station_id", archive.station_id},
                          {"device_id", archive.device_id},
                          {"collector_id", archive.collector_id},
                          {"work_order", archive.work_order},
                          {"product_sn", archive.product_sn},
                          {"file_type", archive.file_type},
                          {"original_filename", archive.original_filename},
                          {"relative_path", archive.relative_path},
                          {"file_size", archive.file_size},
                          {"file_sha256", archive.file_sha256},
                          {"produced_at", archive.produced_at},
                          {"archived_at", archive.archived_at},
                          {"parser_profile", parsed.profile.name},
                          {"parser_version", parsed.profile.version},
                          {"document_count", parsed.documents.size()}};
}

nlohmann::json documentJson(std::uint64_t local_id, const ParsedDocument& document) {
    return nlohmann::json{{"local_id", local_id},
                          {"archive_id", document.archive_id},
                          {"byte_offset", document.byte_offset},
                          {"byte_length", document.byte_length},
                          {"occurred_at", document.occurred_at},
                          {"archived_at", document.archived_at},
                          {"line_id", document.line_id},
                          {"station_id", document.station_id},
                          {"device_id", document.device_id},
                          {"collector_id", document.collector_id},
                          {"work_order", document.work_order},
                          {"product_sn", document.product_sn},
                          {"source_type", document.source_type},
                          {"level", document.level},
                          {"module_name", document.module_name},
                          {"error_code", document.error_code},
                          {"event_name", document.event_name},
                          {"term_count", document.term_count}};
}

}  // namespace

ParsedBatchWriter::ParsedBatchWriter(const std::string& index_root) : index_root_(index_root) {}

ParsedBatchArtifact ParsedBatchWriter::write(std::uint64_t batch_id, std::uint64_t first_archive_id,
                                             std::uint64_t last_archive_id,
                                             std::size_t source_file_count,
                                             std::size_t failed_file_count,
                                             const std::vector<ParsedArchive>& archives) const {
    const std::string parsed_root = index_root_ + "/parsed";
    const std::string building_root = parsed_root + "/.building";
    const std::string directory_name = "batch_" + std::to_string(batch_id);
    const std::string building_path = building_root + "/" + directory_name;
    const std::string final_path = parsed_root + "/" + directory_name;
    ensureDirectory(parsed_root);
    ensureDirectory(building_root);
    if (isDirectory(final_path)) {
        throw std::runtime_error("parsed batch already exists: " + directory_name);
    }
    removeDirectoryIfPresent(building_path);
    ensureDirectory(building_path);

    try {
        DurableFile archives_file(building_path + "/archives.jsonl");
        DurableFile documents_file(building_path + "/documents.jsonl");
        std::uint64_t local_id = 0;
        for (std::vector<ParsedArchive>::const_iterator archive = archives.begin();
             archive != archives.end(); ++archive) {
            archives_file.append(archiveJson(*archive).dump() + "\n");
            for (std::vector<ParsedDocument>::const_iterator document = archive->documents.begin();
                 document != archive->documents.end(); ++document) {
                documents_file.append(documentJson(local_id, *document).dump() + "\n");
                ++local_id;
            }
        }
        const std::string archives_sha256 = archives_file.finish();
        const std::string documents_sha256 = documents_file.finish();

        const nlohmann::json manifest{{"format_version", 1},
                                      {"batch_id", batch_id},
                                      {"first_archive_id", first_archive_id},
                                      {"last_archive_id", last_archive_id},
                                      {"source_file_count", source_file_count},
                                      {"parsed_file_count", archives.size()},
                                      {"failed_file_count", failed_file_count},
                                      {"document_count", local_id},
                                      {"created_at", currentUtcMilliseconds()},
                                      {"archives_sha256", archives_sha256},
                                      {"documents_sha256", documents_sha256}};
        DurableFile manifest_file(building_path + "/manifest.json");
        manifest_file.append(manifest.dump(2) + "\n");
        const std::string manifest_sha256 = manifest_file.finish();

        syncDirectory(building_path);
        if (::rename(building_path.c_str(), final_path.c_str()) != 0) {
            throw std::runtime_error("cannot publish parsed batch: " +
                                     std::string(std::strerror(errno)));
        }
        syncDirectory(parsed_root);
        return ParsedBatchArtifact{"parsed/" + directory_name, manifest_sha256};
    } catch (const std::exception&) {
        removeDirectoryIfPresent(building_path);
        throw;
    }
}

void ParsedBatchWriter::remove(std::uint64_t batch_id) const {
    const std::string directory_name = "batch_" + std::to_string(batch_id);
    removeDirectoryIfPresent(index_root_ + "/parsed/.building/" + directory_name);
    removeDirectoryIfPresent(index_root_ + "/parsed/" + directory_name);
}

}  // namespace logtrace
}  // namespace smt
