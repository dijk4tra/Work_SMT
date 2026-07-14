/**
 * @file parsed_batch_reader.h
 * @brief 声明第二阶段 PARSED 工件的严格加载和摘要校验接口。
 */

#ifndef LOGTRACE_INDEXING_PARSED_BATCH_READER_H_
#define LOGTRACE_INDEXING_PARSED_BATCH_READER_H_

#include <string>

#include "logtrace/indexing/segment_models.h"

namespace smt {
namespace logtrace {

/// @brief 加载 manifest 和两个 JSONL 文件，并验证批次内全部关联关系。
class ParsedBatchReader {
   public:
    /// @brief 保存二期索引根目录。
    /// @param index_root 已初始化的索引根目录。
    explicit ParsedBatchReader(const std::string& index_root);

    /// @brief 加载状态库指定的 PARSED 工件。
    /// @param descriptor 状态库中的批次、路径和摘要事实。
    /// @return 完整校验的文件与文档。
    /// @throws std::runtime_error 当路径、摘要、JSON 或关联关系无效时抛出。
    ParsedBatchData load(const ParsedBatchDescriptor& descriptor) const;

   private:
    std::string index_root_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_PARSED_BATCH_READER_H_
