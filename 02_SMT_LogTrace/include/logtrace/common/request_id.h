/**
 * @file request_id.h
 * @brief 声明服务端请求标识生成函数。
 */

#ifndef LOGTRACE_COMMON_REQUEST_ID_H_
#define LOGTRACE_COMMON_REQUEST_ID_H_

#include <string>

namespace smt {
namespace logtrace {

/// @brief 使用 OpenSSL 随机源生成 32 字符十六进制请求标识。
/// @return 新请求标识。
/// @throws std::runtime_error 当安全随机源失败时抛出。
std::string generateRequestId();

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_REQUEST_ID_H_
