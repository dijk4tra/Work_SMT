/**
 * @file collector_config_test.cpp
 * @brief 验证参考采集程序配置和密钥环境边界。
 */

#include "datastream/collector/collector_config.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>

namespace smt {
namespace datastream {
namespace {

std::string writeConfig(const std::string& extra_field) {
    char path[] = "/tmp/collector-config-XXXXXX";
    const int descriptor = ::mkstemp(path);
    ::close(descriptor);
    std::ofstream output(path);
    output << "{\"server_url\":\"http://127.0.0.1:8080\","
              "\"spool_root\":\"/tmp/spool\",\"scan_interval_ms\":100,"
              "\"stable_scan_count\":3,\"request_timeout_ms\":1000,"
              "\"chunk_size_bytes\":1048576,\"spool_max_bytes\":10485760,"
              "\"spool_min_free_bytes\":0,\"retry\":{\"max_backoff_steps\":4,"
              "\"base_delay_ms\":10,\"max_delay_ms\":100},\"devices\":[{"
              "\"line_id\":\"LINE-01\",\"station_id\":\"ST-AOI-01\","
              "\"device_id\":\"AOI-VT-01\",\"collector_id\":\"IPC-L01-01\","
              "\"input_dir\":\"/tmp/inbox\",\"secret_env\":\"COLLECTOR_TEST_SECRET\","
              "\"seal_mode\":\"STABLE_WINDOW\"}]"
           << extra_field << "}";
    return path;
}

TEST(CollectorConfigTest, LoadsStrictDeviceMapping) {
    ::setenv("COLLECTOR_TEST_SECRET", "test-secret", 1);
    const std::string path = writeConfig("");
    const CollectorConfig config = CollectorConfig::load(path);
    ASSERT_EQ(config.devices.size(), 1U);
    EXPECT_EQ(config.devices[0].seal_mode, SealMode::StableWindow);
    EXPECT_EQ(config.devices[0].secret, "test-secret");
}

TEST(CollectorConfigTest, RejectsUnknownRootField) {
    ::setenv("COLLECTOR_TEST_SECRET", "test-secret", 1);
    const std::string path = writeConfig(",\"unknown\":1");
    EXPECT_THROW(CollectorConfig::load(path), CollectorConfigError);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
