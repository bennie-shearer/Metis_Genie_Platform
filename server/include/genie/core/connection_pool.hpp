/**
 * @file connection_pool.hpp
 * @brief HTTP client connection pooling for external API integrations
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides connection pooling for HttpClient instances:
 *   - Reuse existing connections to reduce latency
 *   - Configurable pool size per host
 *   - Idle connection timeout and eviction
 *   - Thread-safe acquisition and release
 *   - Health-checked connections
 *   - Statistics and monitoring
 *
 * Usage:
 *   ConnectionPool pool;
 *   pool.configure("api.alpaca.markets", {.max_connections = 5});
 *   auto conn = pool.acquire("api.alpaca.markets");
 *   if (conn) {
 *       auto resp = conn->get("/v2/account");
 *       pool.release(std::move(conn));
 *   }
 */
#pragma once
#ifndef GENIE_CORE_CONNECTION_POOL_HPP
#define GENIE_CORE_CONNECTION_POOL_HPP

#include "http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <thread>
#include <condition_variable>
#include <stdexcept>

namespace genie::core {

/**
 * @brief Configuration for a per-host connection pool
 */
struct PoolConfig {
    int max_connections{5};                              // Max connections per host
    int min_idle{1};                                     // Minimum idle connections to maintain
    std::chrono::seconds idle_timeout{300};               // Close idle connections after 5 min
    std::chrono::seconds max_lifetime{1800};              // Max connection lifetime (30 min)
    std::chrono::milliseconds acquire_timeout{5000};      // Max wait time to acquire a connection
    std::chrono::seconds health_check_interval{60};       // Health check frequency
    bool validate_on_acquire{true};                       // Validate connection before returning
    int max_retries_on_fail{2};                           // Retries if acquired connection fails
    HttpClient::Config client_config;                     // Default client configuration
};

/**
 * @brief Statistics for a connection pool
 */
struct PoolStats {
    std::string host;
    int total_connections{0};         // Total connections created
    int active_connections{0};        // Currently in use
    int idle_connections{0};          // Available in pool
    int64_t total_acquired{0};        // Total acquisitions
    int64_t total_released{0};        // Total releases
    int64_t total_created{0};         // Total connections created
    int64_t total_destroyed{0};       // Total connections destroyed
    int64_t total_timeouts{0};        // Acquisition timeouts
    int64_t total_health_failures{0}; // Failed health checks
    double avg_acquire_ms{0};         // Average acquire latency
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point last_acquired_at{};
};

/**
 * @brief Pooled connection wrapper with automatic return-to-pool
 *
 * Wraps an HttpClient and returns it to the pool on destruction.
 */
class PooledConnection {
public:
    PooledConnection() = default;

    PooledConnection(std::unique_ptr<HttpClient> client,
                     const std::string& host,
                     std::function<void(std::unique_ptr<HttpClient>, const std::string&)> return_fn)
        : client_(std::move(client))
        , host_(host)
        , return_fn_(std::move(return_fn))
        , created_at_(std::chrono::steady_clock::now())
        , last_used_(std::chrono::steady_clock::now()) {}

    ~PooledConnection() {
        if (client_ && return_fn_) {
            return_fn_(std::move(client_), host_);
        }
    }

    // Move only
    PooledConnection(PooledConnection&& other) noexcept
        : client_(std::move(other.client_))
        , host_(std::move(other.host_))
        , return_fn_(std::move(other.return_fn_))
        , created_at_(other.created_at_)
        , last_used_(other.last_used_) {
        other.return_fn_ = nullptr;
    }

    PooledConnection& operator=(PooledConnection&& other) noexcept {
        if (this != &other) {
            if (client_ && return_fn_) return_fn_(std::move(client_), host_);
            client_ = std::move(other.client_);
            host_ = std::move(other.host_);
            return_fn_ = std::move(other.return_fn_);
            created_at_ = other.created_at_;
            last_used_ = other.last_used_;
            other.return_fn_ = nullptr;
        }
        return *this;
    }

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    /** Access the underlying HttpClient */
    HttpClient* operator->() { touch(); return client_.get(); }
    HttpClient& operator*() { touch(); return *client_; }
    explicit operator bool() const { return client_ != nullptr; }

    /** Get connection age */
    std::chrono::milliseconds age() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - created_at_);
    }

    /** Get time since last use */
    std::chrono::milliseconds idle_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_used_);
    }

    /** Detach from pool (won't be returned) */
    std::unique_ptr<HttpClient> detach() {
        return_fn_ = nullptr;
        return std::move(client_);
    }

