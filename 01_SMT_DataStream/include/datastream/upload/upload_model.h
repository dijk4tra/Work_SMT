/**
 * @file upload_model.h
 * @brief 声明上传会话元数据、Redis 会话视图和严格请求解析。
 */

#ifndef DATASTREAM_UPLOAD_UPLOAD_MODEL_H_
#define DATASTREAM_UPLOAD_UPLOAD_MODEL_H_

#include <cstdint>
#include <string>
#include <vector>

#include "datastream/config/app_config.h"

namespace smt {
namespace datastream {

/// @brief 已通过创建上传接口校验的文件元数据。
struct CreateUploadRequest {
    std::string station_id;
    std::string collector_id;
    std::string work_order;
    bool has_work_order;
    std::string product_sn;
    bool has_product_sn;
    std::string file_type;
    std::string result;
    bool has_result;
    std::string original_filename;
    std::string extension;
    std::uint64_t file_size;
    std::string file_sha256;
    std::size_t chunk_size;
    std::size_t chunk_count;
    std::string produced_at;
};

/// @brief Redis 中一次上传会话的必要字段。
struct UploadSession {
    std::string upload_id;
    std::string state;
    std::string device_id;
    std::string station_id;
    std::string line_id;
    std::string collector_id;
    std::string work_order;
    std::string product_sn;
    std::string file_type;
    std::string result;
    std::string original_filename;
    std::string extension;
    std::string temp_path;
    std::string relative_path;
    std::string produced_at;
    std::uint64_t file_size;
    std::string file_sha256;
    std::size_t chunk_size;
    std::size_t chunk_count;
    std::int64_t expires_at_seconds;
    std::string failure_code;
    std::uint64_t archive_id;
    std::int64_t archived_at_milliseconds;
};

/// @brief 严格解析创建上传会话 JSON。
/// @param body HTTP JSON 请求体。
/// @param config 上传大小与分片配置。
/// @param request 成功时接收已校验请求。
/// @param error_message 失败时接收稳定的非敏感原因。
/// @return 请求符合字段和文件类型契约时返回 true。
bool parseCreateUploadRequest(const std::string& body, const UploadConfig& config,
                              CreateUploadRequest* request, std::string* error_message);

/// @brief 从 Redis HGETALL 数组解析上传会话。
/// @param values Redis 字符串数组。
/// @param session 成功时接收会话。
/// @return 必要字段完整且数值合法时返回 true。
bool parseUploadSession(const std::vector<std::string>& values, UploadSession* session);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_UPLOAD_UPLOAD_MODEL_H_
