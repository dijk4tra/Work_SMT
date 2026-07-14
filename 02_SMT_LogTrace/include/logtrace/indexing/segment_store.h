/**
 * @file segment_store.h
 * @brief 声明不可变 Segment 的构建、校验、加载和清理接口。
 */

#ifndef LOGTRACE_INDEXING_SEGMENT_STORE_H_
#define LOGTRACE_INDEXING_SEGMENT_STORE_H_

#include <string>
#include <vector>

#include "logtrace/indexing/segment_models.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 管理固定版本的 terms、postings、documents 和 files Segment 文件。
class SegmentStore {
   public:
    /// @brief 保存索引目录和一期归档读取边界。
    /// @param storage 已初始化的存储目录。
    explicit SegmentStore(const StoragePaths& storage);

    /// @brief 从完整 PARSED 工件构建并原子发布 Segment。
    /// @param batch 已校验的解析批次。
    /// @return Segment 名称、manifest 摘要和统计。
    /// @throws std::runtime_error 当归档变化、格式生成或文件 I/O 失败时抛出。
    SegmentBuildResult build(const ParsedBatchData& batch) const;

    /// @brief 加载数据库登记为 READY 的 Segment。
    /// @param descriptor READY 批次名称和 manifest 摘要。
    /// @return 已完成文件布局、摘要和关联校验的 Segment。
    /// @throws std::runtime_error 当 Segment 缺失、损坏或数据库事实不一致时抛出。
    LoadedSegment load(const ReadySegmentDescriptor& descriptor) const;

    /// @brief 加载已 rename 但尚未登记 READY 的 Segment。
    /// @param batch_id BUILDING 批次编号。
    /// @return 已校验 Segment，用于启动恢复发布。
    /// @throws std::runtime_error 当 Segment 缺失或损坏时抛出。
    LoadedSegment loadUnpublished(std::uint64_t batch_id) const;

    /// @brief 判断指定批次的正式 Segment 目录是否存在。
    /// @param batch_id 批次编号。
    /// @return 正式目录存在时为 true。
    bool publishedExists(std::uint64_t batch_id) const;

    /// @brief 删除指定批次的临时和正式 Segment。
    /// @param batch_id 批次编号。
    /// @throws std::runtime_error 当已存在工件无法删除时抛出。
    void remove(std::uint64_t batch_id) const;

    /// @brief 清理所有未 rename 的临时 Segment 目录。
    /// @throws std::runtime_error 当临时目录无法检查或删除时抛出。
    void cleanupBuilding() const;

    /// @brief 枚举正式 segments 目录中的合法目录名。
    /// @return 按名称升序排列的 `segment_<batch_id>` 目录。
    /// @throws std::runtime_error 当目录无法读取时抛出。
    std::vector<std::string> listPublishedNames() const;

   private:
    const StoragePaths& storage_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_SEGMENT_STORE_H_
