/**
 * @file logsearch_server.h
 * @brief 声明 Search Server 初始化和生命周期。
 */

#ifndef LOGTRACE_SERVER_LOGSEARCH_SERVER_H_
#define LOGTRACE_SERVER_LOGSEARCH_SERVER_H_

#include <srpc/rpc_define.h>

#include "logtrace/config/app_config.h"
#include "logtrace/indexing/incremental_indexer.h"
#include "logtrace/indexing/index_snapshot.h"
#include "logtrace/indexing/index_worker.h"
#include "logtrace/indexing/segment_manager.h"
#include "logtrace/rpc/search_health_service.h"
#include "logtrace/search/search_engine.h"
#include "logtrace/storage/mysql_client.h"
#include "logtrace/storage/redis_client.h"
#include "logtrace/storage/storage_paths.h"

namespace smt {
namespace logtrace {

/// @brief 承载索引基础设施和 SRPC 服务的进程对象。
class LogSearchServer {
   public:
    /// @brief 根据完整配置构造依赖对象并注册健康服务。
    /// @param config 完整应用配置。
    explicit LogSearchServer(const AppConfig& config);

    /// @brief 检查目录、两个 MySQL 和 Redis。
    /// @throws std::runtime_error 当必要依赖不可用时抛出。
    void initialize();

    /// @brief 启动 SRPC 监听。
    /// @return 监听成功时为 true。
    bool start();

    /// @brief 停止 SRPC 服务并等待连接结束。
    void stop();

   private:
    const AppConfig& config_;
    StoragePaths storage_;
    MySqlClient source_mysql_;
    MySqlClient state_mysql_;
    RedisClient redis_;
    IncrementalIndexer indexer_;
    IndexSnapshotStore snapshots_;
    SegmentManager segment_manager_;
    SearchEngine search_engine_;
    IndexWorker index_worker_;
    SearchHealthService search_service_;
    srpc::SRPCServer rpc_server_;
    bool started_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_SERVER_LOGSEARCH_SERVER_H_
