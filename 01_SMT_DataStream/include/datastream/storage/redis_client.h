/**
 * @file redis_client.h
 * @brief 封装 Workflow Redis 任务创建和启动探测。
 */

#ifndef DATASTREAM_STORAGE_REDIS_CLIENT_H_
#define DATASTREAM_STORAGE_REDIS_CLIENT_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>
#include <vector>

#include "datastream/config/app_config.h"

namespace smt {
namespace datastream {

/// @brief 使用固定连接配置创建无自动重试的 Redis 异步任务。
class RedisClient {
   public:
    /// @brief 构造 Redis 客户端。
    /// @param config 已解析可选密码的 Redis 配置。
    explicit RedisClient(const RedisConfig& config);

    /// @brief 创建尚未启动的 Redis 命令任务。
    /// @param command Redis 命令名称。
    /// @param params 命令参数。
    /// @param timeout_ms 网络任务总观察超时毫秒数。
    /// @param callback 任务完成回调。
    /// @return 由调用方加入 SeriesWork 或启动的 Workflow 任务。
    WFRedisTask* createCommand(const std::string& command, const std::vector<std::string>& params,
                               int timeout_ms,
                               const std::function<void(WFRedisTask*)>& callback) const;

    /// @brief 同步等待一次只读 PING 探测。
    /// @param timeout_ms 网络任务总观察超时毫秒数。
    /// @return Redis 返回 PONG 时返回 true。
    bool ping(int timeout_ms) const;

   private:
    std::string uri_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_STORAGE_REDIS_CLIENT_H_
