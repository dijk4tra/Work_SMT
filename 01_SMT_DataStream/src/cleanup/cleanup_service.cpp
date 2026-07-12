/**
 * @file cleanup_service.cpp
 * @brief 实现严格临时文件和结束态 Redis 会话清理。
 */

#include "datastream/cleanup/cleanup_service.h"

#include <dirent.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <workflow/WFFacilities.h>

#include <chrono>
#include <cstring>

#include "datastream/common/time_utils.h"
#include "datastream/common/uuid.h"

namespace smt {
namespace datastream {
namespace {

enum class CleanupSessionStatus { Missing, Terminal, Active, Unavailable };

CleanupSessionStatus lookupSession(const RedisClient& redis, const std::string& key,
                                   int timeout_ms) {
    CleanupSessionStatus status = CleanupSessionStatus::Unavailable;
    WFFacilities::WaitGroup wait_group(1);
    WFRedisTask* task = redis.createCommand(
        "HGET", {key, "state"}, timeout_ms, [&status, &wait_group](WFRedisTask* completed) {
            if (completed->get_state() == WFT_STATE_SUCCESS) {
                protocol::RedisValue value;
                completed->get_resp()->get_result(value);
                if (value.is_nil()) {
                    status = CleanupSessionStatus::Missing;
                } else if (value.is_string()) {
                    const std::string state = value.string_value();
                    status = state == "FAILED" || state == "ARCHIVED"
                                 ? CleanupSessionStatus::Terminal
                                 : CleanupSessionStatus::Active;
                }
            }
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return status;
}

bool deleteSessionKeys(const RedisClient& redis, const std::string& key, int timeout_ms) {
    bool deleted = false;
    WFFacilities::WaitGroup wait_group(1);
    WFRedisTask* task =
        redis.createCommand("DEL", {key, key + ":chunks", key + ":digests"}, timeout_ms,
                            [&deleted, &wait_group](WFRedisTask* completed) {
                                if (completed->get_state() == WFT_STATE_SUCCESS) {
                                    protocol::RedisValue value;
                                    completed->get_resp()->get_result(value);
                                    deleted = value.is_int();
                                }
                                wait_group.done();
                            });
    task->start();
    wait_group.wait();
    return deleted;
}

}  // namespace

CleanupService::CleanupService(const RedisClient& redis, const RedisConfig& redis_config,
                               const StoragePaths& storage, const CleanupConfig& config,
                               int timeout_ms)
    : redis_(redis),
      storage_(storage),
      key_prefix_(redis_config.key_prefix),
      config_(config),
      timeout_ms_(timeout_ms),
      started_(false),
      stopping_(false) {}

CleanupService::~CleanupService() { stop(); }

void CleanupService::start() {
    if (started_.exchange(true)) {
        return;
    }
    stopping_ = false;
    worker_ = std::thread([this]() {
        while (true) {
            runOnce();
            std::unique_lock<std::mutex> lock(mutex_);
            if (wakeup_.wait_for(lock, std::chrono::seconds(config_.interval_seconds),
                                 [this]() { return stopping_; })) {
                break;
            }
        }
    });
}

void CleanupService::stop() {
    if (!started_.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wakeup_.notify_one();
    worker_.join();
}

void CleanupService::runOnce() {
    const std::int64_t cutoff = currentUnixSeconds() - config_.expired_retention_seconds;
    const std::vector<std::string> candidates = findCleanupCandidates(storage_.tempRoot(), cutoff);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const std::string& path = candidates[index];
        const std::size_t slash = path.rfind('/');
        const std::string filename = path.substr(slash + 1);
        const std::string upload_id = filename.substr(0, 36);
        const std::string key = key_prefix_ + "upload:" + upload_id;
        const CleanupSessionStatus status = lookupSession(redis_, key, timeout_ms_);
        if (status == CleanupSessionStatus::Unavailable || status == CleanupSessionStatus::Active) {
            continue;
        }
        if (::unlink(path.c_str()) != 0) {
            spdlog::warn("event=cleanup_delete_failed path={} errno={}", path, errno);
            continue;
        }
        if (status == CleanupSessionStatus::Terminal) {
            if (!deleteSessionKeys(redis_, key, timeout_ms_)) {
                spdlog::warn("event=cleanup_redis_delete_failed upload_id={}", upload_id);
            }
        }
        spdlog::info("event=cleanup_deleted upload_id={} state={}", upload_id,
                     status == CleanupSessionStatus::Missing ? "EXPIRED" : "TERMINAL");
    }
}

bool isTemporaryUploadFilename(const std::string& filename) {
    return filename.size() == 41 && filename.compare(36, 5, ".part") == 0 &&
           isUuid(filename.substr(0, 36));
}

std::vector<std::string> findCleanupCandidates(const std::string& temp_root,
                                               std::int64_t cutoff_seconds) {
    std::vector<std::string> candidates;
    DIR* directory = ::opendir(temp_root.c_str());
    if (directory == nullptr) {
        spdlog::warn("event=cleanup_scan_failed path={} errno={}", temp_root, errno);
        return candidates;
    }
    while (dirent* entry = ::readdir(directory)) {
        const std::string filename = entry->d_name;
        if (!isTemporaryUploadFilename(filename)) {
            if (filename != "." && filename != "..") {
                spdlog::warn("event=cleanup_unknown_file path={}/{}", temp_root, filename);
            }
            continue;
        }
        std::string path = temp_root;
        path += "/";
        path += filename;
        struct stat info;
        if (::lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
            info.st_mtime < cutoff_seconds) {
            candidates.push_back(path);
        }
    }
    ::closedir(directory);
    return candidates;
}

}  // namespace datastream
}  // namespace smt
