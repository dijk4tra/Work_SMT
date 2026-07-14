/**
 * @file index_snapshot.h
 * @brief 声明 READY Segment 的不可变查询快照和原文回读入口。
 */

#ifndef LOGTRACE_INDEXING_INDEX_SNAPSHOT_H_
#define LOGTRACE_INDEXING_INDEX_SNAPSHOT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "logtrace/indexing/segment_models.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 一组按 batch_id 升序排列的 READY Segment。
class IndexSnapshot {
   public:
    /// @brief 构造空查询快照。
    IndexSnapshot();

    /// @brief 保存完整加载的 READY Segment。
    /// @param segments 按 batch_id 升序且不重复的 Segment。
    explicit IndexSnapshot(const std::vector<std::shared_ptr<const LoadedSegment> >& segments);

    /// @brief 返回快照版本，即最大 READY batch_id。
    /// @return 无 Segment 时为零。
    std::uint64_t version() const;

    /// @brief 返回快照中的 Segment 数量。
    /// @return Segment 数量。
    std::size_t segmentCount() const;

    /// @brief 返回快照中的总文档数。
    /// @return 文档总数。
    std::size_t documentCount() const;

    /// @brief 返回按 batch_id 升序排列的不可变 Segment。
    /// @return Segment shared_ptr 集合的常量引用。
    const std::vector<std::shared_ptr<const LoadedSegment> >& segments() const;

    /// @brief 按稳定 doc_id 查找文档及所属文件。
    /// @param doc_id 批次高 32 位与局部编号低 32 位组成的文档编号。
    /// @param document 成功时接收文档指针。
    /// @param file 成功时接收文件表指针。
    /// @return READY 快照中存在该文档时为 true。
    bool findDocument(std::uint64_t doc_id, const SegmentDocumentRecord** document,
                      const SegmentFileRecord** file) const;

   private:
    std::vector<std::shared_ptr<const LoadedSegment> > segments_;
    std::uint64_t version_;
    std::size_t document_count_;
};

/// @brief 以短锁交换 shared_ptr 查询快照。
class IndexSnapshotStore {
   public:
    /// @brief 创建只包含空快照的存储。
    IndexSnapshotStore();

    /// @brief 原子替换完整 READY 快照。
    /// @param snapshot 已完整加载和校验的新快照。
    void replace(const std::shared_ptr<const IndexSnapshot>& snapshot);

    /// @brief 获取当前不可变快照。
    /// @return 可跨本次调用安全持有的 shared_ptr。
    std::shared_ptr<const IndexSnapshot> current() const;

    /// @brief 从当前快照定位文档并 pread 原始记录。
    /// @param doc_id 稳定文档编号。
    /// @param storage 受控归档目录。
    /// @return 精确原始记录字节。
    /// @throws std::out_of_range 当文档不在当前 READY 快照时抛出。
    /// @throws std::runtime_error 当归档文件或范围读取失败时抛出。
    std::string readOriginal(std::uint64_t doc_id, const StoragePaths& storage) const;

   private:
    mutable std::mutex mutex_;
    std::shared_ptr<const IndexSnapshot> snapshot_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_INDEX_SNAPSHOT_H_
