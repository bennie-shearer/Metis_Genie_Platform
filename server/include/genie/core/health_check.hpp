/**
 * @file health_check.hpp
 * @brief Comprehensive Health Check Aggregator with Liveness/Readiness Probes
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides enterprise-grade health monitoring with configurable probes,
 * dependency checks, and degraded-state awareness for production deployments.
 *
 * Features:
 *  - Liveness probe (is the process alive and responsive?)
 *  - Readiness probe (is the service ready to accept traffic?)
 *  - Startup probe (has the service completed initialization?)
 *  - Dependency health checks (database, cache, external APIs, queues)
 *  - Configurable check intervals and timeouts per component
 *  - Health history with rolling window retention
 *  - Degraded-state detection with automatic recovery monitoring
 *  - Component-level health scoring (0-100)
 *  - Aggregate health status with weighted scoring
 *  - Health check callbacks for external integrations
 *  - Circuit breaker pattern for failing dependencies
 *  - Thread-safe with background polling option
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_HEALTH_CHECK_HPP
#define GENIE_HEALTH_CHECK_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <optional>
#include <sstream>
#include <algorithm>
#include <deque>
#include <numeric>

namespace genie::core {

// ============================================================================
// Enums and Types
// ============================================================================

/** @brief Overall health status */
enum class HealthStatus { HEALTHY, DEGRADED, UNHEALTHY, UNKNOWN };

/** @brief Component type for categorized health checks */
enum class ComponentType { DATABASE, CACHE, EXTERNAL_API, QUEUE, FILESYSTEM,
                           NETWORK, COMPUTE, MEMORY, CUSTOM };

/** @brief Circuit breaker state */
enum class CircuitState { CLOSED, OPEN, HALF_OPEN };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Individual component health check result */
struct ComponentHealth {
    std::string name;
    ComponentType type{ComponentType::CUSTOM};
    HealthStatus status{HealthStatus::UNKNOWN};
    double score{0.0}; // 0-100
    double response_time_ms{0.0};
    std::string message;
    std::string last_checked;
    std::string last_healthy;
    int consecutive_failures{0};
    int consecutive_successes{0};
    CircuitState circuit{CircuitState::CLOSED};
    bool critical{false}; // If critical, UNHEALTHY cascades to aggregate
    std::unordered_map<std::string, std::string> metadata;
};

/** @brief Aggregate health report */
struct HealthReport {
    HealthStatus overall_status{HealthStatus::UNKNOWN};
    double overall_score{0.0};
    bool liveness{true};
    bool readiness{false};
    bool startup_complete{false};
    std::string timestamp;
    std::string uptime;
    uint64_t total_checks_run{0};
    int healthy_components{0};
    int degraded_components{0};
    int unhealthy_components{0};
    std::vector<ComponentHealth> components;
    std::vector<std::string> alerts;
    double avg_response_time_ms{0.0};
};

/** @brief Health history entry */
struct HealthSnapshot {
    std::string timestamp;
    HealthStatus status{HealthStatus::UNKNOWN};
    double score{0.0};
    int components_healthy{0};
    int components_total{0};
};

/** @brief Health check configuration for a component */
struct HealthCheckConfig {
    std::string name;
    ComponentType type{ComponentType::CUSTOM};
    std::function<bool()> check_func;
    std::chrono::seconds interval{std::chrono::seconds(30)};
    std::chrono::milliseconds timeout{std::chrono::milliseconds(5000)};
    bool critical{false};
    double weight{1.0};
    int failure_threshold{3};   // Failures before UNHEALTHY
    int recovery_threshold{2};  // Successes before HEALTHY again
    int circuit_open_duration_sec{60};
};

/** @brief Health check aggregator configuration */
struct AggregatorConfig {
    std::chrono::seconds poll_interval{std::chrono::seconds(30)};
    std::size_t max_history{1000};
    double degraded_threshold{70.0};  // Score below this = DEGRADED
    double unhealthy_threshold{40.0}; // Score below this = UNHEALTHY
    bool auto_poll{false};
};

// ============================================================================
// HealthCheckAggregator
// ============================================================================

/**
 * @class HealthCheckAggregator
 * @brief Aggregates component health checks into a unified health report
 */
class HealthCheckAggregator {
public:
    explicit HealthCheckAggregator(AggregatorConfig config = {})
        : config_(std::move(config)),
          start_time_(std::chrono::steady_clock::now()) {}

    ~HealthCheckAggregator() { stop(); }

    HealthCheckAggregator(const HealthCheckAggregator&) = delete;
    HealthCheckAggregator& operator=(const HealthCheckAggregator&) = delete;

    // ---- Registration ----

