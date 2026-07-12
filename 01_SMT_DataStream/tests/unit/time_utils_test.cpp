/**
 * @file time_utils_test.cpp
 * @brief 验证 ISO 8601 时区换算和日历边界校验。
 */

#include "datastream/common/time_utils.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace smt {
namespace datastream {
namespace {

TEST(TimeUtilsTest, ConvertsOffsetTimeToSameUnixMilliseconds) {
    std::int64_t utc = 0;
    std::int64_t china = 0;
    ASSERT_TRUE(parseIso8601Milliseconds("2026-07-11T00:00:00.123Z", &utc));
    ASSERT_TRUE(parseIso8601Milliseconds("2026-07-11T08:00:00.123+08:00", &china));
    EXPECT_EQ(utc, china);
}

TEST(TimeUtilsTest, RejectsInvalidCalendarAndTimezone) {
    std::int64_t value = 0;
    EXPECT_FALSE(parseIso8601Milliseconds("2026-02-29T00:00:00.000Z", &value));
    EXPECT_FALSE(parseIso8601Milliseconds("2026-07-11T00:00:00Z", &value));
    EXPECT_FALSE(parseIso8601Milliseconds("2026-07-11T00:00:00.000+14:30", &value));
}

TEST(TimeUtilsTest, ProducesUtcApiAndMysqlFormats) {
    const ServerTime now = currentServerTime();
    EXPECT_EQ(now.iso8601.size(), 24U);
    EXPECT_EQ(now.iso8601.back(), 'Z');
    EXPECT_EQ(now.mysql.size(), 23U);
}

TEST(TimeUtilsTest, ConvertsMysqlDatetimeWithoutSessionTimezone) {
    std::string iso8601;
    ASSERT_TRUE(mysqlDateTimeToIso8601("2026-07-11 00:00:00.123", &iso8601));
    EXPECT_EQ(iso8601, "2026-07-11T00:00:00.123Z");
    EXPECT_FALSE(mysqlDateTimeToIso8601("2026-07-11 00:00:00", &iso8601));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
