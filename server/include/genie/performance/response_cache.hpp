/**
 * @file response_cache.hpp
 * @brief Response caching system with multiple backends
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Performance - Implement response caching
 */

#ifndef GENIE_PERFORMANCE_RESPONSE_CACHE_HPP
#define GENIE_PERFORMANCE_RESPONSE_CACHE_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <list>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>

namespace genie {
namespace performance {

/**
 * @brief Cache entry metadata
 */
struct CacheEntry {
    std::string key;
    std::string value;
    std::string content_type;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::chrono::system_clock::time_point last_accessed;
    int64_t access_count{0};
    size_t size_bytes{0};
    std::vector<std::string> tags;
    std::string etag;
    bool compressed{false};
    
    bool is_expired() const {
        return std::chrono::system_clock::now() > expires_at;
    }
    
    bool is_stale(std::chrono::seconds max_age) const {
        return std::chrono::system_clock::now() - created_at > max_age;
    }
};

/**
 * @brief Cache statistics
 */
struct CacheStats {
    std::atomic<int64_t> hits{0};
    std::atomic<int64_t> misses{0};
    std::atomic<int64_t> evictions{0};
    std::atomic<int64_t> expirations{0};
    std::atomic<int64_t> sets{0};
    std::atomic<int64_t> deletes{0};
    std::atomic<size_t> current_size{0};
    std::atomic<size_t> current_items{0};
    std::chrono::system_clock::time_point started_at{std::chrono::system_clock::now()};
    
    double hit_rate() const {
        int64_t total = hits.load() + misses.load();
        return total > 0 ? 100.0 * hits.load() / total : 0.0;
    }
    
    std::map<std::string, int64_t> to_map() const {
        return {
            {"hits", hits.load()},
            {"misses", misses.load()},
            {"evictions", evictions.load()},
            {"expirations", expirations.load()},
            {"sets", sets.load()},
            {"deletes", deletes.load()},
            {"current_items", static_cast<int64_t>(current_items.load())},
            {"current_size_bytes", static_cast<int64_t>(current_size.load())}
        };
    }
};

/**
 * @brief LRU eviction policy
 */
template<typename Key>
class LRUPolicy {
public:
    void access(const Key& key) {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            order_.erase(it->second);
        }
        order_.push_front(key);
        key_map_[key] = order_.begin();
    }
    
    void remove(const Key& key) {
        auto it = key_map_.find(key);
        if (it != key_map_.end()) {
            order_.erase(it->second);
            key_map_.erase(it);
        }
    }
    
    Key get_eviction_candidate() const {
        return order_.back();
    }
    
    bool empty() const {
        return order_.empty();
    }

private:
    std::list<Key> order_;
    std::unordered_map<Key, typename std::list<Key>::iterator> key_map_;
};

/**
 * @brief In-memory cache with LRU eviction
 */
class MemoryCache {
public:
    struct Config {
        size_t max_size_bytes{100 * 1024 * 1024};  // 100 MB
        size_t max_items{10000};
        std::chrono::seconds default_ttl{300};     // 5 minutes
        bool enable_compression{true};
        size_t compression_threshold{1024};        // Compress if > 1KB
    };
    
    explicit MemoryCache(const Config& config) : config_(config) {}
    
    /**
     * @brief Get cached value
     */
    std::optional<CacheEntry> get(const std::string& key) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            stats_.misses++;
            return std::nullopt;
        }
        
        auto& entry = it->second;
        
        // Check expiration
        if (entry.is_expired()) {
            stats_.misses++;
            stats_.expirations++;
            // Schedule for cleanup (don't block read)
            return std::nullopt;
        }
        
        entry.last_accessed = std::chrono::system_clock::now();
        entry.access_count++;
        stats_.hits++;
        
        // Update LRU order
        lru_.access(key);
        
