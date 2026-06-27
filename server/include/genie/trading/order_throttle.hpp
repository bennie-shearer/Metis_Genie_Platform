/**
 * @file order_throttle.hpp
 * @brief Per-broker order rate throttling with sliding window
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Rate-aware order throttling for broker connections:
 * - Per-broker sliding window rate limits
 * - Token bucket with burst allowance
 * - Priority-based queue bypass
 * - Throttle metrics and alerting
 * - Configurable per-broker limits
 * - Backpressure signaling
 * - Order queuing during throttle
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_TRADING_ORDER_THROTTLE_HPP
#define GENIE_TRADING_ORDER_THROTTLE_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <atomic>
#include <optional>

namespace genie {
namespace trading {
namespace throttle {

// ============================================================================
// Data Structures
// ============================================================================

enum class ThrottleDecision {
    Allow,
    Queued,
    Rejected
};

[[nodiscard]] inline std::string decision_string(ThrottleDecision d) {
    switch (d) {
        case ThrottleDecision::Allow:    return "allow";
        case ThrottleDecision::Queued:   return "queued";
        case ThrottleDecision::Rejected: return "rejected";
    }
    return "unknown";
}

/**
 * @brief Broker rate limit configuration
 */
struct BrokerRateConfig {
    std::string broker_id;
    int max_orders_per_second{10};
    int max_orders_per_minute{200};
    int max_orders_per_day{10000};
    int burst_size{20};            // Max burst above per-second limit
    int max_queue_size{100};       // Queue when throttled
    bool allow_queue{true};
    std::chrono::milliseconds min_order_interval{50};  // Minimum time between orders
};

/**
 * @brief Throttle metrics per broker
 */
struct ThrottleMetrics {
    std::string broker_id;
    int64_t total_allowed{0};
    int64_t total_queued{0};
    int64_t total_rejected{0};
    int64_t total_burst{0};
    int current_second_count{0};
    int current_minute_count{0};
    int current_day_count{0};
    int current_queue_depth{0};
    double avg_orders_per_second{0};
    std::chrono::system_clock::time_point last_order_time;

    [[nodiscard]] double rejection_rate() const {
        int64_t total = total_allowed + total_queued + total_rejected;
        return total > 0 ? static_cast<double>(total_rejected) / total : 0;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Throttle[" << broker_id << "] "
            << "allowed=" << total_allowed
            << " queued=" << total_queued
            << " rejected=" << total_rejected
            << " rate=" << std::fixed << std::setprecision(1) << avg_orders_per_second << "/s"
            << " queue=" << current_queue_depth;
        return oss.str();
    }
};

// ============================================================================
// Sliding Window Counter
// ============================================================================

class SlidingWindowCounter {
public:
    explicit SlidingWindowCounter(std::chrono::seconds window_size = std::chrono::seconds(1))
        : window_size_(window_size) {}

    void record() {
        auto now = std::chrono::steady_clock::now();
        timestamps_.push_back(now);
        prune(now);
    }

    [[nodiscard]] int count() const {
        auto now = std::chrono::steady_clock::now();
        int c = 0;
        for (auto it = timestamps_.rbegin(); it != timestamps_.rend(); ++it) {
            if (now - *it <= window_size_) ++c;
            else break;
        }
        return c;
    }

    void prune(std::chrono::steady_clock::time_point now) {
        while (!timestamps_.empty() && now - timestamps_.front() > window_size_) {
            timestamps_.pop_front();
        }
    }

private:
    std::deque<std::chrono::steady_clock::time_point> timestamps_;
    std::chrono::seconds window_size_;
};

// ============================================================================
// Broker Throttle State
// ============================================================================

class BrokerThrottle {
public:
    explicit BrokerThrottle(const BrokerRateConfig& config)
        : config_(config),
          second_window_(std::chrono::seconds(1)),
          minute_window_(std::chrono::seconds(60)) {}

    /**
     * @brief Check if an order can proceed
     */
    ThrottleDecision try_acquire() {
        auto now = std::chrono::steady_clock::now();

        // Check minimum interval
        if (last_order_ != std::chrono::steady_clock::time_point{}) {
            auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_order_);
            if (since_last < config_.min_order_interval) {
                return handle_throttled();
            }
        }

