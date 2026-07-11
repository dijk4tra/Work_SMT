/**
 * @file upload_repository.h
 * @brief 声明 Redis 上传会话、配额、分片摘要和 Bitmap 操作。
 */

#ifndef DATASTREAM_UPLOAD_UPLOAD_REPOSITORY_H_
#define DATASTREAM_UPLOAD_UPLOAD_REPOSITORY_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>
#include <vector>

#include "datastream/config/app_config.h"
#include "datastream/storage/redis_client.h"
#include "datastream/upload/upload_model.h"

namespace smt {
namespace datastream {

/// @brief 创建 Redis 上传会话的结果。
enum class CreateSessionStatus {
    Created,        ///< 会话和配额记录已创建。
    LimitExceeded,  ///< 任一会话或预留字节配额已达到。
    Unavailable     ///< Redis 命令失败。
};

/// @brief Redis 上传会话查询结果。
enum class SessionLookupStatus {
    Found,       ///< 找到完整会话。
    NotFound,    ///< 会话不存在或已过期。
    Unavailable  ///< Redis 失败或返回损坏状态。
};

/// @brief 分片写入占位结果。
enum class BeginChunkStatus {
    Writable,         ///< 当前请求可以写入文件。
    AlreadyComplete,  ///< 相同摘要的分片已经完成。
    ContentConflict,  ///< 当前编号已登记不同摘要。
    StateConflict,    ///< 会话状态不允许写入。
    NotFound,         ///< 会话不存在或已过期。
    Unavailable       ///< Redis 命令失败。
};

/// @brief 通过固定 Lua 脚本维护上传会话的 Redis 适配器。
class UploadRepository {
   public:
    /// @brief 保存 Redis 客户端和上传配置。
    /// @param redis Redis 客户端。
    /// @param redis_config Redis 键前缀。
    /// @param upload_config 会话 TTL 与配额配置。
    /// @param timeout_ms Redis 命令观察超时毫秒数。
    UploadRepository(const RedisClient& redis, const RedisConfig& redis_config,
                     const UploadConfig& upload_config, int timeout_ms);

    /// @brief 创建会话并原子占用三类数量配额和全局字节配额。
    /// @param session 待保存会话。
    /// @param metadata 已校验创建请求。
    /// @param callback 返回创建状态。
    /// @return 尚未启动的 Redis 任务。
    WFRedisTask* createSessionTask(const UploadSession& session,
                                   const CreateUploadRequest& metadata,
                                   const std::function<void(CreateSessionStatus)>& callback) const;

    /// @brief 查询一个完整上传会话。
    /// @param upload_id 上传编号。
    /// @param callback 返回查询状态和会话。
    /// @return 尚未启动的 Redis 任务。
    WFRedisTask* createSessionLookupTask(
        const std::string& upload_id,
        const std::function<void(SessionLookupStatus, const UploadSession&)>& callback) const;

    /// @brief 在文件写入前登记分片摘要并判断幂等状态。
    /// @param upload_id 上传编号。
    /// @param chunk_no 分片编号。
    /// @param digest 请求体摘要。
    /// @param callback 返回写入占位结果。
    /// @return 尚未启动的 Redis 任务。
    WFRedisTask* createBeginChunkTask(const std::string& upload_id, std::size_t chunk_no,
                                      const std::string& digest,
                                      const std::function<void(BeginChunkStatus)>& callback) const;

    /// @brief 文件写满后置位 Bitmap 并刷新三个会话键的 TTL。
    /// @param session 已查询的上传会话。
    /// @param chunk_no 分片编号。
    /// @param digest 已写入分片摘要。
    /// @param callback 返回命令是否成功。
    /// @return 尚未启动的 Redis 任务。
    WFRedisTask* createFinishChunkTask(const UploadSession& session, std::size_t chunk_no,
                                       const std::string& digest,
                                       const std::function<void(bool)>& callback) const;

    /// @brief 读取上传会话的原始 Bitmap。
    /// @param upload_id 上传编号。
    /// @param callback 返回 Redis 是否可用及 Bitmap 字节串。
    /// @return 尚未启动的 Redis 任务。
    WFRedisTask* createBitmapTask(
        const std::string& upload_id,
        const std::function<void(bool, const std::string&)>& callback) const;

   private:
    const RedisClient& redis_;
    std::string key_prefix_;
    UploadConfig config_;
    int timeout_ms_;
};

/// @brief 判断 Redis Bitmap 中指定分片是否完成。
/// @param bitmap Redis GET 返回的字节串。
/// @param chunk_no 分片编号。
/// @return 对应位为 1 时返回 true。
bool bitmapContains(const std::string& bitmap, std::size_t chunk_no);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_UPLOAD_UPLOAD_REPOSITORY_H_
