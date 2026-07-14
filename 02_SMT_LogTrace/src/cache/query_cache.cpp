/**
 * @file query_cache.cpp
 * @brief 实现 Redis 查询缓存的规范化摘要、严格载荷和分层 TTL。
 */

#include "logtrace/cache/query_cache.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <vector>

#include "logtrace/common/sha256.h"
#include "logtrace/indexing/term_tokenizer.h"
#include "logtrace/search/search_engine.h"

namespace smt {
namespace logtrace {
namespace {

using Json = nlohmann::json;

std::vector<std::string> normalizeKeywords(const std::vector<std::string>& keywords) {
    std::set<std::string> terms;
    for (std::vector<std::string>::const_iterator keyword = keywords.begin();
         keyword != keywords.end(); ++keyword) {
        const std::vector<std::string> tokens = tokenizeTerms(*keyword);
        terms.insert(tokens.begin(), tokens.end());
    }
    return std::vector<std::string>(terms.begin(), terms.end());
}

std::string digest(const std::string& value) {
    Sha256 sha;
    sha.update(value.data(), value.size());
    return sha.finishHex();
}

}  // namespace

QueryCache::QueryCache(const RedisConfig& redis, const CacheConfig& cache)
    : key_prefix_(redis.key_prefix), config_(cache) {}

std::string QueryCache::key(const SearchQuery& query, std::uint64_t snapshot_version) const {
    std::vector<std::string> levels = query.levels;
    std::sort(levels.begin(), levels.end());
    const Json normalized = {{"keywords", normalizeKeywords(query.keywords)},
                             {"line_id", query.line_id},
                             {"station_id", query.station_id},
                             {"device_id", query.device_id},
                             {"work_order", query.work_order},
                             {"product_sn", query.product_sn},
                             {"levels", levels},
                             {"module_name", query.module_name},
                             {"error_code", query.error_code},
                             {"has_time_range", query.has_time_range},
                             {"occurred_from_ms", query.occurred_from_ms},
                             {"occurred_to_ms", query.occurred_to_ms},
                             {"anomaly_only", query.anomaly_only},
                             {"offset", query.offset},
                             {"page_size", query.page_size}};
    return key_prefix_ + "query:v1:" + std::to_string(snapshot_version) + ':' +
           digest(normalized.dump());
}

std::string QueryCache::serialize(const SearchPage& page) const {
    Json ids = Json::array();
    Json scores = Json::array();
    for (std::vector<SearchHit>::const_iterator hit = page.items.begin(); hit != page.items.end();
         ++hit) {
        ids.push_back(hit->document.doc_id);
        scores.push_back(hit->score);
    }
    return Json{{"format", 1},
                {"snapshot_version", page.snapshot_version},
                {"total_hits", page.total_hits},
                {"doc_ids", ids},
                {"scores", scores}}
        .dump();
}

bool QueryCache::restore(const std::string& value, const SearchEngine& engine,
                         SearchPage* page) const {
    try {
        const Json parsed = Json::parse(value);
        if (!parsed.is_object() || parsed.size() != 5 || parsed.at("format") != 1 ||
            !parsed.at("snapshot_version").is_number_unsigned() ||
            !parsed.at("total_hits").is_number_unsigned() || !parsed.at("doc_ids").is_array() ||
            !parsed.at("scores").is_array() ||
            parsed.at("doc_ids").size() != parsed.at("scores").size() ||
            parsed.at("doc_ids").size() > 200) {
            return false;
        }
        const std::vector<std::uint64_t> ids =
            parsed.at("doc_ids").get<std::vector<std::uint64_t> >();
        const std::vector<double> scores = parsed.at("scores").get<std::vector<double> >();
        *page = engine.restore(parsed.at("snapshot_version").get<std::uint64_t>(),
                               parsed.at("total_hits").get<std::size_t>(), ids, scores);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int QueryCache::ttlSeconds(const SearchQuery& query, bool empty, std::int64_t now_ms) const {
    const bool historical =
        query.has_time_range &&
        query.occurred_to_ms <
            now_ms - static_cast<std::int64_t>(config_.active_window_seconds) * 1000;
    if (historical) {
        return empty ? config_.historical_empty_ttl_seconds : config_.historical_ttl_seconds;
    }
    return empty ? config_.active_empty_ttl_seconds : config_.active_ttl_seconds;
}

}  // namespace logtrace
}  // namespace smt
