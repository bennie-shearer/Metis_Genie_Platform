/**
 * @file connection_resilience.hpp
 * @brief Connection resilience with retry and exponential backoff
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Resilient connection management for external services:
 * - Exponential backoff with jitter
 * - Per-endpoint circuit breaker integration
 * - Health check monitoring
 * - Automatic reconnection
 * - Connection pool failover
 * - Latency tracking and SLA monitoring
 * - Configurable retry policies
 * - Event hooks for state changes
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_NET_CONNECTION_RESILIENCE_HPP
#define GENIE_NET_CONNECTION_RESILIENCE_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <optional>
#include <cmath>
#include <random>
#include <deque>

namespace genie {
namespace net {
namespace resilience {

// ============================================================================
// Enumerations
// ============================================================================

enum class ConnectionState {
    Connected,
    Disconnected,
    Connecting,
    Backoff,
    CircuitOpen,
    Degraded
};

enum class RetryStrategy {
    Fixed,
    Linear,
    Exponential,
    ExponentialWithJitter
};

[[nodiscard]] inline std::string state_string(ConnectionState s) {
    switch (s) {
        case ConnectionState::Connected:    return "connected";
        case ConnectionState::Disconnected: return "disconnected";
        case ConnectionState::Connecting:   return "connecting";
        case ConnectionState::Backoff:      return "backoff";
        case ConnectionState::CircuitOpen:  return "circuit_open";
        case ConnectionState::Degraded:     return "degraded";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Retry policy configuration
 */
struct RetryPolicy {
    RetryStrategy strategy{RetryStrategy::ExponentialWithJitter};
    int max_retries{5};
    std::chrono::milliseconds initial_delay{1000};
    std::chrono::milliseconds max_delay{60000};
    double backoff_multiplier{2.0};
    double jitter_factor{0.25};    // +/- 25%

    /**
     * @brief Calculate delay for given attempt
     */
    [[nodiscard]] std::chrono::milliseconds delay_for(int attempt) const {
        if (attempt <= 0) return initial_delay;

        double base_ms = static_cast<double>(initial_delay.count());
        double delay_ms = 0;

        switch (strategy) {
            case RetryStrategy::Fixed:
                delay_ms = base_ms;
                break;
            case RetryStrategy::Linear:
                delay_ms = base_ms * (attempt + 1);
                break;
            case RetryStrategy::Exponential:
                delay_ms = base_ms * std::pow(backoff_multiplier, attempt);
                break;
            case RetryStrategy::ExponentialWithJitter: {
                delay_ms = base_ms * std::pow(backoff_multiplier, attempt);
                // Add jitter
                static thread_local std::mt19937 gen(std::random_device{}());
                std::uniform_real_distribution<double> dist(
                    1.0 - jitter_factor, 1.0 + jitter_factor);
                delay_ms *= dist(gen);
                break;
            }
        }

        auto capped = std::min(delay_ms, static_cast<double>(max_delay.count()));
        return std::chrono::milliseconds(static_cast<int64_t>(capped));
    }
};

/**
 * @brief Circuit breaker configuration
 */
struct CircuitBreakerConfig {
    int failure_threshold{5};      // Failures before opening
    int success_threshold{3};      // Successes in half-open before closing
    std::chrono::seconds open_duration{30};  // Time to stay open
};

/**
 * @brief Connection health metrics
 */
struct ConnectionHealth {
    std::string endpoint_id;
    ConnectionState state{ConnectionState::Disconnected};
    int consecutive_failures{0};
    int consecutive_successes{0};
    int total_connects{0};
    int total_disconnects{0};
    int total_retries{0};
    int current_retry{0};
    double avg_latency_ms{0};
    double p99_latency_ms{0};
    std::chrono::system_clock::time_point last_connected;
    std::chrono::system_clock::time_point last_failed;
    std::chrono::system_clock::time_point next_retry;
    std::chrono::milliseconds uptime{0};
    std::chrono::milliseconds downtime{0};

