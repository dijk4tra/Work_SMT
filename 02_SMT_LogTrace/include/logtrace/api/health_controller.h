/**
 * @file health_controller.h
 * @brief 声明 Gateway 健康检查 HTTP 路由。
 */

#ifndef LOGTRACE_API_HEALTH_CONTROLLER_H_
#define LOGTRACE_API_HEALTH_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "logtrace/rpc/search_rpc_client.h"

namespace smt {
namespace logtrace {

/// @brief 提供 Gateway liveness 和 Search Server readiness。
class HealthController {
   public:
    /// @brief 保存 Search RPC 客户端。
    /// @param search_rpc Search Server 客户端。
    explicit HealthController(const SearchRpcClient& search_rpc);

    /// @brief 注册 `/health/live` 与 `/health/ready`。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const SearchRpcClient& search_rpc_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_API_HEALTH_CONTROLLER_H_
