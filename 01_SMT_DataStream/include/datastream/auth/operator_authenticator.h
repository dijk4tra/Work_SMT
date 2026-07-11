/**
 * @file operator_authenticator.h
 * @brief 声明部署级 Bearer Token 校验器。
 */

#ifndef DATASTREAM_AUTH_OPERATOR_AUTHENTICATOR_H_
#define DATASTREAM_AUTH_OPERATOR_AUTHENTICATOR_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 校验运维接口使用的单一部署级 Bearer Token。
class OperatorAuthenticator {
   public:
    /// @brief 保存已从受控环境变量加载的令牌。
    /// @param bearer_token 运维 Bearer Token。
    explicit OperatorAuthenticator(const std::string& bearer_token);

    /// @brief 校验完整 Authorization 请求头。
    /// @param authorization Authorization 请求头值。
    /// @return 严格符合 Bearer 格式且令牌匹配时返回 true。
    bool authenticate(const std::string& authorization) const;

   private:
    std::string bearer_token_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_AUTH_OPERATOR_AUTHENTICATOR_H_
