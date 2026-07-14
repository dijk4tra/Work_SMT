/**
 * @file search_health_service.cpp
 * @brief 实现 Search Server 的异步 SRPC 健康检查。
 */

#include "logtrace/rpc/search_health_service.h"

#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/WFTaskFactory.h>

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "logtrace/common/time_utils.h"
#include "logtrace/indexing/term_tokenizer.h"

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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
SearchQuery makeQuery(const rpc::SearchFilter& filter, std::size_t offset, std::size_t page_size,
                      bool anomaly_only) {
    SearchQuery query;
    query.line_id = filter.line_id();
    query.station_id = filter.station_id();
    query.device_id = filter.device_id();
    query.work_order = filter.work_order();
    query.product_sn = filter.product_sn();
    query.levels.assign(filter.levels().begin(), filter.levels().end());
    query.module_name = filter.module_name();
    query.error_code = filter.error_code();
    query.has_time_range = filter.has_time_range();
    query.occurred_from_ms = filter.occurred_from_ms();
    query.occurred_to_ms = filter.occurred_to_ms();
    query.anomaly_only = anomaly_only;
    query.offset = offset;
    query.page_size = page_size;
    return query;
}

bool validPagination(std::uint32_t offset, std::uint32_t page_size) {
    return page_size != 0 && page_size <= 200 && offset + page_size <= 1000;
}

bool validFilter(const rpc::SearchFilter& filter) {
    if (filter.line_id().size() > 64 || filter.station_id().size() > 64 ||
        filter.device_id().size() > 64 || filter.work_order().size() > 64 ||
        filter.product_sn().size() > 96 || filter.module_name().size() > 64 ||
        filter.error_code().size() > 64 || filter.levels_size() > 3) {
        return false;
    }
    for (int index = 0; index < filter.levels_size(); ++index) {
        if (filter.levels(index) != "INFO" && filter.levels(index) != "WARN" &&
            filter.levels(index) != "ERROR") {
            return false;
        }
    }
    if (filter.has_time_range() &&
        (filter.occurred_from_ms() < 946684800000LL || filter.occurred_to_ms() > 4102444800000LL ||
         filter.occurred_from_ms() > filter.occurred_to_ms() ||
         filter.occurred_to_ms() - filter.occurred_from_ms() > 31LL * 86400 * 1000)) {
        return false;
    }
    return !filter.device_id().empty() || !filter.work_order().empty() ||
           !filter.product_sn().empty() || !filter.error_code().empty() || filter.has_time_range();
}

bool validKeywords(const rpc::SearchLogsRequest& request) {
    if (request.keywords_size() > 8) return false;
    for (int index = 0; index < request.keywords_size(); ++index) {
        if (request.keywords(index).empty() || request.keywords(index).size() > 64 ||
            tokenizeTerms(request.keywords(index)).empty()) {
            return false;
        }
    }
    return true;
}

void fillSummary(const SearchHit& hit, rpc::LogSummary* summary) {
    const SegmentDocumentRecord& document = hit.document;
    summary->set_doc_id(document.doc_id);
    summary->set_score(hit.score);
    summary->set_occurred_at(formatUtcMilliseconds(document.occurred_at_ms));
    summary->set_archived_at(formatUtcMilliseconds(document.archived_at_ms));
    summary->set_line_id(document.line_id);
    summary->set_station_id(document.station_id);
    summary->set_device_id(document.device_id);
    summary->set_collector_id(document.collector_id);
    summary->set_work_order(document.work_order);
    summary->set_product_sn(document.product_sn);
    summary->set_source_type(document.source_type);
    summary->set_level(document.level);
    summary->set_module_name(document.module_name);
    summary->set_error_code(document.error_code);
    summary->set_event_name(document.event_name);
}

template <typename Response>
void fillPage(const SearchPage& page, Response* response) {
    response->set_code("OK");
    response->set_message("success");
    response->set_snapshot_version(page.snapshot_version);
    response->set_total_hits(page.total_hits);
    for (std::vector<SearchHit>::const_iterator hit = page.items.begin(); hit != page.items.end();
         ++hit) {
        fillSummary(*hit, response->add_items());
    }
}

struct ErrorCodeState {
    bool available;
    bool found;
    std::string module_name;
    std::string title;
    std::string description;
    std::string recommended_action;
};

std::int64_t nowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

