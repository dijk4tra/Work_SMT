/**
 * @file request_id.h
 * @brief 提供线程安全的服务端请求标识生成函数。
 */

#ifndef DATASTREAM_COMMON_REQUEST_ID_H_
#define DATASTREAM_COMMON_REQUEST_ID_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 生成进程内唯一且便于日志关联的请求标识。
/// @return 以 srv- 开头的服务端请求标识。
std::string generateRequestId();

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_REQUEST_ID_H_
