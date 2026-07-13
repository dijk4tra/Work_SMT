/**
 * @file redis_client.cpp
 * @brief 实现 Workflow Redis URI、命令任务和启动探测。
 */

#include "logtrace/storage/redis_client.h"

#include <workflow/WFFacilities.h>

#include <atomic>
#include <sstream>

#include "logtrace/common/uri_utils.h"

namespace smt {
namespace logtrace {
namespace {

std::string buildUri(const RedisConfig& config) {
    std::ostringstream output;
    output << "redis://";
    if (!config.password.empty()) {
        output << encodeUriUserInfo(config.username) << ':' << encodeUriUserInfo(config.password)
               << '@';
    }
    output << config.host << ':' << config.port << '/' << config.database;
    return output.str();
}

}  // namespace

RedisClient::RedisClient(const RedisConfig& config) : uri_(buildUri(config)) {}

WFRedisTask* RedisClient::createCommand(const std::string& command,
                                        const std::vector<std::string>& params, int timeout_ms,
                                        const std::function<void(WFRedisTask*)>& callback) const {
    WFRedisTask* task = WFTaskFactory::create_redis_task(uri_, 0, callback);
    task->set_watch_timeout(timeout_ms);
    task->get_req()->set_request(command, params);
    return task;
}

bool RedisClient::ping(int timeout_ms) const {
    std::atomic<bool> ready(false);
    WFFacilities::WaitGroup wait_group(1);
    WFRedisTask* task =
        createCommand("PING", std::vector<std::string>(), timeout_ms,
                      [&ready, &wait_group](WFRedisTask* completed) {
                          protocol::RedisValue result;
                          if (completed->get_state() == WFT_STATE_SUCCESS) {
                              completed->get_resp()->get_result(result);
                          }
                          ready.store(result.string_value() == "PONG", std::memory_order_release);
                          wait_group.done();
                      });
    task->start();
    wait_group.wait();
    return ready.load(std::memory_order_acquire);
}

}  // namespace logtrace
}  // namespace smt