template <typename Response>
void executeCachedSearch(const SearchQuery& query, const RedisClient& redis,
                         const QueryCache& cache, const SearchEngine& engine, int timeout_ms,
                         Response* response, SeriesWork* series) {
    const std::uint64_t version = engine.snapshotVersion();
    const std::string key = cache.key(query, version);
    WFRedisTask* get = redis.createCommand(
        "GET", std::vector<std::string>(1, key), timeout_ms,
        [query, key, &redis, &cache, &engine, timeout_ms, response](WFRedisTask* task) {
            protocol::RedisValue result;
            if (task->get_state() == WFT_STATE_SUCCESS) task->get_resp()->get_result(result);
            SearchPage page;
            if (result.is_string() && cache.restore(result.string_value(), engine, &page)) {
                fillPage(page, response);
                return;
            }
            try {
                page = engine.search(query);
                fillPage(page, response);
            } catch (const std::exception&) {
                response->set_code("INDEX_CORRUPTED");
                response->set_message("search index cannot complete the query");
                return;
            }
            const std::string value = cache.serialize(page);
            const int ttl = cache.ttlSeconds(query, page.total_hits == 0, nowMilliseconds());
            WFRedisTask* set = redis.createCommand(
                "SETEX", std::vector<std::string>{key, std::to_string(ttl), value}, timeout_ms,
                [](WFRedisTask*) {});
            series_of(task)->push_back(set);
        });
    series->push_back(get);
}

void executeCachedMatches(const SearchQuery& query, const RedisClient& redis,
                          const QueryCache& cache, const SearchEngine& engine, int timeout_ms,
                          rpc::GetErrorCodeResponse* response, SeriesWork* series) {
    const std::string key = cache.key(query, engine.snapshotVersion());
    WFRedisTask* get = redis.createCommand(
        "GET", std::vector<std::string>(1, key), timeout_ms,
        [query, key, &redis, &cache, &engine, timeout_ms, response](WFRedisTask* task) {
            protocol::RedisValue result;
            if (task->get_state() == WFT_STATE_SUCCESS) task->get_resp()->get_result(result);
            SearchPage page;
            const bool hit =
                result.is_string() && cache.restore(result.string_value(), engine, &page);
            if (!hit) {
                try {
                    page = engine.search(query);
                } catch (const std::exception&) {
                    response->Clear();
                    response->set_code("INDEX_CORRUPTED");
                    response->set_message("search index cannot complete the query");
                    return;
                }
                const int ttl = cache.ttlSeconds(query, page.total_hits == 0, nowMilliseconds());
                series_of(task)->push_back(redis.createCommand(
                    "SETEX",
                    std::vector<std::string>{key, std::to_string(ttl), cache.serialize(page)},
                    timeout_ms, [](WFRedisTask*) {}));
            }
            for (std::vector<SearchHit>::const_iterator item = page.items.begin();
                 item != page.items.end(); ++item)
                fillSummary(*item, response->add_matching_logs());
        });
    series->push_back(get);
}

}  // namespace

SearchHealthService::SearchHealthService(const SearchHealthDependencies& dependencies,
                                         int timeout_ms)
    : source_mysql_(dependencies.source_mysql),
      state_mysql_(dependencies.state_mysql),
      redis_(dependencies.redis),
      storage_(dependencies.storage),
      search_engine_(dependencies.search_engine),
      query_cache_(dependencies.redis_config, dependencies.cache_config),
      error_code_cache_(dependencies.cache_config.probation_capacity,
                        dependencies.cache_config.protected_capacity),
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

void SearchHealthService::SearchLogs(rpc::SearchLogsRequest* request,
                                     rpc::SearchLogsResponse* response, srpc::RPCContext* context) {
    context->log({{"request_id", request->request_id()}, {"method", "SearchLogs"}});
    if (!validPagination(request->offset(), request->page_size()) ||
        !validFilter(request->filter()) || !validKeywords(*request)) {
        response->set_code("INVALID_ARGUMENT");
        response->set_message("search pagination is invalid");
        return;
    }
    SearchQuery query =
        makeQuery(request->filter(), request->offset(), request->page_size(), false);
    query.keywords.assign(request->keywords().begin(), request->keywords().end());
    executeCachedSearch(query, redis_, query_cache_, search_engine_, timeout_ms_, response,
                        context->get_series());
}

void SearchHealthService::ListAnomalies(rpc::ListAnomaliesRequest* request,
                                        rpc::ListAnomaliesResponse* response,
                                        srpc::RPCContext* context) {
    context->log({{"request_id", request->request_id()}, {"method", "ListAnomalies"}});
    if (!validPagination(request->offset(), request->page_size()) ||
        !validFilter(request->filter())) {
        response->set_code("INVALID_ARGUMENT");
        response->set_message("anomaly pagination is invalid");
        return;
    }
    executeCachedSearch(makeQuery(request->filter(), request->offset(), request->page_size(), true),
                        redis_, query_cache_, search_engine_, timeout_ms_, response,
                        context->get_series());
}

