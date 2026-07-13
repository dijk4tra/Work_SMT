/**
 * @file search_health_service.h
 * @brief 声明 Search Server 的 SRPC 健康服务。
 */

#ifndef LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_
#define LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_

#include "logtrace.srpc.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/redis_client.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief Search 健康服务依赖集合，避免同类型构造参数顺序混淆。
struct SearchHealthDependencies {
    const MySqlClient& source_mysql;
    const MySqlClient& state_mysql;
    const RedisClient& redis;
    const StoragePaths& storage;
};

/// @brief 异步检查两个 MySQL、Redis 和目录状态。
class SearchHealthService final : public rpc::LogSearchService::Service {
   public:
    /// @brief 保存健康检查依赖。
    /// @param dependencies 两个 MySQL、Redis 和目录依赖。
    /// @param timeout_ms 单次依赖检查观察超时。
    SearchHealthService(const SearchHealthDependencies& dependencies, int timeout_ms);

    /// @brief 响应 SRPC 健康请求，并在同一 Series 中异步检查依赖。
    /// @param request 健康请求。
    /// @param response 健康响应。
    /// @param context SRPC 调用上下文。
    void Health(rpc::HealthRequest* request, rpc::HealthResponse* response,
                srpc::RPCContext* context) override;

   private:
    const MySqlClient& source_mysql_;
    const MySqlClient& state_mysql_;
    const RedisClient& redis_;
    const StoragePaths& storage_;
    int timeout_ms_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_
