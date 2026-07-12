/**
 * @file collector_app.h
 * @brief 声明目录扫描、持久队列和断点上传采集程序。
 */

#ifndef DATASTREAM_COLLECTOR_COLLECTOR_APP_H_
#define DATASTREAM_COLLECTOR_COLLECTOR_APP_H_

#include <atomic>
#include <cstdint>
#include <map>
#include <string>

#include "datastream/collector/collector_config.h"
#include "datastream/collector/http_uploader.h"
#include "datastream/collector/spool_store.h"

namespace smt {
namespace datastream {

/// @brief 组合设备目录扫描、spool 和 HTTP 上传状态机。
class CollectorApp {
   public:
    /// @brief 使用完整采集配置构造程序。
    /// @param config 已校验配置。
    explicit CollectorApp(const CollectorConfig& config);

    /// @brief 初始化 spool 和设备目录。
    /// @throws SpoolError 当本地持久状态不可用时抛出。
    void initialize();

    /// @brief 循环扫描和处理任务，直到停止标志置位。
    /// @param stop 外部信号处理设置的停止标志。
    void run(const std::atomic<bool>& stop);

    /// @brief 执行一次扫描和到期任务处理。
    void runOnce();

    /// @brief 返回当前任务总数，供测试和运行摘要使用。
    /// @return spool 中任务数量。
    std::size_t taskCount() const;

   private:
    /// @brief 扫描所有显式设备目录并接纳已封口文件。
    void scanInputs();

    /// @brief 处理所有达到允许尝试时间的非结束任务。
    void processTasks();

    /// @brief 处理一个任务的下一状态。
    /// @param task 待处理任务。
    void processTask(CollectorTask* task);

    /// @brief 保存可重试失败和下一次尝试时间。
    /// @param task 失败任务。
    /// @param error_code 稳定错误码。
    void scheduleRetry(CollectorTask* task, const std::string& error_code);

    /// @brief 将确定不可恢复的失败持久化。
    /// @param task 失败任务。
    /// @param error_code 稳定错误码。
    void failTask(CollectorTask* task, const std::string& error_code);

    CollectorConfig config_;
    SpoolStore spool_;
    HttpUploader uploader_;
    std::map<std::string, CollectorTask> tasks_;
    std::map<std::string, std::pair<std::uint64_t, std::int64_t>> observations_;
    std::map<std::string, int> stable_counts_;
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COLLECTOR_COLLECTOR_APP_H_
