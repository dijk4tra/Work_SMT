/**
 * @file http_uploader.h
 * @brief 声明使用设备 HMAC 的同步 Workflow 上传客户端。
 */

#ifndef DATASTREAM_COLLECTOR_HTTP_UPLOADER_H_
#define DATASTREAM_COLLECTOR_HTTP_UPLOADER_H_

#include <cstddef>
#include <set>
#include <string>

#include "datastream/collector/collector_config.h"
#include "datastream/collector/spool_store.h"

namespace smt {
namespace datastream {

/// @brief 单次 HTTP 调用的分类结果。
enum class UploadCallStatus {
    Success,          ///< 请求成功并解析响应。
    Retryable,        ///< 网络、超时或 5xx，可按上限退避。
    SessionMissing,   ///< 服务端会话不存在，需要重建本文件会话。
    PermanentFailure  ///< 其他 4xx 或损坏响应，不自动重试。
};

/// @brief 上传步骤结果及服务端返回字段。
struct UploadCallResult {
    UploadCallStatus status;
    std::string error_code;
    std::string upload_id;
    std::size_t chunk_size;
    std::size_t chunk_count;
    std::set<std::size_t> missing_chunks;
    std::uint64_t archive_id;
};

/// @brief 使用 Workflow HTTP 任务执行创建、进度、分片和完成调用。
class HttpUploader {
   public:
    /// @brief 保存服务地址和请求观察超时。
    /// @param server_url DataStream 服务根地址。
    /// @param timeout_ms 单次请求观察超时毫秒数。
    HttpUploader(const std::string& server_url, int timeout_ms);

    /// @brief 创建服务端上传会话。
    /// @param task 本地采集任务。
    /// @param device 设备身份和密钥。
    /// @return 创建结果及分片参数。
    UploadCallResult createSession(const CollectorTask& task,
                                   const CollectorDeviceConfig& device) const;

    /// @brief 查询服务端会话缺失分片。
    /// @param task 已保存 upload_id 的任务。
    /// @param device 设备身份和密钥。
    /// @return 查询结果和缺失分片集合。
    UploadCallResult queryProgress(const CollectorTask& task,
                                   const CollectorDeviceConfig& device) const;

    /// @brief 上传一个 payload 分片。
    /// @param task 上传任务。
    /// @param device 设备身份和密钥。
    /// @param chunk_no 分片编号。
    /// @return 分片调用分类结果。
    UploadCallResult uploadChunk(const CollectorTask& task, const CollectorDeviceConfig& device,
                                 std::size_t chunk_no) const;

    /// @brief 请求服务端完成归档。
    /// @param task 已上传全部分片的任务。
    /// @param device 设备身份和密钥。
    /// @return 完成结果和归档编号。
    UploadCallResult complete(const CollectorTask& task, const CollectorDeviceConfig& device) const;

   private:
    /// @brief 执行带设备签名的 HTTP 请求。
    /// @param method HTTP 方法。
    /// @param path 签名和请求使用的绝对路径。
    /// @param content_type 可空的 Content-Type。
    /// @param body 请求正文。
    /// @param device 设备身份和密钥。
    /// @return 已完成通用错误分类的响应。
    UploadCallResult request(const std::string& method, const std::string& path,
                             const std::string& content_type, const std::string& body,
                             const CollectorDeviceConfig& device) const;

    std::string server_url_;
    int timeout_ms_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COLLECTOR_HTTP_UPLOADER_H_
