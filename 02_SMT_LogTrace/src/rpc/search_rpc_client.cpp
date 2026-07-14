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

RpcCallStatus classifyCallFailure(int timeout_reason) {
    return timeout_reason == TOR_NOT_TIMEOUT ? RpcCallStatus::Unavailable : RpcCallStatus::Timeout;
}

}  // namespace

SearchRpcClient::SearchRpcClient(const GatewayConfig& config) {
    const srpc::RPCClientParams params = makeClientParams(config);
    client_.reset(new rpc::LogSearchService::SRPCClient(&params));
}

srpc::SRPCClientTask* SearchRpcClient::createSearchLogsTask(
    const rpc::SearchLogsRequest& request,
    const std::function<void(const RpcCallResult<rpc::SearchLogsResponse>&)>& callback) const {
    srpc::SRPCClientTask* task = client_->create_SearchLogs_task(
        [callback](rpc::SearchLogsResponse* response, srpc::RPCContext* context) {
            callback(RpcCallResult<rpc::SearchLogsResponse>{
                context->success() ? RpcCallStatus::Success
                                   : classifyCallFailure(context->get_timeout_reason()),
                context->success() ? *response : rpc::SearchLogsResponse()});
        });
    task->serialize_input(&request);
    return task;
}

srpc::SRPCClientTask* SearchRpcClient::createListAnomaliesTask(
    const rpc::ListAnomaliesRequest& request,
    const std::function<void(const RpcCallResult<rpc::ListAnomaliesResponse>&)>& callback) const {
    srpc::SRPCClientTask* task = client_->create_ListAnomalies_task(
        [callback](rpc::ListAnomaliesResponse* response, srpc::RPCContext* context) {
            callback(RpcCallResult<rpc::ListAnomaliesResponse>{
                context->success() ? RpcCallStatus::Success
                                   : classifyCallFailure(context->get_timeout_reason()),
                context->success() ? *response : rpc::ListAnomaliesResponse()});
        });
    task->serialize_input(&request);
    return task;
}

srpc::SRPCClientTask* SearchRpcClient::createGetLogDetailTask(
    const rpc::GetLogDetailRequest& request,
    const std::function<void(const RpcCallResult<rpc::GetLogDetailResponse>&)>& callback) const {
    srpc::SRPCClientTask* task = client_->create_GetLogDetail_task(
        [callback](rpc::GetLogDetailResponse* response, srpc::RPCContext* context) {
            callback(RpcCallResult<rpc::GetLogDetailResponse>{
                context->success() ? RpcCallStatus::Success
                                   : classifyCallFailure(context->get_timeout_reason()),
                context->success() ? *response : rpc::GetLogDetailResponse()});
        });
    task->serialize_input(&request);
    return task;
}

srpc::SRPCClientTask* SearchRpcClient::createGetErrorCodeTask(
    const rpc::GetErrorCodeRequest& request,
    const std::function<void(const RpcCallResult<rpc::GetErrorCodeResponse>&)>& callback) const {
    srpc::SRPCClientTask* task = client_->create_GetErrorCode_task(
        [callback](rpc::GetErrorCodeResponse* response, srpc::RPCContext* context) {
            callback(RpcCallResult<rpc::GetErrorCodeResponse>{
                context->success() ? RpcCallStatus::Success
                                   : classifyCallFailure(context->get_timeout_reason()),
                context->success() ? *response : rpc::GetErrorCodeResponse()});
        });
    task->serialize_input(&request);
    return task;
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
