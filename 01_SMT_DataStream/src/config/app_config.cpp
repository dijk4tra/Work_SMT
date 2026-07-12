/**
 * @file app_config.cpp
 * @brief 实现 JSON 配置和环境变量的严格加载。
 */

#include "datastream/config/app_config.h"

#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace smt {
namespace datastream {
namespace {

using Json = nlohmann::json;

void requireExactKeys(const Json& object, std::initializer_list<const char*> keys,
                      const std::string& context) {
    if (!object.is_object()) {
        throw ConfigError(context + " must be an object");
    }

    std::set<std::string> expected;
    for (std::initializer_list<const char*>::const_iterator it = keys.begin(); it != keys.end();
         ++it) {
        expected.insert(*it);
    }

    std::set<std::string> actual;
    for (Json::const_iterator it = object.begin(); it != object.end(); ++it) {
        actual.insert(it.key());
    }

    if (actual != expected) {
        throw ConfigError(context + " has missing or unknown fields");
    }
}

std::string requireString(const Json& object, const char* key, const std::string& context) {
    const Json& value = object.at(key);
    if (!value.is_string()) {
        throw ConfigError(context + "." + key + " must be a string");
    }

    std::string result = value.get<std::string>();
    if (result.empty()) {
        throw ConfigError(context + "." + key + " must not be empty");
    }
    return result;
}

std::uint64_t requireUnsigned(const Json& object, const char* key, std::uint64_t minimum,
                              std::uint64_t maximum, const std::string& context) {
    const Json& value = object.at(key);
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw ConfigError(context + "." + key + " must be an integer");
    }

    std::uint64_t result = 0;
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
    } else {
        const std::int64_t signed_result = value.get<std::int64_t>();
        if (signed_result < 0) {
            throw ConfigError(context + "." + key + " is out of range");
        }
        result = static_cast<std::uint64_t>(signed_result);
    }

    if (result < minimum || result > maximum) {
        throw ConfigError(context + "." + key + " is out of range");
    }
    return result;
}

std::string requireEnvironment(const std::string& name, const std::string& field) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr || value[0] == '\0') {
        throw ConfigError(field + " references a missing or empty environment variable");
    }
    return value;
}

bool isSimpleHost(const std::string& value) {
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const char c = *it;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool isSimpleIdentifier(const std::string& value) {
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const char c = *it;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_')) {
            return false;
        }
    }
    return true;
}

bool isRedisUsername(const std::string& value) {
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const char c = *it;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

void validateLogLevel(const std::string& level) {
    static const std::set<std::string> levels = {"trace", "debug",    "info", "warn",
                                                 "error", "critical", "off"};
    if (levels.count(level) == 0) {
        throw ConfigError("logging.level is not supported");
    }
}

}  // namespace

ConfigError::ConfigError(const std::string& message) : std::runtime_error(message) {}