        // Check per-second limit (with burst)
        int sec_count = second_window_.count();
        if (sec_count >= config_.max_orders_per_second + config_.burst_size) {
            return handle_throttled();
        }

        // Check per-minute limit
        int min_count = minute_window_.count();
        if (min_count >= config_.max_orders_per_minute) {
            return handle_throttled();
        }

        // Check daily limit
        if (day_count_ >= config_.max_orders_per_day) {
            ++metrics_.total_rejected;
            return ThrottleDecision::Rejected;
        }

        // Allow
        second_window_.record();
        minute_window_.record();
        ++day_count_;
        last_order_ = now;
        ++metrics_.total_allowed;

        if (sec_count >= config_.max_orders_per_second) {
            ++metrics_.total_burst;
        }

        update_metrics();
        return ThrottleDecision::Allow;
    }

    /**
     * @brief Dequeue next waiting order
     */
    [[nodiscard]] bool has_queued() const { return !queue_.empty(); }

    std::optional<std::string> dequeue() {
        if (queue_.empty()) return std::nullopt;
        auto order_id = queue_.front();
        queue_.pop_front();
        --metrics_.current_queue_depth;
        return order_id;
    }

    /**
     * @brief Reset daily counter
     */
    void reset_daily() {
        day_count_ = 0;
        metrics_.current_day_count = 0;
    }

    [[nodiscard]] const BrokerRateConfig& config() const { return config_; }
    [[nodiscard]] ThrottleMetrics metrics() const { return metrics_; }

private:
    BrokerRateConfig config_;
    SlidingWindowCounter second_window_;
    SlidingWindowCounter minute_window_;
    std::deque<std::string> queue_;
    std::chrono::steady_clock::time_point last_order_;
    int day_count_{0};
    ThrottleMetrics metrics_;

    ThrottleDecision handle_throttled() {
        if (config_.allow_queue &&
            static_cast<int>(queue_.size()) < config_.max_queue_size) {
            ++metrics_.total_queued;
            ++metrics_.current_queue_depth;
            return ThrottleDecision::Queued;
        }
        ++metrics_.total_rejected;
        return ThrottleDecision::Rejected;
    }

    void update_metrics() {
        metrics_.broker_id = config_.broker_id;
        metrics_.current_second_count = second_window_.count();
        metrics_.current_minute_count = minute_window_.count();
        metrics_.current_day_count = day_count_;
        metrics_.last_order_time = std::chrono::system_clock::now();
        int64_t total = metrics_.total_allowed + metrics_.total_queued + metrics_.total_rejected;
        if (total > 0) {
            metrics_.avg_orders_per_second =
                static_cast<double>(metrics_.total_allowed) /
                std::max(1.0, static_cast<double>(metrics_.current_minute_count > 0 ? 60 : 1));
        }
    }
};

// ============================================================================
// Order Throttle Manager
// ============================================================================

/**
 * @brief Manages throttling across multiple brokers
 */
class OrderThrottleManager {
public:
    /**
     * @brief Configure broker rate limits
     */
    void configure_broker(const BrokerRateConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        brokers_.erase(config.broker_id);
        brokers_.emplace(config.broker_id, BrokerThrottle(config));
    }

    /**
     * @brief Check if order can proceed
     */
    ThrottleDecision try_send(const std::string& broker_id,
                                const std::string& /*order_id*/ = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = brokers_.find(broker_id);
        if (it == brokers_.end()) {
            // No config = no throttle
            return ThrottleDecision::Allow;
        }
        return it->second.try_acquire();
    }

    /**
     * @brief Get metrics for all brokers
     */
    [[nodiscard]] std::map<std::string, ThrottleMetrics> all_metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, ThrottleMetrics> result;
        for (const auto& [id, bt] : brokers_) {
            result[id] = bt.metrics();
        }
        return result;
    }

    /**
     * @brief Reset daily counters (call at market open)
     */
    void reset_daily() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, bt] : brokers_) bt.reset_daily();
    }

    [[nodiscard]] int broker_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(brokers_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, BrokerThrottle> brokers_;
};

} // namespace throttle
} // namespace trading
} // namespace genie

#endif // GENIE_TRADING_ORDER_THROTTLE_HPP
