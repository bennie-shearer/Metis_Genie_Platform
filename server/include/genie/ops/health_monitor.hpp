/**
 * @file health_monitor.hpp
 * @brief Health check endpoints and error rate monitoring
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Operations - Health check endpoints, error rate monitoring
 */

#ifndef GENIE_OPS_HEALTH_MONITOR_HPP
#define GENIE_OPS_HEALTH_MONITOR_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>
#include <thread>
#include <deque>

namespace genie {
namespace ops {

/**
 * @brief Health status levels
 */
enum class HealthStatus {
    HEALTHY,
    DEGRADED,
    UNHEALTHY,
    UNKNOWN
};

/**
 * @brief Component health check result
 */
struct HealthCheckResult {
    std::string component;
    HealthStatus status{HealthStatus::UNKNOWN};
    std::string message;
    std::chrono::milliseconds latency{0};
    std::map<std::string, std::string> details;
    std::chrono::system_clock::time_point checked_at;
    
    std::string status_string() const {
        switch (status) {
            case HealthStatus::HEALTHY: return "healthy";
            case HealthStatus::DEGRADED: return "degraded";
            case HealthStatus::UNHEALTHY: return "unhealthy";
            default: return "unknown";
        }
    }
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"component\":\"" << component << "\",";
        json << "\"status\":\"" << status_string() << "\",";
        json << "\"message\":\"" << message << "\",";
        json << "\"latency_ms\":" << latency.count() << ",";
        
        auto time_t = std::chrono::system_clock::to_time_t(checked_at);
        json << "\"checked_at\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\"";
        
        if (!details.empty()) {
            json << ",\"details\":{";
            bool first = true;
            for (const auto& [k, v] : details) {
                if (!first) json << ",";
                json << "\"" << k << "\":\"" << v << "\"";
                first = false;
            }
            json << "}";
        }
        
        json << "}";
        return json.str();
    }
};

/**
 * @brief Overall system health
 */
struct SystemHealth {
    HealthStatus overall_status{HealthStatus::UNKNOWN};
    std::vector<HealthCheckResult> components;
    std::string version;
    std::chrono::system_clock::time_point server_time;
    std::chrono::seconds uptime{0};
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"status\":\"" << status_string() << "\",";
        json << "\"version\":\"" << version << "\",";
        json << "\"uptime_seconds\":" << uptime.count() << ",";
        
        auto time_t = std::chrono::system_clock::to_time_t(server_time);
        json << "\"server_time\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\",";
        
        json << "\"components\":[";
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) json << ",";
            json << components[i].to_json();
        }
        json << "]";
        
        json << "}";
        return json.str();
    }
    
    std::string status_string() const {
        switch (overall_status) {
            case HealthStatus::HEALTHY: return "healthy";
            case HealthStatus::DEGRADED: return "degraded";
            case HealthStatus::UNHEALTHY: return "unhealthy";
            default: return "unknown";
        }
    }
};

/**
 * @brief Health check function type
 */
using HealthChecker = std::function<HealthCheckResult()>;

/**
 * @brief Health monitor
 */
class HealthMonitor {
public:
    struct Config {
        std::chrono::seconds check_interval{30};
        std::chrono::seconds timeout{10};
        int unhealthy_threshold{3};
        bool enable_background_checks{true};
        double disk_warning_pct{85.0};     // Warn when disk usage exceeds this %
        double memory_warning_pct{90.0};   // Warn when memory usage exceeds this %
    };
    
    explicit HealthMonitor(const Config& config)
        : config_(config), running_(false),
          start_time_(std::chrono::system_clock::now()) {}
    
    ~HealthMonitor() {
        stop();
    }
    
    /**
     * @brief Register health checker
     */
    void register_check(const std::string& component, HealthChecker checker) {
        std::lock_guard<std::mutex> lock(mutex_);
        checkers_[component] = std::move(checker);
    }
    
