/**
 * @file search_controller.cpp
 * @brief 实现业务 HTTP 认证、参数校验、RPC 调用和错误映射。
 */

#include "logtrace/api/search_controller.h"

#include <wfrest/CodeUtil.h>

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "logtrace/common/api_response.h"
#include "logtrace/common/request_id.h"
#include "logtrace/common/time_utils.h"

namespace smt {
namespace logtrace {
namespace {

bool authenticate(const wfrest::HttpReq& request, const OperatorAuthenticator& authenticator,
                  wfrest::HttpResp* response, const std::string& request_id) {
    if (!request.has_header("Authorization") ||
        !authenticator.authenticate(request.header("Authorization"))) {
        sendApiResponse(response, ErrorCode::OperatorTokenInvalid, "operator token is invalid",
                        request_id, nullptr);
        return false;
    }
    return true;
}

bool parseUnsigned(const std::string& value, std::uint64_t* result) {
    if (value.empty()) return false;
    std::uint64_t parsed = 0;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it < '0' || *it > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(*it - '0');
        if (parsed > (UINT64_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *result = parsed;
    return true;
}

bool validText(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char byte = static_cast<unsigned char>(*it);
        if (byte < 0x20 || byte == 0x7f) return false;
    }
    return true;
}

bool optionalString(const nlohmann::json& object, const char* key, std::size_t maximum,
                    std::string* value) {
    if (!object.count(key) || object.at(key).is_null()) return true;
    if (!object.at(key).is_string()) return false;
    *value = object.at(key).get<std::string>();
    return validText(*value, maximum);
}

bool parseFilterJson(const nlohmann::json& body, rpc::SearchFilter* filter) {
    std::string value;
#define LOGTRACE_OPTIONAL_FILTER(field, maximum)                      \
    value.clear();                                                    \
    if (!optionalString(body, #field, maximum, &value)) return false; \
    filter->set_##field(value)
    LOGTRACE_OPTIONAL_FILTER(line_id, 64);
    LOGTRACE_OPTIONAL_FILTER(station_id, 64);
    LOGTRACE_OPTIONAL_FILTER(device_id, 64);
    LOGTRACE_OPTIONAL_FILTER(work_order, 64);
    LOGTRACE_OPTIONAL_FILTER(product_sn, 96);
    LOGTRACE_OPTIONAL_FILTER(module_name, 64);
    LOGTRACE_OPTIONAL_FILTER(error_code, 64);
#undef LOGTRACE_OPTIONAL_FILTER
    if (body.count("levels") && !body.at("levels").is_null()) {
        if (!body.at("levels").is_array() || body.at("levels").size() > 3) return false;
        static const std::set<std::string> allowed = {"INFO", "WARN", "ERROR"};
        for (nlohmann::json::const_iterator level = body.at("levels").begin();
             level != body.at("levels").end(); ++level) {
            if (!level->is_string() || allowed.count(level->get<std::string>()) == 0) return false;
            filter->add_levels(level->get<std::string>());
        }
    }
    const bool has_from = body.count("occurred_from") && !body.at("occurred_from").is_null();
    const bool has_to = body.count("occurred_to") && !body.at("occurred_to").is_null();
    if (has_from != has_to) return false;
    if (has_from) {
        if (!body.at("occurred_from").is_string() || !body.at("occurred_to").is_string())
            return false;
        std::int64_t from = 0;
        std::int64_t to = 0;
        if (!parseIso8601Milliseconds(body.at("occurred_from").get<std::string>(), &from) ||
            !parseIso8601Milliseconds(body.at("occurred_to").get<std::string>(), &to) ||
            from > to || to - from > 31LL * 86400 * 1000)
            return false;
        filter->set_has_time_range(true);
        filter->set_occurred_from_ms(from);
        filter->set_occurred_to_ms(to);
    }
    return !filter->device_id().empty() || !filter->work_order().empty() ||
           !filter->product_sn().empty() || !filter->error_code().empty() || has_from;
}

bool containsOnly(const nlohmann::json& object, const std::set<std::string>& allowed) {
    for (nlohmann::json::const_iterator it = object.begin(); it != object.end(); ++it) {
        if (allowed.count(it.key()) == 0) return false;
    }
    return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool parsePage(const nlohmann::json& body, std::uint32_t* offset, std::uint32_t* page_size) {
    if (!body.count("offset") || !body.count("page_size") ||
        !body.at("offset").is_number_unsigned() || !body.at("page_size").is_number_unsigned())
        return false;
    const std::uint64_t parsed_offset = body.at("offset").get<std::uint64_t>();
    const std::uint64_t parsed_size = body.at("page_size").get<std::uint64_t>();
    if (parsed_size == 0 || parsed_size > 200 || parsed_offset + parsed_size > 1000) return false;
    *offset = static_cast<std::uint32_t>(parsed_offset);
    *page_size = static_cast<std::uint32_t>(parsed_size);
    return true;
}

ErrorCode businessCode(const std::string& code) {
    if (code == "INVALID_ARGUMENT") return ErrorCode::InvalidArgument;
    if (code == "LOG_NOT_FOUND") return ErrorCode::LogNotFound;
    if (code == "ERROR_CODE_NOT_FOUND") return ErrorCode::ErrorCodeNotFound;
    if (code == "INDEX_CORRUPTED") return ErrorCode::IndexCorrupted;
    if (code == "STORAGE_IO_ERROR") return ErrorCode::StorageIoError;
    if (code == "MYSQL_UNAVAILABLE") return ErrorCode::MySqlUnavailable;
    return ErrorCode::IndexCorrupted;
}

template <typename Result>
bool handleTransport(const Result& result, wfrest::HttpResp* response,
                     const std::string& request_id) {
    if (result.status == RpcCallStatus::Success) return true;
    const ErrorCode code = result.status == RpcCallStatus::Timeout
                               ? ErrorCode::SearchRpcTimeout
                               : ErrorCode::SearchRpcUnavailable;
    sendApiResponse(response, code,
                    result.status == RpcCallStatus::Timeout ? "search RPC timed out"
                                                            : "search RPC is unavailable",
                    request_id, nullptr);
    return false;
}

nlohmann::json summaryJson(const rpc::LogSummary& item) {
    nlohmann::json result =
        nlohmann::json{{"doc_id", item.doc_id()},           {"score", item.score()},
                       {"occurred_at", item.occurred_at()}, {"archived_at", item.archived_at()},
                       {"line_id", item.line_id()},         {"station_id", item.station_id()},
                       {"device_id", item.device_id()},     {"collector_id", item.collector_id()},
                       {"work_order", item.work_order()},   {"product_sn", item.product_sn()},
                       {"source_type", item.source_type()}, {"level", item.level()},
                       {"module_name", item.module_name()}, {"error_code", item.error_code()},
                       {"event_name", item.event_name()}};
    if (item.work_order().empty()) result["work_order"] = nullptr;
    if (item.product_sn().empty()) result["product_sn"] = nullptr;
    if (item.error_code().empty()) result["error_code"] = nullptr;
    return result;
}

template <typename Response>
nlohmann::json pageJson(const Response& rpc_response) {
    nlohmann::json items = nlohmann::json::array();
    for (int index = 0; index < rpc_response.items_size(); ++index) {
        items.push_back(summaryJson(rpc_response.items(index)));
    }
    return nlohmann::json{{"snapshot_version", rpc_response.snapshot_version()},
                          {"total_hits", rpc_response.total_hits()},
                          {"items", items}};
}

}  // namespace

SearchController::SearchController(const OperatorAuthenticator& authenticator,
                                   const SearchRpcClient& search_rpc)
    : authenticator_(authenticator), search_rpc_(search_rpc) {}

void SearchController::registerRoutes(wfrest::HttpServer& server) {
    server.POST("/api/v1/logs/search", [this](const wfrest::HttpReq* request,
                                              wfrest::HttpResp* response, SeriesWork* series) {
        const std::string request_id = generateRequestId();
        if (!authenticate(*request, authenticator_, response, request_id)) return;
        const nlohmann::json body = nlohmann::json::parse(request->body(), nullptr, false);
        static const std::set<std::string> allowed = {
            "keywords",    "line_id", "station_id",  "device_id",  "work_order",
            "product_sn",  "levels",  "module_name", "error_code", "occurred_from",
            "occurred_to", "offset",  "page_size"};
        rpc::SearchLogsRequest rpc_request;
        std::uint32_t offset = 0;
        std::uint32_t page_size = 0;
        if (!body.is_object() || !containsOnly(body, allowed) || !body.count("keywords") ||
            !body.at("keywords").is_array() || body.at("keywords").size() > 8 ||
            !parsePage(body, &offset, &page_size) ||
            !parseFilterJson(body, rpc_request.mutable_filter())) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "search request is invalid",
                            request_id, nullptr);
            return;
        }
        for (nlohmann::json::const_iterator keyword = body.at("keywords").begin();
             keyword != body.at("keywords").end(); ++keyword) {
            if (!keyword->is_string() || !validText(keyword->get<std::string>(), 64)) {
                sendApiResponse(response, ErrorCode::InvalidArgument, "search keyword is invalid",
                                request_id, nullptr);
                return;
            }
            rpc_request.add_keywords(keyword->get<std::string>());
        }
        rpc_request.set_request_id(request_id);
        rpc_request.set_offset(offset);
        rpc_request.set_page_size(page_size);
        series->push_back(search_rpc_.createSearchLogsTask(
            rpc_request,
            [response, request_id](const RpcCallResult<rpc::SearchLogsResponse>& result) {
                if (!handleTransport(result, response, request_id)) return;
                if (result.response.code() != "OK") {
                    sendApiResponse(response, businessCode(result.response.code()),
                                    result.response.message(), request_id, nullptr);
                    return;
                }
                sendApiResponse(response, ErrorCode::Ok, "success", request_id,
                                pageJson(result.response));
            }));
    });

    server.GET("/api/v1/logs/anomalies", [this](const wfrest::HttpReq* request,
                                                wfrest::HttpResp* response, SeriesWork* series) {
        const std::string request_id = generateRequestId();
        if (!authenticate(*request, authenticator_, response, request_id)) return;
        nlohmann::json body = nlohmann::json::object();
        static const std::set<std::string> allowed = {
            "line_id",     "station_id", "device_id",     "work_order",  "product_sn", "levels",
            "module_name", "error_code", "occurred_from", "occurred_to", "offset",     "page_size"};
        for (std::map<std::string, std::string>::const_iterator it = request->query_list().begin();
             it != request->query_list().end(); ++it) {
            if (allowed.count(it->first) == 0) {
                sendApiResponse(response, ErrorCode::InvalidArgument,
                                "anomaly query contains an unknown parameter", request_id, nullptr);
                return;
            }
            body[it->first] = wfrest::CodeUtil::url_decode(it->second);
        }
        if (body.count("levels")) {
            const std::string levels = body["levels"].get<std::string>();
            body["levels"] = nlohmann::json::array();
            std::size_t begin = 0;
            while (begin <= levels.size()) {
                const std::size_t comma = levels.find(',', begin);
                body["levels"].push_back(levels.substr(begin, comma - begin));
                if (comma == std::string::npos) break;
                begin = comma + 1;
            }
        }
        std::uint64_t offset = 0;
        std::uint64_t page_size = 0;
        if (!request->has_query("offset") || !request->has_query("page_size") ||
            !parseUnsigned(body["offset"].get<std::string>(), &offset) ||
            !parseUnsigned(body["page_size"].get<std::string>(), &page_size)) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "anomaly query is invalid",
                            request_id, nullptr);
            return;
        }
        body["offset"] = offset;
        body["page_size"] = page_size;
        rpc::ListAnomaliesRequest rpc_request;
        std::uint32_t parsed_offset = 0;
        std::uint32_t parsed_size = 0;
        if (!parsePage(body, &parsed_offset, &parsed_size) ||
            !parseFilterJson(body, rpc_request.mutable_filter())) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "anomaly query is invalid",
                            request_id, nullptr);
            return;
        }
        rpc_request.set_request_id(request_id);
        rpc_request.set_offset(parsed_offset);
        rpc_request.set_page_size(parsed_size);
        series->push_back(search_rpc_.createListAnomaliesTask(
            rpc_request,
            [response, request_id](const RpcCallResult<rpc::ListAnomaliesResponse>& result) {
                if (!handleTransport(result, response, request_id)) return;
                if (result.response.code() != "OK") {
                    sendApiResponse(response, businessCode(result.response.code()),
                                    result.response.message(), request_id, nullptr);
                    return;
                }
                sendApiResponse(response, ErrorCode::Ok, "success", request_id,
                                pageJson(result.response));
            }));
    });

