/**
 * @file app_config_test.cpp
 * @brief 验证配置边界、环境变量和关联约束。
 */

#include "datastream/config/app_config.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace smt {
namespace datastream {
namespace {

std::string writeTemporaryConfig(const nlohmann::json& content) {
    const std::string path = "/tmp/datastream-config-test-" +
                             std::to_string(static_cast<long long>(::getpid())) + ".json";
    std::ofstream output(path.c_str());
    output << content.dump(2) << '\n';
    output.close();
    return path;
}

nlohmann::json readExampleConfig() {
    std::ifstream input(DATASTREAM_TEST_CONFIG_PATH);
    nlohmann::json content;
    input >> content;
    return content;
}

/// @brief 为配置测试设置和清理必要环境变量。
class AppConfigTest : public testing::Test {
   protected:
    /// @brief 设置测试使用的非生产凭据。
    void SetUp() override {
        ASSERT_EQ(::setenv("SMT_DATASTREAM_MYSQL_PASSWORD", "unit-test-password", 1), 0);
        ASSERT_EQ(::setenv("SMT_DATASTREAM_OPERATOR_TOKEN", "unit-test-operator-token", 1), 0);
    }

    /// @brief 清理测试进程修改的环境变量和临时文件。
    void TearDown() override {
        ::unsetenv("SMT_DATASTREAM_MYSQL_PASSWORD");
        ::unsetenv("SMT_DATASTREAM_OPERATOR_TOKEN");
        std::remove(temporary_path_.c_str());
    }

    std::string temporary_path_;
};

TEST_F(AppConfigTest, LoadsCompleteContract) {
    const AppConfig config = AppConfig::load(DATASTREAM_TEST_CONFIG_PATH);

    EXPECT_EQ(config.http.listen_address, "127.0.0.1");
    EXPECT_EQ(config.mysql.user, "root");
    EXPECT_EQ(config.mysql.password, "unit-test-password");
    EXPECT_EQ(config.redis.username, "default");
    EXPECT_TRUE(config.redis.password.empty());
    EXPECT_EQ(config.health.check_timeout_ms, 3000);
    EXPECT_LE(config.upload.max_chunk_size_bytes, config.http.request_body_limit_bytes);
}

TEST_F(AppConfigTest, RejectsMissingRequiredEnvironment) {
    ASSERT_EQ(::unsetenv("SMT_DATASTREAM_MYSQL_PASSWORD"), 0);
    EXPECT_THROW(AppConfig::load(DATASTREAM_TEST_CONFIG_PATH), ConfigError);
}

TEST_F(AppConfigTest, RejectsUnknownField) {
    nlohmann::json content = readExampleConfig();
    content["http"]["unknown_port"] = 8081;
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

TEST_F(AppConfigTest, RejectsInconsistentChunkLimit) {
    nlohmann::json content = readExampleConfig();
    content["http"]["request_body_limit_bytes"] = 1024;
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

TEST_F(AppConfigTest, RejectsWeakOperatorToken) {
    ASSERT_EQ(::setenv("SMT_DATASTREAM_OPERATOR_TOKEN", "short", 1), 0);
    EXPECT_THROW(AppConfig::load(DATASTREAM_TEST_CONFIG_PATH), ConfigError);
}

TEST_F(AppConfigTest, RejectsReplayTtlShorterThanAcceptedWindow) {
    nlohmann::json content = readExampleConfig();
    content["auth"]["request_id_ttl_seconds"] = 60;
    temporary_path_ = writeTemporaryConfig(content);

    EXPECT_THROW(AppConfig::load(temporary_path_), ConfigError);
}

}  // namespace
}  // namespace datastream
}  // namespace smt
