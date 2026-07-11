/**
 * @file logging.h
 * @brief 声明服务日志初始化接口。
 */

#ifndef DATASTREAM_COMMON_LOGGING_H_
#define DATASTREAM_COMMON_LOGGING_H_

#include "datastream/config/app_config.h"

namespace smt {
namespace datastream {

/// @brief 根据已校验配置初始化同步轮转日志。
/// @param config 日志文件、级别和轮转配置。
/// @throws spdlog::spdlog_ex 当日志文件无法创建时抛出。
void initializeLogging(const LoggingConfig& config);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COMMON_LOGGING_H_
