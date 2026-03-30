/**
 * @file circuit_breaker.hpp
 * @brief Circuit Breaker pattern for resilient external service calls
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements the Circuit Breaker pattern to prevent cascading failures
 * when external services (brokers, market data feeds, etc.) become
 * unresponsive. Three states: CLOSED (normal), OPEN (blocking), and
 * HALF_OPEN (testing recovery).
 *
 * Zero external dependencies. Thread-safe. Cross-platform (Windows/Linux/macOS).
 */
#pragma once
#ifndef GENIE_CIRCUIT_BREAKER_HPP
#define GENIE_CIRCUIT_BREAKER_HPP

#include <string>
#include <functional>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <atomic>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <ctime>

namespace genie {

// ============================================================================
// Circuit Breaker State
// ============================================================================

enum class CircuitState {
    CLOSED,     // Normal operation - requests pass through
    OPEN,       // Failure threshold exceeded - requests blocked
    HALF_OPEN   // Recovery test - limited requests allowed
};

inline const char* circuit_state_name(CircuitState s) {
    switch (s) {
        case CircuitState::CLOSED:    return "CLOSED";
        case CircuitState::OPEN:      return "OPEN";
        case CircuitState::HALF_OPEN: return "HALF_OPEN";
        default:                      return "UNKNOWN";
    }
}

// ============================================================================
// Circuit Breaker Configuration
// ============================================================================

struct CircuitBreakerConfig {
    int         failure_threshold       = 5;        // Failures before opening
    int         success_threshold       = 3;        // Successes in half-open to close
    int         half_open_max_calls     = 1;        // Max concurrent calls in half-open
    double      timeout_seconds         = 30.0;     // Time in OPEN before trying HALF_OPEN
    double      call_timeout_seconds    = 10.0;     // Max time per call (0 = no limit)
    bool        log_state_changes       = true;     // Log transitions
};

// ============================================================================
// Circuit Breaker Exception
// ============================================================================

class CircuitOpenException : public std::runtime_error {
public:
    explicit CircuitOpenException(const std::string& service)
        : std::runtime_error("Circuit breaker OPEN for service: " + service)
        , service_name(service) {}

    std::string service_name;
};

// ============================================================================
// Circuit Breaker Event (for logging/monitoring)
// ============================================================================

struct CircuitBreakerEvent {
    std::string                                         service_name;
    CircuitState                                        from_state{CircuitState::CLOSED};
    CircuitState                                        to_state{CircuitState::CLOSED};
    std::chrono::system_clock::time_point               timestamp;
    std::string                                         reason;
};

// ============================================================================
// Circuit Breaker
// ============================================================================

class CircuitBreaker {
public:
    explicit CircuitBreaker(const std::string& name,
                           const CircuitBreakerConfig& cfg = {})
        : service_name_(name), config_(cfg) {}

    // Execute a callable through the circuit breaker
    // Throws CircuitOpenException if circuit is OPEN
    template<typename Func>
    auto execute(Func&& func) -> decltype(func()) {
        using ReturnType = decltype(func());
        pre_call_check();
        try {
            ReturnType result = func();
            on_success();
            return result;
        } catch (...) {
            on_failure();
            throw;
        }
    }

    // Specialization for void return
    template<typename Func>
    auto execute_void(Func&& func) -> void {
        pre_call_check();
        try {
            func();
            on_success();
        } catch (...) {
            on_failure();
            throw;
        }
    }

    // Manual state management
    void record_success() { on_success(); }
    void record_failure() { on_failure(); }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        transition_to(CircuitState::CLOSED, "Manual reset");
        failure_count_ = 0;
        success_count_ = 0;
        half_open_calls_ = 0;
    }

    // Getters
    CircuitState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_state_;
    }

    std::string name() const { return service_name_; }
    int failure_count() const { return failure_count_.load(); }
    int success_count() const { return success_count_.load(); }
    int total_calls() const { return total_calls_.load(); }
    int rejected_calls() const { return rejected_calls_.load(); }

    // State info
    struct StateInfo {
        CircuitState    state{CircuitState::CLOSED};
        int             failure_count{0};
        int             success_count{0};
        int             total_calls{0};
        int             rejected_calls{0};
        double          seconds_in_state{0};
    };

    StateInfo get_info() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - state_entered_at_).count();
        return {
            current_state_,
            failure_count_.load(),
            success_count_.load(),
            total_calls_.load(),
            rejected_calls_.load(),
            elapsed
        };
    }

    // Event history
    std::vector<CircuitBreakerEvent> event_history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    // JSON status
    std::string to_json() const {
        auto info = get_info();
        std::ostringstream oss;
        oss << "{\"service\":\"" << service_name_ << "\""
            << ",\"state\":\"" << circuit_state_name(info.state) << "\""
            << ",\"failure_count\":" << info.failure_count
            << ",\"success_count\":" << info.success_count
            << ",\"total_calls\":" << info.total_calls
            << ",\"rejected_calls\":" << info.rejected_calls
            << ",\"seconds_in_state\":" << info.seconds_in_state
            << "}";
        return oss.str();
    }

