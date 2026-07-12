/**
 * @file api_response.cpp
 * @brief 实现统一业务码映射和 JSON 响应输出。
 */

#include "datastream/common/api_response.h"

#include <wfrest/HttpMsg.h>

#include <stdexcept>

namespace smt {
namespace datastream {

const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return "OK";
        case ErrorCode::InvalidArgument:
            return "INVALID_ARGUMENT";
        case ErrorCode::AuthRequired:
            return "AUTH_REQUIRED";
        case ErrorCode::SignatureInvalid:
            return "SIGNATURE_INVALID";
        case ErrorCode::TimestampExpired:
            return "TIMESTAMP_EXPIRED";
        case ErrorCode::RequestReplayed:
            return "REQUEST_REPLAYED";
        case ErrorCode::DeviceDisabled:
            return "DEVICE_DISABLED";
        case ErrorCode::DeviceNotFound:
            return "DEVICE_NOT_FOUND";
        case ErrorCode::CollectorDeviceMismatch:
            return "COLLECTOR_DEVICE_MISMATCH";
        case ErrorCode::UploadNotFound:
            return "UPLOAD_NOT_FOUND";
        case ErrorCode::UploadDeviceMismatch:
            return "UPLOAD_DEVICE_MISMATCH";
        case ErrorCode::UploadStateConflict:
            return "UPLOAD_STATE_CONFLICT";
        case ErrorCode::ChunkContentConflict:
            return "CHUNK_CONTENT_CONFLICT";
        case ErrorCode::FileTooLarge:
            return "FILE_TOO_LARGE";
        case ErrorCode::ChunkTooLarge:
            return "CHUNK_TOO_LARGE";
        case ErrorCode::UploadLimitExceeded:
            return "UPLOAD_LIMIT_EXCEEDED";
        case ErrorCode::StorageCapacityExceeded:
            return "STORAGE_CAPACITY_EXCEEDED";
        case ErrorCode::ChunksIncomplete:
            return "CHUNKS_INCOMPLETE";
        case ErrorCode::FileIntegrityMismatch:
            return "FILE_INTEGRITY_MISMATCH";
        case ErrorCode::InvalidCursor:
            return "INVALID_CURSOR";
        case ErrorCode::ArchiveNotFound:
            return "ARCHIVE_NOT_FOUND";
        case ErrorCode::OperatorTokenInvalid:
            return "OPERATOR_TOKEN_INVALID";
        case ErrorCode::ServiceNotReady:
            return "SERVICE_NOT_READY";
        case ErrorCode::MySqlUnavailable:
            return "MYSQL_UNAVAILABLE";
        case ErrorCode::RedisUnavailable:
            return "REDIS_UNAVAILABLE";
        case ErrorCode::StorageIoError:
            return "STORAGE_IO_ERROR";
    }
    throw std::logic_error("unknown ErrorCode");
}

int httpStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return 200;
        case ErrorCode::InvalidArgument:
        case ErrorCode::InvalidCursor:
            return 400;
        case ErrorCode::AuthRequired:
        case ErrorCode::SignatureInvalid:
        case ErrorCode::TimestampExpired:
        case ErrorCode::OperatorTokenInvalid:
            return 401;
        case ErrorCode::DeviceDisabled:
        case ErrorCode::CollectorDeviceMismatch:
        case ErrorCode::UploadDeviceMismatch:
            return 403;
        case ErrorCode::DeviceNotFound:
        case ErrorCode::UploadNotFound:
        case ErrorCode::ArchiveNotFound:
            return 404;
        case ErrorCode::RequestReplayed:
        case ErrorCode::UploadStateConflict:
        case ErrorCode::ChunkContentConflict:
        case ErrorCode::ChunksIncomplete:
            return 409;
        case ErrorCode::FileTooLarge:
        case ErrorCode::ChunkTooLarge:
            return 413;
        case ErrorCode::UploadLimitExceeded:
            return 429;
        case ErrorCode::StorageCapacityExceeded:
            return 507;
        case ErrorCode::FileIntegrityMismatch:
            return 422;
        case ErrorCode::StorageIoError:
            return 500;
        case ErrorCode::ServiceNotReady:
        case ErrorCode::MySqlUnavailable:
        case ErrorCode::RedisUnavailable:
            return 503;
    }
    throw std::logic_error("unknown ErrorCode");
}

nlohmann::json makeApiResponse(ErrorCode code, const std::string& message,
                               const std::string& request_id, const nlohmann::json& data) {
    return nlohmann::json{{"code", errorCodeName(code)},
                          {"message", message},
                          {"request_id", request_id},
                          {"data", data}};
}

void sendApiResponse(wfrest::HttpResp* response, ErrorCode code, const std::string& message,
                     const std::string& request_id, const nlohmann::json& data) {
    response->set_status(httpStatus(code));
    response->add_header("X-Request-Id", request_id);
    response->Json(makeApiResponse(code, message, request_id, data).dump());
}

}  // namespace datastream
}  // namespace smt
