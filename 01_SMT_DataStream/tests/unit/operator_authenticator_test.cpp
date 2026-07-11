/**
 * @file operator_authenticator_test.cpp
 * @brief 验证部署级 Bearer Token 的严格格式和内容校验。
 */

#include "datastream/auth/operator_authenticator.h"

#include <gtest/gtest.h>

namespace smt {
namespace datastream {
namespace {

TEST(OperatorAuthenticatorTest, AcceptsExactBearerToken) {
    const OperatorAuthenticator authenticator("operator-secret");
    EXPECT_TRUE(authenticator.authenticate("Bearer operator-secret"));
}

TEST(OperatorAuthenticatorTest, RejectsMalformedOrDifferentToken) {
    const OperatorAuthenticator authenticator("operator-secret");
    EXPECT_FALSE(authenticator.authenticate("bearer operator-secret"));
    EXPECT_FALSE(authenticator.authenticate("Bearer  operator-secret"));
    EXPECT_FALSE(authenticator.authenticate("Bearer other-secret"));
    EXPECT_FALSE(authenticator.authenticate(""));
}

}  // namespace
}  // namespace datastream
}  // namespace smt
