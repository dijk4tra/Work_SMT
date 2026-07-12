/**
 * @file spool_store.cpp
 * @brief 实现 JSON 状态文件和 payload 快照的原子持久化。
 */

#include "datastream/collector/spool_store.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace smt {
namespace datastream {
namespace {

void ensureDirectory(const std::string& path) {
    struct stat info;
    if (::mkdir(path.c_str(), 0750) == 0) return;
    if (errno != EEXIST || ::stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        throw SpoolError("cannot create spool directory: " + path);
    }
}

void writeAll(int descriptor, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t result = ::write(descriptor, data.data() + offset, data.size() - offset);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) throw SpoolError("cannot write spool state");
        offset += static_cast<std::size_t>(result);
    }
}

CollectorTaskState parseState(const std::string& value) {
    if (value == "DISCOVERED") return CollectorTaskState::Discovered;
    if (value == "READY") return CollectorTaskState::Ready;
    if (value == "UPLOADING") return CollectorTaskState::Uploading;
    if (value == "COMPLETING") return CollectorTaskState::Completing;
    if (value == "DONE") return CollectorTaskState::Done;
    if (value == "FAILED") return CollectorTaskState::Failed;
    throw SpoolError("spool task has invalid state");
}

nlohmann::json toJson(const CollectorTask& task) {
    return nlohmann::json{{"task_id", task.task_id},
                          {"source_path", task.source_path},
                          {"payload_path", task.payload_path},
                          {"line_id", task.line_id},
                          {"station_id", task.station_id},
                          {"device_id", task.device_id},
                          {"collector_id", task.collector_id},
                          {"work_order", task.work_order},
                          {"product_sn", task.product_sn},
                          {"file_type", task.file_type},
                          {"result", task.result},
                          {"original_filename", task.original_filename},
                          {"produced_at", task.produced_at},
                          {"file_size", task.file_size},
                          {"source_mtime_ns", task.source_mtime_ns},
                          {"file_sha256", task.file_sha256},
                          {"state", collectorTaskStateName(task.state)},
                          {"upload_id", task.upload_id},
                          {"chunk_size", task.chunk_size},
                          {"chunk_count", task.chunk_count},
                          {"retry_attempts", task.retry_attempts},
                          {"next_attempt_milliseconds", task.next_attempt_milliseconds},
                          {"last_error", task.last_error},
                          {"archive_id", task.archive_id}};
}

CollectorTask fromJson(const nlohmann::json& value) {
    static const std::set<std::string> expected{"task_id",         "source_path",
                                                "payload_path",    "line_id",
                                                "station_id",      "device_id",
                                                "collector_id",    "work_order",
                                                "product_sn",      "file_type",
                                                "result",          "original_filename",
                                                "produced_at",     "file_size",
                                                "source_mtime_ns", "file_sha256",
                                                "state",           "upload_id",
                                                "chunk_size",      "chunk_count",
                                                "retry_attempts",  "next_attempt_milliseconds",
                                                "last_error",      "archive_id"};
    if (!value.is_object() || value.size() != expected.size()) {
        throw SpoolError("spool task fields do not match contract");
    }
    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (expected.count(it.key()) == 0) throw SpoolError("spool task contains unknown field");
    }
    CollectorTask task;
    task.task_id = value.at("task_id").get<std::string>();
    task.source_path = value.at("source_path").get<std::string>();
    task.payload_path = value.at("payload_path").get<std::string>();
    task.line_id = value.at("line_id").get<std::string>();
    task.station_id = value.at("station_id").get<std::string>();
    task.device_id = value.at("device_id").get<std::string>();
    task.collector_id = value.at("collector_id").get<std::string>();
    task.work_order = value.at("work_order").get<std::string>();
    task.product_sn = value.at("product_sn").get<std::string>();
    task.file_type = value.at("file_type").get<std::string>();
    task.result = value.at("result").get<std::string>();
    task.original_filename = value.at("original_filename").get<std::string>();
    task.produced_at = value.at("produced_at").get<std::string>();
    task.file_size = value.at("file_size").get<std::uint64_t>();
    task.source_mtime_ns = value.at("source_mtime_ns").get<std::int64_t>();
    task.file_sha256 = value.at("file_sha256").get<std::string>();
    task.state = parseState(value.at("state").get<std::string>());
    task.upload_id = value.at("upload_id").get<std::string>();
    task.chunk_size = value.at("chunk_size").get<std::size_t>();
    task.chunk_count = value.at("chunk_count").get<std::size_t>();
    task.retry_attempts = value.at("retry_attempts").get<int>();
    task.next_attempt_milliseconds = value.at("next_attempt_milliseconds").get<std::int64_t>();
    task.last_error = value.at("last_error").get<std::string>();
    task.archive_id = value.at("archive_id").get<std::uint64_t>();
    if (task.task_id.size() != 64 || task.source_path.empty() || task.payload_path.empty()) {
        throw SpoolError("spool task values are invalid");
    }
    return task;
}

