/**
 * @file index_worker.h
 * @brief 声明 Search Server 后台解析与 Segment 发布线程。
 */

#ifndef LOGTRACE_INDEXING_INDEX_WORKER_H_
#define LOGTRACE_INDEXING_INDEX_WORKER_H_

#include <condition_variable>
#include <mutex>
#include <thread>

#include "logtrace/indexing/incremental_indexer.h"
#include "logtrace/indexing/segment_manager.h"

namespace smt {
namespace logtrace {

/// @brief 以固定轮询周期串行执行解析扫描和单个 Segment 构建。
class IndexWorker {
   public:
    /// @brief 保存解析索引器、Segment 管理器和轮询周期。
    /// @param indexer 增量索引器。
    /// @param segment_manager Segment 构建和快照管理器。
    /// @param poll_interval_ms 两次扫描之间的等待毫秒数。
    IndexWorker(IncrementalIndexer& indexer, SegmentManager& segment_manager, int poll_interval_ms);

    /// @brief 停止仍在运行的线程并释放资源。
    ~IndexWorker();

    /// @brief 启动后台线程，首次扫描在一个轮询周期后发生。
    /// @throws std::system_error 当线程无法创建时抛出。
    void start();

    /// @brief 通知线程退出并等待当前有界批次结束。
    void stop();

   private:
    /// @brief 执行可中断轮询循环。
    void run();

    IncrementalIndexer& indexer_;
    SegmentManager& segment_manager_;
    int poll_interval_ms_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    bool running_;
    bool stopping_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_INDEXING_INDEX_WORKER_H_
