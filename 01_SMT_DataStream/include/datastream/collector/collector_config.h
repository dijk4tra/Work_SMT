/**
 * @file collector_config.h
 * @brief 定义参考采集程序的严格配置结构。
 */

#ifndef DATASTREAM_COLLECTOR_COLLECTOR_CONFIG_H_
#define DATASTREAM_COLLECTOR_COLLECTOR_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace smt {
namespace datastream {

/// @brief 设备目录使用的文件封口方式。
enum class SealMode {
    AtomicRename,  ///< 临时名原子改为正式文件名。
    StableWindow,  ///< 连续多个扫描周期大小和 mtime 不变。
    DoneMarker     ///< 配对的 .done 标志出现。
};

/// @brief 一台数据源设备的目录和受控密钥配置。
struct CollectorDeviceConfig {
    std::string line_id;
    std::string station_id;
    std::string device_id;
    std::string collector_id;
    std::string input_dir;
    std::string secret_env;
    std::string secret;
    SealMode seal_mode;
};

/// @brief 有上限指数退避配置。
struct CollectorRetryConfig {
    int max_backoff_steps;
    int base_delay_ms;
    int max_delay_ms;
};

/// @brief 参考采集程序完整配置。
struct CollectorConfig {
    std::string server_url;
    std::string spool_root;
    int scan_interval_ms;
    int stable_scan_count;
    int request_timeout_ms;
    std::size_t chunk_size_bytes;
    std::uint64_t spool_max_bytes;
    std::uint64_t spool_min_free_bytes;
    CollectorRetryConfig retry;
    std::vector<CollectorDeviceConfig> devices;

    /// @brief 从 JSON 和环境变量加载并严格校验配置。
    /// @param path 配置文件路径。
    /// @return 已完成所有边界校验的采集配置。
    /// @throws CollectorConfigError 当字段、目录映射或环境变量无效时抛出。
    static CollectorConfig load(const std::string& path);
};

/// @brief 采集配置不符合启动契约时抛出的异常。
class CollectorConfigError : public std::runtime_error {
   public:
    /// @brief 使用非敏感原因构造配置异常。
    /// @param message 错误原因。
    explicit CollectorConfigError(const std::string& message);
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_COLLECTOR_COLLECTOR_CONFIG_H_