    /**
     * @brief Run all health checks
     */
    SystemHealth check_all() {
        SystemHealth health;
        health.version = "2.25.0";
        health.server_time = std::chrono::system_clock::now();
        health.uptime = std::chrono::duration_cast<std::chrono::seconds>(
            health.server_time - start_time_);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        bool all_healthy = true;
        bool any_unhealthy = false;
        
        for (const auto& [name, checker] : checkers_) {
            auto start = std::chrono::steady_clock::now();
            
            try {
                auto result = checker();
                result.component = name;
                result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                result.checked_at = std::chrono::system_clock::now();
                
                health.components.push_back(result);
                last_results_[name] = result;
                
                if (result.status != HealthStatus::HEALTHY) {
                    all_healthy = false;
                }
                if (result.status == HealthStatus::UNHEALTHY) {
                    any_unhealthy = true;
                }
            } catch (const std::exception& e) {
                HealthCheckResult result;
                result.component = name;
                result.status = HealthStatus::UNHEALTHY;
                result.message = e.what();
                result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
                result.checked_at = std::chrono::system_clock::now();
                
                health.components.push_back(result);
                last_results_[name] = result;
                any_unhealthy = true;
            }
        }
        
        if (all_healthy) {
            health.overall_status = HealthStatus::HEALTHY;
        } else if (any_unhealthy) {
            health.overall_status = HealthStatus::UNHEALTHY;
        } else {
            health.overall_status = HealthStatus::DEGRADED;
        }
        
        return health;
    }
    
    /**
     * @brief Quick liveness check
     */
    bool is_alive() const {
        return running_ || true;  // Basic liveness
    }
    
    /**
     * @brief Check specific component
     */
    HealthCheckResult check_component(const std::string& component) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = checkers_.find(component);
        if (it == checkers_.end()) {
            HealthCheckResult result;
            result.component = component;
            result.status = HealthStatus::UNKNOWN;
            result.message = "Component not found";
            return result;
        }
        
        auto start = std::chrono::steady_clock::now();
        auto result = it->second();
        result.component = component;
        result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        result.checked_at = std::chrono::system_clock::now();
        
        last_results_[component] = result;
        return result;
    }
    
    /**
     * @brief Get cached results
     */
    std::map<std::string, HealthCheckResult> get_cached_results() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_results_;
    }
    
    /**
     * @brief Start background health checks
     */
    void start() {
        if (running_.exchange(true)) return;
        
        if (config_.enable_background_checks) {
            check_thread_ = std::thread([this]() { background_loop(); });
        }
    }
    
    /**
     * @brief Stop background health checks
     */
    void stop() {
        running_ = false;
        if (check_thread_.joinable()) {
            check_thread_.join();
        }
    }

private:
    Config config_;
    std::map<std::string, HealthChecker> checkers_;
    std::map<std::string, HealthCheckResult> last_results_;
    std::atomic<bool> running_;
    std::chrono::system_clock::time_point start_time_;
    std::thread check_thread_;
    mutable std::mutex mutex_;
    
    void background_loop() {
        while (running_) {
            check_all();
            std::this_thread::sleep_for(config_.check_interval);
        }
    }
};

/**
 * @brief Error rate tracker
 */
class ErrorRateMonitor {
public:
    struct Config {
        std::chrono::seconds window_size{60};
        int bucket_count{60};
        double alert_threshold{0.05};  // 5% error rate
        double critical_threshold{0.20};  // 20% error rate
    };
    
    struct ErrorStats {
        int64_t total_requests{0};
        int64_t total_errors{0};
        double error_rate{0.0};
        int64_t window_requests{0};
        int64_t window_errors{0};
        double window_error_rate{0.0};
        std::map<int, int64_t> errors_by_code;
        std::map<std::string, int64_t> errors_by_endpoint;
    };
    
    explicit ErrorRateMonitor(const Config& config)
        : config_(config), current_bucket_(0) {
        buckets_.resize(config.bucket_count);
    }
    
    /**
     * @brief Record successful request
     */
    void record_success(const std::string& endpoint = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        advance_bucket();
        
        total_requests_++;
        buckets_[current_bucket_].requests++;
        
        if (!endpoint.empty()) {
            endpoint_requests_[endpoint]++;
        }
    }
    