void SearchHealthService::GetLogDetail(rpc::GetLogDetailRequest* request,
                                       rpc::GetLogDetailResponse* response,
                                       srpc::RPCContext* context) {
    context->log({{"request_id", request->request_id()}, {"method", "GetLogDetail"}});
    if (request->doc_id() == 0) {
        response->set_code("INVALID_ARGUMENT");
        response->set_message("doc_id is invalid");
        return;
    }
    try {
        const LogDetail detail = search_engine_.detail(request->doc_id());
        response->set_code("OK");
        response->set_message("success");
        fillSummary(SearchHit{detail.document, 0.0}, response->mutable_document());
        response->set_archive_id(detail.file.archive_id);
        response->set_byte_offset(detail.document.byte_offset);
        response->set_byte_length(detail.document.byte_length);
        response->set_raw(detail.raw);
    } catch (const std::out_of_range&) {
        response->set_code("LOG_NOT_FOUND");
        response->set_message("log document was not found");
    } catch (const std::exception&) {
        response->set_code("STORAGE_IO_ERROR");
        response->set_message("original log cannot be read");
    }
}

void SearchHealthService::GetErrorCode(rpc::GetErrorCodeRequest* request,
                                       rpc::GetErrorCodeResponse* response,
                                       srpc::RPCContext* context) {
    context->log({{"request_id", request->request_id()}, {"method", "GetErrorCode"}});
    if (request->error_code().empty() || request->error_code().size() > 64) {
        response->set_code("INVALID_ARGUMENT");
        response->set_message("error_code is invalid");
        return;
    }
    ErrorCodeKnowledge cached;
    if (error_code_cache_.get(request->error_code(), &cached)) {
        response->set_code("OK");
        response->set_message("success");
        response->set_error_code(request->error_code());
        response->set_module_name(cached.module_name);
        response->set_title(cached.title);
        response->set_description(cached.description);
        response->set_recommended_action(cached.recommended_action);
        SearchQuery search = makeQuery(rpc::SearchFilter(), 0, 5, false);
        search.error_code = request->error_code();
        executeCachedMatches(search, redis_, query_cache_, search_engine_, timeout_ms_, response,
                             context->get_series());
        return;
    }
    const std::shared_ptr<ErrorCodeState> state(new ErrorCodeState{false, false, "", "", "", ""});
    const std::string escaped =
        protocol::MySQLUtil::escape_string_quote(request->error_code(), '\'');
    WFMySQLTask* query = state_mysql_.createQuery(
        "SELECT module_name,title,description,recommended_action FROM error_code_catalog "
        "WHERE enabled=1 AND error_code='" +
            escaped + "' LIMIT 1",
        timeout_ms_, [state](WFMySQLTask* task) {
            if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error_packet())
                return;
            state->available = true;
            protocol::MySQLResultCursor cursor(task->get_resp());
            std::vector<protocol::MySQLCell> row;
            if (!cursor.fetch_row(row)) return;
            if (row.size() != 4 || row[0].is_null() || row[1].is_null() || row[2].is_null() ||
                row[3].is_null()) {
                state->available = false;
                return;
            }
            state->found = true;
            state->module_name = row[0].as_string();
            state->title = row[1].as_string();
            state->description = row[2].as_string();
            state->recommended_action = row[3].as_string();
        });
    // Series 尾任务执行时 request 已不能作为寿命保证，因此必须拷贝。
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    std::string error_code = request->error_code();
    WFTimerTask* finish = WFTaskFactory::create_timer_task(
        0, 0, [this, state, error_code, response](WFTimerTask* timer) {
            if (!state->available) {
                response->set_code("MYSQL_UNAVAILABLE");
                response->set_message("error code catalog is unavailable");
                return;
            }
            if (!state->found) {
                response->set_code("ERROR_CODE_NOT_FOUND");
                response->set_message("error code was not found");
                return;
            }
            response->set_code("OK");
            response->set_message("success");
            response->set_error_code(error_code);
            response->set_module_name(state->module_name);
            response->set_title(state->title);
            response->set_description(state->description);
            response->set_recommended_action(state->recommended_action);
            error_code_cache_.put(
                error_code, ErrorCodeKnowledge{state->module_name, state->title, state->description,
                                               state->recommended_action});
            SearchQuery search = makeQuery(rpc::SearchFilter(), 0, 5, false);
            search.error_code = error_code;
            executeCachedMatches(search, redis_, query_cache_, search_engine_, timeout_ms_,
                                 response, series_of(timer));
        });
    context->get_series()->push_back(query);
    context->get_series()->push_back(finish);
}

}  // namespace logtrace
}  // namespace smt
