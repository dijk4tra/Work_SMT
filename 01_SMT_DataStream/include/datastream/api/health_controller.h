/**
 * @file health_controller.h
 * @brief 声明存活与就绪健康检查路由控制器。
 */

#ifndef DATASTREAM_API_HEALTH_CONTROLLER_H_
#define DATASTREAM_API_HEALTH_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "datastream/storage/mysql_client.h"
#include "datastream/storage/redis_client.h"
#include "datastream/storage/storage_paths.h"

namespace smt {
namespace datastream {

/// @brief 注册不泄露依赖细节的健康检查接口。
class HealthController {
   public:
    /// @brief 保存健康检查所需依赖。
    /// @param mysql MySQL 客户端。
    /// @param redis Redis 客户端。
    /// @param storage 文件存储目录管理器。
    /// @param timeout_ms 单个依赖检查超时毫秒数。
    HealthController(const MySqlClient& mysql, const RedisClient& redis,
                     const StoragePaths& storage, int timeout_ms);

    /// @brief 在指定 Wfrest 服务中注册健康路由。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const MySqlClient& mysql_;
    const RedisClient& redis_;
    const StoragePaths& storage_;
    int timeout_ms_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_API_HEALTH_CONTROLLER_H_