AppConfig AppConfig::load(const std::string& path) {
    std::ifstream input(path.c_str());
    if (!input.is_open()) {
        throw ConfigError("cannot open config file: " + path);
    }

    Json root;
    try {
        input >> root;
    } catch (const Json::exception& error) {
        throw ConfigError(std::string("invalid config JSON: ") + error.what());
    }

    try {
        requireExactKeys(root,
                         {"http", "mysql", "redis", "operator_auth", "health", "auth", "device",
                          "upload", "cleanup", "query", "logging"},
                         "config");

        AppConfig config;

        const Json& http = root.at("http");
        requireExactKeys(http,
                         {"listen_address", "port", "request_body_limit_bytes",
                          "graceful_shutdown_timeout_seconds"},
                         "http");
        config.http.listen_address = requireString(http, "listen_address", "http");
        if (!isSimpleHost(config.http.listen_address)) {
            throw ConfigError("http.listen_address has invalid characters");
        }
        config.http.port =
            static_cast<std::uint16_t>(requireUnsigned(http, "port", 1, 65535, "http"));
        config.http.request_body_limit_bytes = static_cast<std::size_t>(
            requireUnsigned(http, "request_body_limit_bytes", 1, 64ULL * 1024 * 1024, "http"));
        config.http.graceful_shutdown_timeout_seconds = static_cast<int>(
            requireUnsigned(http, "graceful_shutdown_timeout_seconds", 1, 300, "http"));

        const Json& mysql = root.at("mysql");
        requireExactKeys(mysql, {"host", "port", "database", "user", "password_env"}, "mysql");
        config.mysql.host = requireString(mysql, "host", "mysql");
        config.mysql.port =
            static_cast<std::uint16_t>(requireUnsigned(mysql, "port", 1, 65535, "mysql"));
        config.mysql.database = requireString(mysql, "database", "mysql");
        config.mysql.user = requireString(mysql, "user", "mysql");
        config.mysql.password_env = requireString(mysql, "password_env", "mysql");
        if (!isSimpleHost(config.mysql.host) || !isSimpleIdentifier(config.mysql.database) ||
            !isSimpleIdentifier(config.mysql.user)) {
            throw ConfigError("mysql host, database, or user has invalid characters");
        }
        config.mysql.password = requireEnvironment(config.mysql.password_env, "mysql.password_env");

        const Json& redis = root.at("redis");
        requireExactKeys(
            redis, {"host", "port", "database", "username", "password_env", "key_prefix"}, "redis");
        config.redis.host = requireString(redis, "host", "redis");
        config.redis.port =
            static_cast<std::uint16_t>(requireUnsigned(redis, "port", 1, 65535, "redis"));
        config.redis.database =
            static_cast<int>(requireUnsigned(redis, "database", 0, 15, "redis"));
        config.redis.username = requireString(redis, "username", "redis");
        config.redis.key_prefix = requireString(redis, "key_prefix", "redis");
        if (!isSimpleHost(config.redis.host) || !isRedisUsername(config.redis.username)) {
            throw ConfigError("redis host or username has invalid characters");
        }
        if (redis.at("password_env").is_null()) {
            config.redis.password_env.clear();
            config.redis.password.clear();
        } else {
            config.redis.password_env = requireString(redis, "password_env", "redis");
            config.redis.password =
                requireEnvironment(config.redis.password_env, "redis.password_env");
        }

        const Json& operator_auth = root.at("operator_auth");
        requireExactKeys(operator_auth, {"bearer_token_env"}, "operator_auth");
        config.operator_auth.bearer_token_env =
            requireString(operator_auth, "bearer_token_env", "operator_auth");
        config.operator_auth.bearer_token = requireEnvironment(
            config.operator_auth.bearer_token_env, "operator_auth.bearer_token_env");
        if (config.operator_auth.bearer_token.size() < 16) {
            throw ConfigError("operator token must contain at least 16 characters");
        }

        const Json& health = root.at("health");
        requireExactKeys(health, {"check_timeout_ms"}, "health");
        config.health.check_timeout_ms =
            static_cast<int>(requireUnsigned(health, "check_timeout_ms", 100, 30000, "health"));

        const Json& auth = root.at("auth");
        requireExactKeys(auth, {"timestamp_tolerance_seconds", "request_id_ttl_seconds"}, "auth");
        config.auth.timestamp_tolerance_seconds =
            static_cast<int>(requireUnsigned(auth, "timestamp_tolerance_seconds", 1, 3600, "auth"));
        config.auth.request_id_ttl_seconds =
            static_cast<int>(requireUnsigned(auth, "request_id_ttl_seconds", 1, 86400, "auth"));
        if (config.auth.request_id_ttl_seconds < config.auth.timestamp_tolerance_seconds) {
            throw ConfigError(
                "auth.request_id_ttl_seconds is shorter than the accepted time window");
        }

        const Json& device = root.at("device");
        requireExactKeys(device, {"heartbeat_ttl_seconds"}, "device");
        config.device.heartbeat_ttl_seconds =
            static_cast<int>(requireUnsigned(device, "heartbeat_ttl_seconds", 1, 3600, "device"));

        const Json& upload = root.at("upload");
        requireExactKeys(
            upload,
            {"session_ttl_seconds", "min_chunk_size_bytes", "max_chunk_size_bytes",
             "max_file_size_bytes", "hash_mmap_window_bytes", "min_free_space_bytes",
             "min_free_space_percent", "max_active_sessions", "max_device_sessions",
             "max_collector_sessions", "max_reserved_bytes", "temp_root", "archive_root"},
            "upload");
        config.upload.session_ttl_seconds = static_cast<int>(
            requireUnsigned(upload, "session_ttl_seconds", 60, 7ULL * 86400, "upload"));
        config.upload.min_chunk_size_bytes = static_cast<std::size_t>(
            requireUnsigned(upload, "min_chunk_size_bytes", 1, 64ULL * 1024 * 1024, "upload"));
        config.upload.max_chunk_size_bytes = static_cast<std::size_t>(
            requireUnsigned(upload, "max_chunk_size_bytes", 1, 64ULL * 1024 * 1024, "upload"));
        config.upload.max_file_size_bytes =
            requireUnsigned(upload, "max_file_size_bytes", 1, 16ULL * 1024 * 1024 * 1024, "upload");
        config.upload.hash_mmap_window_bytes = static_cast<std::size_t>(requireUnsigned(
            upload, "hash_mmap_window_bytes", 4096, 1024ULL * 1024 * 1024, "upload"));
        config.upload.min_free_space_bytes = requireUnsigned(
            upload, "min_free_space_bytes", 0, 1024ULL * 1024 * 1024 * 1024, "upload");
        config.upload.min_free_space_percent =
            static_cast<int>(requireUnsigned(upload, "min_free_space_percent", 0, 99, "upload"));
        config.upload.max_active_sessions =
            static_cast<int>(requireUnsigned(upload, "max_active_sessions", 1, 10000, "upload"));
        config.upload.max_device_sessions =
            static_cast<int>(requireUnsigned(upload, "max_device_sessions", 1, 1000, "upload"));
        config.upload.max_collector_sessions =
            static_cast<int>(requireUnsigned(upload, "max_collector_sessions", 1, 1000, "upload"));
        config.upload.max_reserved_bytes = requireUnsigned(
            upload, "max_reserved_bytes", 1, 16ULL * 1024 * 1024 * 1024 * 1024, "upload");
        config.upload.temp_root = requireString(upload, "temp_root", "upload");
        config.upload.archive_root = requireString(upload, "archive_root", "upload");
        if (config.upload.temp_root == config.upload.archive_root) {
            throw ConfigError("upload.temp_root and upload.archive_root must differ");
        }
        if (config.upload.min_chunk_size_bytes > config.upload.max_chunk_size_bytes ||
            config.upload.max_chunk_size_bytes > config.upload.max_file_size_bytes ||
            config.upload.max_chunk_size_bytes > config.http.request_body_limit_bytes) {
            throw ConfigError("upload chunk and file size limits are inconsistent");
        }
        if (config.upload.max_device_sessions > config.upload.max_active_sessions ||
            config.upload.max_collector_sessions > config.upload.max_active_sessions ||
            config.upload.max_reserved_bytes < config.upload.max_file_size_bytes) {
            throw ConfigError("upload session quota limits are inconsistent");
        }

        const Json& cleanup = root.at("cleanup");
        requireExactKeys(cleanup, {"interval_seconds", "expired_retention_seconds"}, "cleanup");
        config.cleanup.interval_seconds =
            static_cast<int>(requireUnsigned(cleanup, "interval_seconds", 1, 86400, "cleanup"));
        config.cleanup.expired_retention_seconds = static_cast<int>(
            requireUnsigned(cleanup, "expired_retention_seconds", 1, 30ULL * 86400, "cleanup"));

        const Json& query = root.at("query");
        requireExactKeys(query, {"default_page_size", "max_page_size", "max_time_range_days"},
                         "query");
        config.query.default_page_size =
            static_cast<int>(requireUnsigned(query, "default_page_size", 1, 1000, "query"));
        config.query.max_page_size =
            static_cast<int>(requireUnsigned(query, "max_page_size", 1, 1000, "query"));
        config.query.max_time_range_days =
            static_cast<int>(requireUnsigned(query, "max_time_range_days", 1, 366, "query"));
        if (config.query.default_page_size > config.query.max_page_size) {
            throw ConfigError("query.default_page_size exceeds query.max_page_size");
        }

        const Json& logging = root.at("logging");
        requireExactKeys(logging, {"level", "file", "max_file_size_bytes", "max_files"}, "logging");
        config.logging.level = requireString(logging, "level", "logging");
        config.logging.file = requireString(logging, "file", "logging");
        config.logging.max_file_size_bytes = static_cast<std::size_t>(requireUnsigned(
            logging, "max_file_size_bytes", 1024ULL * 1024, 1024ULL * 1024 * 1024, "logging"));
        config.logging.max_files =
            static_cast<std::size_t>(requireUnsigned(logging, "max_files", 1, 100, "logging"));
        validateLogLevel(config.logging.level);

        return config;
    } catch (const ConfigError&) {
        throw;
    } catch (const Json::exception& error) {
        throw ConfigError(std::string("invalid config field: ") + error.what());
    }
}

}  // namespace datastream
}  // namespace smt
