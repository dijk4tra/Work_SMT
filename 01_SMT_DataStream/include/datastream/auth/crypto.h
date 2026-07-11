/**
 * @file crypto.h
 * @brief 声明设备认证使用的 SHA-256、HMAC 和常量时间比较工具。
 */

#ifndef DATASTREAM_AUTH_CRYPTO_H_
#define DATASTREAM_AUTH_CRYPTO_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 计算字节串的 SHA-256 小写十六进制摘要。
/// @param data 原始字节串。
/// @return 64 字符小写十六进制摘要。
std::string sha256Hex(const std::string& data);

/// @brief 使用指定二进制密钥计算 HMAC-SHA256。
/// @param key 二进制 HMAC 密钥。
/// @param data 待认证字节串。
/// @return 64 字符小写十六进制摘要。
std::string hmacSha256Hex(const std::string& key, const std::string& data);

/// @brief 以常量时间比较两个等长字节串。
/// @param left 左侧字节串。
/// @param right 右侧字节串。
/// @return 长度相同且内容相同时返回 true。
bool constantTimeEquals(const std::string& left, const std::string& right);

/// @brief 构造 v1 设备签名规范串。
/// @param method HTTP 方法。
/// @param path 不含查询参数的路由路径。
/// @param device_id 设备编号。
/// @param timestamp Unix 秒原始字符串。
/// @param request_id 请求标识。
/// @param content_sha256 请求体摘要。
/// @return 使用换行符连接的 v1 规范串。
std::string buildDeviceCanonicalString(const std::string& method, const std::string& path,
                                       const std::string& device_id, const std::string& timestamp,
                                       const std::string& request_id,
                                       const std::string& content_sha256);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_AUTH_CRYPTO_H_