    /**
     * @brief Record error
     */
    void record_error(int status_code, const std::string& endpoint = "",
                      const std::string& message = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        advance_bucket();
        
        total_requests_++;
        total_errors_++;
        buckets_[current_bucket_].requests++;
        buckets_[current_bucket_].errors++;
        
        errors_by_code_[status_code]++;
        
        if (!endpoint.empty()) {
            endpoint_requests_[endpoint]++;
            endpoint_errors_[endpoint]++;
        }
        
        // Store recent error
        ErrorEntry entry;
        entry.timestamp = std::chrono::system_clock::now();
        entry.status_code = status_code;
        entry.endpoint = endpoint;
        entry.message = message;
        recent_errors_.push_back(entry);
        
        if (recent_errors_.size() > 100) {
            recent_errors_.pop_front();
        }
    }
    
    /**
     * @brief Get error statistics
     */
    ErrorStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        ErrorStats stats;
        stats.total_requests = total_requests_;
        stats.total_errors = total_errors_;
        stats.error_rate = total_requests_ > 0 ? 
            static_cast<double>(total_errors_) / total_requests_ : 0.0;
        
        // Calculate window stats
        for (const auto& bucket : buckets_) {
            stats.window_requests += bucket.requests;
            stats.window_errors += bucket.errors;
        }
        
        stats.window_error_rate = stats.window_requests > 0 ?
            static_cast<double>(stats.window_errors) / stats.window_requests : 0.0;
        
        stats.errors_by_code = errors_by_code_;
        
        // Top error endpoints
        std::vector<std::pair<std::string, int64_t>> sorted_endpoints(
            endpoint_errors_.begin(), endpoint_errors_.end());
        std::sort(sorted_endpoints.begin(), sorted_endpoints.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (size_t i = 0; i < std::min(size_t(10), sorted_endpoints.size()); ++i) {
            stats.errors_by_endpoint[sorted_endpoints[i].first] = sorted_endpoints[i].second;
        }
        
        return stats;
    }
    
    /**
     * @brief Get current error rate (in window)
     */
    double get_error_rate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int64_t requests = 0, errors = 0;
        for (const auto& bucket : buckets_) {
            requests += bucket.requests;
            errors += bucket.errors;
        }
        
        return requests > 0 ? static_cast<double>(errors) / requests : 0.0;
    }
    
    /**
     * @brief Check if alert threshold exceeded
     */
    bool is_alerting() const {
        return get_error_rate() >= config_.alert_threshold;
    }
    
    /**
     * @brief Check if critical threshold exceeded
     */
    bool is_critical() const {
        return get_error_rate() >= config_.critical_threshold;
    }
    
    /**
     * @brief Get recent errors
     */
    std::vector<std::map<std::string, std::string>> get_recent_errors(int count = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::map<std::string, std::string>> result;
        int start = std::max(0, static_cast<int>(recent_errors_.size()) - count);
        
        for (size_t i = start; i < recent_errors_.size(); ++i) {
            const auto& err = recent_errors_[i];
            auto time_t = std::chrono::system_clock::to_time_t(err.timestamp);
            
            std::ostringstream ts;
            ts << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
            
            result.push_back({
                {"timestamp", ts.str()},
                {"status_code", std::to_string(err.status_code)},
                {"endpoint", err.endpoint},
                {"message", err.message}
            });
        }
        
        return result;
    }
    
    /**
     * @brief Reset statistics
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        total_requests_ = 0;
        total_errors_ = 0;
        errors_by_code_.clear();
        endpoint_requests_.clear();
        endpoint_errors_.clear();
        recent_errors_.clear();
        
        for (auto& bucket : buckets_) {
            bucket = Bucket{};
        }
    }

private:
    struct Bucket {
        int64_t requests{0};
        int64_t errors{0};
        std::chrono::system_clock::time_point start_time;
    };
    
    struct ErrorEntry {
        std::chrono::system_clock::time_point timestamp;
        int status_code{0};
        std::string endpoint;
        std::string message;
    };
    
    Config config_;
    std::vector<Bucket> buckets_;
    int current_bucket_;
    
    int64_t total_requests_{0};
    int64_t total_errors_{0};
    std::map<int, int64_t> errors_by_code_;
    std::map<std::string, int64_t> endpoint_requests_;
    std::map<std::string, int64_t> endpoint_errors_;
    std::deque<ErrorEntry> recent_errors_;
    
    mutable std::mutex mutex_;
    std::chrono::system_clock::time_point last_bucket_time_{std::chrono::system_clock::now()};
    
    void advance_bucket() {
        auto now = std::chrono::system_clock::now();
        auto bucket_duration = config_.window_size / config_.bucket_count;
        
        while (now - last_bucket_time_ >= bucket_duration) {
            current_bucket_ = (current_bucket_ + 1) % config_.bucket_count;
            buckets_[current_bucket_] = Bucket{};
            buckets_[current_bucket_].start_time = last_bucket_time_ + bucket_duration;
            last_bucket_time_ += bucket_duration;
        }
    }
};

/**
 * @brief Pre-built health checkers
 */
