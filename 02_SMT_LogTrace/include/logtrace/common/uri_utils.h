/**
 * @file uri_utils.h
 * @brief 声明数据库 URI 用户信息编码函数。
 */

#ifndef LOGTRACE_COMMON_URI_UTILS_H_
#define LOGTRACE_COMMON_URI_UTILS_H_

#include <string>

namespace smt {
namespace logtrace {

/// @brief 对 URI user-info 部分执行百分号编码。
/// @param value 原始用户名或密码。
/// @return 编码后的字符串。
std::string encodeUriUserInfo(const std::string& value);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_URI_UTILS_H_