    server.GET("/api/v1/logs/{doc_id}", [this](const wfrest::HttpReq* request,
                                               wfrest::HttpResp* response, SeriesWork* series) {
        const std::string request_id = generateRequestId();
        if (!authenticate(*request, authenticator_, response, request_id)) return;
        std::uint64_t doc_id = 0;
        if (!parseUnsigned(request->param("doc_id"), &doc_id) || doc_id == 0) {
            sendApiResponse(response, ErrorCode::InvalidArgument, "doc_id is invalid", request_id,
                            nullptr);
            return;
        }
        rpc::GetLogDetailRequest rpc_request;
        rpc_request.set_request_id(request_id);
        rpc_request.set_doc_id(doc_id);
        series->push_back(search_rpc_.createGetLogDetailTask(
            rpc_request,
            [response, request_id](const RpcCallResult<rpc::GetLogDetailResponse>& result) {
                if (!handleTransport(result, response, request_id)) return;
                if (result.response.code() != "OK") {
                    sendApiResponse(response, businessCode(result.response.code()),
                                    result.response.message(), request_id, nullptr);
                    return;
                }
                nlohmann::json data = summaryJson(result.response.document());
                data["archive_id"] = result.response.archive_id();
                data["byte_offset"] = result.response.byte_offset();
                data["byte_length"] = result.response.byte_length();
                data["raw"] = result.response.raw();
                sendApiResponse(response, ErrorCode::Ok, "success", request_id, data);
            }));
    });