        return entry;
    }
    
    /**
     * @brief Set cached value
     */
    bool set(const std::string& key, const std::string& value,
             std::chrono::seconds ttl = std::chrono::seconds{0},
             const std::string& content_type = "application/json",
             const std::vector<std::string>& tags = {}) {
        
        if (ttl == std::chrono::seconds{0}) {
            ttl = config_.default_ttl;
        }
        
        CacheEntry entry;
        entry.key = key;
        entry.value = value;
        entry.content_type = content_type;
        entry.created_at = std::chrono::system_clock::now();
        entry.expires_at = entry.created_at + ttl;
        entry.last_accessed = entry.created_at;
        entry.size_bytes = key.size() + value.size();
        entry.tags = tags;
        entry.etag = generate_etag(value);
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // Evict if necessary
        while (needs_eviction(entry.size_bytes)) {
            evict_one();
        }
        
        // Update size tracking
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            stats_.current_size -= it->second.size_bytes;
        } else {
            stats_.current_items++;
        }
        
        cache_[key] = entry;
        stats_.current_size += entry.size_bytes;
        stats_.sets++;
        lru_.access(key);
        
        // Update tag index
        for (const auto& tag : tags) {
            tag_index_[tag].insert(key);
        }
        
        return true;
    }
    
    /**
     * @brief Delete cached value
     */
    bool remove(const std::string& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return false;
        }
        
        // Remove from tag index
        for (const auto& tag : it->second.tags) {
            auto tag_it = tag_index_.find(tag);
            if (tag_it != tag_index_.end()) {
                tag_it->second.erase(key);
            }
        }
        
        stats_.current_size -= it->second.size_bytes;
        stats_.current_items--;
        stats_.deletes++;
        
        cache_.erase(it);
        lru_.remove(key);
        
        return true;
    }
    
    /**
     * @brief Invalidate by tag
     */
    int invalidate_by_tag(const std::string& tag) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto tag_it = tag_index_.find(tag);
        if (tag_it == tag_index_.end()) {
            return 0;
        }
        
        int count = 0;
        std::vector<std::string> keys_to_remove(tag_it->second.begin(), tag_it->second.end());
        
        for (const auto& key : keys_to_remove) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                stats_.current_size -= it->second.size_bytes;
                stats_.current_items--;
                cache_.erase(it);
                lru_.remove(key);
                count++;
            }
        }
        
        tag_index_.erase(tag_it);
        stats_.deletes += count;
        
        return count;
    }
    
    /**
     * @brief Invalidate by key prefix
     */
    int invalidate_by_prefix(const std::string& prefix) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        std::vector<std::string> keys_to_remove;
        for (const auto& [key, entry] : cache_) {
            if (key.find(prefix) == 0) {
                keys_to_remove.push_back(key);
            }
        }
        
        for (const auto& key : keys_to_remove) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                stats_.current_size -= it->second.size_bytes;
                stats_.current_items--;
                cache_.erase(it);
                lru_.remove(key);
            }
        }
        
        stats_.deletes += keys_to_remove.size();
        return static_cast<int>(keys_to_remove.size());
    }
    
    /**
     * @brief Check if key exists
     */
    bool exists(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = cache_.find(key);
        return it != cache_.end() && !it->second.is_expired();
    }
    
    /**
     * @brief Get or set with loader function
     */
    std::string get_or_set(const std::string& key,
                           std::function<std::string()> loader,
                           std::chrono::seconds ttl = std::chrono::seconds{0}) {
        auto cached = get(key);
        if (cached) {
            return cached->value;
        }
        
        std::string value = loader();
        set(key, value, ttl);
        return value;
    }
    
    /**
     * @brief Clear all cache
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        stats_.deletes += cache_.size();
        stats_.current_size = 0;
        stats_.current_items = 0;
        
        cache_.clear();
        tag_index_.clear();
        lru_ = LRUPolicy<std::string>();
    }
    
    /**
     * @brief Cleanup expired entries
     */
    int cleanup_expired() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        std::vector<std::string> expired;
        for (const auto& [key, entry] : cache_) {
            if (entry.is_expired()) {
                expired.push_back(key);
            }
        }
        
        for (const auto& key : expired) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                stats_.current_size -= it->second.size_bytes;
                stats_.current_items--;
                cache_.erase(it);
                lru_.remove(key);
            }
        }
        
        stats_.expirations += expired.size();
        return static_cast<int>(expired.size());
    }
    
    /**
     * @brief Get statistics
     */
    const CacheStats& get_stats() const {
        return stats_;
    }
    
    /**
     * @brief Get all keys
     */
    std::vector<std::string> get_keys() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<std::string> keys;
        keys.reserve(cache_.size());
        for (const auto& [key, entry] : cache_) {
            keys.push_back(key);
        }
        return keys;
    }

private:
    Config config_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::map<std::string, std::set<std::string>> tag_index_;
    LRUPolicy<std::string> lru_;
    CacheStats stats_;
    mutable std::shared_mutex mutex_;
    
    bool needs_eviction(size_t new_item_size) const {
        return stats_.current_size.load() + new_item_size > config_.max_size_bytes ||
               stats_.current_items.load() >= config_.max_items;
    }
    
    void evict_one() {
        if (lru_.empty()) return;
        
        std::string key = lru_.get_eviction_candidate();
        auto it = cache_.find(key);
        
        if (it != cache_.end()) {
            stats_.current_size -= it->second.size_bytes;
            stats_.current_items--;
            stats_.evictions++;
            cache_.erase(it);
        }
        
        lru_.remove(key);
    }
    
    static std::string generate_etag(const std::string& content) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : content) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x100000001b3ULL;
        }
        
        std::ostringstream ss;
        ss << std::hex << hash;
        return "\"" + ss.str() + "\"";
    }
};

/**
 * @brief HTTP response cache key generator
 */