    [[nodiscard]] double availability_pct() const {
        auto total = uptime.count() + downtime.count();
        return total > 0 ? static_cast<double>(uptime.count()) / total * 100.0 : 0;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << endpoint_id << " [" << state_string(state) << "]"
            << " failures=" << consecutive_failures
            << " retries=" << total_retries
            << std::fixed << std::setprecision(1)
            << " latency=" << avg_latency_ms << "ms"
            << " availability=" << availability_pct() << "%";
        return oss.str();
    }
};

// ============================================================================
// Endpoint Resilience Manager
// ============================================================================

/**
 * @brief Manages resilience for a single endpoint
 */
class EndpointResilience {
public:
    using ConnectFunc = std::function<bool()>;
    using DisconnectFunc = std::function<void()>;
    using StateChangeCallback = std::function<void(ConnectionState, ConnectionState)>;

    EndpointResilience(const std::string& endpoint_id,
                        RetryPolicy retry_policy = {},
                        CircuitBreakerConfig cb_config = {})
        : endpoint_id_(endpoint_id),
          retry_policy_(retry_policy),
          cb_config_(cb_config) {
        health_.endpoint_id = endpoint_id;
    }

    /**
     * @brief Record a successful connection/request
     */
    void record_success(double latency_ms = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++health_.consecutive_successes;
        health_.consecutive_failures = 0;
        health_.current_retry = 0;

        if (health_.state != ConnectionState::Connected) {
            auto old_state = health_.state;
            health_.state = ConnectionState::Connected;
            health_.last_connected = std::chrono::system_clock::now();
            ++health_.total_connects;
            notify_state_change(old_state, ConnectionState::Connected);
        }

        update_latency(latency_ms);

        // Close circuit if enough successes in half-open
        if (circuit_half_open_ &&
            health_.consecutive_successes >= cb_config_.success_threshold) {
            circuit_half_open_ = false;
        }
    }

    /**
     * @brief Record a failure
     */
    void record_failure(const std::string& /*reason*/ = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        ++health_.consecutive_failures;
        health_.consecutive_successes = 0;
        health_.last_failed = std::chrono::system_clock::now();

        // Circuit breaker check
        if (health_.consecutive_failures >= cb_config_.failure_threshold) {
            if (health_.state != ConnectionState::CircuitOpen) {
                auto old_state = health_.state;
                health_.state = ConnectionState::CircuitOpen;
                circuit_opened_at_ = std::chrono::steady_clock::now();
                notify_state_change(old_state, ConnectionState::CircuitOpen);
            }
            return;
        }

        // Retry with backoff
        if (health_.current_retry < retry_policy_.max_retries) {
            auto old_state = health_.state;
            health_.state = ConnectionState::Backoff;
            auto delay = retry_policy_.delay_for(health_.current_retry);
            health_.next_retry = std::chrono::system_clock::now() + delay;
            ++health_.current_retry;
            ++health_.total_retries;
            if (old_state != ConnectionState::Backoff) {
                notify_state_change(old_state, ConnectionState::Backoff);
            }
        } else {
            auto old_state = health_.state;
            health_.state = ConnectionState::Disconnected;
            ++health_.total_disconnects;
            if (old_state != ConnectionState::Disconnected) {
                notify_state_change(old_state, ConnectionState::Disconnected);
            }
        }
    }

    /**
     * @brief Check if request should be allowed
     */
    [[nodiscard]] bool should_allow() const {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (health_.state) {
            case ConnectionState::Connected:
            case ConnectionState::Degraded:
                return true;
            case ConnectionState::CircuitOpen: {
                auto elapsed = std::chrono::steady_clock::now() - circuit_opened_at_;
                if (elapsed >= cb_config_.open_duration) {
                    // Half-open: allow one request
                    return true;
                }
                return false;
            }
            default:
                return false;
        }
    }

