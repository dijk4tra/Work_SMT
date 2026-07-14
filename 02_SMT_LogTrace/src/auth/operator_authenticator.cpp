/**
 * @file operator_authenticator.cpp
 * @brief 实现 Gateway Bearer Token 常量时间校验。
 */

#include "logtrace/auth/operator_authenticator.h"

#include <openssl/crypto.h>

namespace smt {
namespace logtrace {

OperatorAuthenticator::OperatorAuthenticator(const std::string& bearer_token)
    : bearer_token_(bearer_token) {}

bool OperatorAuthenticator::authenticate(const std::string& authorization) const {
    const std::string prefix = "Bearer ";
    if (authorization.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string candidate = authorization.substr(prefix.size());
    return candidate.size() == bearer_token_.size() &&
           CRYPTO_memcmp(candidate.data(), bearer_token_.data(), bearer_token_.size()) == 0;
}

}  // namespace logtrace
}  // namespace smt
