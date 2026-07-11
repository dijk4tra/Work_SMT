/**
 * @file validation.h
 * @brief 声明设备认证与业务模型共用的 SMT 标识校验。
 */

#ifndef DATASTREAM_COMMON_VALIDATION_H_
#define DATASTREAM_COMMON_VALIDATION_H_

#include <string>

namespace smt {
namespace datastream {

/// @brief 校验 SMT 产线、工位、设备或采集器标识。
/// @param value 待校验标识。
/// @return 长度为 2 至 64 且仅含大写字母、数字、下划线和连字符时返回 true。
bool isSmtIdentifier(const std::string& value);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_VALIDATION_H_