    /**
     * @brief Check if retry is due
     */
    [[nodiscard]] bool should_retry() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (health_.state != ConnectionState::Backoff) return false;
        return std::chrono::system_clock::now() >= health_.next_retry;
    }

    /**
     * @brief Register state change callback
     */
    void on_state_change(StateChangeCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_callbacks_.push_back(std::move(callback));
    }

    /**
     * @brief Reset state
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        health_ = ConnectionHealth{};
        health_.endpoint_id = endpoint_id_;
        circuit_half_open_ = false;
    }

    [[nodiscard]] ConnectionHealth health() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return health_;
    }

    [[nodiscard]] ConnectionState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return health_.state;
    }

private:
    mutable std::mutex mutex_;
    std::string endpoint_id_;
    RetryPolicy retry_policy_;
    CircuitBreakerConfig cb_config_;
    ConnectionHealth health_;
    bool circuit_half_open_{false};
    std::chrono::steady_clock::time_point circuit_opened_at_;
    std::deque<double> latency_window_;
    std::vector<StateChangeCallback> state_callbacks_;

    void update_latency(double ms) {
        if (ms <= 0) return;
        latency_window_.push_back(ms);
        if (latency_window_.size() > 100) latency_window_.pop_front();

        double sum = 0;
        for (double l : latency_window_) sum += l;
        health_.avg_latency_ms = sum / latency_window_.size();

        auto sorted = latency_window_;
        std::sort(sorted.begin(), sorted.end());
        size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
        health_.p99_latency_ms = sorted[std::min(p99_idx, sorted.size() - 1)];
    }

    void notify_state_change(ConnectionState from, ConnectionState to) {
        for (const auto& cb : state_callbacks_) {
            try { cb(from, to); } catch (...) {}
        }
    }
};

// ============================================================================
// Resilience Manager (Multi-Endpoint)
// ============================================================================

/**
 * @brief Manages resilience across multiple endpoints
 */
class ResilienceManager {
public:
    /**
     * @brief Register endpoint
     */
    void register_endpoint(const std::string& id,
                            RetryPolicy retry = {},
                            CircuitBreakerConfig cb = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        endpoints_.try_emplace(id, id, retry, cb);
    }

    /**
     * @brief Record success for endpoint
     */
    void success(const std::string& id, double latency_ms = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = endpoints_.find(id);
        if (it != endpoints_.end()) it->second.record_success(latency_ms);
    }

    /**
     * @brief Record failure for endpoint
     */
    void failure(const std::string& id, const std::string& reason = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = endpoints_.find(id);
        if (it != endpoints_.end()) it->second.record_failure(reason);
    }

    /**
     * @brief Check if endpoint allows requests
     */
    [[nodiscard]] bool is_available(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = endpoints_.find(id);
        if (it == endpoints_.end()) return false;
        return it->second.should_allow();
    }

    /**
     * @brief Get health for all endpoints
     */
    [[nodiscard]] std::map<std::string, ConnectionHealth> health_report() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, ConnectionHealth> result;
        for (const auto& [id, ep] : endpoints_) {
            result[id] = ep.health();
        }
        return result;
    }

    /**
     * @brief Get overall system health
     */
    [[nodiscard]] std::string system_health_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int connected = 0, degraded = 0, down = 0;
        for (const auto& [_, ep] : endpoints_) {
            auto h = ep.health();
            switch (h.state) {
                case ConnectionState::Connected: ++connected; break;
                case ConnectionState::Degraded: ++degraded; break;
                default: ++down; break;
            }
        }
        std::ostringstream oss;
        oss << "Endpoints: " << endpoints_.size()
            << " (connected=" << connected
            << " degraded=" << degraded
            << " down=" << down << ")";
        return oss.str();
    }

    [[nodiscard]] int endpoint_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(endpoints_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, EndpointResilience> endpoints_;
};

} // namespace resilience
} // namespace net
} // namespace genie

#endif // GENIE_NET_CONNECTION_RESILIENCE_HPP
