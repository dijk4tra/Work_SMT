/**
 * @file uri_utils_test.cpp
 * @brief 验证连接 URI 用户信息编码不会破坏连接串边界。
 */

#include "datastream/common/uri_utils.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(UriUtilsTest, PreservesUnreservedCharacters) {
    EXPECT_EQ(encodeUriUserInfo("user-._~09"), "user-._~09");
}

TEST(UriUtilsTest, EncodesReservedAndNonAsciiBytes) {
    EXPECT_EQ(encodeUriUserInfo("p@ss:word/中文"), "p%40ss%3Aword%2F%E4%B8%AD%E6%96%87");
}

}  // namespace
}  // namespace datastream
}  // namespace smt
