/**
 * @file collector_app.cpp
 * @brief 实现三种封口判定、去重、spool 快照和上传状态机。
 */

#include "datastream/collector/collector_app.h"

#include <dirent.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <thread>

#include "datastream/auth/crypto.h"
#include "datastream/common/time_utils.h"

namespace smt {
namespace datastream {
namespace {

std::int64_t nowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t mtimeNanoseconds(const struct stat& info) {
    return static_cast<std::int64_t>(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec;
}

std::string hashFile(const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) throw SpoolError("cannot open payload for hashing");
    EVP_MD_CTX* raw = EVP_MD_CTX_new();
    if (raw == nullptr) {
        ::close(descriptor);
        throw SpoolError("cannot allocate payload digest context");
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw, EVP_MD_CTX_free);
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        ::close(descriptor);
        throw SpoolError("cannot initialize payload digest");
    }
    char buffer[65536];
    while (true) {
        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 ||
            EVP_DigestUpdate(context.get(), buffer, static_cast<std::size_t>(count)) != 1) {
            ::close(descriptor);
            throw SpoolError("cannot hash payload");
        }
        if (count == 0) break;
    }
    if (::close(descriptor) != 0) throw SpoolError("cannot close payload after hashing");
    unsigned char digest[32];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1 || length != 32) {
        throw SpoolError("cannot finalize payload digest");
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < length; ++index) {
        output << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return output.str();
}

const CollectorDeviceConfig* findDevice(const CollectorConfig& config,
                                        const std::string& device_id) {
    for (std::size_t index = 0; index < config.devices.size(); ++index) {
        if (config.devices[index].device_id == device_id) return &config.devices[index];
    }
    return nullptr;
}

bool suffix(const std::string& value, const std::string& ending) {
    return value.size() >= ending.size() &&
           value.compare(value.size() - ending.size(), ending.size(), ending) == 0;
}

CollectorTask metadataTask(const std::string& path, const CollectorDeviceConfig& device,
                           std::uint64_t size, std::int64_t mtime_ns, std::size_t chunk_size) {
    std::ifstream input((path + ".meta.json").c_str());
    nlohmann::json metadata;
    try {
        input >> metadata;
    } catch (const nlohmann::json::exception& error) {
        throw SpoolError(std::string("invalid device sidecar: ") + error.what());
    }
    static const std::set<std::string> expected{"work_order", "product_sn", "file_type", "result",
                                                "produced_at"};
    if (!metadata.is_object() || metadata.size() != expected.size()) {
        throw SpoolError("device sidecar fields do not match contract");
    }
    for (nlohmann::json::const_iterator it = metadata.begin(); it != metadata.end(); ++it) {
        if (expected.count(it.key()) == 0) throw SpoolError("device sidecar has unknown field");
    }
    if (!(metadata.at("work_order").is_null() || metadata.at("work_order").is_string()) ||
        !(metadata.at("product_sn").is_null() || metadata.at("product_sn").is_string()) ||
        !metadata.at("file_type").is_string() ||
        !(metadata.at("result").is_null() || metadata.at("result").is_string()) ||
        !metadata.at("produced_at").is_string()) {
        throw SpoolError("device sidecar field type is invalid");
    }
    CollectorTask task;
    task.source_path = path;
    task.line_id = device.line_id;
    task.station_id = device.station_id;
    task.device_id = device.device_id;
    task.collector_id = device.collector_id;
    task.work_order =
        metadata.at("work_order").is_null() ? "" : metadata.at("work_order").get<std::string>();
    task.product_sn =
        metadata.at("product_sn").is_null() ? "" : metadata.at("product_sn").get<std::string>();
    task.file_type = metadata.at("file_type").get<std::string>();
    task.result = metadata.at("result").is_null() ? "" : metadata.at("result").get<std::string>();
    task.produced_at = metadata.at("produced_at").get<std::string>();
    const std::size_t slash = path.rfind('/');
    task.original_filename = path.substr(slash + 1);
    task.file_size = size;
    task.source_mtime_ns = mtime_ns;
    task.chunk_size = chunk_size;
    task.chunk_count = static_cast<std::size_t>((size + chunk_size - 1) / chunk_size);
    task.state = CollectorTaskState::Discovered;
    task.retry_attempts = 0;
    task.next_attempt_milliseconds = 0;
    task.archive_id = 0;
    std::int64_t produced = 0;
    if (task.file_type.empty() || task.original_filename.empty() ||
        !parseIso8601Milliseconds(task.produced_at, &produced)) {
        throw SpoolError("device sidecar values are invalid");
    }
    return task;
}

}  // namespace

CollectorApp::CollectorApp(const CollectorConfig& config)
    : config_(config),
      spool_(config.spool_root, config.spool_max_bytes, config.spool_min_free_bytes),
      uploader_(config.server_url, config.request_timeout_ms) {}

