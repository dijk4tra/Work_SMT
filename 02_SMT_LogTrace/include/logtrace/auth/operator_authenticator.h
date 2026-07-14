/**
 * @file operator_authenticator.h
 * @brief 声明 Gateway 部署级 Bearer Token 校验器。
 */

#ifndef LOGTRACE_AUTH_OPERATOR_AUTHENTICATOR_H_
#define LOGTRACE_AUTH_OPERATOR_AUTHENTICATOR_H_

#include <string>

namespace smt {
namespace logtrace {

/// @brief 使用常量时间比较校验业务 HTTP 接口的 Bearer Token。
class OperatorAuthenticator {
   public:
    /// @brief 保存从受控环境变量加载的令牌。
    /// @param bearer_token 非空部署级令牌。
    explicit OperatorAuthenticator(const std::string& bearer_token);

    /// @brief 校验完整 Authorization 请求头。
    /// @param authorization Authorization 请求头值。
    /// @return Bearer 格式和令牌均匹配时为 true。
    bool authenticate(const std::string& authorization) const;

   private:
    std::string bearer_token_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_AUTH_OPERATOR_AUTHENTICATOR_H_
