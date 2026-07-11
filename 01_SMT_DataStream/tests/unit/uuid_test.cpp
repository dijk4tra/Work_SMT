/**
 * @file uuid_test.cpp
 * @brief 验证 UUIDv4 生成格式和随机性。
 */

#include "datastream/common/uuid.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(UuidTest, GeneratesDistinctVersionFourValues) {
    const std::string first = generateUuidV4();
    const std::string second = generateUuidV4();
    EXPECT_TRUE(isUuid(first));
    EXPECT_TRUE(isUuid(second));
    EXPECT_EQ(first[14], '4');
    EXPECT_TRUE(first[19] == '8' || first[19] == '9' || first[19] == 'a' || first[19] == 'b');
    EXPECT_NE(first, second);
}

TEST(UuidTest, RejectsUppercaseAndWrongSeparators) {
    EXPECT_FALSE(isUuid("550E8400-e29b-41d4-a716-446655440000"));
    EXPECT_FALSE(isUuid("550e8400e29b-41d4-a716-446655440000"));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