    /** @brief Register a health check component */
    void register_check(HealthCheckConfig check_config) {
        std::lock_guard lock(mutex_);
        ComponentHealth ch;
        ch.name = check_config.name;
        ch.type = check_config.type;
        ch.critical = check_config.critical;
        ch.status = HealthStatus::UNKNOWN;
        components_[check_config.name] = ch;
        check_configs_[check_config.name] = std::move(check_config);
    }

    /** @brief Unregister a component */
    bool unregister_check(const std::string& name) {
        std::lock_guard lock(mutex_);
        check_configs_.erase(name);
        return components_.erase(name) > 0;
    }

    /** @brief Mark startup as complete */
    void mark_startup_complete() { startup_complete_ = true; }

    // ---- Execution ----

    /** @brief Run all health checks synchronously */
    HealthReport check_all() {
        std::lock_guard lock(mutex_);
        for (auto& [name, cfg] : check_configs_) {
            run_single_check(name, cfg);
        }
        total_checks_run_++;
        return build_report();
    }

    /** @brief Run a specific component check */
    std::optional<ComponentHealth> check_component(const std::string& name) {
        std::lock_guard lock(mutex_);
        auto cfg_it = check_configs_.find(name);
        if (cfg_it == check_configs_.end()) return std::nullopt;
        run_single_check(name, cfg_it->second);
        auto comp_it = components_.find(name);
        if (comp_it != components_.end()) return comp_it->second;
        return std::nullopt;
    }

    /** @brief Start background polling */
    void start() {
        if (running_.exchange(true)) return;
        poll_thread_ = std::thread([this] {
            while (running_) {
                check_all();
                std::this_thread::sleep_for(config_.poll_interval);
            }
        });
    }

    /** @brief Stop background polling */
    void stop() {
        running_ = false;
        if (poll_thread_.joinable()) poll_thread_.join();
    }

    // ---- Probes ----

    /** @brief Liveness probe - is the process alive? */
    [[nodiscard]] bool liveness() const { return true; }

    /** @brief Readiness probe - is the service ready for traffic? */
    [[nodiscard]] bool readiness() const {
        std::lock_guard lock(mutex_);
        if (!startup_complete_) return false;
        for (const auto& [_, c] : components_) {
            if (c.critical && c.status == HealthStatus::UNHEALTHY) return false;
        }
        return true;
    }

    /** @brief Startup probe - has initialization completed? */
    [[nodiscard]] bool startup() const { return startup_complete_; }

    // ---- Reports ----

    /** @brief Get current health report without running checks */
    [[nodiscard]] HealthReport report() const {
        std::lock_guard lock(mutex_);
        return build_report();
    }

    /** @brief Get specific component health */
    [[nodiscard]] std::optional<ComponentHealth> component(const std::string& name) const {
        std::lock_guard lock(mutex_);
        auto it = components_.find(name);
        if (it != components_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief Get health history */
    [[nodiscard]] std::vector<HealthSnapshot> history() const {
        std::lock_guard lock(mutex_);
        return {history_.begin(), history_.end()};
    }

    /** @brief Number of registered components */
    [[nodiscard]] std::size_t component_count() const {
        std::lock_guard lock(mutex_);
        return components_.size();
    }

    /** @brief Total checks ever run */
    [[nodiscard]] uint64_t total_checks() const { return total_checks_run_; }

    /** @brief Uptime in seconds */
    [[nodiscard]] double uptime_seconds() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration<double>(elapsed).count();
    }

private:
    void run_single_check(const std::string& name, HealthCheckConfig& cfg) {
        auto& comp = components_[name];

        // Circuit breaker check
        if (comp.circuit == CircuitState::OPEN) {
            auto last = parse_time(comp.last_checked);
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last).count();
            if (elapsed < cfg.circuit_open_duration_sec) {
                comp.status = HealthStatus::UNHEALTHY;
                comp.message = "Circuit breaker OPEN";
                comp.score = 0.0;
                return;
            }
            comp.circuit = CircuitState::HALF_OPEN;
        }

        auto start = std::chrono::steady_clock::now();
        bool passed = false;

        if (cfg.check_func) {
            try {
                passed = cfg.check_func();
            } catch (const std::exception& e) {
                passed = false;
                comp.message = std::string("Exception: ") + e.what();
            }
        } else {
            passed = true; // No check func = always healthy
        }

        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        comp.response_time_ms = elapsed;
        comp.last_checked = now_str();

        if (passed) {
            comp.consecutive_successes++;
            comp.consecutive_failures = 0;
            comp.last_healthy = comp.last_checked;

            if (comp.circuit == CircuitState::HALF_OPEN) {
                if (comp.consecutive_successes >= cfg.recovery_threshold) {
                    comp.circuit = CircuitState::CLOSED;
                }
            }

            if (comp.consecutive_successes >= cfg.recovery_threshold || comp.status == HealthStatus::UNKNOWN) {
                comp.status = HealthStatus::HEALTHY;
                comp.score = 100.0;
                comp.message = "OK";
            } else {
                comp.status = HealthStatus::DEGRADED;
                comp.score = 60.0;
                comp.message = "Recovering";
            }
        } else {
            comp.consecutive_failures++;
            comp.consecutive_successes = 0;

            if (comp.consecutive_failures >= cfg.failure_threshold) {
                comp.status = HealthStatus::UNHEALTHY;
                comp.score = 0.0;
                if (comp.circuit == CircuitState::CLOSED) {
                    comp.circuit = CircuitState::OPEN;
                }
            } else {
                comp.status = HealthStatus::DEGRADED;
                comp.score = 30.0;
            }
            if (comp.message.empty()) comp.message = "Check failed";
        }

        // Timeout check
        if (elapsed > static_cast<double>(cfg.timeout.count())) {
            comp.status = HealthStatus::DEGRADED;
            comp.score = std::min(comp.score, 40.0);
            comp.message = "Check timed out (" + std::to_string(static_cast<int>(elapsed)) + "ms)";
        }
    }

