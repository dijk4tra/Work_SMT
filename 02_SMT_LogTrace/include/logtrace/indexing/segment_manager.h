/**
 * @file segment_manager.h
 * @brief 声明 PARSED 到 READY 的构建协调、启动恢复和快照刷新接口。
 */

#ifndef LOGTRACE_INDEXING_SEGMENT_MANAGER_H_
#define LOGTRACE_INDEXING_SEGMENT_MANAGER_H_

#include "logtrace/indexing/index_snapshot.h"
#include "logtrace/indexing/index_state_repository.h"
#include "logtrace/indexing/parsed_batch_reader.h"
#include "logtrace/indexing/segment_models.h"
#include "logtrace/indexing/segment_store.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 协调解析工件、Segment 文件、MySQL 发布状态和内存快照。
class SegmentManager {
   public:
    /// @brief 保存状态库、存储目录、超时和快照存储。
    /// @param state_mysql 二期状态 MySQL。
    /// @param storage 已初始化的目录边界。
    /// @param timeout_ms MySQL 查询观察超时。
    /// @param snapshots 查询快照存储。
    SegmentManager(const MySqlClient& state_mysql, const StoragePaths& storage, int timeout_ms,
                   IndexSnapshotStore& snapshots);

    /// @brief 清理临时目录、恢复 BUILDING 批次并加载全部 READY Segment。
    /// @throws std::runtime_error 当状态库、READY Segment 或目录恢复失败时抛出。
    void recoverAndLoad();

    /// @brief 构建并发布至多一个最早的 PARSED 批次。
    /// @return 本次是否发布及 Segment 统计。
    /// @throws std::runtime_error 当构建、发布或快照刷新失败时抛出。
    SegmentBuildSummary buildNext();

    /// @brief 从数据库 READY 事实重新加载并原子替换查询快照。
    /// @throws std::runtime_error 当数据库或任一 READY Segment 无效时抛出。
    void refreshSnapshot();

   private:
    /// @brief 恢复 rename 前后中断的 BUILDING 批次。
    /// @throws std::runtime_error 当恢复状态无法持久化时抛出。
    void recoverBuildingBatches();

    IndexStateRepository state_repository_;
    ParsedBatchReader parsed_reader_;
    SegmentStore segment_store_;
    IndexSnapshotStore& snapshots_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_SEGMENT_MANAGER_H_