namespace checkers {

/**
 * @brief Database health checker
 */
inline HealthChecker database_checker(std::function<bool()> ping_func) {
    return [ping_func]() -> HealthCheckResult {
        HealthCheckResult result;
        result.component = "database";
        
        try {
            if (ping_func()) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Database connection OK";
            } else {
                result.status = HealthStatus::UNHEALTHY;
                result.message = "Database ping failed";
            }
        } catch (const std::exception& e) {
            result.status = HealthStatus::UNHEALTHY;
            result.message = e.what();
        }
        
        return result;
    };
}

/**
 * @brief Memory health checker
 */
inline HealthChecker memory_checker(size_t warn_threshold_mb = 1024,
                                     size_t critical_threshold_mb = 2048) {
    return [warn_threshold_mb, critical_threshold_mb]() -> HealthCheckResult {
        HealthCheckResult result;
        result.component = "memory";
        
        // Get memory usage (platform-specific, simplified here)
        size_t used_mb = 512;  // Placeholder
        
        result.details["used_mb"] = std::to_string(used_mb);
        result.details["warn_threshold_mb"] = std::to_string(warn_threshold_mb);
        result.details["critical_threshold_mb"] = std::to_string(critical_threshold_mb);
        
        if (used_mb >= critical_threshold_mb) {
            result.status = HealthStatus::UNHEALTHY;
            result.message = "Memory usage critical";
        } else if (used_mb >= warn_threshold_mb) {
            result.status = HealthStatus::DEGRADED;
            result.message = "Memory usage high";
        } else {
            result.status = HealthStatus::HEALTHY;
            result.message = "Memory usage OK";
        }
        
        return result;
    };
}

/**
 * @brief Disk health checker
 */
inline HealthChecker disk_checker(const std::string& path,
                                   double warn_threshold_pct = 80.0,
                                   double critical_threshold_pct = 95.0) {
    return [path, warn_threshold_pct, critical_threshold_pct]() -> HealthCheckResult {
        HealthCheckResult result;
        result.component = "disk";
        
        // Get disk usage (platform-specific, simplified here)
        double used_pct = 50.0;  // Placeholder
        
        result.details["path"] = path;
        result.details["used_pct"] = std::to_string(used_pct);
        
        if (used_pct >= critical_threshold_pct) {
            result.status = HealthStatus::UNHEALTHY;
            result.message = "Disk space critical";
        } else if (used_pct >= warn_threshold_pct) {
            result.status = HealthStatus::DEGRADED;
            result.message = "Disk space low";
        } else {
            result.status = HealthStatus::HEALTHY;
            result.message = "Disk space OK";
        }
        
        return result;
    };
}

/**
 * @brief External API health checker
 */
inline HealthChecker api_checker(const std::string& name,
                                  std::function<bool()> check_func,
                                  std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    return [name, check_func, timeout]() -> HealthCheckResult {
        HealthCheckResult result;
        result.component = name;
        
        auto start = std::chrono::steady_clock::now();
        
        try {
            bool ok = check_func();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            
            result.details["response_time_ms"] = std::to_string(elapsed.count());
            
            if (!ok) {
                result.status = HealthStatus::UNHEALTHY;
                result.message = "API check failed";
            } else if (elapsed > timeout) {
                result.status = HealthStatus::DEGRADED;
                result.message = "API response slow";
            } else {
                result.status = HealthStatus::HEALTHY;
                result.message = "API OK";
            }
        } catch (const std::exception& e) {
            result.status = HealthStatus::UNHEALTHY;
            result.message = e.what();
        }
        
        return result;
    };
}

} // namespace checkers

} // namespace ops
} // namespace genie

#endif // GENIE_OPS_HEALTH_MONITOR_HPP
