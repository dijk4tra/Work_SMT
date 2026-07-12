/**
 * @file archive_controller.h
 * @brief 声明 Operator 归档列表与详情查询控制器。
 */

#ifndef DATASTREAM_API_ARCHIVE_CONTROLLER_H_
#define DATASTREAM_API_ARCHIVE_CONTROLLER_H_

#include <wfrest/HttpServer.h>

#include "datastream/archive/archive_repository.h"
#include "datastream/auth/operator_authenticator.h"
#include "datastream/config/app_config.h"

namespace smt {
namespace datastream {

/// @brief 校验运维身份并编排归档元数据只读查询。
class ArchiveController {
   public:
    /// @brief 保存归档查询接口依赖。
    /// @param authenticator Operator Bearer Token 校验器。
    /// @param repository MySQL 归档仓储。
    /// @param config 历史查询边界配置。
    ArchiveController(const OperatorAuthenticator& authenticator,
                      const ArchiveRepository& repository, const QueryConfig& config);

    /// @brief 注册归档列表和详情路由。
    /// @param server Wfrest HTTP 服务。
    void registerRoutes(wfrest::HttpServer& server);

   private:
    const OperatorAuthenticator& authenticator_;
    const ArchiveRepository& repository_;
    QueryConfig config_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_API_ARCHIVE_CONTROLLER_H_
