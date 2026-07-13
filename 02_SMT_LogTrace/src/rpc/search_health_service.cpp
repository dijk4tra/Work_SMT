/**
 * @file search_health_service.cpp
 * @brief 实现 Search Server 的异步 SRPC 健康检查。
 */

#include "logtrace/rpc/search_health_service.h"

#include <workflow/WFTaskFactory.h>

#include <memory>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

// 保存一次健康请求中五个依赖的检查结果。
struct DependencyState {
    bool source_mysql_ready;
    bool state_mysql_ready;
    bool redis_ready;
    bool storage_ready;
};

}  // namespace

SearchHealthService::SearchHealthService(const SearchHealthDependencies& dependencies,
                                         int timeout_ms)
    : source_mysql_(dependencies.source_mysql),
      state_mysql_(dependencies.state_mysql),
      redis_(dependencies.redis),
      storage_(dependencies.storage),
      timeout_ms_(timeout_ms) {}

void SearchHealthService::Health(rpc::HealthRequest* request, rpc::HealthResponse* response,
                                 srpc::RPCContext* context) {
    context->log({{"request_id", request->request_id()}, {"method", "Health"}});
    const std::shared_ptr<DependencyState> state(
        new DependencyState{false, false, false, storage_.ready()});

    WFMySQLTask* source_task =
        source_mysql_.createQuery("SELECT 1", timeout_ms_, [state](WFMySQLTask* task) {
            state->source_mysql_ready =
                task->get_state() == WFT_STATE_SUCCESS && !task->get_resp()->is_error_packet();
        });
    WFMySQLTask* state_task =
        state_mysql_.createQuery("SELECT 1", timeout_ms_, [state](WFMySQLTask* task) {
            state->state_mysql_ready =
                task->get_state() == WFT_STATE_SUCCESS && !task->get_resp()->is_error_packet();
        });
    WFRedisTask* redis_task = redis_.createCommand(
        "PING", std::vector<std::string>(), timeout_ms_, [state](WFRedisTask* task) {
            protocol::RedisValue result;
            if (task->get_state() == WFT_STATE_SUCCESS) {
                task->get_resp()->get_result(result);
            }
            state->redis_ready = result.string_value() == "PONG";
        });
    WFTimerTask* finish_task =
        WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
            const bool ready = state->source_mysql_ready && state->state_mysql_ready &&
                               state->redis_ready && state->storage_ready;
            response->set_status(ready ? rpc::SERVICE_STATUS_READY : rpc::SERVICE_STATUS_NOT_READY);
            response->set_code(ready ? "OK" : "SERVICE_NOT_READY");
            response->set_message(ready ? "success" : "search service is not ready");
        });

    context->get_series()->push_back(source_task);
    context->get_series()->push_back(state_task);
    context->get_series()->push_back(redis_task);
    context->get_series()->push_back(finish_task);
}

}  // namespace logtrace
}  // namespace smt
