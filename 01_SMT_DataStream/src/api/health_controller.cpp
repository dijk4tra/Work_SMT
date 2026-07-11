/**
 * @file health_controller.cpp
 * @brief 实现 liveness 和 readiness HTTP 路由。
 */

#include "datastream/api/health_controller.h"

#include <workflow/WFTaskFactory.h>

#include <memory>
#include <nlohmann/json.hpp>

#include "datastream/common/api_response.h"
#include "datastream/common/request_id.h"

namespace smt {
namespace datastream {
namespace {

/// @brief 保存一次 readiness 请求中三个依赖的检查结果。
struct ReadinessState {
    bool mysql_ready;
    bool redis_ready;
    bool storage_ready;
};

}  // namespace

HealthController::HealthController(const MySqlClient& mysql, const RedisClient& redis,
                                   const StoragePaths& storage, int timeout_ms)
    : mysql_(mysql), redis_(redis), storage_(storage), timeout_ms_(timeout_ms) {}

void HealthController::registerRoutes(wfrest::HttpServer& server) {
    server.GET("/health/live", [](const wfrest::HttpReq*, wfrest::HttpResp* response) {
        const std::string request_id = generateRequestId();
        sendApiResponse(response, ErrorCode::Ok, "success", request_id,
                        nlohmann::json{{"status", "alive"}});
    });

    server.GET("/health/ready", [this](const wfrest::HttpReq*, wfrest::HttpResp* response,
                                       SeriesWork* series) {
        const std::shared_ptr<ReadinessState> state(
            new ReadinessState{false, false, storage_.ready()});

        WFMySQLTask* mysql_task =
            mysql_.createQuery("SELECT 1", timeout_ms_, [state](WFMySQLTask* task) {
                state->mysql_ready =
                    task->get_state() == WFT_STATE_SUCCESS && !task->get_resp()->is_error_packet();
            });
        WFRedisTask* redis_task = redis_.createCommand(
            "PING", std::vector<std::string>(), timeout_ms_, [state](WFRedisTask* task) {
                protocol::RedisValue result;
                if (task->get_state() == WFT_STATE_SUCCESS) {
                    task->get_resp()->get_result(result);
                }
                state->redis_ready = result.string_value() == "PONG";
            });
        WFTimerTask* finish_task =
            WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                const bool ready = state->mysql_ready && state->redis_ready && state->storage_ready;
                const std::string request_id = generateRequestId();
                sendApiResponse(response, ready ? ErrorCode::Ok : ErrorCode::ServiceNotReady,
                                ready ? "success" : "service is not ready", request_id,
                                nlohmann::json{{"status", ready ? "ready" : "not_ready"}});
            });

        series->push_back(mysql_task);
        series->push_back(redis_task);
        series->push_back(finish_task);
    });
}

}  // namespace datastream
}  // namespace smt
