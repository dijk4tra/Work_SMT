/**
 * @file parsed_batch_writer.h
 * @brief 声明第二阶段解析工件原子写入接口。
 */

#ifndef LOGTRACE_INDEXING_PARSED_BATCH_WRITER_H_
#define LOGTRACE_INDEXING_PARSED_BATCH_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "logtrace/indexing/index_models.h"

namespace smt {
namespace logtrace {

/// @brief 已原子发布解析批次的路径和 manifest 摘要。
struct ParsedBatchArtifact {
    std::string relative_path;
    std::string manifest_sha256;
};

/// @brief 将结构化元数据写入可重建 JSONL 解析工件。
class ParsedBatchWriter {
   public:
    /// @brief 保存二期索引根目录。
    /// @param index_root 已初始化且可写的索引根目录。
    explicit ParsedBatchWriter(const std::string& index_root);

    /// @brief 在 `.building` 完整写入后原子发布 PARSED 批次。
    /// @param batch_id 批次编号。
    /// @param first_archive_id 批次最小归档编号。
    /// @param last_archive_id 批次最大归档编号。
    /// @param source_file_count 纳入批次的文件总数。
    /// @param failed_file_count 解析失败文件数。
    /// @param archives 解析成功的归档及文档。
    /// @return 正式相对路径和 manifest SHA-256。
    /// @throws std::runtime_error 当目录或文件 I/O 失败时抛出。
    ParsedBatchArtifact write(std::uint64_t batch_id, std::uint64_t first_archive_id,
                              std::uint64_t last_archive_id, std::size_t source_file_count,
                              std::size_t failed_file_count,
                              const std::vector<ParsedArchive>& archives) const;

    /// @brief 删除指定批次的正式和临时解析工件。
    /// @param batch_id 批次编号。
    /// @throws std::runtime_error 当已存在工件无法删除时抛出。
    void remove(std::uint64_t batch_id) const;

   private:
    std::string index_root_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_PARSED_BATCH_WRITER_H_
