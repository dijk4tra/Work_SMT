/**
 * @file datastream_server.h
 * @brief 定义数据采集服务第一阶段生命周期。
 */

#ifndef DATASTREAM_SERVER_DATASTREAM_SERVER_H_
#define DATASTREAM_SERVER_DATASTREAM_SERVER_H_

#include <wfrest/HttpServer.h>

#include "datastream/api/health_controller.h"
#include "datastream/config/app_config.h"
#include "datastream/storage/mysql_client.h"
#include "datastream/storage/redis_client.h"
#include "datastream/storage/storage_paths.h"

namespace smt {
namespace datastream {

/// @brief 组合配置、基础设施客户端、存储目录和 Wfrest 服务。
class DataStreamServer {
   public:
    /// @brief 使用完整配置构造服务对象。
    /// @param config 已完成边界校验的应用配置。
    explicit DataStreamServer(const AppConfig& config);

    /// @brief 初始化目录并验证 MySQL 和 Redis 可用性。
    /// @throws StorageError 当文件目录不满足运行条件时抛出。
    /// @throws std::runtime_error 当 MySQL 或 Redis 启动探测失败时抛出。
    void initialize();

    /// @brief 绑定配置地址并开始接收 HTTP 请求。
    /// @return 启动成功时返回 true。
    bool start();

    /// @brief 停止接收请求并等待已进入的请求结束。
    void stop();

   private:
    AppConfig config_;
    StoragePaths storage_;
    MySqlClient mysql_;
    RedisClient redis_;
    wfrest::HttpServer http_server_;
    HealthController health_controller_;
    bool started_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_SERVER_DATASTREAM_SERVER_H_
