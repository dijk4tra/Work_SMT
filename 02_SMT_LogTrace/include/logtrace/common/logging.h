/**
 * @file logging.h
 * @brief 声明 LogTrace 进程日志初始化接口。
 */

#ifndef LOGTRACE_COMMON_LOGGING_H_
#define LOGTRACE_COMMON_LOGGING_H_

#include <string>

#include "logtrace/config/app_config.h"

namespace smt {
namespace logtrace {

/// @brief 初始化指定进程的轮转文件日志和控制台日志。
/// @param config 日志配置。
/// @param file_path 当前进程日志文件路径。
/// @param logger_name 当前进程日志器名称。
void initializeLogging(const LoggingConfig& config, const std::string& file_path,
                       const char* logger_name);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_COMMON_LOGGING_H_
