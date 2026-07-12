/**
 * @file cleanup_service.h
 * @brief 声明严格临时文件和结束态 Redis 会话清理服务。
 */

#ifndef DATASTREAM_CLEANUP_CLEANUP_SERVICE_H_
#define DATASTREAM_CLEANUP_CLEANUP_SERVICE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "datastream/config/app_config.h"
#include "datastream/storage/redis_client.h"
#include "datastream/storage/storage_paths.h"

namespace smt {
namespace datastream {

/// @brief 周期扫描无会话或结束态旧临时文件，不触碰正式归档。
class CleanupService {
   public:
    /// @brief 保存清理依赖和时间边界。
    /// @param redis Redis 客户端。
    /// @param redis_config Redis 键前缀。
    /// @param storage 已初始化存储目录。
    /// @param config 清理周期与保留时间。
    /// @param timeout_ms Redis 命令观察超时毫秒数。
    CleanupService(const RedisClient& redis, const RedisConfig& redis_config,
                   const StoragePaths& storage, const CleanupConfig& config, int timeout_ms);

    /// @brief 停止并回收后台线程。
    ~CleanupService();

    /// @brief 启动时立即扫描一次，之后按配置周期执行。
    void start();

    /// @brief 请求停止并等待当前扫描结束。
    void stop();

    /// @brief 同步执行一次严格清理，供启动扫描和测试使用。
    void runOnce();

   private:
    const RedisClient& redis_;
    const StoragePaths& storage_;
    std::string key_prefix_;
    CleanupConfig config_;
    int timeout_ms_;
    std::atomic<bool> started_;
    bool stopping_;
    std::mutex mutex_;
    std::condition_variable wakeup_;
    std::thread worker_;
};

/// @brief 判断文件名是否严格符合 UUID.part 临时文件格式。
/// @param filename 不含目录的文件名。
/// @return 格式严格匹配时返回 true。
bool isTemporaryUploadFilename(const std::string& filename);

/// @brief 扫描超过截止时间的严格临时文件。
/// @param temp_root 临时文件根目录。
/// @param cutoff_seconds mtime 必须早于该 Unix 秒。
/// @return 符合条件的绝对路径列表。
std::vector<std::string> findCleanupCandidates(const std::string& temp_root,
                                               std::int64_t cutoff_seconds);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_CLEANUP_CLEANUP_SERVICE_H_
