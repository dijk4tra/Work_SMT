/**
 * @file heartbeat.cpp
 * @brief 实现设备心跳 JSON 的严格结构和字段边界校验。
 */

#include "datastream/device/heartbeat.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>

#include "datastream/common/time_utils.h"
#include "datastream/common/validation.h"

namespace smt {
namespace datastream {
namespace {

bool isPrintableAscii(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x20 || character > 0x7e) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool parseDeviceHeartbeat(const std::string& body, DeviceHeartbeat* heartbeat,
                          std::string* error_message) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        *error_message = "request body must be a JSON object";
        return false;
    }
    const std::set<std::string> expected{"collector_id", "software_version", "runtime_status",
                                         "work_order", "reported_at"};
    if (json.size() != expected.size()) {
        *error_message = "heartbeat fields do not match the contract";
        return false;
    }
    for (nlohmann::json::const_iterator item = json.begin(); item != json.end(); ++item) {
        if (expected.count(item.key()) == 0) {
            *error_message = "heartbeat fields do not match the contract";
            return false;
        }
    }
    if (!json["collector_id"].is_string() || !json["software_version"].is_string() ||
        !json["runtime_status"].is_string() || !json["reported_at"].is_string() ||
        !(json["work_order"].is_string() || json["work_order"].is_null())) {
        *error_message = "heartbeat field type is invalid";
        return false;
    }

    heartbeat->collector_id = json["collector_id"].get<std::string>();
    heartbeat->software_version = json["software_version"].get<std::string>();
    heartbeat->runtime_status = json["runtime_status"].get<std::string>();
    heartbeat->has_work_order = json["work_order"].is_string();
    heartbeat->work_order =
        heartbeat->has_work_order ? json["work_order"].get<std::string>() : std::string();
    heartbeat->reported_at = json["reported_at"].get<std::string>();

    if (!isSmtIdentifier(heartbeat->collector_id)) {
        *error_message = "collector_id is invalid";
        return false;
    }
    if (!isPrintableAscii(heartbeat->software_version, 64)) {
        *error_message = "software_version is invalid";
        return false;
    }
    if (heartbeat->runtime_status != "RUNNING" && heartbeat->runtime_status != "IDLE" &&
        heartbeat->runtime_status != "ALARM") {
        *error_message = "runtime_status is invalid";
        return false;
    }
    if ((!heartbeat->has_work_order && heartbeat->runtime_status != "IDLE") ||
        (heartbeat->has_work_order && !isPrintableAscii(heartbeat->work_order, 64))) {
        *error_message = "work_order is invalid";
        return false;
    }
    std::int64_t reported_milliseconds = 0;
    if (!parseIso8601Milliseconds(heartbeat->reported_at, &reported_milliseconds)) {
        *error_message = "reported_at is invalid";
        return false;
    }
    return true;
}

}  // namespace datastream
}  // namespace smt
