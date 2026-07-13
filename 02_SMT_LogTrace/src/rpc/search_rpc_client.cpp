/**
 * @file search_rpc_client.cpp
 * @brief 实现 Gateway 到 Search Server 的健康 RPC 客户端。
 */

#include "logtrace/rpc/search_rpc_client.h"

#include <workflow/CommRequest.h>

namespace smt {
namespace logtrace {
namespace {

srpc::RPCClientParams makeClientParams(const GatewayConfig& config) {
    srpc::RPCClientParams params = srpc::RPC_CLIENT_PARAMS_DEFAULT;
    params.host = config.rpc_host;
    params.port = config.rpc_port;
    params.task_params.send_timeout = config.rpc_timeout_ms;
    params.task_params.receive_timeout = config.rpc_timeout_ms;
    params.task_params.watch_timeout = config.rpc_timeout_ms;
    params.task_params.retry_max = 0;
    params.callee_timeout = config.rpc_timeout_ms;
    params.caller = "logtrace_gateway";
    return params;
}

RpcHealthResult classifyResponse(const rpc::HealthResponse& response) {
    if (response.status() == rpc::SERVICE_STATUS_READY && response.code() == "OK") {
        return RpcHealthResult{RpcHealthStatus::Ready, response.message()};
    }
    if (response.status() == rpc::SERVICE_STATUS_NOT_READY &&
        response.code() == "SERVICE_NOT_READY") {
        return RpcHealthResult{RpcHealthStatus::NotReady, response.message()};
    }
    return RpcHealthResult{RpcHealthStatus::Unavailable, "invalid health RPC response"};
}

RpcHealthResult classifyFailure(int timeout_reason) {
    if (timeout_reason != TOR_NOT_TIMEOUT) {
        return RpcHealthResult{RpcHealthStatus::Timeout, "search RPC timed out"};
    }
    return RpcHealthResult{RpcHealthStatus::Unavailable, "search RPC is unavailable"};
}

}  // namespace

SearchRpcClient::SearchRpcClient(const GatewayConfig& config) {
    const srpc::RPCClientParams params = makeClientParams(config);
    client_.reset(new rpc::LogSearchService::SRPCClient(&params));
}

RpcHealthResult SearchRpcClient::health(const std::string& request_id) const {
    rpc::HealthRequest request;
    request.set_request_id(request_id);
    rpc::HealthResponse response;
    srpc::RPCSyncContext context;
    client_->Health(&request, &response, &context);
    if (!context.success) {
        return classifyFailure(context.timeout_reason);
    }
    return classifyResponse(response);
}

srpc::SRPCClientTask* SearchRpcClient::createHealthTask(
    const std::string& request_id,
    const std::function<void(const RpcHealthResult&)>& callback) const {
    srpc::SRPCClientTask* task = client_->create_Health_task(
        [callback](rpc::HealthResponse* response, srpc::RPCContext* context) {
            if (!context->success()) {
                callback(classifyFailure(context->get_timeout_reason()));
                return;
            }
            callback(classifyResponse(*response));
        });
    rpc::HealthRequest request;
    request.set_request_id(request_id);
    task->serialize_input(&request);
    return task;
}

}  // namespace logtrace
}  // namespace smt
