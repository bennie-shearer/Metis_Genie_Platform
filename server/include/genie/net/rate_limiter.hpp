/**
 * @file rate_limiter.hpp
 * @brief Token Bucket rate limiting for API endpoints
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements token bucket algorithm for API rate limiting.
 * Supports per-client, per-endpoint, and global rate limits.
 * Thread-safe. Zero external dependencies. Cross-platform.
 */
#pragma once
#ifndef GENIE_RATE_LIMITER_HPP
#define GENIE_RATE_LIMITER_HPP

#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <algorithm>

namespace genie {

// ============================================================================
// Rate Limit Configuration
// ============================================================================

struct RateLimitConfig {
    double  tokens_per_second   = 10.0;     // Refill rate
    double  max_tokens          = 100.0;    // Bucket capacity
    double  initial_tokens      = 100.0;    // Starting tokens
    bool    enabled             = true;
};

// ============================================================================
// Rate Limit Result
// ============================================================================

struct RateLimitResult {
    bool    allowed{true};
    double  tokens_remaining{0};
    double  retry_after_seconds{0};     // Seconds until next token available
    int     limit{0};                   // Max tokens (for headers)
    int     remaining{0};               // Remaining tokens (for headers)
    int     reset_seconds{0};           // Seconds until full refill
};

// ============================================================================
// Token Bucket
// ============================================================================

class TokenBucket {
public:
    explicit TokenBucket(const RateLimitConfig& cfg = {})
        : config_(cfg)
        , tokens_(cfg.initial_tokens)
        , last_refill_(std::chrono::steady_clock::now()) {}

    RateLimitResult try_consume(double tokens = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();

        RateLimitResult result;
        result.limit = static_cast<int>(config_.max_tokens);

        if (!config_.enabled) {
            result.allowed = true;
            result.tokens_remaining = config_.max_tokens;
            result.remaining = result.limit;
            return result;
        }

        if (tokens_ >= tokens) {
            tokens_ -= tokens;
            result.allowed = true;
            result.tokens_remaining = tokens_;
            result.remaining = static_cast<int>(tokens_);
            result.reset_seconds = static_cast<int>(
                (config_.max_tokens - tokens_) / config_.tokens_per_second);
        } else {
            result.allowed = false;
            result.tokens_remaining = tokens_;
            result.remaining = 0;
            result.retry_after_seconds =
                (tokens - tokens_) / config_.tokens_per_second;
            result.reset_seconds = static_cast<int>(
                config_.max_tokens / config_.tokens_per_second);
        }
        return result;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_ = config_.max_tokens;
        last_refill_ = std::chrono::steady_clock::now();
    }

    double available_tokens() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tokens_;
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(config_.max_tokens,
                          tokens_ + elapsed * config_.tokens_per_second);
        last_refill_ = now;
    }

    RateLimitConfig                             config_;
    double                                      tokens_;
    std::chrono::steady_clock::time_point       last_refill_;
    mutable std::mutex                          mutex_;
};

// ============================================================================
// Rate Limiter (manages per-key buckets)
// ============================================================================

class RateLimiter {
public:
    explicit RateLimiter(const RateLimitConfig& default_cfg = {})
        : default_config_(default_cfg) {}

    // Check rate limit for a given key (e.g., client IP, API key, endpoint)
    RateLimitResult check(const std::string& key, double tokens = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanup_expired();

        auto it = buckets_.find(key);
        if (it == buckets_.end()) {
            auto config = get_config_for_key(key);
            auto [inserted, success] = buckets_.try_emplace(
                key, config, std::chrono::steady_clock::now());
            it = inserted;
        }
        it->second.last_access = std::chrono::steady_clock::now();
        return it->second.bucket.try_consume(tokens);
    }