private:
    void pre_call_check() {
        std::lock_guard<std::mutex> lock(mutex_);
        total_calls_.fetch_add(1);

        switch (current_state_) {
            case CircuitState::CLOSED:
                // Normal - allow call
                break;

            case CircuitState::OPEN: {
                // Check if timeout has elapsed
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - state_entered_at_).count();
                if (elapsed >= config_.timeout_seconds) {
                    transition_to(CircuitState::HALF_OPEN, "Timeout elapsed, testing recovery");
                    half_open_calls_ = 1;
                } else {
                    rejected_calls_.fetch_add(1);
                    throw CircuitOpenException(service_name_);
                }
                break;
            }

            case CircuitState::HALF_OPEN:
                if (half_open_calls_ >= config_.half_open_max_calls) {
                    rejected_calls_.fetch_add(1);
                    throw CircuitOpenException(service_name_);
                }
                half_open_calls_++;
                break;
        }
    }

    void on_success() {
        std::lock_guard<std::mutex> lock(mutex_);
        failure_count_.store(0);
        success_count_.fetch_add(1);

        if (current_state_ == CircuitState::HALF_OPEN) {
            if (success_count_.load() >= config_.success_threshold) {
                transition_to(CircuitState::CLOSED, "Recovery confirmed");
                success_count_.store(0);
                half_open_calls_ = 0;
            }
        }
    }

    void on_failure() {
        std::lock_guard<std::mutex> lock(mutex_);
        failure_count_.fetch_add(1);
        success_count_.store(0);

        switch (current_state_) {
            case CircuitState::CLOSED:
                if (failure_count_.load() >= config_.failure_threshold) {
                    transition_to(CircuitState::OPEN, "Failure threshold reached");
                }
                break;

            case CircuitState::HALF_OPEN:
                transition_to(CircuitState::OPEN, "Failure during recovery test");
                half_open_calls_ = 0;
                break;

            case CircuitState::OPEN:
                // Already open
                break;
        }
    }

    void transition_to(CircuitState new_state, const std::string& reason) {
        if (current_state_ == new_state) return;

        CircuitBreakerEvent evt;
        evt.service_name = service_name_;
        evt.from_state = current_state_;
        evt.to_state = new_state;
        evt.timestamp = std::chrono::system_clock::now();
        evt.reason = reason;
        events_.push_back(evt);

        // Keep event history bounded
        if (events_.size() > 100) {
            events_.erase(events_.begin(), events_.begin() + 50);
        }

        current_state_ = new_state;
        state_entered_at_ = std::chrono::steady_clock::now();
    }

    std::string                                     service_name_;
    CircuitBreakerConfig                            config_;
    mutable std::mutex                              mutex_;
    CircuitState                                    current_state_{CircuitState::CLOSED};
    std::chrono::steady_clock::time_point           state_entered_at_{std::chrono::steady_clock::now()};
    std::atomic<int>                                failure_count_{0};
    std::atomic<int>                                success_count_{0};
    std::atomic<int>                                total_calls_{0};
    std::atomic<int>                                rejected_calls_{0};
    int                                             half_open_calls_{0};
    std::vector<CircuitBreakerEvent>                events_;
};

// ============================================================================
// Circuit Breaker Registry (manages multiple breakers)
// ============================================================================

class CircuitBreakerRegistry {
public:
    static CircuitBreakerRegistry& instance() {
        static CircuitBreakerRegistry reg;
        return reg;
    }

    CircuitBreaker& get_or_create(const std::string& name,
                                  const CircuitBreakerConfig& cfg = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = breakers_.find(name);
        if (it == breakers_.end()) {
            auto [inserted, _] = breakers_.emplace(
                name, std::make_unique<CircuitBreaker>(name, cfg));
            return *inserted->second;
        }
        return *it->second;
    }

    bool exists(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return breakers_.count(name) > 0;
    }

    void remove(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        breakers_.erase(name);
    }

    void reset_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, cb] : breakers_) {
            cb->reset();
        }
    }

    // JSON status of all breakers
    std::string status_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "[";
        bool first = true;
        for (const auto& [_, cb] : breakers_) {
            if (!first) oss << ",";
            oss << cb->to_json();
            first = false;
        }
        oss << "]";
        return oss.str();
    }

    std::vector<std::string> list_services() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(breakers_.size());
        for (const auto& [name, _] : breakers_) {
            names.push_back(name);
        }
        return names;
    }

private:
    CircuitBreakerRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<CircuitBreaker>> breakers_;
};

} // namespace genie

#endif // GENIE_CIRCUIT_BREAKER_HPP
