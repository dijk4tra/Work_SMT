/**
 * @file search_engine.h
 * @brief 声明低 DF AND、BM25、业务权重和 Top-K 检索引擎。
 */

#ifndef LOGTRACE_SEARCH_SEARCH_ENGINE_H_
#define LOGTRACE_SEARCH_SEARCH_ENGINE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "logtrace/cache/slru_cache.h"
#include "logtrace/config/app_config.h"
#include "logtrace/indexing/index_snapshot.h"
#include "logtrace/search/search_models.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 在单个不可变 READY 快照上执行日志检索和详情回读。
class SearchEngine {
   public:
    /// @brief 保存快照存储和归档目录边界。
    /// @param snapshots 查询快照存储。
    /// @param storage 一期归档目录边界。
    SearchEngine(const IndexSnapshotStore& snapshots, const StoragePaths& storage,
                 const CacheConfig& cache_config);

    /// @brief 执行关键词 AND、结构化过滤、BM25 和 Top-K。
    /// @param query 已完成边界校验的检索条件。
    /// @return 同一快照版本上的总命中数和当前页。
    /// @throws std::invalid_argument 当分页超出内部契约时抛出。
    SearchPage search(const SearchQuery& query) const;

    /// @brief 按缓存中的稳定顺序从指定快照恢复一页摘要。
    /// @param snapshot_version 缓存 Key 中的快照版本。
    /// @param total_hits 缓存记录的总命中数。
    /// @param doc_ids 当前页稳定文档编号。
    /// @param scores 与文档编号一一对应的相关性分数。
    /// @return 当前快照仍匹配时恢复的检索页。
    /// @throws std::out_of_range 当快照版本或文档不再匹配时抛出。
    SearchPage restore(std::uint64_t snapshot_version, std::size_t total_hits,
                       const std::vector<std::uint64_t>& doc_ids,
                       const std::vector<double>& scores) const;

    /// @brief 返回当前 READY 快照版本。
    /// @return 最大 READY batch_id，无 Segment 时为零。
    std::uint64_t snapshotVersion() const;

    /// @brief 定位稳定 doc_id 并回读原始日志。
    /// @param doc_id 稳定文档编号。
    /// @return 文档、文件定位和精确原文。
    /// @throws std::out_of_range 当文档不在当前快照时抛出。
    /// @throws std::runtime_error 当归档文件读取失败时抛出。
    LogDetail detail(std::uint64_t doc_id) const;

   private:
    const IndexSnapshotStore& snapshots_;
    const StoragePaths& storage_;
    std::size_t max_detail_bytes_;
    mutable SlruCache<std::string, LogDetail> detail_cache_;
    mutable SlruCache<std::string, SegmentFileRecord> file_cache_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_SEARCH_SEARCH_ENGINE_H_
