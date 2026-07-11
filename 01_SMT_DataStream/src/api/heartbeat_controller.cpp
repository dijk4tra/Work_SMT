/**
 * @file heartbeat_controller.cpp
 * @brief 实现设备心跳认证、MySQL 更新和 Redis 在线状态写入。
 */

#include "datastream/api/heartbeat_controller.h"

#include <spdlog/spdlog.h>
#include <workflow/WFTaskFactory.h>

#include <memory>
#include <vector>

#include "datastream/common/api_response.h"
#include "datastream/common/request_id.h"
#include "datastream/common/time_utils.h"
#include "datastream/device/heartbeat.h"

namespace smt {
namespace datastream {
namespace {

/// @brief 保存一次心跳写入链的业务数据和依赖状态。
struct HeartbeatState {
    DeviceAuthResult auth;
    DeviceHeartbeat heartbeat;
    ServerTime server_time;
    bool mysql_saved;
    bool redis_saved;
};

const char kHeartbeatScript[] =
    "redis.call('HSET',KEYS[1],'collector_id',ARGV[1],'software_version',ARGV[2],"
    "'runtime_status',ARGV[3],'work_order',ARGV[4],'reported_at',ARGV[5],"
    "'last_seen_at',ARGV[6]);redis.call('EXPIRE',KEYS[1],ARGV[7]);return 1";

}  // namespace

HeartbeatController::HeartbeatController(const DeviceAuthenticator& authenticator,
                                         const DeviceRepository& repository,
                                         const RedisClient& redis, const AppConfig& config)
    : authenticator_(authenticator),
      repository_(repository),
      redis_(redis),
      redis_key_prefix_(config.redis.key_prefix),
      heartbeat_ttl_seconds_(config.device.heartbeat_ttl_seconds),
      timeout_ms_(config.health.check_timeout_ms),
      body_limit_bytes_(config.http.request_body_limit_bytes) {}

void HeartbeatController::registerRoutes(wfrest::HttpServer& server) {
    server.POST("/api/v1/devices/heartbeat", [this](const wfrest::HttpReq* request,
                                                    wfrest::HttpResp* response,
                                                    SeriesWork* series) {
        if (request->body().size() > body_limit_bytes_) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "request body is too large",
                            generateRequestId(), nullptr);
            return;
        }
        authenticator_.authenticate(
            *request, *series, [this, request, response, series](const DeviceAuthResult& auth) {
                if (auth.code != ErrorCode::Ok) {
                    spdlog::warn("device_auth_failed code={} request_id={}",
                                 errorCodeName(auth.code), auth.request_id);
                    sendApiResponse(response, auth.code, auth.message, auth.request_id, nullptr);
                    return;
                }
                if (request->content_type() != wfrest::APPLICATION_JSON) {
                    sendApiResponse(response, ErrorCode::InvalidArgument,
                                    "Content-Type must be application/json", auth.request_id,
                                    nullptr);
                    return;
                }

                const std::shared_ptr<HeartbeatState> state(new HeartbeatState());
                state->auth = auth;
                state->mysql_saved = false;
                state->redis_saved = false;
                std::string error_message;
                if (!parseDeviceHeartbeat(request->body(), &state->heartbeat, &error_message)) {
                    sendApiResponse(response, ErrorCode::InvalidArgument, error_message,
                                    auth.request_id, nullptr);
                    return;
                }
                state->server_time = currentServerTime();

                WFMySQLTask* mysql_task = repository_.createHeartbeatUpdateTask(
                    auth.identity.device_id, state->heartbeat.software_version,
                    state->server_time.mysql, [this, state, series](bool saved) {
                        state->mysql_saved = saved;
                        if (!saved) {
                            return;
                        }
                        const std::string key =
                            redis_key_prefix_ + "heartbeat:" + state->auth.identity.device_id;
                        std::vector<std::string> params{kHeartbeatScript,
                                                        "1",
                                                        key,
                                                        state->heartbeat.collector_id,
                                                        state->heartbeat.software_version,
                                                        state->heartbeat.runtime_status,
                                                        state->heartbeat.work_order,
                                                        state->heartbeat.reported_at,
                                                        state->server_time.iso8601,
                                                        std::to_string(heartbeat_ttl_seconds_)};
                        WFRedisTask* redis_task = redis_.createCommand(
                            "EVAL", params, timeout_ms_, [state](WFRedisTask* task) {
                                if (task->get_state() != WFT_STATE_SUCCESS) {
                                    return;
                                }
                                protocol::RedisValue value;
                                task->get_resp()->get_result(value);
                                state->redis_saved = value.is_int() && value.int_value() == 1;
                            });
                        series->push_front(redis_task);
                    });
                WFTimerTask* finish =
                    WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                        if (!state->mysql_saved) {
                            spdlog::error("heartbeat_mysql_update_failed request_id={}",
                                          state->auth.request_id);
                            sendApiResponse(response, ErrorCode::MySqlUnavailable,
                                            "MySQL is unavailable", state->auth.request_id,
                                            nullptr);
                            return;
                        }
                        if (!state->redis_saved) {
                            spdlog::error("heartbeat_redis_update_failed request_id={}",
                                          state->auth.request_id);
                            sendApiResponse(response, ErrorCode::RedisUnavailable,
                                            "Redis is unavailable", state->auth.request_id,
                                            nullptr);
                            return;
                        }
                        sendApiResponse(
                            response, ErrorCode::Ok, "success", state->auth.request_id,
                            nlohmann::json{{"device_id", state->auth.identity.device_id},
                                           {"last_seen_at", state->server_time.iso8601},
                                           {"online", true}});
                    });
                series->push_back(mysql_task);
                series->push_back(finish);
            });
    });
}

}  // namespace datastream
}  // namespace smt
