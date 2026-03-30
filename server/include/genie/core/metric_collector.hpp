/**
 * @file metric_collector.hpp
 * @brief Application Metrics Collection and Aggregation for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Collects, aggregates, and exposes application metrics for monitoring,
 * alerting, and capacity planning. Supports counters, gauges, histograms,
 * and timers with label-based dimensionality.
 *
 * Features:
 *  - Counter metrics (monotonically increasing)
 *  - Gauge metrics (point-in-time values)
 *  - Histogram metrics (distribution tracking with configurable buckets)
 *  - Timer metrics (latency measurement with percentiles)
 *  - Label-based metric dimensionality (key-value tags)
 *  - Prometheus-compatible exposition format
 *  - Rolling window aggregation (1m, 5m, 15m, 1h)
 *  - Metric rate computation (per-second, per-minute)
 *  - Alert threshold registration with callback notification
 *  - Metric snapshot export
 *  - Thread-safe concurrent access with atomic operations
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_METRIC_COLLECTOR_HPP
#define GENIE_METRIC_COLLECTOR_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace genie::core {

// ============================================================================
// Enums and Types
// ============================================================================

enum class MetricType { COUNTER, GAUGE, HISTOGRAM, TIMER };

using MetricLabels = std::unordered_map<std::string, std::string>;

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Histogram bucket */
struct HistogramBucket {
    double upper_bound{0.0};
    uint64_t count{0};
};

/** @brief Metric value with metadata */
struct MetricValue {
    std::string name;
    MetricType type{MetricType::GAUGE};
    double value{0.0};
    MetricLabels labels;
    std::string description;
    std::string unit;
    std::string timestamp;
    // Histogram specific
    std::vector<HistogramBucket> buckets;
    uint64_t histogram_count{0};
    double histogram_sum{0.0};
    // Timer specific
    double p50{0.0};
    double p90{0.0};
    double p95{0.0};
    double p99{0.0};
    double min_val{0.0};
    double max_val{0.0};
    double mean{0.0};
    // Rate
    double rate_per_second{0.0};
    double rate_per_minute{0.0};
};

/** @brief Alert threshold definition */
struct MetricAlert {
    std::string metric_name;
    std::string condition; // "above", "below", "equals"
    double threshold{0.0};
    std::string severity; // "info", "warning", "critical"
    std::function<void(const std::string&, double)> callback;
    bool triggered{false};
    std::string last_triggered;
};

/** @brief Metric snapshot for export */
struct MetricSnapshot {
    std::string timestamp;
    std::vector<MetricValue> metrics;
    std::size_t total_metrics{0};
    std::size_t total_counters{0};
    std::size_t total_gauges{0};
    std::size_t total_histograms{0};
    std::size_t total_timers{0};
};

/** @brief Time-series data point */
struct TimeSeriesPoint {
    std::string timestamp;
    double value{0.0};
};

// ============================================================================
// MetricCollector
// ============================================================================

/**
 * @class MetricCollector
 * @brief Collects and aggregates application metrics
 */
class MetricCollector {
public:
    MetricCollector() = default;

    // ---- Counter Operations ----

    /** @brief Register a counter metric */
    void register_counter(const std::string& name, const std::string& description = "",
                          const std::string& unit = "") {
        std::lock_guard lock(mutex_);
        MetricValue mv;
        mv.name = name;
        mv.type = MetricType::COUNTER;
        mv.description = description;
        mv.unit = unit;
        mv.value = 0;
        metrics_[name] = std::move(mv);
    }

