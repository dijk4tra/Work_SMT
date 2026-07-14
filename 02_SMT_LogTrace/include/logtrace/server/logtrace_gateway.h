/**
 * @file logtrace_gateway.h
 * @brief 声明 Wfrest Gateway 初始化和生命周期。
 */

#ifndef LOGTRACE_SERVER_LOGTRACE_GATEWAY_H_
#define LOGTRACE_SERVER_LOGTRACE_GATEWAY_H_

#include <wfrest/HttpServer.h>

#include "logtrace/api/health_controller.h"
#include "logtrace/api/search_controller.h"
#include "logtrace/auth/operator_authenticator.h"
#include "logtrace/config/app_config.h"
#include "logtrace/rpc/search_rpc_client.h"

namespace smt {
namespace logtrace {

/// @brief 对外提供 HTTP 并通过 SRPC 调用 Search Server。
class LogTraceGateway {
   public:
    /// @brief 根据完整配置构造 RPC 客户端并注册健康路由。
    /// @param config 完整应用配置。
    explicit LogTraceGateway(const AppConfig& config);

    /// @brief 在监听前确认 Search Server 已就绪。
    /// @throws std::runtime_error 当 Search Server 未就绪时抛出。
    void initialize();

    /// @brief 启动 Wfrest HTTP 监听。
    /// @return 监听成功时为 true。
    bool start();

    /// @brief 停止 HTTP 服务。
    void stop();

   private:
    const AppConfig& config_;
    SearchRpcClient search_rpc_;
    OperatorAuthenticator authenticator_;
    HealthController health_controller_;
    SearchController search_controller_;
    wfrest::HttpServer http_server_;
    bool started_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_SERVER_LOGTRACE_GATEWAY_H_
