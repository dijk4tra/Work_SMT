/**
 * @file upload_controller.h
 * @brief 声明上传会话创建、分片写入和进度查询控制器。
 */

#ifndef DATASTREAM_API_UPLOAD_CONTROLLER_H_
#define DATASTREAM_API_UPLOAD_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "datastream/auth/device_authenticator.h"
#include "datastream/config/app_config.h"
#include "datastream/device/device_repository.h"
#include "datastream/storage/storage_paths.h"
#include "datastream/upload/upload_repository.h"

namespace smt {
namespace datastream {

/// @brief 编排设备认证、Redis 会话和 POSIX 临时文件操作。
class UploadController {
   public:
    /// @brief 保存上传接口所需依赖。
    /// @param authenticator 设备认证器。
    /// @param device_repository 设备与采集器绑定仓储。
    /// @param upload_repository Redis 上传会话仓储。
    /// @param storage 文件存储目录。
    /// @param config 完整应用配置。
    UploadController(const DeviceAuthenticator& authenticator,
                     const DeviceRepository& device_repository,
                     const UploadRepository& upload_repository, const StoragePaths& storage,
                     const AppConfig& config);

    /// @brief 注册创建会话、上传分片和查询进度路由。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const DeviceAuthenticator& authenticator_;
    const DeviceRepository& device_repository_;
    const UploadRepository& upload_repository_;
    const StoragePaths& storage_;
    UploadConfig config_;
    std::size_t body_limit_bytes_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_API_UPLOAD_CONTROLLER_H_
