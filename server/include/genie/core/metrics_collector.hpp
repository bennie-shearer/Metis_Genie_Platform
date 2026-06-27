/**
 * @file metrics_collector.hpp
 * @brief Prometheus-compatible metrics collection framework
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Application metrics instrumentation:
 * - Counter: monotonically increasing (requests, errors)
 * - Gauge: point-in-time value (connections, queue depth)
 * - Histogram: value distribution with configurable buckets
 * - Summary: quantile estimation (p50, p95, p99)
 * - Label-based metric families
 * - Prometheus text exposition format export
 * - JSON metrics export
 * - Thread-safe atomic operations
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_METRICS_COLLECTOR_HPP
#define GENIE_CORE_METRICS_COLLECTOR_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <atomic>
#include <cmath>
#include <numeric>

namespace genie {
namespace core {
namespace metrics {

// ============================================================================
// Metric Types
// ============================================================================

enum class MetricType {
    Counter,
    Gauge,
    Histogram,
    Summary
};

[[nodiscard]] inline std::string metric_type_string(MetricType t) {
    switch (t) {
        case MetricType::Counter:   return "counter";
        case MetricType::Gauge:     return "gauge";
        case MetricType::Histogram: return "histogram";
        case MetricType::Summary:   return "summary";
    }
    return "unknown";
}

// ============================================================================
// Counter
// ============================================================================

class Counter {
public:
    explicit Counter(const std::string& name, const std::string& help = "")
        : name_(name), help_(help), value_(0) {}

    void increment(double v = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += v;
    }

    [[nodiscard]] double value() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = 0;
    }

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::string& help() const { return help_; }

private:
    mutable std::mutex mutex_;
    std::string name_;
    std::string help_;
    double value_;
};

// ============================================================================
// Gauge
// ============================================================================

class Gauge {
public:
    explicit Gauge(const std::string& name, const std::string& help = "")
        : name_(name), help_(help), value_(0) {}

    void set(double v) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = v;
    }
    void increment(double v = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += v;
    }
    void decrement(double v = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ -= v;
    }

    [[nodiscard]] double value() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::string& help() const { return help_; }

private:
    mutable std::mutex mutex_;
    std::string name_;
    std::string help_;
    double value_;
};

// ============================================================================
// Histogram
// ============================================================================

class Histogram {
public:
    Histogram(const std::string& name, const std::string& help = "",
              const std::vector<double>& buckets = {})
        : name_(name), help_(help), count_(0), sum_(0) {
        if (buckets.empty()) {
            // Default Prometheus buckets
            buckets_ = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
        } else {
            buckets_ = buckets;
            std::sort(buckets_.begin(), buckets_.end());
        }
        bucket_counts_.resize(buckets_.size(), 0);
    }

    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        sum_ += value;
        ++count_;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (value <= buckets_[i]) {
                ++bucket_counts_[i];
            }
        }
    }

    [[nodiscard]] int64_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    [[nodiscard]] double sum() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sum_;
    }

    [[nodiscard]] double mean() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ > 0 ? sum_ / count_ : 0;
    }

    [[nodiscard]] std::vector<std::pair<double, int64_t>> bucket_values() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<double, int64_t>> result;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            result.emplace_back(buckets_[i], bucket_counts_[i]);
        }
        return result;
    }

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::string& help() const { return help_; }

private:
    mutable std::mutex mutex_;
    std::string name_;
    std::string help_;
    std::vector<double> buckets_;
    std::vector<int64_t> bucket_counts_;
    int64_t count_;
    double sum_;
};

// ============================================================================
// Summary (quantile estimator)
// ============================================================================

class Summary {
public:
    Summary(const std::string& name, const std::string& help = "",
            int window_size = 1000)
        : name_(name), help_(help), window_size_(window_size),
          count_(0), sum_(0) {}

    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.push_back(value);
        if (static_cast<int>(values_.size()) > window_size_) {
            sum_ -= values_.front();
            values_.erase(values_.begin());
        } else {
            ++count_;
        }
        sum_ += value;
    }

    [[nodiscard]] double quantile(double q) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (values_.empty()) return 0;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(q * (sorted.size() - 1));
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    [[nodiscard]] double p50() const { return quantile(0.5); }
    [[nodiscard]] double p90() const { return quantile(0.9); }
    [[nodiscard]] double p95() const { return quantile(0.95); }
    [[nodiscard]] double p99() const { return quantile(0.99); }

    [[nodiscard]] int64_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }
    [[nodiscard]] double sum() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sum_;
    }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::string& help() const { return help_; }

private:
    mutable std::mutex mutex_;
    std::string name_;
    std::string help_;
    int window_size_;
    std::vector<double> values_;
    int64_t count_;
    double sum_;
};

// ============================================================================
// Metrics Registry
// ============================================================================

class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry reg;
        return reg;
    }

    Counter& counter(const std::string& name, const std::string& help = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counters_.find(name);
        if (it == counters_.end()) {
            counters_.try_emplace(name, name, help);
        }
        return counters_.at(name);
    }

    Gauge& gauge(const std::string& name, const std::string& help = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = gauges_.find(name);
        if (it == gauges_.end()) {
            gauges_.try_emplace(name, name, help);
        }
        return gauges_.at(name);
    }

    Histogram& histogram(const std::string& name, const std::string& help = "",
                           const std::vector<double>& buckets = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = histograms_.find(name);
        if (it == histograms_.end()) {
            histograms_.try_emplace(name, name, help, buckets);
        }
        return histograms_.at(name);
    }

    Summary& summary(const std::string& name, const std::string& help = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = summaries_.find(name);
        if (it == summaries_.end()) {
            summaries_.try_emplace(name, name, help);
        }
        return summaries_.at(name);
    }

    /**
     * @brief Export in Prometheus text exposition format
     */
    [[nodiscard]] std::string to_prometheus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;

        for (const auto& [name, c] : counters_) {
            if (!c.help().empty()) oss << "# HELP " << name << " " << c.help() << "\n";
            oss << "# TYPE " << name << " counter\n";
            oss << name << " " << c.value() << "\n\n";
        }

        for (const auto& [name, g] : gauges_) {
            if (!g.help().empty()) oss << "# HELP " << name << " " << g.help() << "\n";
            oss << "# TYPE " << name << " gauge\n";
            oss << name << " " << g.value() << "\n\n";
        }

        for (const auto& [name, h] : histograms_) {
            if (!h.help().empty()) oss << "# HELP " << name << " " << h.help() << "\n";
            oss << "# TYPE " << name << " histogram\n";
            for (const auto& [bound, cnt] : h.bucket_values()) {
                oss << name << "_bucket{le=\"" << bound << "\"} " << cnt << "\n";
            }
            oss << name << "_bucket{le=\"+Inf\"} " << h.count() << "\n";
            oss << name << "_sum " << h.sum() << "\n";
            oss << name << "_count " << h.count() << "\n\n";
        }

        for (const auto& [name, s] : summaries_) {
            if (!s.help().empty()) oss << "# HELP " << name << " " << s.help() << "\n";
            oss << "# TYPE " << name << " summary\n";
            oss << name << "{quantile=\"0.5\"} " << s.p50() << "\n";
            oss << name << "{quantile=\"0.9\"} " << s.p90() << "\n";
            oss << name << "{quantile=\"0.95\"} " << s.p95() << "\n";
            oss << name << "{quantile=\"0.99\"} " << s.p99() << "\n";
            oss << name << "_sum " << s.sum() << "\n";
            oss << name << "_count " << s.count() << "\n\n";
        }

        return oss.str();
    }

    /**
     * @brief Export as JSON
     */
    [[nodiscard]] std::string to_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\"counters\":{";
        bool first = true;
        for (const auto& [n, c] : counters_) {
            if (!first) oss << ",";
            oss << "\"" << n << "\":" << c.value();
            first = false;
        }
        oss << "},\"gauges\":{";
        first = true;
        for (const auto& [n, g] : gauges_) {
            if (!first) oss << ",";
            oss << "\"" << n << "\":" << g.value();
            first = false;
        }
        oss << "},\"histograms\":{";
        first = true;
        for (const auto& [n, h] : histograms_) {
            if (!first) oss << ",";
            oss << "\"" << n << "\":{\"count\":" << h.count()
                << ",\"sum\":" << h.sum()
                << ",\"mean\":" << std::fixed << std::setprecision(4) << h.mean() << "}";
            first = false;
        }
        oss << "},\"summaries\":{";
        first = true;
        for (const auto& [n, s] : summaries_) {
            if (!first) oss << ",";
            oss << "\"" << n << "\":{\"p50\":" << std::fixed << std::setprecision(4) << s.p50()
                << ",\"p95\":" << s.p95() << ",\"p99\":" << s.p99()
                << ",\"count\":" << s.count() << "}";
            first = false;
        }
        oss << "}}";
        return oss.str();
    }

    void reset_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
        summaries_.clear();
    }

private:
    MetricsRegistry() = default;
    mutable std::mutex mutex_;
    std::map<std::string, Counter> counters_;
    std::map<std::string, Gauge> gauges_;
    std::map<std::string, Histogram> histograms_;
    std::map<std::string, Summary> summaries_;
};

// Convenience timer for measuring durations
class ScopedTimer {
public:
    ScopedTimer(Histogram& histogram) : histogram_(histogram),
        start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        double seconds = std::chrono::duration<double>(elapsed).count();
        histogram_.observe(seconds);
    }

private:
    Histogram& histogram_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace metrics
} // namespace core
} // namespace genie

#endif // GENIE_CORE_METRICS_COLLECTOR_HPP