    // Set custom config for specific key pattern
    void set_config(const std::string& key_prefix, const RateLimitConfig& cfg) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_configs_[key_prefix] = cfg;
    }

    // Reset a specific key's bucket
    void reset_key(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = buckets_.find(key);
        if (it != buckets_.end()) {
            it->second.bucket.reset();
        }
    }

    // Reset all buckets
    void reset_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.clear();
    }

    // Get rate limit headers for HTTP response
    struct RateLimitHeaders {
        std::string x_ratelimit_limit;
        std::string x_ratelimit_remaining;
        std::string x_ratelimit_reset;
        std::string retry_after;  // Only set when rate limited
    };

    static RateLimitHeaders to_headers(const RateLimitResult& result) {
        RateLimitHeaders h;
        h.x_ratelimit_limit = std::to_string(result.limit);
        h.x_ratelimit_remaining = std::to_string(result.remaining);
        h.x_ratelimit_reset = std::to_string(result.reset_seconds);
        if (!result.allowed) {
            h.retry_after = std::to_string(
                static_cast<int>(result.retry_after_seconds + 1));
        }
        return h;
    }

    // Statistics
    struct Stats {
        size_t  active_buckets{0};
        size_t  custom_configs{0};
    };

    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {buckets_.size(), custom_configs_.size()};
    }

    // JSON status
    std::string to_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\"active_buckets\":" << buckets_.size()
            << ",\"custom_configs\":" << custom_configs_.size()
            << ",\"default_rate\":" << default_config_.tokens_per_second
            << ",\"default_capacity\":" << default_config_.max_tokens
            << "}";
        return oss.str();
    }

private:
    struct BucketEntry {
        TokenBucket                             bucket;
        std::chrono::steady_clock::time_point   last_access;

        BucketEntry(const RateLimitConfig& cfg,
                    std::chrono::steady_clock::time_point t)
            : bucket(cfg), last_access(t) {}
    };

    RateLimitConfig get_config_for_key(const std::string& key) const {
        // Check custom configs by prefix match
        for (const auto& [prefix, cfg] : custom_configs_) {
            if (key.find(prefix) == 0) {
                return cfg;
            }
        }
        return default_config_;
    }

    void cleanup_expired() {
        // Remove buckets idle for more than 10 minutes
        auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(10);
        for (auto it = buckets_.begin(); it != buckets_.end(); ) {
            if (it->second.last_access < cutoff) {
                it = buckets_.erase(it);
            } else {
                ++it;
            }
        }
    }

    RateLimitConfig                                     default_config_;
    mutable std::mutex                                  mutex_;
    std::unordered_map<std::string, BucketEntry>        buckets_;
    std::unordered_map<std::string, RateLimitConfig>    custom_configs_;
};

// ============================================================================
// Endpoint Rate Limiter (convenience for REST API)
// ============================================================================

class EndpointRateLimiter {
public:
    EndpointRateLimiter()
        : global_limiter_(std::make_unique<RateLimiter>()) {}

    // Configure rate limit for a specific endpoint
    void configure_endpoint(const std::string& method,
                          const std::string& path,
                          const RateLimitConfig& cfg) {
        std::string key = method + ":" + path;
        endpoint_limiters_[key] = std::make_unique<RateLimiter>(cfg);
    }

    // Check rate limit for client + endpoint combination
    RateLimitResult check(const std::string& client_id,
                         const std::string& method,
                         const std::string& path) {
        std::string endpoint_key = method + ":" + path;
        auto it = endpoint_limiters_.find(endpoint_key);
        if (it != endpoint_limiters_.end()) {
            return it->second->check(client_id);
        }
        // Use global limiter if no endpoint-specific config
        return global_limiter_->check(client_id);
    }

    void set_global_config(const RateLimitConfig& cfg) {
        global_limiter_ = std::make_unique<RateLimiter>(cfg);
    }

private:
    std::unique_ptr<RateLimiter>                                    global_limiter_;
    std::unordered_map<std::string, std::unique_ptr<RateLimiter>>   endpoint_limiters_;
};

} // namespace genie

#endif // GENIE_RATE_LIMITER_HPP
