/**
 * @file uri_utils_test.cpp
 * @brief 验证数据库 URI 用户信息编码。
 */

#include "logtrace/common/uri_utils.h"

#include <gtest/gtest.h>

namespace smt {
namespace logtrace {
namespace {

TEST(UriUtilsTest, PreservesUnreservedCharacters) {
    EXPECT_EQ(encodeUriUserInfo("user-name_1.test~"), "user-name_1.test~");
}

TEST(UriUtilsTest, EncodesReservedAndNonAsciiBytes) {
    EXPECT_EQ(encodeUriUserInfo("p@ss:word/"), "p%40ss%3Aword%2F");
    EXPECT_EQ(encodeUriUserInfo("中"), "%E4%B8%AD");
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
