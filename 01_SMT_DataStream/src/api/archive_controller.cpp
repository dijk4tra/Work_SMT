/**
 * @file archive_controller.cpp
 * @brief 实现 Operator 归档列表、稳定游标和详情查询。
 */

#include "datastream/api/archive_controller.h"

#include <wfrest/CodeUtil.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "datastream/common/api_response.h"
#include "datastream/common/archive_cursor.h"
#include "datastream/common/request_id.h"
#include "datastream/common/time_utils.h"
#include "datastream/common/validation.h"

namespace smt {
namespace datastream {
namespace {

/// @brief 保存列表查询异步状态。
struct ListState {
    std::string request_id;
    ArchiveQuery query;
    ArchivePage page;
    ErrorCode code;
    std::string message;
};

/// @brief 保存详情查询异步状态。
struct DetailState {
    std::string request_id;
    ArchiveRecord record;
    ErrorCode code;
    std::string message;
};

bool parseUnsigned(const std::string& value, std::uint64_t* result) {
    if (value.empty()) return false;
    std::uint64_t parsed = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(value[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *result = parsed;
    return true;
}

bool printableAscii(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

std::string queryValue(const wfrest::HttpReq& request, const std::string& name) {
    return request.has_query(name) ? wfrest::CodeUtil::url_decode(request.query(name)) : "";
}

bool parseArchiveQuery(const wfrest::HttpReq& request, const QueryConfig& config,
                       ArchiveQuery* query, ErrorCode* code, std::string* message) {
    static const std::set<std::string> allowed{
        "device_id", "station_id",    "work_order",  "product_sn", "file_type",
        "result",    "archived_from", "archived_to", "page_size",  "cursor"};
    const std::map<std::string, std::string>& values = request.query_list();
    for (std::map<std::string, std::string>::const_iterator it = values.begin(); it != values.end();
         ++it) {
        if (allowed.count(it->first) == 0 || it->second.empty()) {
            *message = "archive query contains an invalid parameter";
            return false;
        }
    }
    query->device_id = queryValue(request, "device_id");
    query->station_id = queryValue(request, "station_id");
    query->work_order = queryValue(request, "work_order");
    query->product_sn = queryValue(request, "product_sn");
    query->file_type = queryValue(request, "file_type");
    query->result = queryValue(request, "result");
    if ((!query->device_id.empty() && !isSmtIdentifier(query->device_id)) ||
        (!query->station_id.empty() && !isSmtIdentifier(query->station_id)) ||
        (!query->work_order.empty() && !printableAscii(query->work_order, 64)) ||
        (!query->product_sn.empty() && !printableAscii(query->product_sn, 96))) {
        *message = "archive query identifier is invalid";
        return false;
    }
    static const std::set<std::string> file_types{"DETECTION_RESULT", "TEST_REPORT", "NG_IMAGE",
                                                  "DEVICE_EXPORT", "RUNTIME_LOG"};
    if ((!query->file_type.empty() && file_types.count(query->file_type) == 0) ||
        (!query->result.empty() && query->result != "PASS" && query->result != "NG")) {
        *message = "archive query business filter is invalid";
        return false;
    }
    query->has_archived_from = request.has_query("archived_from");
    query->has_archived_to = request.has_query("archived_to");
    if (query->has_archived_from != query->has_archived_to ||
        (query->has_archived_from &&
         (!parseIso8601Milliseconds(queryValue(request, "archived_from"),
                                    &query->archived_from_milliseconds) ||
          !parseIso8601Milliseconds(queryValue(request, "archived_to"),
                                    &query->archived_to_milliseconds) ||
          query->archived_from_milliseconds > query->archived_to_milliseconds ||
          query->archived_to_milliseconds - query->archived_from_milliseconds >
              static_cast<std::int64_t>(config.max_time_range_days) * 86400 * 1000))) {
        *message = "archive time range is invalid";
        return false;
    }
    if (query->work_order.empty() && query->product_sn.empty() && !query->has_archived_from) {
        *message = "archive time range is required without an exact trace filter";
        return false;
    }
    query->page_size = config.default_page_size;
    if (request.has_query("page_size")) {
        std::uint64_t page_size = 0;
        if (!parseUnsigned(queryValue(request, "page_size"), &page_size) || page_size == 0 ||
            page_size > static_cast<std::uint64_t>(config.max_page_size)) {
            *message = "page_size is invalid";
            return false;
        }
        query->page_size = static_cast<int>(page_size);
    }
    query->has_cursor = request.has_query("cursor");
    if (query->has_cursor && !decodeArchiveCursor(queryValue(request, "cursor"), &query->cursor)) {
        *code = ErrorCode::InvalidCursor;
        *message = "cursor is invalid";
        return false;
    }
    return true;
}

nlohmann::json summaryJson(const ArchiveRecord& record) {
    return nlohmann::json{
        {"archive_id", record.archive_id},
        {"device_id", record.device_id},
        {"station_id", record.station_id},
        {"work_order",
         record.work_order.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.work_order)},
        {"product_sn",
         record.product_sn.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.product_sn)},
        {"file_type", record.file_type},
        {"result", record.result.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.result)},
        {"original_filename", record.original_filename},
        {"file_size", record.file_size},
        {"produced_at", record.produced_at},
        {"archived_at", record.archived_at}};
}

}  // namespace

ArchiveController::ArchiveController(const OperatorAuthenticator& authenticator,
                                     const ArchiveRepository& repository, const QueryConfig& config)
    : authenticator_(authenticator), repository_(repository), config_(config) {}

void ArchiveController::registerRoutes(wfrest::HttpServer& server) {
    server.GET("/api/v1/archives", [this](const wfrest::HttpReq* request,
                                          wfrest::HttpResp* response, SeriesWork* series) {
        const std::string request_id = generateRequestId();
        if (!request->has_header("Authorization")) {
            sendApiResponse(response, ErrorCode::AuthRequired, "Authorization header is required",
                            request_id, nullptr);
            return;
        }
        if (!authenticator_.authenticate(request->header("Authorization"))) {
            sendApiResponse(response, ErrorCode::OperatorTokenInvalid, "operator token is invalid",
                            request_id, nullptr);
            return;
        }
        const std::shared_ptr<ListState> state(new ListState());
        state->request_id = request_id;
        state->code = ErrorCode::InvalidArgument;
        state->message = "archive query is invalid";
        if (!parseArchiveQuery(*request, config_, &state->query, &state->code, &state->message)) {
            sendApiResponse(response, state->code, state->message, request_id, nullptr);
            return;
        }
        state->code = ErrorCode::Ok;
        state->message = "success";
        WFMySQLTask* list = repository_.createListTask(
            state->query, [state](bool available, const ArchivePage& page) {
                if (!available) {
                    state->code = ErrorCode::MySqlUnavailable;
                    state->message = "MySQL is unavailable";
                    return;
                }
                state->page = page;
            });
        WFTimerTask* finish =
            WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                if (state->code != ErrorCode::Ok) {
                    sendApiResponse(response, state->code, state->message, state->request_id,
                                    nullptr);
                    return;
                }
                nlohmann::json items = nlohmann::json::array();
                for (std::size_t index = 0; index < state->page.items.size(); ++index) {
                    items.push_back(summaryJson(state->page.items[index]));
                }
                nlohmann::json next_cursor = nullptr;
                if (state->page.has_more && !state->page.items.empty()) {
                    const ArchiveRecord& last = state->page.items.back();
                    next_cursor = encodeArchiveCursor(
                        ArchiveCursor{last.archived_at_milliseconds, last.archive_id});
                }
                sendApiResponse(response, ErrorCode::Ok, "success", state->request_id,
                                nlohmann::json{{"items", items}, {"next_cursor", next_cursor}});
            });
        series->push_back(list);
        series->push_back(finish);
    });

