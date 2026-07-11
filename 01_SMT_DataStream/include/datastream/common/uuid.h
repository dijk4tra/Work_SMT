/**
 * @file uuid.h
 * @brief 声明 OpenSSL 随机源生成的 UUIDv4 工具。
 */

#ifndef DATASTREAM_COMMON_UUID_H_
#define DATASTREAM_COMMON_UUID_H_

#include <stdexcept>
#include <string>

namespace smt {
namespace datastream {

/// @brief 随机源不可用时抛出的异常。
class RandomError : public std::runtime_error {
   public:
    /// @brief 使用明确原因构造随机源异常。
    /// @param message 不包含敏感数据的错误说明。
    explicit RandomError(const std::string& message);
};

/// @brief 使用 OpenSSL RAND_bytes 生成 UUIDv4。
/// @return 小写标准 UUID 字符串。
/// @throws RandomError 当系统随机源失败时抛出。
std::string generateUuidV4();

/// @brief 校验小写标准 UUID 字符串。
/// @param value 待校验字符串。
/// @return 符合 8-4-4-4-12 小写十六进制格式时返回 true。
bool isUuid(const std::string& value);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_UUID_H_
