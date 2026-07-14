/**
 * @file logtrace_gateway.cpp
 * @brief 实现 Gateway 初始化、监听和停止流程。
 */

#include "logtrace/server/logtrace_gateway.h"

#include <cassert>
#include <stdexcept>

#include "logtrace/common/request_id.h"

namespace smt {
namespace logtrace {

LogTraceGateway::LogTraceGateway(const AppConfig& config)
    : config_(config),
      search_rpc_(config.gateway),
      authenticator_(config.gateway.operator_token),
      health_controller_(search_rpc_),
      search_controller_(authenticator_, search_rpc_),
      started_(false) {
    http_server_.request_size_limit(config.gateway.request_body_limit_bytes);
    health_controller_.registerRoutes(http_server_);
    search_controller_.registerRoutes(http_server_);
}

void LogTraceGateway::initialize() {
    const RpcHealthResult result = search_rpc_.health(generateRequestId());
    if (result.status != RpcHealthStatus::Ready) {
        throw std::runtime_error("Search Server startup probe failed: " + result.message);
    }
}

bool LogTraceGateway::start() {
    assert(!started_);
    started_ =
        http_server_.start(config_.gateway.listen_address.c_str(), config_.gateway.port) == 0;
    return started_;
}

void LogTraceGateway::stop() {
    assert(started_);
    http_server_.stop();
    started_ = false;
}

}  // namespace logtrace
}  // namespace smt
