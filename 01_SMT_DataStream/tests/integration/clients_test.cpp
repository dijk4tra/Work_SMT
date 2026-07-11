/**
 * @file clients_test.cpp
 * @brief 使用本机服务验证 Workflow MySQL 和 Redis 客户端。
 */

#include <gtest/gtest.h>

#include "datastream/config/app_config.h"
#include "datastream/storage/mysql_client.h"
#include "datastream/storage/redis_client.h"

namespace smt {
namespace datastream {
namespace {

TEST(ClientsIntegrationTest, ConnectsToConfiguredServices) {
    const AppConfig config = AppConfig::load(DATASTREAM_TEST_CONFIG_PATH);
    const MySqlClient mysql(config.mysql);
    const RedisClient redis(config.redis);

    EXPECT_TRUE(mysql.ping(config.health.check_timeout_ms));
    EXPECT_TRUE(redis.ping(config.health.check_timeout_ms));
}

TEST(ClientsIntegrationTest, ReportsUnavailableRedis) {
    AppConfig config = AppConfig::load(DATASTREAM_TEST_CONFIG_PATH);
    config.redis.port = 1;
    const RedisClient redis(config.redis);

    EXPECT_FALSE(redis.ping(config.health.check_timeout_ms));
}

TEST(ClientsIntegrationTest, ReportsUnavailableMySql) {
    AppConfig config = AppConfig::load(DATASTREAM_TEST_CONFIG_PATH);
    config.mysql.port = 1;
    const MySqlClient mysql(config.mysql);

    EXPECT_FALSE(mysql.ping(config.health.check_timeout_ms));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