void CollectorApp::initialize() {
    tasks_ = spool_.initialize();
    for (std::map<std::string, CollectorTask>::const_iterator it = tasks_.begin();
         it != tasks_.end(); ++it) {
        if (it->second.state == CollectorTaskState::Done) spool_.removePayload(it->second);
    }
    for (std::size_t index = 0; index < config_.devices.size(); ++index) {
        struct stat info;
        if (::stat(config_.devices[index].input_dir.c_str(), &info) != 0 ||
            !S_ISDIR(info.st_mode)) {
            throw SpoolError("device input directory is unavailable");
        }
    }
}

void CollectorApp::run(const std::atomic<bool>& stop) {
    while (!stop.load(std::memory_order_acquire)) {
        runOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.scan_interval_ms));
    }
}

void CollectorApp::runOnce() {
    scanInputs();
    processTasks();
}

std::size_t CollectorApp::taskCount() const { return tasks_.size(); }

void CollectorApp::scanInputs() {
    for (std::size_t device_index = 0; device_index < config_.devices.size(); ++device_index) {
        const CollectorDeviceConfig& device = config_.devices[device_index];
        DIR* raw_directory = ::opendir(device.input_dir.c_str());
        if (raw_directory == nullptr) throw SpoolError("cannot scan device input directory");
        std::unique_ptr<DIR, int (*)(DIR*)> directory(raw_directory, ::closedir);
        while (dirent* entry = ::readdir(directory.get())) {
            const std::string name = entry->d_name;
            if (name.empty() || name[0] == '.' || suffix(name, ".meta.json") ||
                suffix(name, ".done") || suffix(name, ".tmp") || suffix(name, ".part")) {
                continue;
            }
            const std::string path = device.input_dir + "/" + name;
            struct stat info;
            if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
            const std::uint64_t size = static_cast<std::uint64_t>(info.st_size);
            const std::int64_t mtime = mtimeNanoseconds(info);
            const std::pair<std::uint64_t, std::int64_t> signature(size, mtime);
            if (observations_.count(path) != 0 && observations_[path] == signature) {
                ++stable_counts_[path];
            } else {
                observations_[path] = signature;
                stable_counts_[path] = 1;
            }
            const bool sealed = device.seal_mode == SealMode::AtomicRename ||
                                (device.seal_mode == SealMode::StableWindow &&
                                 stable_counts_[path] >= config_.stable_scan_count) ||
                                (device.seal_mode == SealMode::DoneMarker &&
                                 ::access((path + ".done").c_str(), F_OK) == 0);
            if (!sealed || ::access((path + ".meta.json").c_str(), R_OK) != 0) continue;
            bool active_same_path = false;
            for (std::map<std::string, CollectorTask>::iterator it = tasks_.begin();
                 it != tasks_.end(); ++it) {
                if (it->second.source_path == path) {
                    if (it->second.file_size == size && it->second.source_mtime_ns == mtime) {
                        active_same_path = true;
                        break;
                    }
                    if (it->second.state != CollectorTaskState::Done &&
                        it->second.state != CollectorTaskState::Failed) {
                        failTask(&it->second, "SOURCE_CHANGED_AFTER_SEAL");
                        active_same_path = true;
                        break;
                    }
                }
            }
            if (active_same_path) continue;
            CollectorTask task;
            try {
                task = metadataTask(path, device, size, mtime, config_.chunk_size_bytes);
            } catch (const SpoolError& error) {
                task.task_id = sha256Hex(path + device.device_id + std::to_string(mtime));
                task.source_path = path;
                task.payload_path = spool_.payloadPath(task.task_id);
                task.line_id = device.line_id;
                task.station_id = device.station_id;
                task.device_id = device.device_id;
                task.collector_id = device.collector_id;
                task.file_size = size;
                task.source_mtime_ns = mtime;
                task.state = CollectorTaskState::Failed;
                task.chunk_size = config_.chunk_size_bytes;
                task.chunk_count = 0;
                task.retry_attempts = 0;
                task.next_attempt_milliseconds = 0;
                task.last_error = error.what();
                task.archive_id = 0;
                tasks_[task.task_id] = task;
                spool_.save(task);
                continue;
            }
            if (size == 0) {
                task.file_sha256 = sha256Hex("");
                task.task_id = sha256Hex(path + device.device_id + std::to_string(mtime));
                task.payload_path = spool_.payloadPath(task.task_id);
                task.state = CollectorTaskState::Failed;
                task.last_error = "ZERO_LENGTH_FILE";
                tasks_[task.task_id] = task;
                spool_.save(task);
                continue;
            }
            task.file_sha256 = hashFile(path);
            task.task_id = sha256Hex(path + "\n" + device.device_id + "\n" + std::to_string(size) +
                                     "\n" + std::to_string(mtime) + "\n" + task.file_sha256);
            if (tasks_.count(task.task_id) != 0) continue;
            try {
                task.payload_path = spool_.snapshot(path, task.task_id, size);
                if (hashFile(task.payload_path) != task.file_sha256) {
                    throw SpoolError("source changed while creating spool snapshot");
                }
            } catch (const SpoolError& error) {
                if (std::string(error.what()) == "spool capacity limit was exceeded") {
                    std::cerr << "collector_agent: " << error.what() << '\n';
                    continue;
                }
                task.payload_path = spool_.payloadPath(task.task_id);
                task.state = CollectorTaskState::Failed;
                task.last_error = error.what();
                tasks_[task.task_id] = task;
                spool_.save(task);
                continue;
            }
            task.state = CollectorTaskState::Ready;
            tasks_[task.task_id] = task;
            spool_.save(task);
        }
    }
}

