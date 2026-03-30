/**
 * @file response_cache.hpp
 * @brief API Response Cache with per-endpoint TTL
 * @version 5.3.1
 *
 * Caches REST API responses to reduce computation for frequently
 * accessed, slow-changing data (portfolios, risk metrics, market summaries).
 *
 * @note Cross-platform: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (AppleClang)
 *
 * Usage:
 * @code
 *   ApiCache cache;
 *   cache.set_ttl("/api/v1/risk", 30);        // 30 seconds
 *   cache.set_ttl("/api/v1/market", 5);        // 5 seconds
 *   cache.set_ttl("/api/v1/portfolios", 60);   // 60 seconds
 *
 *   // In request handler:
 *   auto cached = cache.get(path);
 *   if (cached) { res.json(*cached); return; }
 *   // ... compute response ...
 *   cache.put(path, response_body);
 * @endcode
 *
 * Copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <optional>
#include <atomic>
#include <vector>
#include <algorithm>

namespace genie::net {

/**
 * @brief Thread-safe API response cache with configurable TTL per endpoint pattern
 */
class ApiCache {
public:
    explicit ApiCache(int default_ttl_sec = 30, size_t max_entries = 1000)
        : default_ttl_sec_(default_ttl_sec), max_entries_(max_entries) {}

    /**
     * @brief Set TTL for a specific endpoint prefix
     * @param path_prefix Endpoint prefix (e.g., "/api/v1/risk")
     * @param ttl_sec Time-to-live in seconds
     */
    void set_ttl(const std::string& path_prefix, int ttl_sec) {
        std::lock_guard<std::mutex> lock(mutex_);
        ttl_overrides_[path_prefix] = ttl_sec;
    }

    /**
     * @brief Get a cached response if available and not expired
     * @param key Cache key (typically the request path + query string)
     * @return Cached response body, or empty optional if miss
     */
    [[nodiscard]] std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            ++misses_;
            return std::nullopt;
        }

        auto age = std::chrono::steady_clock::now() - it->second.created_at;
        int ttl = ttl_for(key);
        if (std::chrono::duration_cast<std::chrono::seconds>(age).count() >= ttl) {
            entries_.erase(it);
            ++misses_;
            return std::nullopt;
        }

        ++hits_;
        return it->second.body;
    }

    /**
     * @brief Store a response in the cache
     * @param key Cache key
     * @param body Response body
     */
    void put(const std::string& key, const std::string& body) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Evict if at capacity
        if (entries_.size() >= max_entries_) {
            evict_oldest();
        }

        entries_[key] = {body, std::chrono::steady_clock::now()};
    }

    /**
     * @brief Invalidate a specific cache entry
     */
    void invalidate(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(key);
    }

    /**
     * @brief Invalidate all entries matching a prefix
     * @param prefix Path prefix (e.g., "/api/v1/portfolios" clears all portfolio caches)
     */
    void invalidate_prefix(const std::string& prefix) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (it->first.find(prefix) == 0) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Clear all cached entries
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    /** @brief Cache statistics */
    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
        size_t entries = 0;
        double hit_rate_pct = 0.0;
    };

    [[nodiscard]] Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = hits_ + misses_;
        double rate = total > 0 ? (100.0 * static_cast<double>(hits_) / static_cast<double>(total)) : 0.0;
        return {hits_, misses_, entries_.size(), rate};
    }

private:
    struct Entry {
        std::string body;
        std::chrono::steady_clock::time_point created_at;
    };

    int default_ttl_sec_;
    size_t max_entries_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, int> ttl_overrides_;

    size_t hits_ = 0;
    size_t misses_ = 0;

    /** @brief Get TTL for a given key by matching prefix overrides */
    int ttl_for(const std::string& key) const {
        // Find longest matching prefix
        int best_ttl = default_ttl_sec_;
        size_t best_len = 0;
        for (const auto& [prefix, ttl] : ttl_overrides_) {
            if (key.find(prefix) == 0 && prefix.size() > best_len) {
                best_ttl = ttl;
                best_len = prefix.size();
            }
        }
        return best_ttl;
    }

    /** @brief Remove oldest entry when cache is full */
    void evict_oldest() {
        if (entries_.empty()) return;
        auto oldest = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.created_at < oldest->second.created_at) {
                oldest = it;
            }
        }
        entries_.erase(oldest);
    }
};

}  // namespace genie::net
