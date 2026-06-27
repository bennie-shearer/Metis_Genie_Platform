/**
 * @file price_cache.hpp
 * @brief Price cache with staleness detection and TTL
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides high-performance price caching:
 * - In-memory price storage with configurable TTL
 * - Staleness detection and alerts
 * - Multi-level cache (L1 hot, L2 warm)
 * - Automatic cleanup of expired entries
 * - Thread-safe operations
 */
#pragma once
#ifndef GENIE_MARKET_PRICE_CACHE_HPP
#define GENIE_MARKET_PRICE_CACHE_HPP

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>

namespace genie::market {

/**
 * @brief Cache entry status
 */
enum class CacheStatus {
    Fresh,      // Within TTL
    Stale,      // Past TTL but within grace period
    Expired,    // Past grace period
    Missing     // Not in cache
};

inline std::string cache_status_to_string(CacheStatus status) {
    switch (status) {
        case CacheStatus::Fresh: return "fresh";
        case CacheStatus::Stale: return "stale";
        case CacheStatus::Expired: return "expired";
        case CacheStatus::Missing: return "missing";
    }
    return "unknown";
}

/**
 * @brief Cached price entry
 */
struct CachedPrice {
    std::string symbol;
    double price{0};
    double bid{0};
    double ask{0};
    int64_t volume{0};
    double change{0};
    double change_percent{0};
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point cached_at;
    std::string source;
    int hit_count{0};
    
    bool is_valid() const {
        return !symbol.empty() && price > 0;
    }
    
    double spread() const {
        return (ask > 0 && bid > 0) ? ask - bid : 0;
    }
    
    double spread_percent() const {
        return (bid > 0) ? (spread() / bid) * 100.0 : 0;
    }
    
    double mid_price() const {
        return (ask > 0 && bid > 0) ? (bid + ask) / 2.0 : price;
    }
};

/**
 * @brief Cache configuration
 */
struct PriceCacheConfig {
    std::chrono::seconds ttl{60};               // Time-to-live for fresh data
    std::chrono::seconds stale_threshold{300};  // Time before considered stale
    std::chrono::seconds grace_period{600};     // Time before eviction
    size_t max_entries{10000};                  // Maximum cache size
    bool enable_cleanup_thread{true};           // Auto-cleanup expired entries
    std::chrono::seconds cleanup_interval{60};  // Cleanup frequency
    bool track_statistics{true};                // Track hit/miss stats
    
    PriceCacheConfig() = default;
    
    // Constructor from integer seconds
    PriceCacheConfig(int ttl_sec, int stale_sec, int grace_sec)
        : ttl(ttl_sec), stale_threshold(stale_sec), grace_period(grace_sec) {}
};

/**
 * @brief Cache statistics
 */
struct CacheStats {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> stale_hits{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> updates{0};
    std::chrono::system_clock::time_point started_at;
    
    double hit_rate() const {
        uint64_t total = hits + misses;
        return total > 0 ? (hits * 100.0 / total) : 0;
    }
    
    void reset() {
        hits = 0;
        misses = 0;
        stale_hits = 0;
        evictions = 0;
        updates = 0;
        started_at = std::chrono::system_clock::now();
    }
};

/**
 * @brief Staleness callback type
 */
using StalenessCallback = std::function<void(const std::string& symbol, CacheStatus status)>;

/**
 * @brief High-performance price cache
 */
class PriceCache {
public:
    explicit PriceCache(const PriceCacheConfig& config = {})
        : config_(config)
        , running_(false) {
        stats_.started_at = std::chrono::system_clock::now();
        
        if (config_.enable_cleanup_thread) {
            start_cleanup_thread();
        }
    }
    
    // Constructor with staleness and max age in seconds
    PriceCache(int staleness_seconds, int max_age_seconds)
        : config_()
        , running_(false) {
        config_.stale_threshold = std::chrono::seconds(staleness_seconds);
        config_.grace_period = std::chrono::seconds(max_age_seconds);
        stats_.started_at = std::chrono::system_clock::now();
        
        if (config_.enable_cleanup_thread) {
            start_cleanup_thread();
        }
    }
    
    // Constructor with ttl, stale threshold, and grace period
    PriceCache(int ttl, int stale_threshold, int grace_period)
        : config_(ttl, stale_threshold, grace_period)
        , running_(false) {
        stats_.started_at = std::chrono::system_clock::now();
        
        if (config_.enable_cleanup_thread) {
            start_cleanup_thread();
        }
    }
    
    ~PriceCache() {
        stop_cleanup_thread();
    }
    
    // === Basic Operations ===
    