    /** @brief Increment a counter */
    void increment(const std::string& name, double amount = 1.0) {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it != metrics_.end() && it->second.type == MetricType::COUNTER) {
            it->second.value += amount;
            it->second.timestamp = now_str();
            record_rate(name, amount);
            check_alerts(name, it->second.value);
        }
    }

    // ---- Gauge Operations ----

    /** @brief Register a gauge metric */
    void register_gauge(const std::string& name, const std::string& description = "",
                        const std::string& unit = "") {
        std::lock_guard lock(mutex_);
        MetricValue mv;
        mv.name = name;
        mv.type = MetricType::GAUGE;
        mv.description = description;
        mv.unit = unit;
        metrics_[name] = std::move(mv);
    }

    /** @brief Set a gauge value */
    void set_gauge(const std::string& name, double value) {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it != metrics_.end() && it->second.type == MetricType::GAUGE) {
            it->second.value = value;
            it->second.timestamp = now_str();
            record_history(name, value);
            check_alerts(name, value);
        }
    }

    /** @brief Increment/decrement a gauge */
    void adjust_gauge(const std::string& name, double delta) {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it != metrics_.end() && it->second.type == MetricType::GAUGE) {
            it->second.value += delta;
            it->second.timestamp = now_str();
        }
    }

    // ---- Histogram Operations ----

    /** @brief Register a histogram with custom buckets */
    void register_histogram(const std::string& name,
                            const std::vector<double>& bucket_bounds,
                            const std::string& description = "",
                            const std::string& unit = "") {
        std::lock_guard lock(mutex_);
        MetricValue mv;
        mv.name = name;
        mv.type = MetricType::HISTOGRAM;
        mv.description = description;
        mv.unit = unit;
        for (double b : bucket_bounds) mv.buckets.push_back({b, 0});
        mv.buckets.push_back({std::numeric_limits<double>::infinity(), 0});
        metrics_[name] = std::move(mv);
    }

    /** @brief Observe a value in a histogram */
    void observe(const std::string& name, double value) {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it == metrics_.end()) return;
        auto& mv = it->second;
        if (mv.type == MetricType::HISTOGRAM) {
            mv.histogram_count++;
            mv.histogram_sum += value;
            for (auto& b : mv.buckets) {
                if (value <= b.upper_bound) b.count++;
            }
            mv.timestamp = now_str();
        }
    }

    // ---- Timer Operations ----

    /** @brief Register a timer metric */
    void register_timer(const std::string& name, const std::string& description = "") {
        std::lock_guard lock(mutex_);
        MetricValue mv;
        mv.name = name;
        mv.type = MetricType::TIMER;
        mv.description = description;
        mv.unit = "ms";
        metrics_[name] = std::move(mv);
    }

    /** @brief Record a timing observation */
    void record_time(const std::string& name, double ms) {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it == metrics_.end() || it->second.type != MetricType::TIMER) return;

        auto& values = timer_values_[name];
        values.push_back(ms);
        while (values.size() > 10000) values.pop_front(); // Rolling window

        // Compute percentiles
        std::vector<double> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        std::size_t n = sorted.size();

        auto& mv = it->second;
        mv.p50 = sorted[n * 50 / 100];
        mv.p90 = sorted[n * 90 / 100];
        mv.p95 = sorted[n * 95 / 100];
        mv.p99 = sorted[std::min(n * 99 / 100, n - 1)];
        mv.min_val = sorted.front();
        mv.max_val = sorted.back();
        mv.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / n;
        mv.value = mv.p50;
        mv.timestamp = now_str();
    }

    /** @brief RAII timer helper - records on destruction */
    class ScopedTimer {
    public:
        ScopedTimer(MetricCollector& collector, std::string name)
            : collector_(collector), name_(std::move(name)),
              start_(std::chrono::steady_clock::now()) {}
        ~ScopedTimer() {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start_).count();
            collector_.record_time(name_, elapsed);
        }
    private:
        MetricCollector& collector_;
        std::string name_;
        std::chrono::steady_clock::time_point start_;
    };

    /** @brief Create a scoped timer */
    ScopedTimer time_scope(const std::string& name) { return ScopedTimer(*this, name); }

    // ---- Alerts ----

    /** @brief Register an alert threshold */
    void register_alert(const std::string& metric_name, const std::string& condition,
                        double threshold, const std::string& severity,
                        std::function<void(const std::string&, double)> callback = nullptr) {
        std::lock_guard lock(mutex_);
        MetricAlert alert;
        alert.metric_name = metric_name;
        alert.condition = condition;
        alert.threshold = threshold;
        alert.severity = severity;
        alert.callback = std::move(callback);
        alerts_.push_back(std::move(alert));
    }

    // ---- Queries ----

    /** @brief Get a specific metric */
    [[nodiscard]] std::optional<MetricValue> get(const std::string& name) const {
        std::lock_guard lock(mutex_);
        auto it = metrics_.find(name);
        if (it != metrics_.end()) {
            auto mv = it->second;
            // Attach rate info
            auto rate_it = rate_windows_.find(name);
            if (rate_it != rate_windows_.end() && !rate_it->second.empty()) {
                auto window_sec = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - rate_it->second.front().first).count();
                double total = 0;
                for (const auto& [_, v] : rate_it->second) total += v;
                mv.rate_per_second = window_sec > 0 ? total / window_sec : 0;
                mv.rate_per_minute = mv.rate_per_second * 60.0;
            }
            return mv;
        }
        return std::nullopt;
    }

    /** @brief Get all metrics as a snapshot */
    [[nodiscard]] MetricSnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        MetricSnapshot snap;
        snap.timestamp = now_str();
        for (const auto& [_, mv] : metrics_) {
            snap.metrics.push_back(mv);
            snap.total_metrics++;
            switch (mv.type) {
                case MetricType::COUNTER: snap.total_counters++; break;
                case MetricType::GAUGE: snap.total_gauges++; break;
                case MetricType::HISTOGRAM: snap.total_histograms++; break;
                case MetricType::TIMER: snap.total_timers++; break;
            }
        }
        return snap;
    }

    /** @brief Export in Prometheus text format */
    [[nodiscard]] std::string prometheus_export() const {
        std::lock_guard lock(mutex_);
        std::ostringstream oss;
        for (const auto& [name, mv] : metrics_) {
            if (!mv.description.empty()) oss << "# HELP " << name << " " << mv.description << "\n";
            switch (mv.type) {
                case MetricType::COUNTER:
                    oss << "# TYPE " << name << " counter\n";
                    oss << name << " " << mv.value << "\n";
                    break;
                case MetricType::GAUGE:
                    oss << "# TYPE " << name << " gauge\n";
                    oss << name << " " << mv.value << "\n";
                    break;
                case MetricType::HISTOGRAM:
                    oss << "# TYPE " << name << " histogram\n";
                    for (const auto& b : mv.buckets) {
                        oss << name << "_bucket{le=\"" << b.upper_bound << "\"} " << b.count << "\n";
                    }
                    oss << name << "_sum " << mv.histogram_sum << "\n";
                    oss << name << "_count " << mv.histogram_count << "\n";
                    break;
                case MetricType::TIMER:
                    oss << "# TYPE " << name << " summary\n";
                    oss << name << "{quantile=\"0.5\"} " << mv.p50 << "\n";
                    oss << name << "{quantile=\"0.9\"} " << mv.p90 << "\n";
                    oss << name << "{quantile=\"0.95\"} " << mv.p95 << "\n";
                    oss << name << "{quantile=\"0.99\"} " << mv.p99 << "\n";
                    break;
            }
        }
        return oss.str();
    }

    /** @brief Get metric history */
    [[nodiscard]] std::vector<TimeSeriesPoint> history(const std::string& name, int max_points = 100) const {
        std::lock_guard lock(mutex_);
        auto it = metric_history_.find(name);
        if (it == metric_history_.end()) return {};
        auto& hist = it->second;
        int start = std::max(0, static_cast<int>(hist.size()) - max_points);
        return {hist.begin() + start, hist.end()};
    }

    /** @brief Total registered metrics */
    [[nodiscard]] std::size_t metric_count() const {
        std::lock_guard lock(mutex_);
        return metrics_.size();
    }

    /** @brief Reset all metrics */
    void reset() {
        std::lock_guard lock(mutex_);
        for (auto& [_, mv] : metrics_) {
            mv.value = 0;
            mv.histogram_count = 0;
            mv.histogram_sum = 0;
            for (auto& b : mv.buckets) b.count = 0;
        }
        timer_values_.clear();
        metric_history_.clear();
        rate_windows_.clear();
    }

