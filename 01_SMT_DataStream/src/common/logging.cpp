/**
 * @file logging.cpp
 * @brief 实现 spdlog 同步轮转日志初始化。
 */

#include "datastream/common/logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace smt {
namespace datastream {

void initializeLogging(const LoggingConfig& config) {
    std::shared_ptr<spdlog::logger> logger = spdlog::rotating_logger_mt(
        "datastream", config.file, config.max_file_size_bytes, config.max_files);
    logger->set_level(spdlog::level::from_str(config.level));
    logger->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z level=%l thread=%t %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

}  // namespace datastream
}  // namespace smt
