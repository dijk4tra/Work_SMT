/**
 * @file http_uploader.cpp
 * @brief 实现设备 HMAC、Workflow HTTP 调用和失败分类。
 */

#include "datastream/collector/http_uploader.h"

#include <fcntl.h>
#include <unistd.h>
#include <workflow/WFFacilities.h>
#include <workflow/WFTaskFactory.h>

#include <chrono>
#include <cstdlib>
#include <nlohmann/json.hpp>

#include "datastream/auth/crypto.h"
#include "datastream/common/uuid.h"

namespace smt {
namespace datastream {
namespace {

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

UploadCallResult emptyResult(UploadCallStatus status, const std::string& code) {
    UploadCallResult result;
    result.status = status;
    result.error_code = code;
    result.chunk_size = 0;
    result.chunk_count = 0;
    result.archive_id = 0;
    return result;
}

std::string hexToBinary(const std::string& value) {
    std::string result(value.size() / 2, '\0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] =
            static_cast<char>(std::strtoul(value.substr(index * 2, 2).c_str(), nullptr, 16));
    }
    return result;
}

bool readChunk(const CollectorTask& task, std::size_t chunk_no, std::string* body) {
    const std::uint64_t offset = static_cast<std::uint64_t>(chunk_no) * task.chunk_size;
    const std::size_t length =
        static_cast<std::size_t>(std::min<std::uint64_t>(task.chunk_size, task.file_size - offset));
    body->assign(length, '\0');
    const int descriptor = ::open(task.payload_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return false;
    std::size_t read_bytes = 0;
    while (read_bytes < length) {
        const ssize_t result = ::pread(descriptor, &(*body)[read_bytes], length - read_bytes,
                                       static_cast<off_t>(offset + read_bytes));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            ::close(descriptor);
            return false;
        }
        read_bytes += static_cast<std::size_t>(result);
    }
    return ::close(descriptor) == 0;
}

}  // namespace

HttpUploader::HttpUploader(const std::string& server_url, int timeout_ms)
    : server_url_(server_url), timeout_ms_(timeout_ms) {}

UploadCallResult HttpUploader::request(const std::string& method, const std::string& path,
                                       const std::string& content_type, const std::string& body,
                                       const CollectorDeviceConfig& device) const {
    const std::string request_id = "col-" + generateUuidV4();
    const std::string timestamp = std::to_string(nowSeconds());
    const std::string digest = sha256Hex(body);
    const std::string canonical =
        buildDeviceCanonicalString(method, path, device.device_id, timestamp, request_id, digest);
    const std::string secret = hexToBinary(sha256Hex(device.secret));
    const std::string signature = hmacSha256Hex(secret, canonical);
    bool transport_ok = false;
    int http_status = 0;
    std::string response_body;
    WFFacilities::WaitGroup wait_group(1);
    WFHttpTask* http = WFTaskFactory::create_http_task(
        server_url_ + path, 0, 0,
        [&transport_ok, &http_status, &response_body, &wait_group](WFHttpTask* completed) {
            if (completed->get_state() == WFT_STATE_SUCCESS) {
                std::string status;
                const void* data = nullptr;
                std::size_t size = 0;
                if (completed->get_resp()->get_status_code(status) &&
                    completed->get_resp()->get_parsed_body(&data, &size)) {
                    transport_ok = true;
                    http_status = std::atoi(status.c_str());
                    response_body.assign(static_cast<const char*>(data), size);
                }
            }
            wait_group.done();
        });
    http->set_watch_timeout(timeout_ms_);
    http->set_send_timeout(timeout_ms_);
    http->set_receive_timeout(timeout_ms_);
    protocol::HttpRequest* outgoing = http->get_req();
    outgoing->set_method(method);
    outgoing->add_header_pair("X-Device-Id", device.device_id);
    outgoing->add_header_pair("X-Timestamp", timestamp);
    outgoing->add_header_pair("X-Request-Id", request_id);
    outgoing->add_header_pair("X-Content-SHA256", digest);
    outgoing->add_header_pair("X-Signature", signature);
    if (!content_type.empty()) outgoing->add_header_pair("Content-Type", content_type);
    if (!body.empty()) outgoing->append_output_body(body);
    http->start();
    wait_group.wait();
    if (!transport_ok) return emptyResult(UploadCallStatus::Retryable, "NETWORK_ERROR");
    const nlohmann::json json = nlohmann::json::parse(response_body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("code") ||
        !json.at("code").is_string() || !json.contains("data")) {
        return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
    }
    const std::string code = json.at("code").get<std::string>();
    if (http_status >= 500 || code == "UPLOAD_LIMIT_EXCEEDED" || code == "CHUNKS_INCOMPLETE") {
        return emptyResult(UploadCallStatus::Retryable, code);
    }
    if (http_status == 404 && code == "UPLOAD_NOT_FOUND") {
        return emptyResult(UploadCallStatus::SessionMissing, code);
    }
    if (http_status < 200 || http_status >= 300) {
        return emptyResult(UploadCallStatus::PermanentFailure, code);
    }
    UploadCallResult result = emptyResult(UploadCallStatus::Success, code);
    const nlohmann::json& data = json.at("data");
    if (!data.is_object()) {
        return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
    }
    if (data.contains("upload_id") && data.at("upload_id").is_string()) {
        result.upload_id = data.at("upload_id").get<std::string>();
    }
    if (data.contains("chunk_size") && data.at("chunk_size").is_number_unsigned()) {
        result.chunk_size = data.at("chunk_size").get<std::size_t>();
    }
    if (data.contains("chunk_count") && data.at("chunk_count").is_number_unsigned()) {
        result.chunk_count = data.at("chunk_count").get<std::size_t>();
    }
    if (data.contains("missing_chunks") && data.at("missing_chunks").is_array()) {
        for (std::size_t index = 0; index < data.at("missing_chunks").size(); ++index) {
            if (!data.at("missing_chunks")[index].is_number_unsigned()) {
                return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
            }
            result.missing_chunks.insert(data.at("missing_chunks")[index].get<std::size_t>());
        }
    }
    if (data.contains("archive_id") && data.at("archive_id").is_number_unsigned()) {
        result.archive_id = data.at("archive_id").get<std::uint64_t>();
    }
    return result;
}

UploadCallResult HttpUploader::createSession(const CollectorTask& task,
                                             const CollectorDeviceConfig& device) const {
    nlohmann::json body{
        {"station_id", task.station_id},
        {"collector_id", task.collector_id},
        {"work_order",
         task.work_order.empty() ? nlohmann::json(nullptr) : nlohmann::json(task.work_order)},
        {"product_sn",
         task.product_sn.empty() ? nlohmann::json(nullptr) : nlohmann::json(task.product_sn)},
        {"file_type", task.file_type},
        {"result", task.result.empty() ? nlohmann::json(nullptr) : nlohmann::json(task.result)},
        {"original_filename", task.original_filename},
        {"file_size", task.file_size},
        {"file_sha256", task.file_sha256},
        {"chunk_size", task.chunk_size},
        {"produced_at", task.produced_at}};
    UploadCallResult result =
        request("POST", "/api/v1/uploads", "application/json", body.dump(), device);
    if (result.status == UploadCallStatus::Success &&
        (result.upload_id.empty() || result.chunk_size == 0 || result.chunk_count == 0)) {
        return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
    }
    return result;
}

UploadCallResult HttpUploader::queryProgress(const CollectorTask& task,
                                             const CollectorDeviceConfig& device) const {
    UploadCallResult result = request("GET", "/api/v1/uploads/" + task.upload_id, "", "", device);
    if (result.status == UploadCallStatus::Success && result.chunk_count != task.chunk_count) {
        return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
    }
    return result;
}

UploadCallResult HttpUploader::uploadChunk(const CollectorTask& task,
                                           const CollectorDeviceConfig& device,
                                           std::size_t chunk_no) const {
    std::string body;
    if (!readChunk(task, chunk_no, &body)) {
        return emptyResult(UploadCallStatus::PermanentFailure, "LOCAL_PAYLOAD_IO_ERROR");
    }
    return request("PUT",
                   "/api/v1/uploads/" + task.upload_id + "/chunks/" + std::to_string(chunk_no),
                   "application/octet-stream", body, device);
}

UploadCallResult HttpUploader::complete(const CollectorTask& task,
                                        const CollectorDeviceConfig& device) const {
    UploadCallResult result =
        request("POST", "/api/v1/uploads/" + task.upload_id + "/complete", "", "", device);
    if (result.status == UploadCallStatus::Success && result.archive_id == 0) {
        return emptyResult(UploadCallStatus::PermanentFailure, "INVALID_SERVER_RESPONSE");
    }
    return result;
}

}  // namespace datastream
}  // namespace smt
