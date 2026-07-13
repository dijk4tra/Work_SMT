/**
 * @file health_controller.cpp
 * @brief 实现 Gateway liveness 和 readiness 路由。
 */

#include "logtrace/api/health_controller.h"

#include <nlohmann/json.hpp>

#include "logtrace/common/api_response.h"
#include "logtrace/common/request_id.h"

namespace smt {
namespace logtrace {

HealthController::HealthController(const SearchRpcClient& search_rpc) : search_rpc_(search_rpc) {}

void HealthController::registerRoutes(wfrest::HttpServer& server) {
    server.GET("/health/live", [](const wfrest::HttpReq*, wfrest::HttpResp* response) {
        const std::string request_id = generateRequestId();
        sendApiResponse(response, ErrorCode::Ok, "success", request_id,
                        nlohmann::json{{"status", "alive"}});
    });

    server.GET("/health/ready", [this](const wfrest::HttpReq*, wfrest::HttpResp* response,
                                       SeriesWork* series) {
        const std::string request_id = generateRequestId();
        srpc::SRPCClientTask* task = search_rpc_.createHealthTask(
            request_id, [response, request_id](const RpcHealthResult& result) {
                switch (result.status) {
                    case RpcHealthStatus::Ready:
                        sendApiResponse(response, ErrorCode::Ok, "success", request_id,
                                        nlohmann::json{{"status", "ready"}});
                        return;
                    case RpcHealthStatus::NotReady:
                        sendApiResponse(response, ErrorCode::ServiceNotReady, result.message,
                                        request_id, nlohmann::json{{"status", "not_ready"}});
                        return;
                    case RpcHealthStatus::Timeout:
                        sendApiResponse(response, ErrorCode::SearchRpcTimeout, result.message,
                                        request_id, nlohmann::json{{"status", "not_ready"}});
                        return;
                    case RpcHealthStatus::Unavailable:
                        sendApiResponse(response, ErrorCode::SearchRpcUnavailable, result.message,
                                        request_id, nlohmann::json{{"status", "not_ready"}});
                        return;
                }
            });
        series->push_back(task);
    });
}

}  // namespace logtrace
}  // namespace smt
