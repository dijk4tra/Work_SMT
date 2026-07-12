/**
 * @file spool_store.h
 * @brief 定义采集任务模型和原子持久 spool。
 */

#ifndef DATASTREAM_COLLECTOR_SPOOL_STORE_H_
#define DATASTREAM_COLLECTOR_SPOOL_STORE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace smt {
namespace datastream {

/// @brief 采集任务持久状态。
enum class CollectorTaskState {
    Discovered,  ///< 已发现但尚未完成快照。
    Ready,       ///< 已完成稳定快照，可以创建会话。
    Uploading,   ///< 已有服务端会话或正在上传。
    Completing,  ///< 全部分片已确认，等待完成归档。
    Done,        ///< 服务端已正式归档。
    Failed       ///< 永久失败，等待人工处理。
};

/// @brief 一份设备文件的持久采集任务。
struct CollectorTask {
    std::string task_id;
    std::string source_path;
    std::string payload_path;
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string collector_id;
    std::string work_order;
    std::string product_sn;
    std::string file_type;
    std::string result;
    std::string original_filename;
    std::string produced_at;
    std::uint64_t file_size;
    std::int64_t source_mtime_ns;
    std::string file_sha256;
    CollectorTaskState state;
    std::string upload_id;
    std::size_t chunk_size;
    std::size_t chunk_count;
    int retry_attempts;
    std::int64_t next_attempt_milliseconds;
    std::string last_error;
    std::uint64_t archive_id;
};

/// @brief spool 文件损坏或原子文件操作失败时抛出的异常。
class SpoolError : public std::runtime_error {
   public:
    /// @brief 使用明确原因构造 spool 异常。
    /// @param message 错误原因。
    explicit SpoolError(const std::string& message);
};

/// @brief 使用 JSON 状态文件和不可变 payload 快照维护本地队列。
class SpoolStore {
   public:
    /// @brief 保存 spool 根目录和容量边界。
    /// @param root spool 根目录。
    /// @param max_bytes payload 总量上限。
    /// @param min_free_bytes 接纳新任务后的最小剩余空间。
    SpoolStore(const std::string& root, std::uint64_t max_bytes, std::uint64_t min_free_bytes);

    /// @brief 创建目录并加载所有状态文件。
    /// @return 按 task_id 索引的持久任务。
    /// @throws SpoolError 当目录或任一状态文件无效时抛出。
    std::map<std::string, CollectorTask> initialize();

    /// @brief 原子保存任务状态并 fsync 文件和父目录。
    /// @param task 待保存任务。
    /// @throws SpoolError 当写入、fsync 或 rename 失败时抛出。
    void save(const CollectorTask& task) const;

    /// @brief 将封口源文件复制成不可变 payload 快照。
    /// @param source_path 源文件路径。
    /// @param task_id 任务编号。
    /// @param expected_size 已通过 stat 的源文件大小。
    /// @return 快照绝对路径。
    /// @throws SpoolError 当容量不足、文件变化或 I/O 失败时抛出。
    std::string snapshot(const std::string& source_path, const std::string& task_id,
                         std::uint64_t expected_size) const;

    /// @brief 返回 payload 快照路径。
    /// @param task_id 任务编号。
    /// @return spool files 目录下的绝对路径。
    std::string payloadPath(const std::string& task_id) const;

    /// @brief 删除已归档任务的本地 payload 快照。
    /// @param task 已持久化为 DONE 的任务。
    /// @throws SpoolError 当删除或目录 fsync 失败时抛出。
    void removePayload(const CollectorTask& task) const;

   private:
    std::string root_;
    std::string state_dir_;
    std::string file_dir_;
    std::uint64_t max_bytes_;
    std::uint64_t min_free_bytes_;
};

/// @brief 返回持久状态字符串。
/// @param state 任务状态。
/// @return JSON 使用的稳定状态名。
const char* collectorTaskStateName(CollectorTaskState state);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COLLECTOR_SPOOL_STORE_H_
