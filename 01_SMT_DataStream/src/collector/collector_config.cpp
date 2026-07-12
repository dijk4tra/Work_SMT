/**
 * @file collector_config.cpp
 * @brief 实现参考采集程序配置的严格加载。
 */

#include "datastream/collector/collector_config.h"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

#include "datastream/common/validation.h"

namespace smt {
namespace datastream {
namespace {

using Json = nlohmann::json;

void exactKeys(const Json& value, const std::set<std::string>& expected,
               const std::string& context) {
    if (!value.is_object()) {
        throw CollectorConfigError(context + " must be an object");
    }
    std::set<std::string> actual;
    for (Json::const_iterator it = value.begin(); it != value.end(); ++it) actual.insert(it.key());
    if (actual != expected) {
        throw CollectorConfigError(context + " has missing or unknown fields");
    }
}

std::string textField(const Json& value, const char* name, const std::string& context) {
    if (!value.at(name).is_string() || value.at(name).get<std::string>().empty()) {
        throw CollectorConfigError(context + "." + name + " must be a non-empty string");
    }
    return value.at(name).get<std::string>();
}

std::uint64_t unsignedField(const Json& value, const char* name, std::uint64_t minimum,
                            std::uint64_t maximum, const std::string& context) {
    if (!value.at(name).is_number_unsigned()) {
        throw CollectorConfigError(context + "." + name + " must be unsigned");
    }
    const std::uint64_t result = value.at(name).get<std::uint64_t>();
    if (result < minimum || result > maximum) {
        throw CollectorConfigError(context + "." + name + " is out of range");
    }
    return result;
}

SealMode parseMode(const std::string& value) {
    if (value == "ATOMIC_RENAME") return SealMode::AtomicRename;
    if (value == "STABLE_WINDOW") return SealMode::StableWindow;
    if (value == "DONE_MARKER") return SealMode::DoneMarker;
    throw CollectorConfigError("device.seal_mode is unsupported");
}

}  // namespace

CollectorConfigError::CollectorConfigError(const std::string& message)
    : std::runtime_error(message) {}

CollectorConfig CollectorConfig::load(const std::string& path) {
    std::ifstream input(path.c_str());
    if (!input.is_open()) throw CollectorConfigError("cannot open collector config: " + path);
    Json root;
    try {
        input >> root;
    } catch (const Json::exception& error) {
        throw CollectorConfigError(std::string("invalid collector JSON: ") + error.what());
    }
    try {
        exactKeys(root,
                  {"server_url", "spool_root", "scan_interval_ms", "stable_scan_count",
                   "request_timeout_ms", "chunk_size_bytes", "spool_max_bytes",
                   "spool_min_free_bytes", "retry", "devices"},
                  "collector");
        CollectorConfig config;
        config.server_url = textField(root, "server_url", "collector");
        config.spool_root = textField(root, "spool_root", "collector");
        config.scan_interval_ms =
            static_cast<int>(unsignedField(root, "scan_interval_ms", 50, 60000, "collector"));
        config.stable_scan_count =
            static_cast<int>(unsignedField(root, "stable_scan_count", 2, 20, "collector"));
        config.request_timeout_ms =
            static_cast<int>(unsignedField(root, "request_timeout_ms", 100, 60000, "collector"));
        config.chunk_size_bytes = static_cast<std::size_t>(
            unsignedField(root, "chunk_size_bytes", 1024, 64ULL * 1024 * 1024, "collector"));
        config.spool_max_bytes =
            unsignedField(root, "spool_max_bytes", 1, 1024ULL * 1024 * 1024 * 1024, "collector");
        config.spool_min_free_bytes = unsignedField(root, "spool_min_free_bytes", 0,
                                                    1024ULL * 1024 * 1024 * 1024, "collector");
        if (config.server_url.compare(0, 7, "http://") != 0 ||
            config.server_url.find('/', 7) != std::string::npos ||
            config.spool_min_free_bytes >= config.spool_max_bytes) {
            throw CollectorConfigError("collector URL or spool limits are invalid");
        }
        const Json& retry = root.at("retry");
        exactKeys(retry, {"max_backoff_steps", "base_delay_ms", "max_delay_ms"}, "retry");
        config.retry.max_backoff_steps =
            static_cast<int>(unsignedField(retry, "max_backoff_steps", 1, 100, "retry"));
        config.retry.base_delay_ms =
            static_cast<int>(unsignedField(retry, "base_delay_ms", 10, 3600000, "retry"));
        config.retry.max_delay_ms =
            static_cast<int>(unsignedField(retry, "max_delay_ms", 10, 8ULL * 3600000, "retry"));
        if (config.retry.base_delay_ms > config.retry.max_delay_ms) {
            throw CollectorConfigError("retry delay limits are inconsistent");
        }
        if (!root.at("devices").is_array() || root.at("devices").empty()) {
            throw CollectorConfigError("devices must be a non-empty array");
        }
        std::set<std::string> device_ids;
        for (std::size_t index = 0; index < root.at("devices").size(); ++index) {
            const Json& item = root.at("devices")[index];
            exactKeys(item,
                      {"line_id", "station_id", "device_id", "collector_id", "input_dir",
                       "secret_env", "seal_mode"},
                      "device");
            CollectorDeviceConfig device;
            device.line_id = textField(item, "line_id", "device");
            device.station_id = textField(item, "station_id", "device");
            device.device_id = textField(item, "device_id", "device");
            device.collector_id = textField(item, "collector_id", "device");
            device.input_dir = textField(item, "input_dir", "device");
            device.secret_env = textField(item, "secret_env", "device");
            device.seal_mode = parseMode(textField(item, "seal_mode", "device"));
            const char* secret = std::getenv(device.secret_env.c_str());
            if (!isSmtIdentifier(device.line_id) || !isSmtIdentifier(device.station_id) ||
                !isSmtIdentifier(device.device_id) || !isSmtIdentifier(device.collector_id) ||
                secret == nullptr || secret[0] == '\0' ||
                !device_ids.insert(device.device_id).second) {
                throw CollectorConfigError("device mapping or secret environment is invalid");
            }
            device.secret = secret;
            config.devices.push_back(device);
        }
        return config;
    } catch (const CollectorConfigError&) {
        throw;
    } catch (const Json::exception& error) {
        throw CollectorConfigError(std::string("invalid collector field: ") + error.what());
    }
}

}  // namespace datastream
}  // namespace smt
