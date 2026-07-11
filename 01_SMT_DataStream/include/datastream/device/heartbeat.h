/**
 * @file heartbeat.h
 * @brief 声明设备心跳请求模型及严格 JSON 解析接口。
 */

#ifndef DATASTREAM_DEVICE_HEARTBEAT_H_
#define DATASTREAM_DEVICE_HEARTBEAT_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 已通过接口契约校验的设备心跳。
struct DeviceHeartbeat {
    std::string collector_id;
    std::string software_version;
    std::string runtime_status;
    std::string work_order;
    bool has_work_order;
    std::string reported_at;
};

/// @brief 严格解析设备心跳 JSON。
/// @param body HTTP 请求体。
/// @param heartbeat 成功时接收已校验模型。
/// @param error_message 失败时接收不含请求正文的原因。
/// @return JSON 结构与全部字段符合契约时返回 true。
bool parseDeviceHeartbeat(const std::string& body, DeviceHeartbeat* heartbeat,
                          std::string* error_message);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_DEVICE_HEARTBEAT_H_
