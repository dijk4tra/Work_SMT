/**
 * @file original_log_reader.h
 * @brief 声明一期归档完整性复核和精确字节范围回读接口。
 */

#ifndef LOGTRACE_INDEXING_ORIGINAL_LOG_READER_H_
#define LOGTRACE_INDEXING_ORIGINAL_LOG_READER_H_

#include <cstdint>
#include <string>

#include "logtrace/indexing/segment_models.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 顺序复核归档大小和 SHA-256。
/// @param storage 受控归档目录。
/// @param file Segment 文件表记录。
/// @throws std::runtime_error 当路径、I/O、大小或摘要不一致时抛出。
void verifyOriginalArchive(const StoragePaths& storage, const SegmentFileRecord& file);

/// @brief 使用 pread 回读归档中的精确原始记录。
/// @param storage 受控归档目录。
/// @param file Segment 文件表记录。
/// @param byte_offset 记录起始字节。
/// @param byte_length 记录字节数。
/// @return 不包含行结束符的原始记录字节。
/// @throws std::runtime_error 当路径、文件大小、范围或 pread 失败时抛出。
std::string readOriginalRecord(const StoragePaths& storage, const SegmentFileRecord& file,
                               std::uint64_t byte_offset, std::uint64_t byte_length);

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_ORIGINAL_LOG_READER_H_
