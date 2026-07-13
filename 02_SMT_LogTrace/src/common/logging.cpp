/**
 * @file logging.cpp
 * @brief 实现双进程轮转文件日志初始化。
 */

#include "logtrace/common/logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

spdlog::level::level_enum parseLevel(const std::string& level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "info") {
        return spdlog::level::info;
    }
    if (level == "warn") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }
    return spdlog::level::off;
}

}  // namespace

void initializeLogging(const LoggingConfig& config, const std::string& file_path,
                       const char* logger_name) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file_path, config.max_file_size_bytes, config.max_files));

    std::shared_ptr<spdlog::logger> logger(
        new spdlog::logger(logger_name, sinks.begin(), sinks.end()));
    logger->set_level(parseLevel(config.level));
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z level=%l %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

}  // namespace logtrace
}  // namespace smt