    server.GET(
        "/api/v1/error-codes/{code}",
        [this](const wfrest::HttpReq* request, wfrest::HttpResp* response, SeriesWork* series) {
            const std::string request_id = generateRequestId();
            if (!authenticate(*request, authenticator_, response, request_id)) return;
            const std::string code = wfrest::CodeUtil::url_decode(request->param("code"));
            if (!validText(code, 64)) {
                sendApiResponse(response, ErrorCode::InvalidArgument, "error code is invalid",
                                request_id, nullptr);
                return;
            }
            rpc::GetErrorCodeRequest rpc_request;
            rpc_request.set_request_id(request_id);
            rpc_request.set_error_code(code);
            series->push_back(search_rpc_.createGetErrorCodeTask(
                rpc_request,
                [response, request_id](const RpcCallResult<rpc::GetErrorCodeResponse>& result) {
                    if (!handleTransport(result, response, request_id)) return;
                    if (result.response.code() != "OK") {
                        sendApiResponse(response, businessCode(result.response.code()),
                                        result.response.message(), request_id, nullptr);
                        return;
                    }
                    nlohmann::json logs = nlohmann::json::array();
                    for (int index = 0; index < result.response.matching_logs_size(); ++index)
                        logs.push_back(summaryJson(result.response.matching_logs(index)));
                    sendApiResponse(
                        response, ErrorCode::Ok, "success", request_id,
                        nlohmann::json{{"error_code", result.response.error_code()},
                                       {"module_name", result.response.module_name()},
                                       {"title", result.response.title()},
                                       {"description", result.response.description()},
                                       {"recommended_action", result.response.recommended_action()},
                                       {"matching_logs", logs}});
                }));
        });
}

}  // namespace logtrace
}  // namespace smt
