/**
 * @file telemetry.hpp
 * @brief Prometheus-compatible metrics export and structured telemetry
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides structured metrics collection and export in Prometheus
 * exposition format. Supports counters, gauges, histograms, and summaries.
 * Zero external dependencies. Thread-safe. Cross-platform.
 *
 * Metrics are exposed via a text endpoint (e.g., /metrics) for scraping
 * by Prometheus or any compatible monitoring system.
 */
#pragma once
#ifndef GENIE_TELEMETRY_HPP
#define GENIE_TELEMETRY_HPP

#include <string>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>

namespace genie {
namespace telemetry {

// ============================================================================
// Label set (key-value pairs for metric dimensions)
// ============================================================================

using Labels = std::unordered_map<std::string, std::string>;

inline std::string labels_to_string(const Labels& labels) {
    if (labels.empty()) return "";
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [k, v] : labels) {
        if (!first) oss << ",";
        oss << k << "=\"" << v << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// Counter - monotonically increasing value
// ============================================================================

class Counter {
public:
    Counter() = default;

    void increment(double amount = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += amount;
    }

    double value() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = 0;
    }

private:
    mutable std::mutex mutex_;
    double value_{0};
};

// ============================================================================
// Gauge - value that can go up and down
// ============================================================================

class Gauge {
public:
    Gauge() = default;

    void set(double val) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = val;
    }

    void increment(double amount = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ += amount;
    }

    void decrement(double amount = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ -= amount;
    }

    double value() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

private:
    mutable std::mutex mutex_;
    double value_{0};
};

// ============================================================================
// Histogram - distribution of values across buckets
// ============================================================================

class Histogram {
public:
    // Default buckets suitable for HTTP request latencies (seconds)
    Histogram(std::vector<double> buckets = {
        0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0
    }) : bucket_bounds_(std::move(buckets)) {
        std::sort(bucket_bounds_.begin(), bucket_bounds_.end());
        bucket_counts_.resize(bucket_bounds_.size() + 1, 0); // +1 for +Inf
    }

    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        sum_ += value;
        count_++;
        for (size_t i = 0; i < bucket_bounds_.size(); ++i) {
            if (value <= bucket_bounds_[i]) {
                bucket_counts_[i]++;
            }
        }
        bucket_counts_.back()++; // +Inf always incremented
    }

    double sum() const { std::lock_guard<std::mutex> l(mutex_); return sum_; }
    uint64_t count() const { std::lock_guard<std::mutex> l(mutex_); return count_; }

    struct BucketInfo {
        double      upper_bound;
        uint64_t    cumulative_count;
    };

    std::vector<BucketInfo> buckets() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BucketInfo> result;
        uint64_t cumulative = 0;
        for (size_t i = 0; i < bucket_bounds_.size(); ++i) {
            cumulative += bucket_counts_[i];
            result.push_back({bucket_bounds_[i], cumulative});
        }
        result.push_back({std::numeric_limits<double>::infinity(), count_});
        return result;
    }

private:
    mutable std::mutex          mutex_;
    std::vector<double>         bucket_bounds_;
    std::vector<uint64_t>       bucket_counts_;
    double                      sum_{0};
    uint64_t                    count_{0};
};

// ============================================================================
// Timer - convenience wrapper for measuring durations
// ============================================================================

class Timer {
public:
    explicit Timer(Histogram& histogram)
        : histogram_(histogram)
        , start_(std::chrono::steady_clock::now()) {}

    ~Timer() {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_).count();
        histogram_.observe(elapsed);
    }

    double elapsed_seconds() const {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_).count();
    }

private:
    Histogram&                              histogram_;
    std::chrono::steady_clock::time_point   start_;
};

// ============================================================================
// Metric Family - named collection of metrics with labels
// ============================================================================

enum class MetricType {
    COUNTER,
    GAUGE,
    HISTOGRAM,
    UNTYPED
};

inline const char* metric_type_name(MetricType t) {
    switch (t) {
        case MetricType::COUNTER:   return "counter";
        case MetricType::GAUGE:     return "gauge";
        case MetricType::HISTOGRAM: return "histogram";
        default:                    return "untyped";
    }
}

// ============================================================================
// Metrics Registry
// ============================================================================

