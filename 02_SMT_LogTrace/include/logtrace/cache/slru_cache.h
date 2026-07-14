/**
 * @file slru_cache.h
 * @brief 实现线程安全的 Probation/Protected 分段 LRU 缓存。
 */

#ifndef LOGTRACE_CACHE_SLRU_CACHE_H_
#define LOGTRACE_CACHE_SLRU_CACHE_H_

#include <cstddef>
#include <list>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace smt {
namespace logtrace {

/// @brief 使用 Probation 隔离一次性访问，并用 Protected 保存重复命中项。
template <typename Key, typename Value>
class SlruCache {
   public:
    /// @brief 使用两个分区的最大条目数构造缓存。
    /// @param probation_capacity Probation 最大条目数。
    /// @param protected_capacity Protected 最大条目数。
    /// @throws std::invalid_argument 当任一容量为零时抛出。
    SlruCache(std::size_t probation_capacity, std::size_t protected_capacity)
        : probation_capacity_(probation_capacity), protected_capacity_(protected_capacity) {
        if (probation_capacity == 0 || protected_capacity == 0) {
            throw std::invalid_argument("SLRU capacities must be positive");
        }
    }

    /// @brief 查找条目，并将 Probation 命中晋升到 Protected。
    /// @param key 缓存键。
    /// @param value 命中时接收值的副本。
    /// @return 命中时为 true。
    bool get(const Key& key, Value* value) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename Map::iterator found = entries_.find(key);
        if (found == entries_.end()) return false;
        if (found->second.is_protected) {
            protected_.splice(protected_.begin(), protected_, found->second.position);
            found->second.position = protected_.begin();
        } else {
            probation_.erase(found->second.position);
            protected_.push_front(key);
            found->second.position = protected_.begin();
            found->second.is_protected = true;
            demoteProtectedIfNeeded();
        }
        *value = found->second.value;
        return true;
    }

    /// @brief 插入或更新条目，新条目首先进入 Probation。
    /// @param key 缓存键。
    /// @param value 要保存的值。
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename Map::iterator found = entries_.find(key);
        if (found != entries_.end()) {
            found->second.value = value;
            if (found->second.is_protected) {
                protected_.splice(protected_.begin(), protected_, found->second.position);
                found->second.position = protected_.begin();
            } else {
                probation_.splice(probation_.begin(), probation_, found->second.position);
                found->second.position = probation_.begin();
            }
            return;
        }
        probation_.push_front(key);
        entries_.insert(std::make_pair(key, Entry{value, probation_.begin(), false}));
        evictProbationIfNeeded();
    }

    /// @brief 删除指定条目。
    /// @param key 缓存键。
    /// @return 删除到条目时为 true。
    bool erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename Map::iterator found = entries_.find(key);
        if (found == entries_.end()) return false;
        if (found->second.is_protected)
            protected_.erase(found->second.position);
        else
            probation_.erase(found->second.position);
        entries_.erase(found);
        return true;
    }

    /// @brief 清空两个分区。
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        probation_.clear();
        protected_.clear();
    }

    /// @brief 返回当前总条目数。
    /// @return 两个分区的条目数之和。
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    /// @brief 返回当前 Probation 条目数。
    /// @return Probation 条目数。
    std::size_t probationSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return probation_.size();
    }

    /// @brief 返回当前 Protected 条目数。
    /// @return Protected 条目数。
    std::size_t protectedSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return protected_.size();
    }

   private:
    struct Entry {
        Value value;
        typename std::list<Key>::iterator position;
        bool is_protected;
    };
    typedef std::unordered_map<Key, Entry> Map;

    void demoteProtectedIfNeeded() {
        if (protected_.size() <= protected_capacity_) return;
        const Key key = protected_.back();
        protected_.pop_back();
        probation_.push_front(key);
        typename Map::iterator found = entries_.find(key);
        found->second.position = probation_.begin();
        found->second.is_protected = false;
        evictProbationIfNeeded();
    }

    void evictProbationIfNeeded() {
        if (probation_.size() <= probation_capacity_) return;
        entries_.erase(probation_.back());
        probation_.pop_back();
    }

    const std::size_t probation_capacity_;
    const std::size_t protected_capacity_;
    mutable std::mutex mutex_;
    std::list<Key> probation_;
    std::list<Key> protected_;
    Map entries_;
};

}  // namespace logtrace
}  // namespace smt

#endif  // LOGTRACE_CACHE_SLRU_CACHE_H_
