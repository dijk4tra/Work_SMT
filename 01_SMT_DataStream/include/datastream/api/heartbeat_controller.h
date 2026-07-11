/**
 * @file heartbeat_controller.h
 * @brief 声明设备心跳认证、持久化和在线状态路由控制器。
 */

#ifndef DATASTREAM_API_HEARTBEAT_CONTROLLER_H_
#define DATASTREAM_API_HEARTBEAT_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "datastream/auth/device_authenticator.h"
#include "datastream/config/app_config.h"
#include "datastream/device/device_repository.h"
#include "datastream/storage/redis_client.h"

namespace smt {
namespace datastream {

/// @brief 处理设备心跳并同步 MySQL 主数据与 Redis 在线状态。
class HeartbeatController {
   public:
    /// @brief 保存心跳链路依赖和边界配置。
    /// @param authenticator 设备认证器。
    /// @param repository 设备仓储。
    /// @param redis Redis 客户端。
    /// @param config 完整应用配置。
    HeartbeatController(const DeviceAuthenticator& authenticator,
                        const DeviceRepository& repository, const RedisClient& redis,
                        const AppConfig& config);

    /// @brief 注册设备心跳路由。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const DeviceAuthenticator& authenticator_;
    const DeviceRepository& repository_;
    const RedisClient& redis_;
    std::string redis_key_prefix_;
    int heartbeat_ttl_seconds_;
    int timeout_ms_;
    std::size_t body_limit_bytes_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_API_HEARTBEAT_CONTROLLER_H_