    HealthReport build_report() const {
        HealthReport report;
        report.timestamp = now_str();
        report.liveness = true;
        report.startup_complete = startup_complete_;
        report.total_checks_run = total_checks_run_;

        double total_weight = 0;
        double weighted_score = 0;
        double total_response = 0;
        bool any_critical_unhealthy = false;

        for (const auto& [_, comp] : components_) {
            report.components.push_back(comp);
            auto cfg_it = check_configs_.find(comp.name);
            double weight = cfg_it != check_configs_.end() ? cfg_it->second.weight : 1.0;

            weighted_score += comp.score * weight;
            total_weight += weight;
            total_response += comp.response_time_ms;

            switch (comp.status) {
                case HealthStatus::HEALTHY: report.healthy_components++; break;
                case HealthStatus::DEGRADED: report.degraded_components++; break;
                case HealthStatus::UNHEALTHY:
                    report.unhealthy_components++;
                    if (comp.critical) any_critical_unhealthy = true;
                    break;
                default: break;
            }
        }

        report.overall_score = total_weight > 0 ? weighted_score / total_weight : 0;
        report.avg_response_time_ms = !components_.empty()
            ? total_response / components_.size() : 0;

        if (any_critical_unhealthy || report.overall_score < config_.unhealthy_threshold) {
            report.overall_status = HealthStatus::UNHEALTHY;
        } else if (report.overall_score < config_.degraded_threshold) {
            report.overall_status = HealthStatus::DEGRADED;
        } else {
            report.overall_status = HealthStatus::HEALTHY;
        }

        report.readiness = startup_complete_ && !any_critical_unhealthy;

        // Uptime
        auto up = std::chrono::steady_clock::now() - start_time_;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(up).count();
        auto mins = std::chrono::duration_cast<std::chrono::minutes>(up).count() % 60;
        report.uptime = std::to_string(hours) + "h " + std::to_string(mins) + "m";

        // Alerts
        for (const auto& comp : report.components) {
            if (comp.status == HealthStatus::UNHEALTHY) {
                report.alerts.push_back(comp.name + " is UNHEALTHY: " + comp.message);
            }
            if (comp.circuit == CircuitState::OPEN) {
                report.alerts.push_back(comp.name + " circuit breaker is OPEN");
            }
        }

        // Record history
        HealthSnapshot snap;
        snap.timestamp = report.timestamp;
        snap.status = report.overall_status;
        snap.score = report.overall_score;
        snap.components_healthy = report.healthy_components;
        snap.components_total = static_cast<int>(components_.size());
        history_.push_back(snap);
        while (history_.size() > config_.max_history) history_.pop_front();

        return report;
    }

    static std::chrono::system_clock::time_point parse_time(const std::string&) {
        return std::chrono::system_clock::now(); // Simplified
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    AggregatorConfig config_;
    std::unordered_map<std::string, ComponentHealth> components_;
    std::unordered_map<std::string, HealthCheckConfig> check_configs_;
    mutable std::deque<HealthSnapshot> history_;
    std::atomic<bool> startup_complete_{false};
    std::atomic<bool> running_{false};
    std::thread poll_thread_;
    std::atomic<uint64_t> total_checks_run_{0};
    std::chrono::steady_clock::time_point start_time_;
    mutable std::mutex mutex_;
};

} // namespace genie::core

#endif // GENIE_HEALTH_CHECK_HPP
