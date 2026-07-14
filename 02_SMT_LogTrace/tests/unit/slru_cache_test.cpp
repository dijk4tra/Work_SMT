/**
 * @file slru_cache_test.cpp
 * @brief 验证 SLRU 晋升、降级、淘汰、更新和并发容量边界。
 */

#include "logtrace/cache/slru_cache.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace smt {
namespace logtrace {
namespace {

TEST(SlruCacheTest, PromotesDemotesAndEvictsBySegmentLru) {
    SlruCache<int, int> cache(2, 1);
    cache.put(1, 10);
    cache.put(2, 20);
    int value = 0;
    ASSERT_TRUE(cache.get(1, &value));
    EXPECT_EQ(value, 10);
    EXPECT_EQ(cache.protectedSize(), 1U);

    ASSERT_TRUE(cache.get(2, &value));
    EXPECT_EQ(cache.protectedSize(), 1U);
    EXPECT_EQ(cache.probationSize(), 1U);
    cache.put(3, 30);
    cache.put(4, 40);
    EXPECT_FALSE(cache.get(1, &value));
    EXPECT_TRUE(cache.get(2, &value));
}

TEST(SlruCacheTest, UpdatesExistingEntryWithoutGrowing) {
    SlruCache<int, int> cache(2, 2);
    cache.put(1, 10);
    cache.put(1, 11);
    int value = 0;
    ASSERT_TRUE(cache.get(1, &value));
    EXPECT_EQ(value, 11);
    EXPECT_EQ(cache.size(), 1U);
}

TEST(SlruCacheTest, KeepsCapacityUnderConcurrentAccess) {
    SlruCache<int, int> cache(16, 16);
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int worker = 0; worker < 8; ++worker) {
        workers.push_back(std::thread([worker, &cache]() {
            for (int index = 0; index < 2000; ++index) {
                const int key = (worker * 2000 + index) % 64;
                cache.put(key, index);
                int value = 0;
                cache.get(key, &value);
            }
        }));
    }
    for (std::vector<std::thread>::iterator worker = workers.begin(); worker != workers.end();
         ++worker)
        worker->join();
    EXPECT_LE(cache.size(), 32U);
}

}  // namespace
}  // namespace logtrace
}  // namespace smt
