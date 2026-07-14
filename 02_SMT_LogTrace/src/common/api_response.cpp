/**
 * @file api_response.cpp
 * @brief 实现 Gateway 统一 JSON 响应和错误映射。
 */

#include "logtrace/common/api_response.h"

namespace smt {
namespace logtrace {

const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "OK";
        case ErrorCode::ServiceNotReady:
            return "SERVICE_NOT_READY";
        case ErrorCode::SearchRpcUnavailable:
            return "SEARCH_RPC_UNAVAILABLE";
        case ErrorCode::SearchRpcTimeout:
            return "SEARCH_RPC_TIMEOUT";
        case ErrorCode::InvalidArgument:
            return "INVALID_ARGUMENT";
        case ErrorCode::OperatorTokenInvalid:
            return "OPERATOR_TOKEN_INVALID";
        case ErrorCode::LogNotFound:
            return "LOG_NOT_FOUND";
        case ErrorCode::ErrorCodeNotFound:
            return "ERROR_CODE_NOT_FOUND";
        case ErrorCode::IndexCorrupted:
            return "INDEX_CORRUPTED";
        case ErrorCode::StorageIoError:
            return "STORAGE_IO_ERROR";
        case ErrorCode::MySqlUnavailable:
            return "MYSQL_UNAVAILABLE";
    }
    return "INTERNAL_ERROR";
}

int httpStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return 200;
        case ErrorCode::ServiceNotReady:
            return 503;
        case ErrorCode::SearchRpcUnavailable:
            return 502;
        case ErrorCode::SearchRpcTimeout:
            return 504;
        case ErrorCode::InvalidArgument:
            return 400;
        case ErrorCode::OperatorTokenInvalid:
            return 401;
        case ErrorCode::LogNotFound:
        case ErrorCode::ErrorCodeNotFound:
            return 404;
        case ErrorCode::IndexCorrupted:
        case ErrorCode::StorageIoError:
            return 500;
        case ErrorCode::MySqlUnavailable:
            return 503;
    }
    return 500;
}

void sendApiResponse(wfrest::HttpResp* response, ErrorCode code, const std::string& message,
                     const std::string& request_id, const nlohmann::json& data) {
    response->set_status(httpStatus(code));
    response->add_header_pair("Content-Type", "application/json; charset=utf-8");
    response->add_header_pair("X-Request-Id", request_id);
    response->String(nlohmann::json{
        {"code", errorCodeName(code)},
        {"message", message},
        {"request_id", request_id},
        {"data", data}}.dump());
}

}  // namespace logtrace
}  // namespace smt