    /**
     * @brief Set/update price in cache
     */
    void set(const std::string& symbol, double price, const std::string& source = "") {
        CachedPrice entry;
        entry.symbol = symbol;
        entry.price = price;
        entry.timestamp = std::chrono::system_clock::now();
        entry.cached_at = entry.timestamp;
        entry.source = source;
        
        set(entry);
    }
    
    /**
     * @brief Set full cached price entry
     */
    void set(const CachedPrice& entry) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // Check capacity
        if (cache_.size() >= config_.max_entries && cache_.find(entry.symbol) == cache_.end()) {
            evict_oldest();
        }
        
        auto now = std::chrono::system_clock::now();
        CachedPrice stored = entry;
        stored.cached_at = now;
        
        // Preserve hit count if updating existing entry
        auto it = cache_.find(entry.symbol);
        if (it != cache_.end()) {
            stored.hit_count = it->second.hit_count;
        }
        
        cache_[entry.symbol] = stored;
        
        if (config_.track_statistics) {
            stats_.updates++;
        }
    }
    
    /**
     * @brief Get price from cache
     */
    std::optional<CachedPrice> get(const std::string& symbol) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = cache_.find(symbol);
        if (it == cache_.end()) {
            if (config_.track_statistics) {
                stats_.misses++;
            }
            return std::nullopt;
        }
        
        CachedPrice& entry = it->second;
        entry.hit_count++;
        
        auto status = get_status_internal(entry);
        
        if (config_.track_statistics) {
            if (status == CacheStatus::Fresh) {
                stats_.hits++;
            } else if (status == CacheStatus::Stale) {
                stats_.stale_hits++;
            } else {
                stats_.misses++;
            }
        }
        
        // Fire staleness callback if stale
        if (status == CacheStatus::Stale && on_stale_) {
            lock.unlock();
            on_stale_(symbol, status);
        }
        
        return entry;
    }
    
    /**
     * @brief Get price value only
     */
    std::optional<double> get_price(const std::string& symbol) {
        auto entry = get(symbol);
        if (entry) return entry->price;
        return std::nullopt;
    }
    
    /**
     * @brief Get prices for multiple symbols
     */
    std::map<std::string, double> get_prices(const std::vector<std::string>& symbols) {
        std::map<std::string, double> result;
        for (const auto& sym : symbols) {
            auto price = get_price(sym);
            if (price) {
                result[sym] = *price;
            }
        }
        return result;
    }
    
    /**
     * @brief Check if symbol is in cache
     */
    bool contains(const std::string& symbol) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.find(symbol) != cache_.end();
    }
    
    /**
     * @brief Remove symbol from cache
     */
    void remove(const std::string& symbol) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.erase(symbol);
    }
    
    /**
     * @brief Clear entire cache
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
    }
    
    // === Status Checking ===
    
    /**
     * @brief Get cache status for symbol
     */
    CacheStatus get_status(const std::string& symbol) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = cache_.find(symbol);
        if (it == cache_.end()) {
            return CacheStatus::Missing;
        }
        
        return get_status_internal(it->second);
    }
    
    /**
     * @brief Check if price is fresh
     */
    bool is_fresh(const std::string& symbol) const {
        return get_status(symbol) == CacheStatus::Fresh;
    }
    
    /**
     * @brief Check if price is stale
     */
    bool is_stale(const std::string& symbol) const {
        auto status = get_status(symbol);
        return status == CacheStatus::Stale || status == CacheStatus::Expired;
    }
    
    /**
     * @brief Get age of cached price in seconds
     */
    std::optional<int64_t> get_age_seconds(const std::string& symbol) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        auto it = cache_.find(symbol);
        if (it == cache_.end()) {
            return std::nullopt;
        }
        
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.cached_at);
        return age.count();
    }
    
    /**
     * @brief Get all stale symbols
     */
    std::vector<std::string> get_stale_symbols() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        std::vector<std::string> result;
        for (const auto& [symbol, entry] : cache_) {
            auto status = get_status_internal(entry);
            if (status == CacheStatus::Stale || status == CacheStatus::Expired) {
                result.push_back(symbol);
            }
        }
        return result;
    }
    
    // === Bulk Operations ===
    
    /**
     * @brief Update multiple prices at once
     */
    void set_batch(const std::map<std::string, double>& prices, 
                   const std::string& source = "") {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        for (const auto& [symbol, price] : prices) {
            CachedPrice entry;
            entry.symbol = symbol;
            entry.price = price;
            entry.timestamp = now;
            entry.cached_at = now;
            entry.source = source;
            
            auto it = cache_.find(symbol);
            if (it != cache_.end()) {
                entry.hit_count = it->second.hit_count;
            }
            
            cache_[symbol] = entry;
        }
        
        if (config_.track_statistics) {
            stats_.updates += prices.size();
        }
    }
    
    /**
     * @brief Get all cached symbols
     */
    std::vector<std::string> get_symbols() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        std::vector<std::string> result;
        result.reserve(cache_.size());
        for (const auto& [symbol, _] : cache_) {
            result.push_back(symbol);
        }
        return result;
    }
    
    /**
     * @brief Get all cached entries
     */
    std::vector<CachedPrice> get_all() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        std::vector<CachedPrice> result;
        result.reserve(cache_.size());
        for (const auto& [_, entry] : cache_) {
            result.push_back(entry);
        }
        return result;
    }
    
    // === Callbacks ===
    
    /**
     * @brief Set callback for stale data detection
     */
    void on_stale(StalenessCallback callback) {
        on_stale_ = callback;
    }
    
    // === Statistics ===
    
    const CacheStats& stats() const { return stats_; }
    CacheStats& get_stats() { return stats_; }
    const CacheStats& get_stats() const { return stats_; }
    
    void reset_stats() { stats_.reset(); }
    
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.size();
    }
    
    // === Configuration ===
    
    const PriceCacheConfig& config() const { return config_; }
    
    void set_ttl(std::chrono::seconds ttl) {
        config_.ttl = ttl;
    }
    
    void set_stale_threshold(std::chrono::seconds threshold) {
        config_.stale_threshold = threshold;
    }

