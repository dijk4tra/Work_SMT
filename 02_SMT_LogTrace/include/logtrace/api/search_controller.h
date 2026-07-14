/**
 * @file search_controller.h
 * @brief 声明日志检索、异常、详情和错误码 HTTP 路由。
 */

#ifndef LOGTRACE_API_SEARCH_CONTROLLER_H_
#define LOGTRACE_API_SEARCH_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "logtrace/auth/operator_authenticator.h"
#include "logtrace/rpc/search_rpc_client.h"

namespace smt {
namespace logtrace {

/// @brief 校验业务 HTTP 输入并异步调用 Search Server。
class SearchController {
   public:
    /// @brief 保存 Operator 认证器和 Search RPC 客户端。
    /// @param authenticator Operator Bearer Token 校验器。
    /// @param search_rpc Search Server 客户端。
    SearchController(const OperatorAuthenticator& authenticator, const SearchRpcClient& search_rpc);

    /// @brief 注册四类 `/api/v1` 业务路由。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const OperatorAuthenticator& authenticator_;
    const SearchRpcClient& search_rpc_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_API_SEARCH_CONTROLLER_H_
