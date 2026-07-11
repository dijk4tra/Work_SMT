/**
 * @file uri_utils.h
 * @brief 声明连接 URI 用户信息编码函数。
 */

#ifndef DATASTREAM_COMMON_URI_UTILS_H_
#define DATASTREAM_COMMON_URI_UTILS_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 按 RFC 3986 百分号编码连接 URI 的用户名或密码。
/// @param value 未编码的用户名或密码。
/// @return 仅保留非保留字符的编码结果。
std::string encodeUriUserInfo(const std::string& value);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_URI_UTILS_H_
