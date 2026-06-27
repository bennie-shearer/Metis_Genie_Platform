/**
 * @file cache.hpp
 * @brief LRU Cache for request caching
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements thread-safe LRU cache with:
 * - Time-to-live (TTL) expiration
 * - Cache statistics (hit rate)
 * - Prefix-based invalidation
 */

#pragma once
#ifndef GENIE_CORE_CACHE_HPP
#define GENIE_CORE_CACHE_HPP

#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace genie {

/**
 * @brief Thread-safe LRU Cache with TTL support
 */
template<typename K, typename V>
class LRUCache {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::milliseconds;
    
    struct CacheEntry {
        K key;
        V value;
        TimePoint expiry;
    };
    
    using List = std::list<CacheEntry>;
    using Iterator = typename List::iterator;
    
    size_t capacity_;
    Duration default_ttl_;
    List items_;
    std::unordered_map<K, Iterator> map_;
    mutable std::mutex mutex_;
    
    // Statistics
    mutable size_t hits_{0};
    mutable size_t misses_{0};

public:
    /**
     * @brief Construct cache with capacity
     * @param capacity Maximum number of items
     * @param default_ttl Default TTL in milliseconds
     */
    explicit LRUCache(
        size_t capacity = 1000,
        Duration default_ttl = std::chrono::minutes(5)
    ) : capacity_(capacity), default_ttl_(default_ttl) {}

    /**
     * @brief Get value from cache
     * @param key Cache key
     * @return Value if found and not expired, nullopt otherwise
     */
    [[nodiscard]] std::optional<V> get(const K& key) {
        std::lock_guard lock(mutex_);
        
        auto it = map_.find(key);
        if (it == map_.end()) {
            ++misses_;
            return std::nullopt;
        }
        
        // Check expiry
        if (Clock::now() > it->second->expiry) {
            // Expired - remove and return not found
            items_.erase(it->second);
            map_.erase(it);
            ++misses_;
            return std::nullopt;
        }
        
        // Move to front (most recently used)
        items_.splice(items_.begin(), items_, it->second);
        ++hits_;
        
        return it->second->value;
    }

    /**
     * @brief Put value in cache
     * @param key Cache key
     * @param value Value to store
     * @param ttl Time-to-live (uses default if not specified)
     */
    void put(const K& key, V value, std::optional<Duration> ttl = std::nullopt) {
        std::lock_guard lock(mutex_);
        
        TimePoint expiry = Clock::now() + ttl.value_or(default_ttl_);
        
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Update existing entry
            it->second->value = std::move(value);
            it->second->expiry = expiry;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        
        // Evict if at capacity
        while (items_.size() >= capacity_) {
            auto& back = items_.back();
            map_.erase(back.key);
            items_.pop_back();
        }
        
        // Insert new entry at front
        items_.push_front({key, std::move(value), expiry});
        map_[key] = items_.begin();
    }

    /**
     * @brief Check if key exists and is not expired
     */
    [[nodiscard]] bool contains(const K& key) const {
        std::lock_guard lock(mutex_);
        
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        
        return Clock::now() <= it->second->expiry;
    }

    /**
     * @brief Invalidate (remove) a specific key
     */
    void invalidate(const K& key) {
        std::lock_guard lock(mutex_);
        
        auto it = map_.find(key);
        if (it != map_.end()) {
            items_.erase(it->second);
            map_.erase(it);
        }
    }

    /**
     * @brief Invalidate all keys starting with prefix (for string keys)
     */
    template<typename = std::enable_if_t<std::is_same_v<K, std::string>>>
    void invalidate_prefix(const std::string& prefix) {
        std::lock_guard lock(mutex_);
        
        auto it = map_.begin();
        while (it != map_.end()) {
            if (it->first.substr(0, prefix.size()) == prefix) {
                items_.erase(it->second);
                it = map_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Clear all entries
     */
    void clear() {
        std::lock_guard lock(mutex_);
        items_.clear();
        map_.clear();
    }

    /**
     * @brief Remove expired entries
     */
    void cleanup() {
        std::lock_guard lock(mutex_);
        
        auto now = Clock::now();
        auto it = items_.begin();
        while (it != items_.end()) {
            if (now > it->expiry) {
                map_.erase(it->key);
                it = items_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Get current size
     */
    [[nodiscard]] size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    /**
     * @brief Get capacity
     */
    [[nodiscard]] size_t capacity() const { return capacity_; }

    /**
     * @brief Get cache hit rate (0.0 - 1.0)
     */
    [[nodiscard]] double hit_rate() const {
        std::lock_guard lock(mutex_);
        size_t total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }

    /**
     * @brief Get cache statistics
     */
    struct Stats {
        size_t size;
        size_t capacity;
        size_t hits;
        size_t misses;
        double hit_rate;
    };

    [[nodiscard]] Stats stats() const {
        std::lock_guard lock(mutex_);
        size_t total = hits_ + misses_;
        return {
            items_.size(),
            capacity_,
            hits_,
            misses_,
            total > 0 ? static_cast<double>(hits_) / total : 0.0
        };
    }

    /**
     * @brief Reset statistics
     */
    void reset_stats() {
        std::lock_guard lock(mutex_);
        hits_ = 0;
        misses_ = 0;
    }
};

/**
 * @brief Convenience type for string-keyed cache
 */
using StringCache = LRUCache<std::string, std::string>;

} // namespace genie

#endif // GENIE_CORE_CACHE_HPP
