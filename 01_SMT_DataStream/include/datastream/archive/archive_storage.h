/**
 * @file archive_storage.h
 * @brief 声明窗口化文件校验和同文件系统原子归档操作。
 */

#ifndef DATASTREAM_ARCHIVE_ARCHIVE_STORAGE_H_
#define DATASTREAM_ARCHIVE_ARCHIVE_STORAGE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "datastream/storage/storage_paths.h"
#include "datastream/upload/upload_model.h"

namespace smt {
namespace datastream {

/// @brief 文件校验与移动结果。
enum class ArchiveStorageStatus {
    Archived,           ///< 文件校验通过且位于正式路径。
    IntegrityMismatch,  ///< 文件大小或摘要与会话不一致。
    IoError             ///< stat、open、mmap、目录或 rename 失败。
};

/// @brief 正式归档文件操作结果。
struct ArchiveStorageResult {
    ArchiveStorageStatus status;
    std::string relative_path;
};

/// @brief 使用服务端路径和窗口化 mmap 校验后原子移动文件。
/// @param storage 已初始化且同文件系统的存储目录。
/// @param session 待归档上传会话。
/// @param relative_path 已持久化到会话的正式相对路径。
/// @param mmap_window_bytes 单次映射窗口大小。
/// @return 校验移动状态和确定性相对路径。
ArchiveStorageResult verifyAndArchiveFile(const StoragePaths& storage, const UploadSession& session,
                                          const std::string& relative_path,
                                          std::size_t mmap_window_bytes);

/// @brief 根据服务端归档日期和会话标识生成正式相对路径。
/// @param session 上传会话。
/// @param archived_at_milliseconds 平台归档时间。
/// @return 不含归档根目录的安全相对路径。
std::string buildArchiveRelativePath(const UploadSession& session,
                                     std::int64_t archived_at_milliseconds);

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_ARCHIVE_ARCHIVE_STORAGE_H_
