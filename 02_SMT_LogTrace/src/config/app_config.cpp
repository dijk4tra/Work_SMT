/**
 * @file app_config.cpp
 * @brief 实现 LogTrace JSON 配置和环境变量严格加载。
 */

#include "logtrace/config/app_config.h"

#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <set>

namespace smt {
namespace logtrace {
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
        const std::int64_t signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            throw ConfigError(context + "." + key + " is out of range");
        }
        result = static_cast<std::uint64_t>(signed_value);
    }
    if (result < minimum || result > maximum) {
        throw ConfigError(context + "." + key + " is out of range");
    }
    return result;
}

struct EnvironmentReference {
    std::string name;
    std::string field;
};

std::string requireEnvironment(const EnvironmentReference& reference) {
    const char* value = std::getenv(reference.name.c_str());
    if (value == nullptr || value[0] == '\0') {
        throw ConfigError(reference.field + " references a missing or empty environment variable");
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

MySqlConfig loadMySql(const Json& value, const std::string& context) {
    requireExactKeys(value, {"host", "port", "database", "user", "password_env"}, context);
    MySqlConfig config;
    config.host = requireString(value, "host", context);
    config.port = static_cast<std::uint16_t>(requireUnsigned(value, "port", 1, 65535, context));
    config.database = requireString(value, "database", context);
    config.user = requireString(value, "user", context);
    config.password_env = requireString(value, "password_env", context);
    if (!isSimpleHost(config.host) || !isSimpleIdentifier(config.database) ||
        !isSimpleIdentifier(config.user) || !isSimpleIdentifier(config.password_env)) {
        throw ConfigError(context + " host, database, user, or password_env is invalid");
    }
    config.password =
        requireEnvironment(EnvironmentReference{config.password_env, context + ".password_env"});
    return config;
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
                         {"gateway", "search_rpc", "source_mysql", "state_mysql", "redis",
                          "storage", "health", "indexing", "logging"},
                         "config");
        AppConfig config;

        const Json& gateway = root.at("gateway");
        requireExactKeys(gateway,
                         {"listen_address", "port", "request_body_limit_bytes", "rpc_host",
                          "rpc_port", "rpc_timeout_ms", "operator_token_env"},
                         "gateway");
        config.gateway.listen_address = requireString(gateway, "listen_address", "gateway");
        config.gateway.port =
            static_cast<std::uint16_t>(requireUnsigned(gateway, "port", 1, 65535, "gateway"));
        config.gateway.request_body_limit_bytes = static_cast<std::size_t>(requireUnsigned(
            gateway, "request_body_limit_bytes", 1, 16ULL * 1024 * 1024, "gateway"));
        config.gateway.rpc_host = requireString(gateway, "rpc_host", "gateway");
        config.gateway.rpc_port =
            static_cast<std::uint16_t>(requireUnsigned(gateway, "rpc_port", 1, 65535, "gateway"));
        config.gateway.rpc_timeout_ms =
            static_cast<int>(requireUnsigned(gateway, "rpc_timeout_ms", 50, 30000, "gateway"));
        config.gateway.operator_token_env = requireString(gateway, "operator_token_env", "gateway");
        if (!isSimpleIdentifier(config.gateway.operator_token_env)) {
            throw ConfigError("gateway.operator_token_env is invalid");
        }
        config.gateway.operator_token = requireEnvironment(
            EnvironmentReference{config.gateway.operator_token_env, "gateway.operator_token_env"});
        if (!isSimpleHost(config.gateway.listen_address) ||
            !isSimpleHost(config.gateway.rpc_host)) {
            throw ConfigError("gateway address has invalid characters");
        }

        const Json& search_rpc = root.at("search_rpc");
        requireExactKeys(search_rpc, {"listen_address", "port"}, "search_rpc");
        config.search_rpc.listen_address =
            requireString(search_rpc, "listen_address", "search_rpc");
        config.search_rpc.port =
            static_cast<std::uint16_t>(requireUnsigned(search_rpc, "port", 1, 65535, "search_rpc"));
        if (!isSimpleHost(config.search_rpc.listen_address)) {
            throw ConfigError("search_rpc.listen_address has invalid characters");
        }

        config.source_mysql = loadMySql(root.at("source_mysql"), "source_mysql");
        config.state_mysql = loadMySql(root.at("state_mysql"), "state_mysql");
        if (config.source_mysql.host == config.state_mysql.host &&
            config.source_mysql.port == config.state_mysql.port &&
            config.source_mysql.database == config.state_mysql.database) {
            throw ConfigError("source_mysql and state_mysql must use different databases");
        }

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
            if (!isSimpleIdentifier(config.redis.password_env)) {
                throw ConfigError("redis.password_env is invalid");
            }
            config.redis.password = requireEnvironment(
                EnvironmentReference{config.redis.password_env, "redis.password_env"});
        }

        const Json& storage = root.at("storage");
        requireExactKeys(storage, {"archive_root", "index_root"}, "storage");
        config.storage.archive_root = requireString(storage, "archive_root", "storage");
        config.storage.index_root = requireString(storage, "index_root", "storage");
        if (config.storage.archive_root == config.storage.index_root) {
            throw ConfigError("storage.archive_root and storage.index_root must differ");
        }

        const Json& health = root.at("health");
        requireExactKeys(health, {"check_timeout_ms"}, "health");
        config.health.check_timeout_ms =
            static_cast<int>(requireUnsigned(health, "check_timeout_ms", 50, 30000, "health"));

        const Json& indexing = root.at("indexing");
        requireExactKeys(
            indexing,
            {"poll_interval_ms", "source_batch_limit", "document_batch_limit", "max_line_bytes"},
            "indexing");
        config.indexing.poll_interval_ms =
            static_cast<int>(requireUnsigned(indexing, "poll_interval_ms", 100, 60000, "indexing"));
        config.indexing.source_batch_limit = static_cast<std::size_t>(
            requireUnsigned(indexing, "source_batch_limit", 1, 100, "indexing"));
        config.indexing.document_batch_limit = static_cast<std::size_t>(
            requireUnsigned(indexing, "document_batch_limit", 1, 100000, "indexing"));
        config.indexing.max_line_bytes = static_cast<std::size_t>(
            requireUnsigned(indexing, "max_line_bytes", 256, 1024ULL * 1024, "indexing"));

        const Json& logging = root.at("logging");
        requireExactKeys(
            logging, {"level", "gateway_file", "search_file", "max_file_size_bytes", "max_files"},
            "logging");
        config.logging.level = requireString(logging, "level", "logging");
        config.logging.gateway_file = requireString(logging, "gateway_file", "logging");
        config.logging.search_file = requireString(logging, "search_file", "logging");
        config.logging.max_file_size_bytes = static_cast<std::size_t>(requireUnsigned(
            logging, "max_file_size_bytes", 1024ULL * 1024, 1024ULL * 1024 * 1024, "logging"));
        config.logging.max_files =
            static_cast<std::size_t>(requireUnsigned(logging, "max_files", 1, 100, "logging"));
        validateLogLevel(config.logging.level);
        if (config.logging.gateway_file == config.logging.search_file) {
            throw ConfigError("gateway and search logs must use different files");
        }

        return config;
    } catch (const ConfigError&) {
        throw;
    } catch (const Json::exception& error) {
        throw ConfigError(std::string("invalid config field: ") + error.what());
    }
}

}  // namespace logtrace
}  // namespace smt
