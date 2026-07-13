/**
 * @file mysql_client.cpp
 * @brief 实现 Workflow MySQL URI、查询任务和启动探测。
 */

#include "logtrace/storage/mysql_client.h"

#include <workflow/WFFacilities.h>

#include <atomic>
#include <sstream>

#include "logtrace/common/uri_utils.h"

namespace smt {
namespace logtrace {
namespace {

std::string buildUri(const MySqlConfig& config) {
    std::ostringstream output;
    output << "mysql://" << encodeUriUserInfo(config.user) << ':'
           << encodeUriUserInfo(config.password) << '@' << config.host << ':' << config.port << '/'
           << config.database << "?character_set=utf8mb4";
    return output.str();
}

}  // namespace

MySqlClient::MySqlClient(const MySqlConfig& config) : uri_(buildUri(config)) {}

WFMySQLTask* MySqlClient::createQuery(const std::string& sql, int timeout_ms,
                                      const std::function<void(WFMySQLTask*)>& callback) const {
    WFMySQLTask* task = WFTaskFactory::create_mysql_task(uri_, 0, callback);
    task->set_watch_timeout(timeout_ms);
    task->get_req()->set_query(sql);
    return task;
}

bool MySqlClient::ping(int timeout_ms) const {
    std::atomic<bool> ready(false);
    WFFacilities::WaitGroup wait_group(1);
    WFMySQLTask* task =
        createQuery("SELECT 1", timeout_ms, [&ready, &wait_group](WFMySQLTask* completed) {
            ready.store(completed->get_state() == WFT_STATE_SUCCESS &&
                            !completed->get_resp()->is_error_packet(),
                        std::memory_order_release);
            wait_group.done();
        });
    task->start();
    wait_group.wait();
    return ready.load(std::memory_order_acquire);
}

}  // namespace logtrace
}  // namespace smt
