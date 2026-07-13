/**
 * @file app_config_test.cpp
 * @brief 验证双进程配置、环境变量和关联约束。
 */

#include "logtrace/config/app_config.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace smt {
namespace logtrace {
namespace {

std::string writeTemporaryConfig(const nlohmann::json& content) {
    const std::string path =
        "/tmp/logtrace-config-test-" + std::to_string(static_cast<long long>(::getpid())) + ".json";
    std::ofstream output(path.c_str());
    output << content.dump(2) << '\n';
    output.close();
    return path;
}

nlohmann::json readExampleConfig() {
    std::ifstream input(LOGTRACE_TEST_CONFIG_PATH);
    nlohmann::json content;
    input >> content;
    return content;
}

/// @brief 为配置测试管理必要环境变量和临时文件。
class AppConfigTest : public testing::Test {
   protected:
    /// @brief 设置测试专用非生产密码。
    void SetUp() override {
        ASSERT_EQ(::setenv("SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD", "source-unit-password", 1), 0);
        ASSERT_EQ(::setenv("SMT_LOGTRACE_STATE_MYSQL_PASSWORD", "state-unit-password", 1), 0);
    }

    /// @brief 清理环境变量和临时文件。
    void TearDown() override {
        ::unsetenv("SMT_LOGTRACE_SOURCE_MYSQL_PASSWORD");
        ::unsetenv("SMT_LOGTRACE_STATE_MYSQL_PASSWORD");
        std::remove(temporary_path_.c_str());
    }

    std::string temporary_path_;
};

TEST_F(AppConfigTest, LoadsCompleteContract) {
    const AppConfig config = AppConfig::load(LOGTRACE_TEST_CONFIG_PATH);

    EXPECT_EQ(config.gateway.port, 8081);
    EXPECT_EQ(config.gateway.rpc_port, 1413);
    EXPECT_EQ(config.search_rpc.port, 1413);
    EXPECT_EQ(config.source_mysql.database, "smt_datastream");
    EXPECT_EQ(config.state_mysql.database, "smt_logtrace");
    EXPECT_EQ(config.source_mysql.password, "source-unit-password");
    EXPECT_EQ(config.state_mysql.password, "state-unit-password");
    EXPECT_TRUE(config.redis.password.empty());
}

TEST_F(AppConfigTest, RejectsMissingStatePassword) {
    ASSERT_EQ(::unsetenv("SMT_LOGTRACE_STATE_MYSQL_PASSWORD"), 0);
    EXPECT_THROW(AppConfig::load(LOGTRACE_TEST_CONFIG_PATH), ConfigError);
}

TEST_F(AppConfigTest, RejectsUnknownGatewayField) {
    nlohmann::json content = readExampleConfig();
    content["gateway"]["retry_count"] = 3;
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

TEST_F(AppConfigTest, RejectsSameSourceAndStateDatabase) {
    nlohmann::json content = readExampleConfig();
    content["state_mysql"]["database"] = "smt_datastream";
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

TEST_F(AppConfigTest, RejectsInvalidRpcTimeout) {
    nlohmann::json content = readExampleConfig();
    content["gateway"]["rpc_timeout_ms"] = 0;
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

TEST_F(AppConfigTest, RejectsSharedLogFile) {
    nlohmann::json content = readExampleConfig();
    content["logging"]["search_file"] = content["logging"]["gateway_file"];
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
