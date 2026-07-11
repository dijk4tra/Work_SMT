/**
 * @file device_authenticator.h
 * @brief 声明设备 HMAC 身份认证与 Redis 防重放流程。
 */

#ifndef DATASTREAM_AUTH_DEVICE_AUTHENTICATOR_H_
#define DATASTREAM_AUTH_DEVICE_AUTHENTICATOR_H_

#include <wfrest/HttpServer.h>

#include <functional>
#include <string>

#include "datastream/common/api_response.h"
#include "datastream/config/app_config.h"
#include "datastream/device/device_repository.h"
#include "datastream/storage/redis_client.h"

namespace smt {
namespace datastream {

/// @brief 一次设备认证的业务结果。
struct DeviceAuthResult {
    ErrorCode code;
    std::string message;
    std::string request_id;
    DeviceIdentity identity;
};

/// @brief 严格校验设备请求头、签名、设备状态和请求唯一性。
class DeviceAuthenticator {
   public:
    /// @brief 保存认证依赖和时间、防重放配置。
    /// @param repository 设备资料仓储。
    /// @param redis Redis 客户端。
    /// @param config 设备认证配置。
    /// @param redis_config Redis 键前缀配置。
    /// @param timeout_ms Redis 命令观察超时毫秒数。
    DeviceAuthenticator(const DeviceRepository& repository, const RedisClient& redis,
                        const AuthConfig& config, const RedisConfig& redis_config, int timeout_ms);

    /// @brief 在当前 HTTP Series 中完成设备认证。
    /// @param request Wfrest HTTP 请求。
    /// @param series 当前请求的 Workflow Series。
    /// @param callback 认证链结束后的业务回调。
    void authenticate(const wfrest::HttpReq& request, SeriesWork& series,
                      const std::function<void(const DeviceAuthResult&)>& callback) const;

   private:
    const DeviceRepository& repository_;
    const RedisClient& redis_;
    AuthConfig config_;
    std::string redis_key_prefix_;
    int timeout_ms_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_AUTH_DEVICE_AUTHENTICATOR_H_