    server.GET(
        "/api/v1/archives/{archive_id}",
        [this](const wfrest::HttpReq* request, wfrest::HttpResp* response, SeriesWork* series) {
            const std::string request_id = generateRequestId();
            if (!request->has_header("Authorization")) {
                sendApiResponse(response, ErrorCode::AuthRequired,
                                "Authorization header is required", request_id, nullptr);
                return;
            }
            if (!authenticator_.authenticate(request->header("Authorization"))) {
                sendApiResponse(response, ErrorCode::OperatorTokenInvalid,
                                "operator token is invalid", request_id, nullptr);
                return;
            }
            std::uint64_t archive_id = 0;
            if (!parseUnsigned(request->param("archive_id"), &archive_id) || archive_id == 0) {
                sendApiResponse(response, ErrorCode::InvalidArgument, "archive_id is invalid",
                                request_id, nullptr);
                return;
            }
            const std::shared_ptr<DetailState> state(new DetailState());
            state->request_id = request_id;
            state->code = ErrorCode::Ok;
            state->message = "success";
            WFMySQLTask* detail = repository_.createFindByIdTask(
                archive_id, [state](ArchiveLookupStatus status, const ArchiveRecord& record) {
                    if (status == ArchiveLookupStatus::NotFound) {
                        state->code = ErrorCode::ArchiveNotFound;
                        state->message = "archive was not found";
                    } else if (status == ArchiveLookupStatus::Unavailable) {
                        state->code = ErrorCode::MySqlUnavailable;
                        state->message = "MySQL is unavailable";
                    } else {
                        state->record = record;
                    }
                });
            WFTimerTask* finish =
                WFTaskFactory::create_timer_task(0, 0, [state, response](WFTimerTask*) {
                    if (state->code != ErrorCode::Ok) {
                        sendApiResponse(response, state->code, state->message, state->request_id,
                                        nullptr);
                        return;
                    }
                    nlohmann::json data = summaryJson(state->record);
                    data["line_id"] = state->record.line_id;
                    data["collector_id"] = state->record.collector_id;
                    data["upload_id"] = state->record.upload_id;
                    data["relative_path"] = state->record.relative_path;
                    data["file_sha256"] = state->record.file_sha256;
                    sendApiResponse(response, ErrorCode::Ok, "success", state->request_id, data);
                });
            series->push_back(detail);
            series->push_back(finish);
        });
}

}  // namespace datastream
}  // namespace smt
