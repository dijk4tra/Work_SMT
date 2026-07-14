/**
 * @file clients_test.cpp
 * @brief 使用本机服务验证两个 MySQL 和 Redis 客户端。
 */

#include <gtest/gtest.h>

#include <cstdlib>

#include "logtrace/config/app_config.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/redis_client.h"

namespace smt {
namespace logtrace {
namespace {

TEST(ClientsIntegrationTest, ConnectsToConfiguredServices) {
    ASSERT_EQ(::setenv("SMT_LOGTRACE_OPERATOR_TOKEN", "integration-token", 1), 0);
    const AppConfig config = AppConfig::load(LOGTRACE_TEST_CONFIG_PATH);
    const MySqlClient source_mysql(config.source_mysql);
    const MySqlClient state_mysql(config.state_mysql);
    const RedisClient redis(config.redis);

    EXPECT_TRUE(source_mysql.ping(config.health.check_timeout_ms));
    EXPECT_TRUE(state_mysql.ping(config.health.check_timeout_ms));
    EXPECT_TRUE(redis.ping(config.health.check_timeout_ms));
}

TEST(ClientsIntegrationTest, ReportsUnavailableEndpoints) {
    ASSERT_EQ(::setenv("SMT_LOGTRACE_OPERATOR_TOKEN", "integration-token", 1), 0);
    AppConfig config = AppConfig::load(LOGTRACE_TEST_CONFIG_PATH);
    config.source_mysql.port = 1;
    config.state_mysql.port = 1;
    config.redis.port = 1;

    EXPECT_FALSE(MySqlClient(config.source_mysql).ping(config.health.check_timeout_ms));
    EXPECT_FALSE(MySqlClient(config.state_mysql).ping(config.health.check_timeout_ms));
    EXPECT_FALSE(RedisClient(config.redis).ping(config.health.check_timeout_ms));
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