private:
    void touch() { last_used_ = std::chrono::steady_clock::now(); }

    std::unique_ptr<HttpClient> client_;
    std::string host_;
    std::function<void(std::unique_ptr<HttpClient>, const std::string&)> return_fn_;
    std::chrono::steady_clock::time_point created_at_;
    std::chrono::steady_clock::time_point last_used_;
};

/**
 * @brief Thread-safe HTTP client connection pool
 *
 * Maintains per-host pools of reusable HttpClient instances
 * with idle timeout, health checking, and lifetime management.
 */
class ConnectionPool {
public:
    ConnectionPool() : running_(true) {
        eviction_thread_ = std::thread([this] { eviction_loop(); });
    }

    explicit ConnectionPool(const PoolConfig& default_config)
        : default_config_(default_config), running_(true) {
        eviction_thread_ = std::thread([this] { eviction_loop(); });
    }

    ~ConnectionPool() {
        shutdown();
    }

    // Non-copyable, non-movable
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    /**
     * @brief Configure pool settings for a specific host
     */
    void configure(const std::string& host, const PoolConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        host_configs_[host] = config;
    }

    /**
     * @brief Acquire a connection from the pool
     *
     * Returns an idle connection or creates a new one if under the limit.
     * Blocks up to acquire_timeout if pool is exhausted.
     *
     * @param host Target host (e.g., "api.alpaca.markets")
     * @param base_url Full base URL (e.g., "https://api.alpaca.markets")
     * @return PooledConnection that auto-returns to pool on destruction
     */
    PooledConnection acquire(const std::string& host,
                              const std::string& base_url = "") {
        auto start = std::chrono::steady_clock::now();
        const auto& config = get_config(host);

        std::unique_lock<std::mutex> lock(mutex_);

        // Try to get an idle connection
        auto& pool = pools_[host];
        while (!pool.idle.empty()) {
            auto client = std::move(pool.idle.front());
            pool.idle.pop();
            pool.idle_count--;

            // Validate if configured
            if (config.validate_on_acquire) {
                // Check lifetime
                auto age = std::chrono::steady_clock::now() - pool.created_times[client.get()];
                if (age > config.max_lifetime) {
                    pool.created_times.erase(client.get());
                    pool.total_count--;
                    stats_[host].total_destroyed++;
                    continue;
                }
            }

            pool.active_count++;
            stats_[host].total_acquired++;
            update_acquire_latency(host, start);

            return make_pooled(std::move(client), host);
        }

        // Create new connection if under limit
        if (pool.total_count < config.max_connections) {
            auto client = create_client(host, base_url, config);
            pool.total_count++;
            pool.active_count++;
            pool.created_times[client.get()] = std::chrono::steady_clock::now();
            stats_[host].total_created++;
            stats_[host].total_acquired++;
            update_acquire_latency(host, start);

            return make_pooled(std::move(client), host);
        }

        // Pool exhausted - wait for a connection to be returned
        auto deadline = start + config.acquire_timeout;
        while (pool.idle.empty()) {
            if (pool_available_.wait_until(lock, deadline) == std::cv_status::timeout) {
                stats_[host].total_timeouts++;
                return PooledConnection(); // Empty - caller must check bool
            }
        }

        auto client = std::move(pool.idle.front());
        pool.idle.pop();
        pool.idle_count--;
        pool.active_count++;
        stats_[host].total_acquired++;
        update_acquire_latency(host, start);

        return make_pooled(std::move(client), host);
    }

    /**
     * @brief Get pool statistics for a host
     */
    PoolStats get_stats(const std::string& host) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stats_.find(host);
        PoolStats empty_stats; empty_stats.host = host; if (it == stats_.end()) return empty_stats;
        PoolStats s = it->second;
        auto pit = pools_.find(host);
        if (pit != pools_.end()) {
            s.active_connections = pit->second.active_count;
            s.idle_connections = pit->second.idle_count;
            s.total_connections = pit->second.total_count;
        }
        return s;
    }

    /**
     * @brief Get statistics for all pools
     */
    std::vector<PoolStats> get_all_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PoolStats> result;
        for (const auto& [host, _] : pools_) {
            result.push_back(get_stats(host));
        }
        return result;
    }

    /**
     * @brief Drain all connections for a host
     */
    void drain(const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pools_.find(host);
        if (it == pools_.end()) return;
        auto& pool = it->second;
        while (!pool.idle.empty()) {
            pool.idle.pop();
            pool.idle_count--;
            pool.total_count--;
            stats_[host].total_destroyed++;
        }
    }

    /**
     * @brief Drain all pools
     */
    void drain_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [host, pool] : pools_) {
            while (!pool.idle.empty()) {
                pool.idle.pop();
                pool.idle_count--;
                pool.total_count--;
                stats_[host].total_destroyed++;
            }
        }
    }

    /**
     * @brief Shutdown the pool and eviction thread
     */
    void shutdown() {
        running_ = false;
        if (eviction_thread_.joinable()) {
            eviction_thread_.join();
        }
        drain_all();
    }

    /**
     * @brief Get total connections across all pools
     */
    int total_connections() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& [_, pool] : pools_) {
            total += pool.total_count;
        }
        return total;
    }

    /**
     * @brief Get total active (in-use) connections
     */
    int total_active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& [_, pool] : pools_) {
            total += pool.active_count;
        }
        return total;
    }

    /**
     * @brief Get total idle connections
     */
    int total_idle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& [_, pool] : pools_) {
            total += pool.idle_count;
        }
        return total;
    }

