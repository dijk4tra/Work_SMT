/**
 * @file redis_client.h
 * @brief 声明 Workflow Redis 命令客户端。
 */

#ifndef LOGTRACE_STORAGE_REDIS_CLIENT_H_
#define LOGTRACE_STORAGE_REDIS_CLIENT_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>
#include <vector>

#include "logtrace/config/app_config.h"

namespace smt {
namespace logtrace {

/// @brief 使用固定连接配置创建 Workflow Redis 任务。
class RedisClient {
   public:
    /// @brief 根据配置构造不含日志输出的连接 URI。
    /// @param config Redis 配置。
    explicit RedisClient(const RedisConfig& config);

    /// @brief 创建尚未启动的 Redis 命令任务。
    /// @param command Redis 命令。
    /// @param params 命令参数。
    /// @param timeout_ms 观察超时毫秒数。
    /// @param callback 任务完成回调。
    /// @return 尚未启动的 Workflow Redis 任务。
    WFRedisTask* createCommand(const std::string& command, const std::vector<std::string>& params,
                               int timeout_ms,
                               const std::function<void(WFRedisTask*)>& callback) const;

    /// @brief 同步执行启动边界的 PING 探测。
    /// @param timeout_ms 观察超时毫秒数。
    /// @return Redis 返回 PONG 时为 true。
    bool ping(int timeout_ms) const;

   private:
    std::string uri_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_STORAGE_REDIS_CLIENT_H_
