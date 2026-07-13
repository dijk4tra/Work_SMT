/**
 * @file mysql_client.h
 * @brief 声明 Workflow MySQL 查询客户端。
 */

#ifndef LOGTRACE_STORAGE_MYSQL_CLIENT_H_
#define LOGTRACE_STORAGE_MYSQL_CLIENT_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>

#include "logtrace/config/app_config.h"

namespace smt {
namespace logtrace {

/// @brief 使用固定连接配置创建 Workflow MySQL 任务。
class MySqlClient {
   public:
    /// @brief 根据配置构造不含日志输出的连接 URI。
    /// @param config MySQL 配置。
    explicit MySqlClient(const MySqlConfig& config);

    /// @brief 创建尚未启动的 SQL 查询任务。
    /// @param sql 固定模板生成的 SQL。
    /// @param timeout_ms 观察超时毫秒数。
    /// @param callback 任务完成回调。
    /// @return 尚未启动的 Workflow MySQL 任务。
    WFMySQLTask* createQuery(const std::string& sql, int timeout_ms,
                             const std::function<void(WFMySQLTask*)>& callback) const;

    /// @brief 同步执行启动边界的 SELECT 1 探测。
    /// @param timeout_ms 观察超时毫秒数。
    /// @return MySQL 可连接且返回正常协议包时为 true。
    bool ping(int timeout_ms) const;

   private:
    std::string uri_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_STORAGE_MYSQL_CLIENT_H_
