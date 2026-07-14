/**
 * @file query_cache_test.cpp
 * @brief 验证 Redis 查询缓存的版本 Key、严格载荷和四类 TTL。
 */

#include "logtrace/cache/query_cache.h"

#include <gtest/gtest.h>

#include "logtrace/search/search_engine.h"

namespace smt {
namespace logtrace {
namespace {

SearchQuery query() {
    SearchQuery value;
    value.keywords.push_back("Camera TIMEOUT");
    value.device_id = "AOI-01";
    value.has_time_range = true;
    value.occurred_from_ms = 1000;
    value.occurred_to_ms = 2000;
    value.anomaly_only = false;
    value.offset = 0;
    value.page_size = 20;
    return value;
}

TEST(QueryCacheTest, IncludesSnapshotVersionAndNormalizesKeywords) {
    const RedisConfig redis{"127.0.0.1", 6379, 0, "default", "", "", "test:"};
    const CacheConfig config{2, 2, 65536, 7200, 30, 10, 600, 300};
    const QueryCache cache(redis, config);
    SearchQuery first = query();
    SearchQuery second = query();
    second.keywords.clear();
    second.keywords.push_back("timeout");
    second.keywords.push_back("camera");
    EXPECT_EQ(cache.key(first, 7), cache.key(second, 7));
    EXPECT_NE(cache.key(first, 7), cache.key(first, 8));
}

TEST(QueryCacheTest, SelectsRecentHistoricalAndEmptyTtls) {
    const RedisConfig redis{"127.0.0.1", 6379, 0, "default", "", "", "test:"};
    const CacheConfig config{2, 2, 65536, 7200, 30, 10, 600, 300};
    const QueryCache cache(redis, config);
    SearchQuery value = query();
    const std::int64_t now = 10000000;
    value.occurred_to_ms = now - 1000;
    EXPECT_EQ(cache.ttlSeconds(value, false, now), 30);
    EXPECT_EQ(cache.ttlSeconds(value, true, now), 10);
    value.occurred_to_ms = now - 7200LL * 1000 - 1;
    EXPECT_EQ(cache.ttlSeconds(value, false, now), 600);
    EXPECT_EQ(cache.ttlSeconds(value, true, now), 300);
}

TEST(QueryCacheTest, RestoresEmptyPageAndRejectsDamagedValue) {
    const RedisConfig redis{"127.0.0.1", 6379, 0, "default", "", "", "test:"};
    const CacheConfig config{2, 2, 65536, 7200, 30, 10, 600, 300};
    const QueryCache cache(redis, config);
    IndexSnapshotStore snapshots;
    const StoragePaths storage(StorageConfig{"/tmp", "/tmp/logtrace-query-cache-test"});
    const SearchEngine engine(snapshots, storage, config);
    const SearchPage original{0, 0, std::vector<SearchHit>()};
    SearchPage restored;
    EXPECT_TRUE(cache.restore(cache.serialize(original), engine, &restored));
    EXPECT_EQ(restored.total_hits, 0U);
    EXPECT_FALSE(cache.restore("{\"format\":1}", engine, &restored));
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
