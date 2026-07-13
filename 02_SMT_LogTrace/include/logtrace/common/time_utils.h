/**
 * @file time_utils.h
 * @brief 声明解析阶段使用的严格时间转换函数。
 */

#ifndef LOGTRACE_COMMON_TIME_UTILS_H_
#define LOGTRACE_COMMON_TIME_UTILS_H_

#include <cstdint>
#include <string>

namespace smt {
namespace logtrace {

/// @brief 校验带毫秒和时区的 ISO 8601 时间并换算为 Unix 毫秒。
/// @param value ISO 8601 时间。
/// @param unix_milliseconds 成功时接收 Unix 毫秒。
/// @return 格式、日历时间和时区均有效时为 true。
bool parseIso8601Milliseconds(const std::string& value, std::int64_t* unix_milliseconds);

/// @brief 把 Unix 毫秒格式化为 UTC ISO 8601。
/// @param unix_milliseconds Unix 毫秒。
/// @return 毫秒精度 UTC 时间。
std::string formatUtcMilliseconds(std::int64_t unix_milliseconds);

/// @brief 把 MySQL UTC DATETIME(3) 转换为 UTC ISO 8601。
/// @param value MySQL DATETIME(3) 字符串。
/// @param iso8601 成功时接收 UTC ISO 8601。
/// @return 输入格式和日历时间有效时为 true。
bool mysqlDateTimeToIso8601(const std::string& value, std::string* iso8601);

/// @brief 返回当前 UTC ISO 8601 毫秒时间。
/// @return 当前 UTC 时间。
std::string currentUtcMilliseconds();

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_TIME_UTILS_H_
