/**
 * @file app_config.h
 * @brief 定义服务配置结构及严格加载接口。
 */

#ifndef DATASTREAM_CONFIG_APP_CONFIG_H_
#define DATASTREAM_CONFIG_APP_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace smt {
namespace datastream {

/// @brief HTTP 服务配置。
struct HttpConfig {
    std::string listen_address;
    std::uint16_t port;
    std::size_t request_body_limit_bytes;
    int graceful_shutdown_timeout_seconds;
};

/// @brief MySQL 连接配置及已解析密码。
struct MySqlConfig {
    std::string host;
    std::uint16_t port;
    std::string database;
    std::string user;
    std::string password_env;
    std::string password;
};

/// @brief Redis 连接配置及可选密码。
struct RedisConfig {
    std::string host;
    std::uint16_t port;
    int database;
    std::string username;
    std::string password_env;
    std::string password;
    std::string key_prefix;
};

/// @brief 运维接口认证配置。
struct OperatorAuthConfig {
    std::string bearer_token_env;
    std::string bearer_token;
};

/// @brief 健康检查配置。
struct HealthConfig {
    int check_timeout_ms;
};

/// @brief 设备请求认证配置。
struct AuthConfig {
    int timestamp_tolerance_seconds;
    int request_id_ttl_seconds;
};

/// @brief 设备状态配置。
struct DeviceConfig {
    int heartbeat_ttl_seconds;
};

/// @brief 文件上传配置。
struct UploadConfig {
    int session_ttl_seconds;
    std::size_t min_chunk_size_bytes;
    std::size_t max_chunk_size_bytes;
    std::uint64_t max_file_size_bytes;
    std::size_t hash_mmap_window_bytes;
    std::uint64_t min_free_space_bytes;
    int min_free_space_percent;
    int max_active_sessions;
    int max_device_sessions;
    int max_collector_sessions;
    std::uint64_t max_reserved_bytes;
    std::string temp_root;
    std::string archive_root;
};

/// @brief 过期数据清理配置。
struct CleanupConfig {
    int interval_seconds;
    int expired_retention_seconds;
};

/// @brief 历史查询边界配置。
struct QueryConfig {
    int default_page_size;
    int max_page_size;
    int max_time_range_days;
};

/// @brief 服务日志配置。
struct LoggingConfig {
    std::string level;
    std::string file;
    std::size_t max_file_size_bytes;
    std::size_t max_files;
};

/// @brief 应用完整配置，所有外部值在加载阶段完成校验。
struct AppConfig {
    HttpConfig http;
    MySqlConfig mysql;
    RedisConfig redis;
    OperatorAuthConfig operator_auth;
    HealthConfig health;
    AuthConfig auth;
    DeviceConfig device;
    UploadConfig upload;
    CleanupConfig cleanup;
    QueryConfig query;
    LoggingConfig logging;

    /// @brief 从 JSON 文件和环境变量加载完整配置。
    /// @param path JSON 配置文件路径。
    /// @return 已完成类型、范围和关联约束校验的配置。
    /// @throws ConfigError 当文件、JSON、字段或环境变量不符合契约时抛出。
    static AppConfig load(const std::string& path);
};

/// @brief 配置文件或环境变量不符合启动契约时抛出的异常。
class ConfigError : public std::runtime_error {
   public:
    /// @brief 使用明确原因构造配置异常。
    /// @param message 不包含敏感值的错误说明。
    explicit ConfigError(const std::string& message);
};

}  // namespace datastream
}  // namespace smt

#endif  // DATASTREAM_CONFIG_APP_CONFIG_H_
