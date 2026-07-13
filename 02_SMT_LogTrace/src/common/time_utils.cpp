/**
 * @file time_utils.cpp
 * @brief 实现严格 ISO 8601 与 MySQL 时间转换。
 */

#include "logtrace/common/time_utils.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <regex>

namespace smt {
namespace logtrace {
namespace {

bool validCalendarTime(const std::tm& input, std::time_t epoch) {
    std::tm normalized;
    gmtime_r(&epoch, &normalized);
    return normalized.tm_year == input.tm_year && normalized.tm_mon == input.tm_mon &&
           normalized.tm_mday == input.tm_mday && normalized.tm_hour == input.tm_hour &&
           normalized.tm_min == input.tm_min && normalized.tm_sec == input.tm_sec;
}

}  // namespace

bool parseIso8601Milliseconds(const std::string& value, std::int64_t* unix_milliseconds) {
    static const std::regex pattern(
        "^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})\\."
        "([0-9]{3})(Z|[+-][0-9]{2}:[0-9]{2})$");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        return false;
    }

    std::tm local = {};
    local.tm_year = std::stoi(match[1].str()) - 1900;
    local.tm_mon = std::stoi(match[2].str()) - 1;
    local.tm_mday = std::stoi(match[3].str());
    local.tm_hour = std::stoi(match[4].str());
    local.tm_min = std::stoi(match[5].str());
    local.tm_sec = std::stoi(match[6].str());
    const int milliseconds = std::stoi(match[7].str());
    const std::tm original_local = local;
    const std::time_t local_epoch = timegm(&local);
    if (!validCalendarTime(original_local, local_epoch)) {
        return false;
    }

    int offset_seconds = 0;
    const std::string timezone = match[8].str();
    if (timezone != "Z") {
        const int offset_hours = std::stoi(timezone.substr(1, 2));
        const int offset_minutes = std::stoi(timezone.substr(4, 2));
        if (offset_hours > 14 || offset_minutes > 59 ||
            (offset_hours == 14 && offset_minutes != 0)) {
            return false;
        }
        offset_seconds = (offset_hours * 60 + offset_minutes) * 60;
        if (timezone[0] == '-') {
            offset_seconds = -offset_seconds;
        }
    }
    *unix_milliseconds =
        (static_cast<std::int64_t>(local_epoch) - offset_seconds) * 1000 + milliseconds;
    return true;
}

std::string formatUtcMilliseconds(std::int64_t unix_milliseconds) {
    const std::time_t seconds = static_cast<std::time_t>(unix_milliseconds / 1000);
    const int milliseconds = static_cast<int>(unix_milliseconds % 1000);
    std::tm utc;
    gmtime_r(&seconds, &utc);
    char base[32];
    std::strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &utc);
    char output[40];
    std::snprintf(output, sizeof(output), "%s.%03dZ", base, milliseconds);
    return output;
}

bool mysqlDateTimeToIso8601(const std::string& value, std::string* iso8601) {
    static const std::regex pattern(
        "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:"
        "[0-9]{2}\\.[0-9]{3}$");
    if (!std::regex_match(value, pattern)) {
        return false;
    }
    *iso8601 = value;
    (*iso8601)[10] = 'T';
    iso8601->push_back('Z');
    std::int64_t milliseconds = 0;
    return parseIso8601Milliseconds(*iso8601, &milliseconds);
}

std::string currentUtcMilliseconds() {
    const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    return formatUtcMilliseconds(milliseconds);
}

}  // namespace logtrace
}  // namespace smt