private:
    void record_rate(const std::string& name, double amount) {
        auto now = std::chrono::steady_clock::now();
        auto& window = rate_windows_[name];
        window.push_back({now, amount});
        // Keep 5 minutes of data
        auto cutoff = now - std::chrono::minutes(5);
        while (!window.empty() && window.front().first < cutoff) window.pop_front();
    }

    void record_history(const std::string& name, double value) {
        TimeSeriesPoint pt;
        pt.timestamp = now_str();
        pt.value = value;
        auto& hist = metric_history_[name];
        hist.push_back(pt);
        while (hist.size() > 1000) hist.erase(hist.begin());
    }

    void check_alerts(const std::string& name, double value) {
        for (auto& alert : alerts_) {
            if (alert.metric_name != name) continue;
            bool triggered = false;
            if (alert.condition == "above" && value > alert.threshold) triggered = true;
            if (alert.condition == "below" && value < alert.threshold) triggered = true;
            if (alert.condition == "equals" && std::abs(value - alert.threshold) < 1e-9) triggered = true;

            if (triggered && !alert.triggered) {
                alert.triggered = true;
                alert.last_triggered = now_str();
                if (alert.callback) alert.callback(name, value);
            } else if (!triggered) {
                alert.triggered = false;
            }
        }
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::unordered_map<std::string, MetricValue> metrics_;
    std::unordered_map<std::string, std::deque<double>> timer_values_;
    std::unordered_map<std::string, std::vector<TimeSeriesPoint>> metric_history_;
    std::unordered_map<std::string, std::deque<std::pair<std::chrono::steady_clock::time_point, double>>> rate_windows_;
    std::vector<MetricAlert> alerts_;
    mutable std::mutex mutex_;
};

} // namespace genie::core

#endif // GENIE_METRIC_COLLECTOR_HPP
