/**
 * @file operator_authenticator_test.cpp
 * @brief 验证 Operator Bearer Token 严格格式和匹配。
 */

#include "logtrace/auth/operator_authenticator.h"

#include <gtest/gtest.h>

namespace smt {
namespace logtrace {
namespace {

TEST(OperatorAuthenticatorTest, RequiresExactBearerToken) {
    const OperatorAuthenticator authenticator("phase4-test-token");
    EXPECT_TRUE(authenticator.authenticate("Bearer phase4-test-token"));
    EXPECT_FALSE(authenticator.authenticate("phase4-test-token"));
    EXPECT_FALSE(authenticator.authenticate("bearer phase4-test-token"));
    EXPECT_FALSE(authenticator.authenticate("Bearer phase4-test-token-extra"));
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
