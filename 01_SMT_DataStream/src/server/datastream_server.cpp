/**
 * @file datastream_server.cpp
 * @brief 实现数据采集服务的初始化、监听和停止流程。
 */

#include "datastream/server/datastream_server.h"

#include <cassert>
#include <stdexcept>

namespace smt {
namespace datastream {

DataStreamServer::DataStreamServer(const AppConfig& config)
    : config_(config),
      storage_(config.upload.temp_root, config.upload.archive_root),
      mysql_(config.mysql),
      redis_(config.redis),
      device_repository_(mysql_, config.health.check_timeout_ms),
      device_authenticator_(device_repository_, redis_, config.auth, config.redis,
                            config.health.check_timeout_ms),
      operator_authenticator_(config.operator_auth.bearer_token),
      upload_repository_(redis_, config.redis, config.upload, config.health.check_timeout_ms),
      health_controller_(mysql_, redis_, storage_, config.health.check_timeout_ms),
      heartbeat_controller_(device_authenticator_, device_repository_, redis_, config),
      upload_controller_(device_authenticator_, device_repository_, upload_repository_, storage_,
                         config),
      started_(false) {
    health_controller_.registerRoutes(http_server_);
    heartbeat_controller_.registerRoutes(http_server_);
    upload_controller_.registerRoutes(http_server_);
}

void DataStreamServer::initialize() {
    storage_.initialize();
    if (!mysql_.ping(config_.health.check_timeout_ms)) {
        throw std::runtime_error("MySQL startup probe failed");
    }
    if (!redis_.ping(config_.health.check_timeout_ms)) {
        throw std::runtime_error("Redis startup probe failed");
    }
}

bool DataStreamServer::start() {
    assert(!started_);
    started_ = http_server_.start(config_.http.listen_address.c_str(), config_.http.port) == 0;
    return started_;
}

void DataStreamServer::stop() {
    assert(started_);
    http_server_.stop();
    started_ = false;
}

}  // namespace datastream
}  // namespace smt