class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry reg;
        return reg;
    }

    // Register and access counters
    Counter& counter(const std::string& name, const std::string& help = "",
                    const Labels& labels = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = make_key(name, labels);
        register_meta(name, help, MetricType::COUNTER);
        auto it = counters_.find(key);
        if (it == counters_.end()) {
            auto [inserted, success] = counters_.try_emplace(
                key, name, labels);
            return inserted->second.counter;
        }
        return it->second.counter;
    }

    // Register and access gauges
    Gauge& gauge(const std::string& name, const std::string& help = "",
                const Labels& labels = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = make_key(name, labels);
        register_meta(name, help, MetricType::GAUGE);
        auto it = gauges_.find(key);
        if (it == gauges_.end()) {
            auto [inserted, success] = gauges_.try_emplace(
                key, name, labels);
            return inserted->second.gauge;
        }
        return it->second.gauge;
    }

    // Register and access histograms
    Histogram& histogram(const std::string& name, const std::string& help = "",
                         const Labels& labels = {},
                         std::vector<double> buckets = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = make_key(name, labels);
        register_meta(name, help, MetricType::HISTOGRAM);
        auto it = histograms_.find(key);
        if (it == histograms_.end()) {
            auto [inserted, success] = histograms_.try_emplace(
                key, std::move(buckets), name, labels);
            return inserted->second.histogram;
        }
        return it->second.histogram;
    }

    // Export all metrics in Prometheus exposition format
    std::string export_text() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);

        // Export counters
        for (const auto& [_, entry] : counters_) {
            write_type_help(oss, entry.name, MetricType::COUNTER);
            oss << entry.name << labels_to_string(entry.labels)
                << " " << entry.counter.value() << "\n";
        }

        // Export gauges
        for (const auto& [_, entry] : gauges_) {
            write_type_help(oss, entry.name, MetricType::GAUGE);
            oss << entry.name << labels_to_string(entry.labels)
                << " " << entry.gauge.value() << "\n";
        }

        // Export histograms
        for (const auto& [_, entry] : histograms_) {
            write_type_help(oss, entry.name, MetricType::HISTOGRAM);
            auto lbl = labels_to_string(entry.labels);
            auto bkts = entry.histogram.buckets();
            for (const auto& b : bkts) {
                oss << entry.name << "_bucket{le=\"";
                if (std::isinf(b.upper_bound)) {
                    oss << "+Inf";
                } else {
                    oss << b.upper_bound;
                }
                oss << "\"";
                if (!entry.labels.empty()) {
                    for (const auto& [k, v] : entry.labels) {
                        oss << "," << k << "=\"" << v << "\"";
                    }
                }
                oss << "} " << b.cumulative_count << "\n";
            }
            oss << entry.name << "_sum" << lbl
                << " " << entry.histogram.sum() << "\n";
            oss << entry.name << "_count" << lbl
                << " " << entry.histogram.count() << "\n";
        }

        return oss.str();
    }

    // Export as JSON (alternative format)
    std::string export_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\"counters\":{";
        bool first = true;
        for (const auto& [key, entry] : counters_) {
            if (!first) oss << ",";
            oss << "\"" << key << "\":" << entry.counter.value();
            first = false;
        }
        oss << "},\"gauges\":{";
        first = true;
        for (const auto& [key, entry] : gauges_) {
            if (!first) oss << ",";
            oss << "\"" << key << "\":" << entry.gauge.value();
            first = false;
        }
        oss << "},\"histograms\":{";
        first = true;
        for (const auto& [key, entry] : histograms_) {
            if (!first) oss << ",";
            oss << "\"" << key << "\":{\"count\":" << entry.histogram.count()
                << ",\"sum\":" << entry.histogram.sum() << "}";
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
        meta_.clear();
    }

private:
    MetricsRegistry() = default;

    struct MetricMeta {
        std::string help;
        MetricType  type;
        bool        header_written{false};
    };

    struct CounterEntry {
        Counter         counter;
        std::string     name;
        Labels          labels;

        CounterEntry(std::string n, Labels l)
            : counter(), name(std::move(n)), labels(std::move(l)) {}
    };

    struct GaugeEntry {
        Gauge           gauge;
        std::string     name;
        Labels          labels;

        GaugeEntry(std::string n, Labels l)
            : gauge(), name(std::move(n)), labels(std::move(l)) {}
    };

    struct HistogramEntry {
        Histogram       histogram;
        std::string     name;
        Labels          labels;

        HistogramEntry(std::vector<double> buckets, std::string n, Labels l)
            : histogram(std::move(buckets)), name(std::move(n)), labels(std::move(l)) {}
    };

    std::string make_key(const std::string& name, const Labels& labels) const {
        std::string key = name;
        for (const auto& [k, v] : labels) {
            key += ";" + k + "=" + v;
        }
        return key;
    }

    void register_meta(const std::string& name, const std::string& help,
                       MetricType type) {
        if (meta_.find(name) == meta_.end() && !help.empty()) {
            meta_[name] = {help, type, false};
        }
    }

    void write_type_help(std::ostringstream& oss, const std::string& name,
                         MetricType type) const {
        auto it = meta_.find(name);
        if (it != meta_.end() && !it->second.header_written) {
            if (!it->second.help.empty()) {
                oss << "# HELP " << name << " " << it->second.help << "\n";
            }
            oss << "# TYPE " << name << " "
                << metric_type_name(type) << "\n";
            // Mark as written (mutable in const context via logical constness)
            const_cast<MetricMeta&>(it->second).header_written = true;
        }
    }

    mutable std::mutex                                  mutex_;
    std::unordered_map<std::string, CounterEntry>       counters_;
    std::unordered_map<std::string, GaugeEntry>         gauges_;
    std::unordered_map<std::string, HistogramEntry>     histograms_;
    std::unordered_map<std::string, MetricMeta>         meta_;
};

// ============================================================================
// Convenience macros and helpers
// ============================================================================

// Increment a named counter
inline void metric_inc(const std::string& name, double amount = 1.0) {
    MetricsRegistry::instance().counter(name).increment(amount);
}

// Set a gauge value
inline void metric_set(const std::string& name, double value) {
    MetricsRegistry::instance().gauge(name).set(value);
}

// Observe a histogram value
inline void metric_observe(const std::string& name, double value) {
    MetricsRegistry::instance().histogram(name).observe(value);
}

} // namespace telemetry
} // namespace genie

#endif // GENIE_TELEMETRY_HPP
