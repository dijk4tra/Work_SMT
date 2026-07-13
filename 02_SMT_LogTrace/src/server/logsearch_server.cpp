/**
 * @file logsearch_server.cpp
 * @brief 实现 Search Server 初始化、监听和停止流程。
 */

#include "logtrace/server/logsearch_server.h"

#include <cassert>
#include <stdexcept>

namespace smt {
namespace logtrace {

LogSearchServer::LogSearchServer(const AppConfig& config)
    : config_(config),
      storage_(config.storage),
      source_mysql_(config.source_mysql),
      state_mysql_(config.state_mysql),
      redis_(config.redis),
      indexer_(IndexerDependencies{source_mysql_, state_mysql_, storage_,
                                   config.health.check_timeout_ms},
               config.indexing),
      index_worker_(indexer_, config.indexing.poll_interval_ms),
      health_service_(SearchHealthDependencies{source_mysql_, state_mysql_, redis_, storage_},
                      config.health.check_timeout_ms),
      started_(false) {
    if (rpc_server_.add_service(&health_service_) != 0) {
        throw std::runtime_error("cannot register LogSearchService");
    }
}

void LogSearchServer::initialize() {
    storage_.initialize();
    if (!source_mysql_.ping(config_.health.check_timeout_ms)) {
        throw std::runtime_error("source MySQL startup probe failed");
    }
    if (!state_mysql_.ping(config_.health.check_timeout_ms)) {
        throw std::runtime_error("state MySQL startup probe failed");
    }
    if (!redis_.ping(config_.health.check_timeout_ms)) {
        throw std::runtime_error("Redis startup probe failed");
    }
    indexer_.recover();
}

bool LogSearchServer::start() {
    assert(!started_);
    started_ =
        rpc_server_.start(config_.search_rpc.listen_address.c_str(), config_.search_rpc.port) == 0;
    if (started_) {
        try {
            index_worker_.start();
        } catch (const std::exception&) {
            rpc_server_.stop();
            started_ = false;
            throw;
        }
    }
    return started_;
}

void LogSearchServer::stop() {
    assert(started_);
    index_worker_.stop();
    rpc_server_.stop();
    started_ = false;
}

}  // namespace logtrace
}  // namespace smt
