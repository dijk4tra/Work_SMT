/**
 * @file device_authenticator.cpp
 * @brief 实现设备 HMAC 身份认证与 Redis 防重放流程。
 */

#include "datastream/auth/device_authenticator.h"

#include <workflow/WFTaskFactory.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "datastream/auth/crypto.h"
#include "datastream/common/request_id.h"
#include "datastream/common/time_utils.h"
#include "datastream/common/validation.h"

namespace smt {
namespace datastream {
namespace {

/// @brief 保存异步认证链使用的请求数据和最终结果。
struct AuthState {
    std::string method;
    std::string path;
    std::string body;
    std::string device_id;
    std::string timestamp;
    std::string request_id;
    std::string content_sha256;
    std::string signature;
    DeviceAuthResult result;
};

bool isRequestId(const std::string& value) {
    if (value.size() < 16 || value.size() > 64) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (!(std::isalnum(character) || character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

bool isLowerHexDigest(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!(std::isdigit(static_cast<unsigned char>(value[index])) ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool parseUnixSeconds(const std::string& value, std::int64_t* seconds) {
    if (value.empty() || value.size() > 11) {
        return false;
    }
    std::int64_t parsed = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
        parsed = parsed * 10 + (value[index] - '0');
    }
    *seconds = parsed;
    return true;
}

DeviceAuthResult errorResult(ErrorCode code, const std::string& message,
                             const std::string& request_id) {
    return DeviceAuthResult{code, message, request_id, DeviceIdentity()};
}

}  // namespace

DeviceAuthenticator::DeviceAuthenticator(const DeviceRepository& repository,
                                         const RedisClient& redis, const AuthConfig& config,
                                         const RedisConfig& redis_config, int timeout_ms)
    : repository_(repository),
      redis_(redis),
      config_(config),
      redis_key_prefix_(redis_config.key_prefix),
      timeout_ms_(timeout_ms) {}

void DeviceAuthenticator::authenticate(
    const wfrest::HttpReq& request, SeriesWork& series,
    const std::function<void(const DeviceAuthResult&)>& callback) const {
    const char* required_headers[] = {"X-Device-Id", "X-Timestamp", "X-Request-Id",
                                      "X-Content-SHA256", "X-Signature"};
    for (std::size_t index = 0; index < sizeof(required_headers) / sizeof(required_headers[0]);
         ++index) {
        if (!request.has_header(required_headers[index])) {
            callback(errorResult(ErrorCode::AuthRequired, "device authentication is required",
                                 generateRequestId()));
            return;
        }
    }

    const std::shared_ptr<AuthState> state(new AuthState());
    state->method = request.get_method();
    state->path = request.current_path();
    state->body = request.body();
    state->device_id = request.header("X-Device-Id");
    state->timestamp = request.header("X-Timestamp");
    state->request_id = request.header("X-Request-Id");
    state->content_sha256 = request.header("X-Content-SHA256");
    state->signature = request.header("X-Signature");
    state->result =
        errorResult(ErrorCode::SignatureInvalid, "device signature is invalid", state->request_id);

    std::int64_t request_seconds = 0;
    if (!isSmtIdentifier(state->device_id) || !isRequestId(state->request_id) ||
        !isLowerHexDigest(state->content_sha256) || !isLowerHexDigest(state->signature) ||
        !parseUnixSeconds(state->timestamp, &request_seconds)) {
        callback(state->result);
        return;
    }
    if (std::llabs(currentUnixSeconds() - request_seconds) > config_.timestamp_tolerance_seconds) {
        callback(errorResult(ErrorCode::TimestampExpired, "device timestamp is expired",
                             state->request_id));
        return;
    }

    WFMySQLTask* lookup = repository_.createLookupTask(
        state->device_id, [this, state, &series](const DeviceLookupResult& lookup_result) {
            if (lookup_result.status == DeviceLookupStatus::Unavailable) {
                state->result = errorResult(ErrorCode::MySqlUnavailable, "MySQL is unavailable",
                                            state->request_id);
                return;
            }
            if (lookup_result.status == DeviceLookupStatus::NotFound) {
                state->result = errorResult(ErrorCode::DeviceNotFound, "device was not found",
                                            state->request_id);
                return;
            }
            if (!lookup_result.identity.enabled) {
                state->result =
                    errorResult(ErrorCode::DeviceDisabled, "device is disabled", state->request_id);
                return;
            }

            const std::string actual_digest = sha256Hex(state->body);
            const std::string canonical = buildDeviceCanonicalString(
                state->method, state->path, state->device_id, state->timestamp, state->request_id,
                state->content_sha256);
            const std::string actual_signature =
                hmacSha256Hex(lookup_result.identity.hmac_secret, canonical);
            if (!constantTimeEquals(actual_digest, state->content_sha256) ||
                !constantTimeEquals(actual_signature, state->signature)) {
                return;
            }

            state->result = DeviceAuthResult{ErrorCode::Ok, "success", state->request_id,
                                             lookup_result.identity};
            const std::string key =
                redis_key_prefix_ + "auth:req:" + state->device_id + ":" + state->request_id;
            std::vector<std::string> params{key, state->timestamp, "NX", "EX",
                                            std::to_string(config_.request_id_ttl_seconds)};
            WFRedisTask* replay =
                redis_.createCommand("SET", params, timeout_ms_, [state](WFRedisTask* task) {
                    if (task->get_state() != WFT_STATE_SUCCESS) {
                        state->result = errorResult(ErrorCode::RedisUnavailable,
                                                    "Redis is unavailable", state->request_id);
                        return;
                    }
                    protocol::RedisValue value;
                    task->get_resp()->get_result(value);
                    if (value.is_nil()) {
                        state->result = errorResult(ErrorCode::RequestReplayed,
                                                    "request was replayed", state->request_id);
                    } else if (value.is_error() || value.string_value() != "OK") {
                        state->result = errorResult(ErrorCode::RedisUnavailable,
                                                    "Redis is unavailable", state->request_id);
                    }
                });
            series.push_front(replay);
        });
    WFTimerTask* finish = WFTaskFactory::create_timer_task(
        0, 0, [state, callback](WFTimerTask*) { callback(state->result); });
    series.push_back(lookup);
    series.push_back(finish);
}

}  // namespace datastream
}  // namespace smt
