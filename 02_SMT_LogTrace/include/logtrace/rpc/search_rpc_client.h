/**
 * @file search_rpc_client.h
 * @brief 声明 Gateway 使用的 Search Server SRPC 客户端。
 */

#ifndef LOGTRACE_RPC_SEARCH_RPC_CLIENT_H_
#define LOGTRACE_RPC_SEARCH_RPC_CLIENT_H_

#include <functional>
#include <memory>
#include <string>

#include "logtrace.srpc.h"
#include "logtrace/config/app_config.h"

namespace smt {
namespace logtrace {

/// @brief Gateway 观察到的健康 RPC 结果类别。
enum class RpcHealthStatus {
    Ready,        ///< RPC 成功且 Search Server 已就绪。
    NotReady,     ///< RPC 成功但 Search Server 依赖未就绪。
    Unavailable,  ///< RPC 连接、传输或协议失败。
    Timeout       ///< RPC 等待、连接或传输超时。
};

/// @brief 单次健康 RPC 的稳定结果。
struct RpcHealthResult {
    RpcHealthStatus status;
    std::string message;
};

/// @brief 包装生成的 SRPC Client 并固定超时和零重试。
class SearchRpcClient {
   public:
    /// @brief 根据 Gateway RPC 配置构造客户端。
    /// @param config Gateway 配置。
    explicit SearchRpcClient(const GatewayConfig& config);

    /// @brief 同步执行启动边界的健康检查。
    /// @param request_id 请求标识。
    /// @return 稳定健康结果。
    RpcHealthResult health(const std::string& request_id) const;

    /// @brief 创建尚未启动的异步健康 RPC 任务。
    /// @param request_id 请求标识。
    /// @param callback 完成后接收稳定结果。
    /// @return 尚未启动的 SRPC 任务。
    srpc::SRPCClientTask* createHealthTask(
        const std::string& request_id,
        const std::function<void(const RpcHealthResult&)>& callback) const;

   private:
    std::shared_ptr<rpc::LogSearchService::SRPCClient> client_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_RPC_SEARCH_RPC_CLIENT_H_
