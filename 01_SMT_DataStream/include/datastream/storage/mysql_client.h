/**
 * @file mysql_client.h
 * @brief 封装 Workflow MySQL 任务创建和启动探测。
 */

#ifndef DATASTREAM_STORAGE_MYSQL_CLIENT_H_
#define DATASTREAM_STORAGE_MYSQL_CLIENT_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>

#include "datastream/config/app_config.h"

namespace smt {
namespace datastream {

/// @brief 使用固定连接配置创建无自动重试的 MySQL 异步任务。
class MySqlClient {
   public:
    /// @brief 构造 MySQL 客户端。
    /// @param config 已解析密码的 MySQL 配置。
    explicit MySqlClient(const MySqlConfig& config);

    /// @brief 创建尚未启动的 SQL 查询任务。
    /// @param sql 固定模板生成的 SQL。
    /// @param timeout_ms 网络任务总观察超时毫秒数。
    /// @param callback 任务完成回调。
    /// @return 由调用方加入 SeriesWork 或启动的 Workflow 任务。
    WFMySQLTask* createQuery(const std::string& sql, int timeout_ms,
                             const std::function<void(WFMySQLTask*)>& callback) const;

    /// @brief 同步等待一次只读连接探测。
    /// @param timeout_ms 网络任务总观察超时毫秒数。
    /// @return SELECT 1 成功且服务端未返回错误包时返回 true。
    bool ping(int timeout_ms) const;

   private:
    std::string uri_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_STORAGE_MYSQL_CLIENT_H_
