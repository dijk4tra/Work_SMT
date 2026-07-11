/**
 * @file time_utils.h
 * @brief 声明服务端 UTC 时间生成和 ISO 8601 时间解析工具。
 */

#ifndef DATASTREAM_COMMON_TIME_UTILS_H_
#define DATASTREAM_COMMON_TIME_UTILS_H_

#include <cstdint>
#include <string>

namespace smt {
namespace datastream {

/// @brief 同一次服务端取时的 API 和 MySQL 表示。
struct ServerTime {
    std::string iso8601;
    std::string mysql;
};

/// @brief 返回当前 Unix 秒。
/// @return 系统时钟当前 Unix 秒。
std::int64_t currentUnixSeconds();

/// @brief 获取当前 UTC 时间的两种毫秒精度格式。
/// @return UTC ISO 8601 和 MySQL DATETIME(3) 字符串。
ServerTime currentServerTime();

/// @brief 校验带毫秒和明确时区的 ISO 8601 时间并换算为 Unix 毫秒。
/// @param value ISO 8601 时间字符串。
/// @param unix_milliseconds 校验成功时接收 Unix 毫秒。
/// @return 格式、日历时间和时区均有效时返回 true。
bool parseIso8601Milliseconds(const std::string& value, std::int64_t* unix_milliseconds);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_TIME_UTILS_H_