private:
    PriceCacheConfig config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, CachedPrice> cache_;
    CacheStats stats_;
    StalenessCallback on_stale_;
    
    std::atomic<bool> running_;
    std::thread cleanup_thread_;
    
    CacheStatus get_status_internal(const CachedPrice& entry) const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - entry.cached_at);
        
        if (age <= config_.ttl) {
            return CacheStatus::Fresh;
        } else if (age <= config_.stale_threshold) {
            return CacheStatus::Stale;
        } else if (age <= config_.grace_period) {
            return CacheStatus::Stale;
        }
        return CacheStatus::Expired;
    }
    
    void evict_oldest() {
        // Find oldest entry
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.cached_at < oldest->second.cached_at) {
                oldest = it;
            }
        }
        
        if (oldest != cache_.end()) {
            cache_.erase(oldest);
            stats_.evictions++;
        }
    }
    
    void cleanup_expired() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.cached_at);
            
            if (age > config_.grace_period) {
                it = cache_.erase(it);
                stats_.evictions++;
            } else {
                ++it;
            }
        }
    }
    
    void start_cleanup_thread() {
        running_ = true;
        cleanup_thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(config_.cleanup_interval);
                if (running_) {
                    cleanup_expired();
                }
            }
        });
    }
    
    void stop_cleanup_thread() {
        running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }
};

/**
 * @brief Multi-tier price cache (L1 hot, L2 warm)
 */
class TieredPriceCache {
public:
    TieredPriceCache() {
        // L1: Very short TTL, small size
        PriceCacheConfig l1_config;
        l1_config.ttl = std::chrono::seconds(5);
        l1_config.stale_threshold = std::chrono::seconds(10);
        l1_config.grace_period = std::chrono::seconds(30);
        l1_config.max_entries = 1000;
        l1_config.enable_cleanup_thread = false;
        l1_cache_ = std::make_unique<PriceCache>(l1_config);
        
        // L2: Longer TTL, larger size
        PriceCacheConfig l2_config;
        l2_config.ttl = std::chrono::seconds(60);
        l2_config.stale_threshold = std::chrono::seconds(300);
        l2_config.grace_period = std::chrono::seconds(3600);
        l2_config.max_entries = 50000;
        l2_cache_ = std::make_unique<PriceCache>(l2_config);
    }
    
    void set(const CachedPrice& entry) {
        l1_cache_->set(entry);
        l2_cache_->set(entry);
    }
    
    std::optional<CachedPrice> get(const std::string& symbol) {
        // Try L1 first
        auto result = l1_cache_->get(symbol);
        if (result && l1_cache_->is_fresh(symbol)) {
            return result;
        }
        
        // Fall back to L2
        result = l2_cache_->get(symbol);
        if (result) {
            // Promote to L1
            l1_cache_->set(*result);
            return result;
        }
        
        return std::nullopt;
    }
    
    PriceCache& l1() { return *l1_cache_; }
    PriceCache& l2() { return *l2_cache_; }

private:
    std::unique_ptr<PriceCache> l1_cache_;
    std::unique_ptr<PriceCache> l2_cache_;
};

} // namespace genie::market

#endif // GENIE_MARKET_PRICE_CACHE_HPP
