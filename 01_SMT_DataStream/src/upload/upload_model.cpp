/**
 * @file upload_model.cpp
 * @brief 实现上传会话请求和 Redis 会话字段解析。
 */

#include "datastream/upload/upload_model.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

#include "datastream/common/time_utils.h"
#include "datastream/common/validation.h"

namespace smt {
namespace datastream {
namespace {

bool isPrintableAscii(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

bool isLowerHex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!(std::isdigit(static_cast<unsigned char>(value[index])) ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool validFilename(const std::string& value) {
    if (value.empty() || value.size() > 255) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(value[index]);
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') {
            return false;
        }
    }
    return true;
}

bool extensionFor(const std::string& filename, const std::string& file_type,
                  std::string* extension) {
    const std::size_t dot = filename.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == filename.size()) {
        return false;
    }
    *extension = filename.substr(dot + 1);
    std::transform(extension->begin(), extension->end(), extension->begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::map<std::string, std::set<std::string>> allowed{
        {"DETECTION_RESULT", {"json", "csv"}},
        {"TEST_REPORT", {"csv"}},
        {"NG_IMAGE", {"png", "jpg", "jpeg"}},
        {"DEVICE_EXPORT", {"zip", "pdf"}},
        {"RUNTIME_LOG", {"log", "txt"}}};
    const std::map<std::string, std::set<std::string>>::const_iterator type =
        allowed.find(file_type);
    return type != allowed.end() && type->second.count(*extension) == 1;
}

bool parseUnsigned(const std::string& value, std::uint64_t* result) {
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(value[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    *result = parsed;
    return true;
}

}  // namespace

bool parseCreateUploadRequest(const std::string& body, const UploadConfig& config,
                              CreateUploadRequest* request, std::string* error_message) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    const std::set<std::string> expected{
        "station_id",        "collector_id", "work_order",  "product_sn", "file_type",  "result",
        "original_filename", "file_size",    "file_sha256", "chunk_size", "produced_at"};
    if (json.is_discarded() || !json.is_object() || json.size() != expected.size()) {
        *error_message = "upload fields do not match the contract";
        return false;
    }
    for (nlohmann::json::const_iterator it = json.begin(); it != json.end(); ++it) {
        if (expected.count(it.key()) == 0) {
            *error_message = "upload fields do not match the contract";
            return false;
        }
    }
    if (!json["station_id"].is_string() || !json["collector_id"].is_string() ||
        !json["file_type"].is_string() || !json["original_filename"].is_string() ||
        !json["file_sha256"].is_string() || !json["produced_at"].is_string() ||
        !json["file_size"].is_number_unsigned() || !json["chunk_size"].is_number_unsigned() ||
        !(json["work_order"].is_null() || json["work_order"].is_string()) ||
        !(json["product_sn"].is_null() || json["product_sn"].is_string()) ||
        !(json["result"].is_null() || json["result"].is_string())) {
        *error_message = "upload field type is invalid";
        return false;
    }

    request->station_id = json["station_id"].get<std::string>();
    request->collector_id = json["collector_id"].get<std::string>();
    request->has_work_order = json["work_order"].is_string();
    request->work_order = request->has_work_order ? json["work_order"].get<std::string>() : "";
    request->has_product_sn = json["product_sn"].is_string();
    request->product_sn = request->has_product_sn ? json["product_sn"].get<std::string>() : "";
    request->file_type = json["file_type"].get<std::string>();
    request->has_result = json["result"].is_string();
    request->result = request->has_result ? json["result"].get<std::string>() : "";
    request->original_filename = json["original_filename"].get<std::string>();
    request->file_size = json["file_size"].get<std::uint64_t>();
    request->file_sha256 = json["file_sha256"].get<std::string>();
    request->chunk_size = json["chunk_size"].get<std::size_t>();
    request->produced_at = json["produced_at"].get<std::string>();

    std::int64_t produced_milliseconds = 0;
    if (request->file_size > config.max_file_size_bytes) {
        *error_message = "file_size exceeds the configured limit";
        return false;
    }
    if (request->chunk_size > config.max_chunk_size_bytes) {
        *error_message = "chunk_size exceeds the configured limit";
        return false;
    }
    if (!isSmtIdentifier(request->station_id) || !isSmtIdentifier(request->collector_id) ||
        !validFilename(request->original_filename) || !isLowerHex(request->file_sha256) ||
        !parseIso8601Milliseconds(request->produced_at, &produced_milliseconds) ||
        request->file_size == 0 || request->chunk_size < config.min_chunk_size_bytes ||
        !extensionFor(request->original_filename, request->file_type, &request->extension)) {
        *error_message = "upload field value is invalid";
        return false;
    }
    if ((request->has_work_order && !isPrintableAscii(request->work_order, 64)) ||
        (request->has_product_sn && !isPrintableAscii(request->product_sn, 96)) ||
        (request->has_result && request->result != "PASS" && request->result != "NG")) {
        *error_message = "upload business field is invalid";
        return false;
    }
    const bool trace_file =
        request->file_type == "DETECTION_RESULT" || request->file_type == "TEST_REPORT";
    if ((trace_file && (!request->has_product_sn || !request->has_result)) ||
        (request->file_type == "NG_IMAGE" &&
         (!request->has_product_sn || !request->has_result || request->result != "NG"))) {
        *error_message = "file type metadata is incomplete";
        return false;
    }
    request->chunk_count = static_cast<std::size_t>((request->file_size + request->chunk_size - 1) /
                                                    request->chunk_size);
    return true;
}

bool parseUploadSession(const std::vector<std::string>& values, UploadSession* session) {
    if (values.empty() || values.size() % 2 != 0) {
        return false;
    }
    std::map<std::string, std::string> fields;
    for (std::size_t index = 0; index < values.size(); index += 2) {
        fields[values[index]] = values[index + 1];
    }
    const char* required[] = {"upload_id",  "state",        "device_id",         "station_id",
                              "line_id",    "collector_id", "work_order",        "product_sn",
                              "file_type",  "result",       "original_filename", "extension",
                              "temp_path",  "produced_at",  "file_size",         "file_sha256",
                              "chunk_size", "chunk_count",  "expires_at",        "failure_code"};
    for (std::size_t index = 0; index < sizeof(required) / sizeof(required[0]); ++index) {
        if (fields.count(required[index]) == 0) {
            return false;
        }
    }
    std::uint64_t file_size = 0;
    std::uint64_t chunk_size = 0;
    std::uint64_t chunk_count = 0;
    std::uint64_t expires_at = 0;
    std::uint64_t archive_id = 0;
    std::uint64_t archived_at = 0;
    if (!parseUnsigned(fields["file_size"], &file_size) ||
        !parseUnsigned(fields["chunk_size"], &chunk_size) ||
        !parseUnsigned(fields["chunk_count"], &chunk_count) ||
        !parseUnsigned(fields["expires_at"], &expires_at)) {
        return false;
    }
    if (fields.count("archive_id") != 0 && !fields["archive_id"].empty() &&
        !parseUnsigned(fields["archive_id"], &archive_id)) {
        return false;
    }
    if (fields.count("archived_at") != 0 && !fields["archived_at"].empty() &&
        !parseUnsigned(fields["archived_at"], &archived_at)) {
        return false;
    }
    *session = UploadSession{fields["upload_id"],
                             fields["state"],
                             fields["device_id"],
                             fields["station_id"],
                             fields["line_id"],
                             fields["collector_id"],
                             fields["work_order"],
                             fields["product_sn"],
                             fields["file_type"],
                             fields["result"],
                             fields["original_filename"],
                             fields["extension"],
                             fields["temp_path"],
                             fields["relative_path"],
                             fields["produced_at"],
                             file_size,
                             fields["file_sha256"],
                             static_cast<std::size_t>(chunk_size),
                             static_cast<std::size_t>(chunk_count),
                             static_cast<std::int64_t>(expires_at),
                             fields["failure_code"],
                             archive_id,
                             static_cast<std::int64_t>(archived_at)};
    return true;
}

}  // namespace datastream
}  // namespace smt
