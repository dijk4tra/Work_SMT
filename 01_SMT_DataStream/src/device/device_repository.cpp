/**
 * @file device_repository.cpp
 * @brief 实现设备认证资料查询和心跳时间更新仓储。
 */

#include "datastream/device/device_repository.h"

#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>

#include <vector>

namespace smt {
namespace datastream {
namespace {

std::string quoteSql(const std::string& value) {
    return "'" + protocol::MySQLUtil::escape_string_quote(value, '\'') + "'";
}

}  // namespace

DeviceRepository::DeviceRepository(const MySqlClient& mysql, int timeout_ms)
    : mysql_(mysql), timeout_ms_(timeout_ms) {}

WFMySQLTask* DeviceRepository::createLookupTask(
    const std::string& device_id,
    const std::function<void(const DeviceLookupResult&)>& callback) const {
    const std::string sql =
        "SELECT d.device_id,d.station_id,s.line_id,d.hmac_secret,"
        "d.enabled,s.enabled,l.enabled FROM device d JOIN station s ON s.station_id=d.station_id "
        "JOIN production_line l ON l.line_id=s.line_id WHERE d.device_id=" +
        quoteSql(device_id) + " LIMIT 1";
    return mysql_.createQuery(sql, timeout_ms_, [callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet()) {
            callback(DeviceLookupResult{DeviceLookupStatus::Unavailable, DeviceIdentity()});
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        std::vector<protocol::MySQLCell> row;
        if (!cursor.fetch_row(row)) {
            callback(DeviceLookupResult{DeviceLookupStatus::NotFound, DeviceIdentity()});
            return;
        }
        if (row.size() != 7) {
            callback(DeviceLookupResult{DeviceLookupStatus::Unavailable, DeviceIdentity()});
            return;
        }
        DeviceIdentity identity{
            row[0].as_string(), row[1].as_string(), row[2].as_string(), row[3].as_binary_string(),
            row[4].as_int() == 1 && row[5].as_int() == 1 && row[6].as_int() == 1};
        callback(DeviceLookupResult{DeviceLookupStatus::Found, identity});
    });
}

WFMySQLTask* DeviceRepository::createHeartbeatUpdateTask(
    const std::string& device_id, const std::string& software_version,
    const std::string& last_seen_mysql, const std::function<void(bool)>& callback) const {
    const std::string sql = "UPDATE device SET software_version=" + quoteSql(software_version) +
                            ",last_seen_at=" + quoteSql(last_seen_mysql) +
                            " WHERE device_id=" + quoteSql(device_id);
    return mysql_.createQuery(sql, timeout_ms_, [callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet()) {
            callback(false);
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        callback(cursor.get_affected_rows() == 1);
    });
}

WFMySQLTask* DeviceRepository::createBindingCheckTask(
    const std::string& collector_id, const std::string& device_id,
    const std::function<void(bool, bool)>& callback) const {
    const std::string sql = "SELECT enabled FROM collector_device_binding WHERE collector_id=" +
                            quoteSql(collector_id) + " AND device_id=" + quoteSql(device_id) +
                            " LIMIT 1";
    return mysql_.createQuery(sql, timeout_ms_, [callback](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet()) {
            callback(false, false);
            return;
        }
        protocol::MySQLResultCursor cursor(task->get_resp());
        std::vector<protocol::MySQLCell> row;
        callback(true, cursor.fetch_row(row) && row.size() == 1 && row[0].as_int() == 1);
    });
}

}  // namespace datastream
}  // namespace smt