void CollectorApp::processTasks() {
    const std::int64_t now = nowMilliseconds();
    for (std::map<std::string, CollectorTask>::iterator it = tasks_.begin(); it != tasks_.end();
         ++it) {
        CollectorTask& task = it->second;
        if (task.state != CollectorTaskState::Done && task.state != CollectorTaskState::Failed &&
            task.next_attempt_milliseconds <= now) {
            processTask(&task);
        }
    }
}

void CollectorApp::processTask(CollectorTask* task) {
    const CollectorDeviceConfig* device = findDevice(config_, task->device_id);
    if (device == nullptr) {
        failTask(task, "DEVICE_CONFIG_MISSING");
        return;
    }
    UploadCallResult result;
    if (task->state == CollectorTaskState::Ready) {
        result = uploader_.createSession(*task, *device);
        if (result.status == UploadCallStatus::Success) {
            task->upload_id = result.upload_id;
            task->chunk_size = result.chunk_size;
            task->chunk_count = result.chunk_count;
            task->state = CollectorTaskState::Uploading;
            task->retry_attempts = 0;
            task->next_attempt_milliseconds = 0;
            task->last_error.clear();
            spool_.save(*task);
            return;
        }
    } else if (task->state == CollectorTaskState::Uploading) {
        result = uploader_.queryProgress(*task, *device);
        if (result.status == UploadCallStatus::SessionMissing) {
            task->upload_id.clear();
            task->state = CollectorTaskState::Ready;
            task->retry_attempts = 0;
            task->last_error = "UPLOAD_SESSION_LOST";
            spool_.save(*task);
            return;
        }
        if (result.status == UploadCallStatus::Success) {
            for (std::set<std::size_t>::const_iterator chunk = result.missing_chunks.begin();
                 chunk != result.missing_chunks.end(); ++chunk) {
                const UploadCallResult uploaded = uploader_.uploadChunk(*task, *device, *chunk);
                if (uploaded.status != UploadCallStatus::Success) {
                    result = uploaded;
                    break;
                }
            }
            if (result.status == UploadCallStatus::Success) {
                task->state = CollectorTaskState::Completing;
                task->retry_attempts = 0;
                task->last_error.clear();
                spool_.save(*task);
                return;
            }
        }
    } else if (task->state == CollectorTaskState::Completing) {
        result = uploader_.complete(*task, *device);
        if (result.status == UploadCallStatus::SessionMissing) {
            task->upload_id.clear();
            task->state = CollectorTaskState::Ready;
            task->retry_attempts = 0;
            task->last_error = "UPLOAD_SESSION_LOST";
            spool_.save(*task);
            return;
        }
        if (result.status == UploadCallStatus::Success) {
            task->state = CollectorTaskState::Done;
            task->archive_id = result.archive_id;
            task->retry_attempts = 0;
            task->next_attempt_milliseconds = 0;
            task->last_error.clear();
            spool_.save(*task);
            spool_.removePayload(*task);
            return;
        }
    } else {
        return;
    }
    if (result.status == UploadCallStatus::Retryable) {
        scheduleRetry(task, result.error_code);
    } else {
        failTask(task, result.error_code);
    }
}

void CollectorApp::scheduleRetry(CollectorTask* task, const std::string& error_code) {
    task->retry_attempts = std::min(task->retry_attempts + 1, config_.retry.max_backoff_steps);
    std::int64_t delay = config_.retry.base_delay_ms;
    for (int exponent = 1; exponent < task->retry_attempts; ++exponent) {
        delay = std::min<std::int64_t>(delay * 2, config_.retry.max_delay_ms);
    }
    task->next_attempt_milliseconds = nowMilliseconds() + delay;
    task->last_error = error_code;
    spool_.save(*task);
}

void CollectorApp::failTask(CollectorTask* task, const std::string& error_code) {
    task->state = CollectorTaskState::Failed;
    task->last_error = error_code;
    task->next_attempt_milliseconds = 0;
    spool_.save(*task);
}

}  // namespace datastream
}  // namespace smt
