/**
 * @file incremental_indexer.h
 * @brief 声明按 archive_id 增量解析归档的协调器。
 */

#ifndef LOGTRACE_INDEXING_INCREMENTAL_INDEXER_H_
#define LOGTRACE_INDEXING_INCREMENTAL_INDEXER_H_

#include <cstdint>

#include "logtrace/config/app_config.h"
#include "logtrace/indexing/archive_source_repository.h"
#include "logtrace/indexing/index_models.h"
#include "logtrace/indexing/index_state_repository.h"
#include "logtrace/indexing/parsed_batch_writer.h"
#include "logtrace/indexing/segment_store.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 增量索引器依赖集合，避免同类型数据库参数顺序混淆。
struct IndexerDependencies {
    const MySqlClient& source_mysql;
    const MySqlClient& state_mysql;
    const StoragePaths& storage;
    int mysql_timeout_ms;
};

/// @brief 协调源扫描、完整性回验、解析工件和状态迁移。
class IncrementalIndexer {
   public:
    /// @brief 根据两个数据库、目录和批次上限构造索引器。
    /// @param dependencies 两个 MySQL、目录和查询超时依赖。
    /// @param config 增量扫描和解析上限。
    IncrementalIndexer(const IndexerDependencies& dependencies, const IndexingConfig& config);

    /// @brief 恢复并清理上次中断的 PARSING 批次。
    /// @throws std::runtime_error 当状态数据库或工件清理失败时抛出。
    void recover();

    /// @brief 扫描并处理至多一个受配置限制的增量批次。
    /// @return 本次是否创建批次及文件、文档统计。
    /// @throws std::runtime_error 当数据库或解析工件持久化失败时抛出。
    ScanSummary scanOnce();

    /// @brief 将指定归档所属批次全部重新置为待解析。
    /// @param archive_id 一期归档编号。
    /// @return 已排队、不存在或状态库不可用。
    /// @throws std::runtime_error 当旧解析工件无法清理时抛出。
    RebuildStatus requestRebuild(std::uint64_t archive_id);

   private:
    /// @brief 在已持有跨进程操作锁时恢复中断批次。
    /// @throws std::runtime_error 当状态数据库或工件清理失败时抛出。
    void recoverUnlocked();

    const StoragePaths& storage_;
    IndexingConfig config_;
    ArchiveSourceRepository source_repository_;
    IndexStateRepository state_repository_;
    ParsedBatchWriter writer_;
    SegmentStore segment_store_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_INCREMENTAL_INDEXER_H_
