/**
 * @file heartbeat_test.cpp
 * @brief 验证设备心跳 JSON 的业务字段边界。
 */

#include "datastream/device/heartbeat.h"

#include <gtest/gtest.h>

#include <string>

namespace smt {
namespace datastream {
namespace {

TEST(HeartbeatTest, ParsesRunningHeartbeat) {
    const std::string body = R"({"collector_id":"IPC-L01-01","software_version":"5.4.7",)"
                             R"("runtime_status":"RUNNING","work_order":"WO-20260711-001",)"
                             R"("reported_at":"2026-07-11T08:00:00.000+08:00"})";
    DeviceHeartbeat heartbeat;
    std::string error;
    ASSERT_TRUE(parseDeviceHeartbeat(body, &heartbeat, &error));
    EXPECT_EQ(heartbeat.collector_id, "IPC-L01-01");
    EXPECT_TRUE(heartbeat.has_work_order);
}

TEST(HeartbeatTest, AllowsNullWorkOrderOnlyWhenIdle) {
    const std::string idle = R"({"collector_id":"IPC-L01-01","software_version":"5.4.7",)"
                             R"("runtime_status":"IDLE","work_order":null,)"
                             R"("reported_at":"2026-07-11T08:00:00.000+08:00"})";
    const std::string alarm = R"({"collector_id":"IPC-L01-01","software_version":"5.4.7",)"
                              R"("runtime_status":"ALARM","work_order":null,)"
                              R"("reported_at":"2026-07-11T08:00:00.000+08:00"})";
    DeviceHeartbeat heartbeat;
    std::string error;
    EXPECT_TRUE(parseDeviceHeartbeat(idle, &heartbeat, &error));
    EXPECT_FALSE(parseDeviceHeartbeat(alarm, &heartbeat, &error));
}

TEST(HeartbeatTest, RejectsUnknownFieldAndInvalidReportedTime) {
    const std::string unknown = R"({"collector_id":"IPC-L01-01","software_version":"5.4.7",)"
                                R"("runtime_status":"RUNNING","work_order":"WO-1",)"
                                R"("reported_at":"2026-07-11T08:00:00.000+08:00","extra":1})";
    const std::string invalid_time = R"({"collector_id":"IPC-L01-01","software_version":"5.4.7",)"
                                     R"("runtime_status":"RUNNING","work_order":"WO-1",)"
                                     R"("reported_at":"2026-07-11 08:00:00"})";
    DeviceHeartbeat heartbeat;
    std::string error;
    EXPECT_FALSE(parseDeviceHeartbeat(unknown, &heartbeat, &error));
    EXPECT_FALSE(parseDeviceHeartbeat(invalid_time, &heartbeat, &error));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