private:
    struct HostPool {
        std::queue<std::unique_ptr<HttpClient>> idle;
        std::map<HttpClient*, std::chrono::steady_clock::time_point> created_times;
        int total_count{0};
        int active_count{0};
        int idle_count{0};
    };

    PoolConfig default_config_;
    mutable std::mutex mutex_;
    std::condition_variable_any pool_available_;
    std::map<std::string, HostPool> pools_;
    std::map<std::string, PoolConfig> host_configs_;
    std::map<std::string, PoolStats> stats_;
    std::atomic<bool> running_{false};
    std::thread eviction_thread_;

    const PoolConfig& get_config(const std::string& host) const {
        auto it = host_configs_.find(host);
        return (it != host_configs_.end()) ? it->second : default_config_;
    }

    std::unique_ptr<HttpClient> create_client(const std::string& host,
                                                const std::string& base_url,
                                                const PoolConfig& config) {
        auto client = std::make_unique<HttpClient>(config.client_config);
        if (!base_url.empty()) {
            client->set_base_url(base_url);
        } else {
            client->set_base_url("https://" + host);
        }
        return client;
    }

    PooledConnection make_pooled(std::unique_ptr<HttpClient> client,
                                  const std::string& host) {
        return PooledConnection(
            std::move(client), host,
            [this](std::unique_ptr<HttpClient> c, const std::string& h) {
                return_connection(std::move(c), h);
            });
    }

    void return_connection(std::unique_ptr<HttpClient> client,
                            const std::string& host) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& pool = pools_[host];
        pool.active_count--;
        stats_[host].total_released++;

        const auto& config = get_config(host);
        if (pool.idle_count < config.max_connections) {
            pool.idle.push(std::move(client));
            pool.idle_count++;
            pool_available_.notify_one();
        } else {
            pool.total_count--;
            stats_[host].total_destroyed++;
        }
    }

    void update_acquire_latency(const std::string& host,
                                 std::chrono::steady_clock::time_point start) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        auto& s = stats_[host];
        double count = static_cast<double>(s.total_acquired);
        s.avg_acquire_ms = (s.avg_acquire_ms * (count - 1) + ms) / count;
        s.last_acquired_at = std::chrono::system_clock::now();
    }

    /** Background eviction of expired idle connections */
    void eviction_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!running_) break;

            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [host, pool] : pools_) {
                const auto& config = get_config(host);
                auto now = std::chrono::steady_clock::now();

                // Evict idle connections past timeout (keep min_idle)
                int to_evict = pool.idle_count - config.min_idle;
                while (to_evict > 0 && !pool.idle.empty()) {
                    auto& front = pool.idle.front();
                    auto it = pool.created_times.find(front.get());
                    if (it != pool.created_times.end()) {
                        auto age = now - it->second;
                        if (age > config.idle_timeout) {
                            pool.created_times.erase(it);
                            pool.idle.pop();
                            pool.idle_count--;
                            pool.total_count--;
                            stats_[host].total_destroyed++;
                            to_evict--;
                            continue;
                        }
                    }
                    break; // Oldest connection not expired, stop
                }

                // Evict connections past max_lifetime
                int idle_size = pool.idle_count;
                for (int i = 0; i < idle_size && !pool.idle.empty(); i++) {
                    auto client = std::move(pool.idle.front());
                    pool.idle.pop();
                    auto it = pool.created_times.find(client.get());
                    if (it != pool.created_times.end() &&
                        (now - it->second) > config.max_lifetime) {
                        pool.created_times.erase(it);
                        pool.total_count--;
                        pool.idle_count--;
                        stats_[host].total_destroyed++;
                    } else {
                        pool.idle.push(std::move(client));
                    }
                }
            }
        }
    }
};

} // namespace genie::core

#endif // GENIE_CORE_CONNECTION_POOL_HPP
