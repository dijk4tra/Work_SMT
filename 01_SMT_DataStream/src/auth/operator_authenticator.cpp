/**
 * @file operator_authenticator.cpp
 * @brief 实现部署级 Bearer Token 常量时间校验。
 */

#include "datastream/auth/operator_authenticator.h"

#include "datastream/auth/crypto.h"

namespace smt {
namespace datastream {

OperatorAuthenticator::OperatorAuthenticator(const std::string& bearer_token)
    : bearer_token_(bearer_token) {}

bool OperatorAuthenticator::authenticate(const std::string& authorization) const {
    const std::string prefix = "Bearer ";
    return authorization.compare(0, prefix.size(), prefix) == 0 &&
           constantTimeEquals(authorization.substr(prefix.size()), bearer_token_);
}

}  // namespace datastream
}  // namespace smt
