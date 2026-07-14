/**
 * @file query_cache.h
 * @brief 声明 Redis 查询结果缓存的规范化 Key、值和 TTL 契约。
 */

#ifndef LOGTRACE_CACHE_QUERY_CACHE_H_
#define LOGTRACE_CACHE_QUERY_CACHE_H_

#include <cstdint>
#include <string>

#include "logtrace/config/app_config.h"
#include "logtrace/search/search_models.h"

namespace smt {
namespace logtrace {

class SearchEngine;

/// @brief 生成和解析仅包含日志 ID 页面的 Redis 缓存载荷。
class QueryCache {
   public:
    /// @brief 保存 Redis 命名空间和四类 TTL。
    /// @param redis Redis Key 前缀配置。
    /// @param cache 查询缓存时间配置。
    QueryCache(const RedisConfig& redis, const CacheConfig& cache);

    /// @brief 生成包含快照版本和规范化查询摘要的 Redis Key。
    /// @param query 已校验的查询条件。
    /// @param snapshot_version 当前最大 READY batch_id。
    /// @return 不暴露原始查询正文的 Redis Key。
    std::string key(const SearchQuery& query, std::uint64_t snapshot_version) const;

    /// @brief 序列化一页稳定文档编号、分数和总命中数。
    /// @param page 本地索引查询结果。
    /// @return 带显式格式版本的紧凑 JSON。
    std::string serialize(const SearchPage& page) const;

    /// @brief 严格解析缓存值并从当前快照恢复摘要。
    /// @param value Redis 返回的缓存正文。
    /// @param engine 当前检索引擎。
    /// @param page 成功时接收恢复结果。
    /// @return 格式、范围和快照均有效时为 true。
    bool restore(const std::string& value, const SearchEngine& engine, SearchPage* page) const;

    /// @brief 按近期/历史以及空/非空结果选择 TTL。
    /// @param query 已校验的查询条件。
    /// @param empty 查询结果是否为空。
    /// @param now_ms 当前 UTC 毫秒时间。
    /// @return Redis EX 秒数。
    int ttlSeconds(const SearchQuery& query, bool empty, std::int64_t now_ms) const;

   private:
    std::string key_prefix_;
    CacheConfig config_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_CACHE_QUERY_CACHE_H_
