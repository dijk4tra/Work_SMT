/**
 * @file device_repository.h
 * @brief 声明设备认证资料查询和心跳时间更新仓储。
 */

#ifndef DATASTREAM_DEVICE_DEVICE_REPOSITORY_H_
#define DATASTREAM_DEVICE_DEVICE_REPOSITORY_H_

#include <workflow/WFTaskFactory.h>

#include <functional>
#include <string>

#include "datastream/storage/mysql_client.h"

namespace smt {
namespace datastream {

/// @brief 设备及所属工位、产线的认证资料。
struct DeviceIdentity {
    std::string device_id;
    std::string station_id;
    std::string line_id;
    std::string hmac_secret;
    bool enabled;
};

/// @brief 设备查询结果状态。
enum class DeviceLookupStatus {
    Found,       ///< 找到设备资料。
    NotFound,    ///< 设备未登记。
    Unavailable  ///< MySQL 查询失败。
};

/// @brief 设备资料查询结果。
struct DeviceLookupResult {
    DeviceLookupStatus status;
    DeviceIdentity identity;
};

/// @brief 使用固定 SQL 模板访问设备主数据。
class DeviceRepository {
   public:
    /// @brief 保存 MySQL 客户端和查询超时。
    /// @param mysql MySQL 客户端。
    /// @param timeout_ms 单次查询观察超时毫秒数。
    DeviceRepository(const MySqlClient& mysql, int timeout_ms);

    /// @brief 创建设备及所属层级查询任务。
    /// @param device_id 已完成格式校验的设备编号。
    /// @param callback 返回找到、未找到或依赖不可用状态。
    /// @return 尚未启动的 Workflow MySQL 任务。
    WFMySQLTask* createLookupTask(
        const std::string& device_id,
        const std::function<void(const DeviceLookupResult&)>& callback) const;

    /// @brief 创建软件版本和最后在线时间更新任务。
    /// @param device_id 已认证设备编号。
    /// @param software_version 已校验软件版本。
    /// @param last_seen_mysql UTC MySQL DATETIME(3) 字符串。
    /// @param callback 返回 SQL 是否成功影响一行。
    /// @return 尚未启动的 Workflow MySQL 任务。
    WFMySQLTask* createHeartbeatUpdateTask(const std::string& device_id,
                                           const std::string& software_version,
                                           const std::string& last_seen_mysql,
                                           const std::function<void(bool)>& callback) const;

    /// @brief 创建采集器与设备启用绑定查询任务。
    /// @param collector_id 已校验采集器编号。
    /// @param device_id 已认证设备编号。
    /// @param callback 返回查询是否成功以及绑定是否启用。
    /// @return 尚未启动的 Workflow MySQL 任务。
    WFMySQLTask* createBindingCheckTask(const std::string& collector_id,
                                        const std::string& device_id,
                                        const std::function<void(bool, bool)>& callback) const;

   private:
    const MySqlClient& mysql_;
    int timeout_ms_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_DEVICE_DEVICE_REPOSITORY_H_