std::uint64_t directoryBytes(const std::string& path) {
    DIR* directory = ::opendir(path.c_str());
    if (directory == nullptr) throw SpoolError("cannot scan spool payload directory");
    std::uint64_t total = 0;
    while (dirent* entry = ::readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        struct stat info;
        const std::string file = path + "/" + entry->d_name;
        if (::stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
            total += static_cast<std::uint64_t>(info.st_size);
        }
    }
    ::closedir(directory);
    return total;
}

bool temporaryName(const std::string& name, const std::string& middle) {
    if (name.size() != 64 + middle.size() + 4 ||
        name.compare(64, middle.size() + 4, middle + ".tmp") != 0) {
        return false;
    }
    for (std::size_t index = 0; index < 64; ++index) {
        if (!((name[index] >= '0' && name[index] <= '9') ||
              (name[index] >= 'a' && name[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

void removeInterruptedFiles(const std::string& directory, const std::string& middle) {
    DIR* handle = ::opendir(directory.c_str());
    if (handle == nullptr) throw SpoolError("cannot scan interrupted spool files");
    while (dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        std::string path = directory;
        path += "/";
        path += name;
        if (temporaryName(name, middle) && ::unlink(path.c_str()) != 0) {
            ::closedir(handle);
            throw SpoolError("cannot remove interrupted spool file");
        }
    }
    ::closedir(handle);
}

}  // namespace

SpoolError::SpoolError(const std::string& message) : std::runtime_error(message) {}

SpoolStore::SpoolStore(const std::string& root, std::uint64_t max_bytes,
                       std::uint64_t min_free_bytes)
    : root_(root),
      state_dir_(root + "/states"),
      file_dir_(root + "/files"),
      max_bytes_(max_bytes),
      min_free_bytes_(min_free_bytes) {}

std::map<std::string, CollectorTask> SpoolStore::initialize() {
    ensureDirectory(root_);
    ensureDirectory(state_dir_);
    ensureDirectory(file_dir_);
    removeInterruptedFiles(state_dir_, ".json");
    removeInterruptedFiles(file_dir_, ".data");
    std::map<std::string, CollectorTask> tasks;
    DIR* directory = ::opendir(state_dir_.c_str());
    if (directory == nullptr) throw SpoolError("cannot scan spool state directory");
    try {
        while (dirent* entry = ::readdir(directory)) {
            const std::string name = entry->d_name;
            if (name.size() != 69 || name.compare(64, 5, ".json") != 0) continue;
            std::ifstream input((state_dir_ + "/" + name).c_str());
            nlohmann::json json;
            input >> json;
            CollectorTask task = fromJson(json);
            if (name.substr(0, 64) != task.task_id || !tasks.insert({task.task_id, task}).second) {
                throw SpoolError("spool task identity is inconsistent");
            }
        }
    } catch (const nlohmann::json::exception& error) {
        ::closedir(directory);
        throw SpoolError(std::string("invalid spool JSON: ") + error.what());
    } catch (const SpoolError&) {
        ::closedir(directory);
        throw;
    }
    ::closedir(directory);
    return tasks;
}

void SpoolStore::save(const CollectorTask& task) const {
    const std::string target = state_dir_ + "/" + task.task_id + ".json";
    const std::string temporary = target + ".tmp";
    const std::string data = toJson(task).dump();
    const int descriptor =
        ::open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0640);
    if (descriptor < 0) throw SpoolError("cannot create spool state");
    try {
        writeAll(descriptor, data);
        if (::fsync(descriptor) != 0 || ::close(descriptor) != 0 ||
            ::rename(temporary.c_str(), target.c_str()) != 0) {
            throw SpoolError("cannot commit spool state");
        }
    } catch (const SpoolError&) {
        ::close(descriptor);
        ::unlink(temporary.c_str());
        throw;
    }
    const int directory = ::open(state_dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0 || ::fsync(directory) != 0 || ::close(directory) != 0) {
        if (directory >= 0) ::close(directory);
        throw SpoolError("cannot fsync spool state directory");
    }
}

std::string SpoolStore::snapshot(const std::string& source_path, const std::string& task_id,
                                 std::uint64_t expected_size) const {
    struct statvfs filesystem;
    if (::statvfs(file_dir_.c_str(), &filesystem) != 0) {
        throw SpoolError("cannot inspect spool capacity");
    }
    const std::uint64_t available =
        static_cast<std::uint64_t>(filesystem.f_bavail) * filesystem.f_frsize;
    if (directoryBytes(file_dir_) + expected_size > max_bytes_ ||
        available < expected_size + min_free_bytes_) {
        throw SpoolError("spool capacity limit was exceeded");
    }
    std::string target = payloadPath(task_id);
    const std::string temporary = target + ".tmp";
    ::unlink(temporary.c_str());
    const int source = ::open(source_path.c_str(), O_RDONLY | O_CLOEXEC);
    const int output = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0640);
    if (source < 0 || output < 0) {
        if (source >= 0) ::close(source);
        if (output >= 0) ::close(output);
        throw SpoolError("cannot open spool snapshot files");
    }
    std::uint64_t copied = 0;
    char buffer[65536];
    while (true) {
        const ssize_t count = ::read(source, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) break;
        if (count == 0) break;
        std::size_t written = 0;
        while (written < static_cast<std::size_t>(count)) {
            const ssize_t result =
                ::write(output, buffer + written, static_cast<std::size_t>(count) - written);
            if (result < 0 && errno == EINTR) continue;
            if (result <= 0) {
                copied = UINT64_MAX;
                break;
            }
            written += static_cast<std::size_t>(result);
            copied += static_cast<std::uint64_t>(result);
        }
        if (copied == UINT64_MAX) break;
    }
    const bool committed = copied == expected_size && ::fsync(output) == 0 &&
                           ::close(source) == 0 && ::close(output) == 0 &&
                           ::rename(temporary.c_str(), target.c_str()) == 0;
    if (!committed) {
        ::close(source);
        ::close(output);
        ::unlink(temporary.c_str());
        throw SpoolError("source changed or spool snapshot failed");
    }
    const int directory = ::open(file_dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0 || ::fsync(directory) != 0 || ::close(directory) != 0) {
        if (directory >= 0) ::close(directory);
        throw SpoolError("cannot fsync spool payload directory");
    }
    return target;
}

std::string SpoolStore::payloadPath(const std::string& task_id) const {
    return file_dir_ + "/" + task_id + ".data";
}

void SpoolStore::removePayload(const CollectorTask& task) const {
    if (task.state != CollectorTaskState::Done) {
        throw SpoolError("only DONE task payload can be removed");
    }
    if (::unlink(task.payload_path.c_str()) != 0 && errno != ENOENT) {
        throw SpoolError("cannot remove archived spool payload");
    }
    const int directory = ::open(file_dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0 || ::fsync(directory) != 0 || ::close(directory) != 0) {
        if (directory >= 0) ::close(directory);
        throw SpoolError("cannot fsync spool payload directory");
    }
}

const char* collectorTaskStateName(CollectorTaskState state) {
    switch (state) {
        case CollectorTaskState::Discovered:
            return "DISCOVERED";
        case CollectorTaskState::Ready:
            return "READY";
        case CollectorTaskState::Uploading:
            return "UPLOADING";
        case CollectorTaskState::Completing:
            return "COMPLETING";
        case CollectorTaskState::Done:
            return "DONE";
        case CollectorTaskState::Failed:
            return "FAILED";
    }
    throw SpoolError("unknown collector task state");
}

}  // namespace datastream
}  // namespace smt