class CacheKeyGenerator {
public:
    /**
     * @brief Generate cache key from request
     */
    static std::string generate(const std::string& method,
                                 const std::string& path,
                                 const std::map<std::string, std::string>& params = {},
                                 const std::string& user_id = "") {
        std::ostringstream key;
        
        key << method << ":" << path;
        
        if (!params.empty()) {
            key << "?";
            bool first = true;
            for (const auto& [k, v] : params) {
                if (!first) key << "&";
                key << k << "=" << v;
                first = false;
            }
        }
        
        if (!user_id.empty()) {
            key << ":user=" << user_id;
        }
        
        return key.str();
    }
    
    /**
     * @brief Generate cache key for API endpoint
     */
    static std::string api_key(const std::string& endpoint,
                                const std::string& user_id = "") {
        return generate("GET", "/api/v1" + endpoint, {}, user_id);
    }
    
    /**
     * @brief Generate cache key for market data
     */
    static std::string market_data_key(const std::string& symbol,
                                        const std::string& data_type = "quote") {
        return "market:" + data_type + ":" + symbol;
    }
    
    /**
     * @brief Generate cache key for portfolio
     */
    static std::string portfolio_key(const std::string& portfolio_id,
                                      const std::string& view = "summary") {
        return "portfolio:" + portfolio_id + ":" + view;
    }
};

/**
 * @brief Cache warming utilities
 */
class CacheWarmer {
public:
    using LoaderFunc = std::function<std::string(const std::string&)>;
    
    /**
     * @brief Warm cache with predefined keys
     */
    static void warm(MemoryCache& cache,
                     const std::vector<std::string>& keys,
                     LoaderFunc loader,
                     std::chrono::seconds ttl = std::chrono::seconds{300}) {
        for (const auto& key : keys) {
            try {
                std::string value = loader(key);
                cache.set(key, value, ttl);
            } catch (...) {
                // Skip failed loads
            }
        }
    }
    
    /**
     * @brief Get popular symbols for warming
     */
    static std::vector<std::string> get_popular_symbols() {
        return {
            "SPY", "QQQ", "IWM", "DIA", "VTI",
            "AAPL", "MSFT", "GOOGL", "AMZN", "META",
            "NVDA", "TSLA", "JPM", "V", "JNJ",
            "WMT", "PG", "XOM", "BAC", "HD"
        };
    }
};

/**
 * @brief Tiered cache (memory + distributed)
 */
class TieredCache {
public:
    struct Config {
        MemoryCache::Config l1_config;
        bool enable_l2{false};
        std::string l2_connection;
    };
    
    explicit TieredCache(const Config& config)
        : config_(config), l1_cache_(config.l1_config) {}
    
    /**
     * @brief Get from tiered cache
     */
    std::optional<CacheEntry> get(const std::string& key) {
        // Try L1 (memory)
        auto l1_result = l1_cache_.get(key);
        if (l1_result) {
            return l1_result;
        }
        
        // Try L2 if enabled
        if (config_.enable_l2) {
            auto l2_result = get_from_l2(key);
            if (l2_result) {
                // Promote to L1
                l1_cache_.set(key, l2_result->value,
                             std::chrono::duration_cast<std::chrono::seconds>(
                                 l2_result->expires_at - std::chrono::system_clock::now()),
                             l2_result->content_type, l2_result->tags);
                return l2_result;
            }
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Set in tiered cache
     */
    bool set(const std::string& key, const std::string& value,
             std::chrono::seconds ttl = std::chrono::seconds{0},
             const std::string& content_type = "application/json",
             const std::vector<std::string>& tags = {}) {
        
        // Set in L1
        bool l1_ok = l1_cache_.set(key, value, ttl, content_type, tags);
        
        // Set in L2 if enabled
        if (config_.enable_l2) {
            set_in_l2(key, value, ttl);
        }
        
        return l1_ok;
    }
    
    /**
     * @brief Remove from all tiers
     */
    bool remove(const std::string& key) {
        bool removed = l1_cache_.remove(key);
        
        if (config_.enable_l2) {
            remove_from_l2(key);
        }
        
        return removed;
    }
    
    /**
     * @brief Get L1 cache reference
     */
    MemoryCache& l1() { return l1_cache_; }
    
private:
    Config config_;
    MemoryCache l1_cache_;
    
    std::optional<CacheEntry> get_from_l2([[maybe_unused]] const std::string& key) {
        // L2 implementation (Redis, Memcached, etc.) would go here
        return std::nullopt;
    }
    
    void set_in_l2([[maybe_unused]] const std::string& key,
                   [[maybe_unused]] const std::string& value,
                   [[maybe_unused]] std::chrono::seconds ttl) {
        // L2 implementation
    }
    
    void remove_from_l2([[maybe_unused]] const std::string& key) {
        // L2 implementation
    }
};

} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_RESPONSE_CACHE_HPP
