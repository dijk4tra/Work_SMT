/**
 * @file search_health_service.h
 * @brief 声明 Search Server 的 SRPC 健康服务。
 */

#ifndef LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_
#define LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_

#include "logtrace.srpc.h"
#include "logtrace/cache/query_cache.h"
#include "logtrace/cache/slru_cache.h"
#include "logtrace/search/search_engine.h"
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
    const SearchEngine& search_engine;
    const RedisConfig& redis_config;
    const CacheConfig& cache_config;
};

/// @brief 错误码知识库的一条可缓存记录。
struct ErrorCodeKnowledge {
    std::string module_name;
    std::string title;
    std::string description;
    std::string recommended_action;
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

    /// @brief 执行日志组合检索。
    /// @param request 检索条件和分页。
    /// @param response 检索结果。
    /// @param context SRPC 调用上下文。
    void SearchLogs(rpc::SearchLogsRequest* request, rpc::SearchLogsResponse* response,
                    srpc::RPCContext* context) override;

    /// @brief 查询异常日志。
    /// @param request 结构化条件和分页。
    /// @param response 异常日志结果。
    /// @param context SRPC 调用上下文。
    void ListAnomalies(rpc::ListAnomaliesRequest* request, rpc::ListAnomaliesResponse* response,
                       srpc::RPCContext* context) override;

    /// @brief 返回日志元数据和精确原文。
    /// @param request 稳定 doc_id。
    /// @param response 日志详情。
    /// @param context SRPC 调用上下文。
    void GetLogDetail(rpc::GetLogDetailRequest* request, rpc::GetLogDetailResponse* response,
                      srpc::RPCContext* context) override;

    /// @brief 查询错误码知识和最近匹配日志。
    /// @param request 错误码。
    /// @param response 知识库记录和日志摘要。
    /// @param context SRPC 调用上下文。
    void GetErrorCode(rpc::GetErrorCodeRequest* request, rpc::GetErrorCodeResponse* response,
                      srpc::RPCContext* context) override;

   private:
    const MySqlClient& source_mysql_;
    const MySqlClient& state_mysql_;
    const RedisClient& redis_;
    const StoragePaths& storage_;
    const SearchEngine& search_engine_;
    QueryCache query_cache_;
    SlruCache<std::string, ErrorCodeKnowledge> error_code_cache_;
    int timeout_ms_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_RPC_SEARCH_HEALTH_SERVICE_H_
