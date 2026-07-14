/**
 * @file app_config.h
 * @brief 定义 LogTrace 双进程配置及严格加载接口。
 */

#ifndef LOGTRACE_CONFIG_APP_CONFIG_H_
#define LOGTRACE_CONFIG_APP_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace smt {
namespace logtrace {

/// @brief Wfrest Gateway 和 SRPC 客户端配置。
struct GatewayConfig {
    std::string listen_address;
    std::uint16_t port;
    std::size_t request_body_limit_bytes;
    std::string rpc_host;
    std::uint16_t rpc_port;
    int rpc_timeout_ms;
    std::string operator_token_env;
    std::string operator_token;
};

/// @brief Search Server 的 SRPC 监听配置。
struct SearchRpcConfig {
    std::string listen_address;
    std::uint16_t port;
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

/// @brief 一期归档目录和二期索引目录配置。
struct StorageConfig {
    std::string archive_root;
    std::string index_root;
};

/// @brief 依赖检查超时配置。
struct HealthConfig {
    int check_timeout_ms;
};

/// @brief 增量扫描、文件解析和批次上限配置。
struct IndexingConfig {
    int poll_interval_ms;
    std::size_t source_batch_limit;
    std::size_t document_batch_limit;
    std::size_t max_line_bytes;
};

/// @brief 本地 SLRU 和 Redis 查询结果缓存配置。
struct CacheConfig {
    std::size_t probation_capacity;
    std::size_t protected_capacity;
    std::size_t max_detail_bytes;
    int active_window_seconds;
    int active_ttl_seconds;
    int active_empty_ttl_seconds;
    int historical_ttl_seconds;
    int historical_empty_ttl_seconds;
};

/// @brief 双进程日志输出配置。
struct LoggingConfig {
    std::string level;
    std::string gateway_file;
    std::string search_file;
    std::size_t max_file_size_bytes;
    std::size_t max_files;
};

/// @brief 应用完整配置，外部值在启动边界完成校验。
struct AppConfig {
    GatewayConfig gateway;
    SearchRpcConfig search_rpc;
    MySqlConfig source_mysql;
    MySqlConfig state_mysql;
    RedisConfig redis;
    StorageConfig storage;
    HealthConfig health;
    IndexingConfig indexing;
    CacheConfig cache;
    LoggingConfig logging;

    /// @brief 从 JSON 文件和环境变量加载完整配置。
    /// @param path JSON 配置文件路径。
    /// @return 已完成类型、范围和关联约束校验的配置。
    /// @throws ConfigError 当文件、JSON、字段或环境变量不符合契约时抛出。
    static AppConfig load(const std::string& path);
};

/// @brief 配置不符合启动契约时抛出的异常。
class ConfigError : public std::runtime_error {
   public:
    /// @brief 使用不含敏感值的原因构造配置异常。
    /// @param message 配置错误说明。
    explicit ConfigError(const std::string& message);
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_CONFIG_APP_CONFIG_H_
