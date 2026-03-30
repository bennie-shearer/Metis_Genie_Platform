/**
 * @file metrics.hpp
 * @brief Prometheus-style metrics collection
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements observability metrics:
 * - Counters (monotonically increasing)
 * - Gauges (arbitrary values)
 * - Histograms (distributions)
 * - Timer helpers
 * - Prometheus text format export
 */

#pragma once
#ifndef GENIE_CORE_METRICS_HPP
#define GENIE_CORE_METRICS_HPP

#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace genie {

/**
 * @brief Label set for metrics
 */
using Labels = std::map<std::string, std::string>;

/**
 * @brief Format labels for Prometheus
 */
inline std::string format_labels(const Labels& labels) {
    if (labels.empty()) return "";
    
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : labels) {
        if (!first) oss << ",";
        oss << key << "=\"" << value << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

/**
 * @brief Metrics collection singleton
 */
class Metrics {
public:
    static Metrics& instance() {
        static Metrics m;
        return m;
    }

    /**
     * @brief Increment a counter
     */
    void increment(const std::string& name, const Labels& labels = {}) {
        add(name, 1.0, labels);
    }

    /**
     * @brief Add to a counter
     */
    void add(const std::string& name, double value, const Labels& labels = {}) {
        std::lock_guard lock(mutex_);
        
        std::string key = name + format_labels(labels);
        counters_[key].value += value;
        counters_[key].name = name;
        counters_[key].labels = labels;
    }

    /**
     * @brief Set a gauge value
     */
    void gauge(const std::string& name, double value, const Labels& labels = {}) {
        std::lock_guard lock(mutex_);
        
        std::string key = name + format_labels(labels);
        gauges_[key].value = value;
        gauges_[key].name = name;
        gauges_[key].labels = labels;
    }

    /**
     * @brief Record a histogram observation
     */
    void histogram(
        const std::string& name,
        double value,
        const std::vector<double>& buckets = default_buckets(),
        const Labels& labels = {}
    ) {
        std::lock_guard lock(mutex_);
        
        std::string key = name + format_labels(labels);
        auto& h = histograms_[key];
        h.name = name;
        h.labels = labels;
        
        if (h.buckets.empty()) {
            h.buckets = buckets;
            h.counts.resize(buckets.size() + 1, 0);  // +1 for +Inf
        }
        
        h.sum += value;
        ++h.count;
        
        for (size_t i = 0; i < h.buckets.size(); ++i) {
            if (value <= h.buckets[i]) {
                ++h.counts[i];
            }
        }
        ++h.counts.back();  // +Inf bucket
    }

    /**
     * @brief Default histogram buckets
     */
    static std::vector<double> default_buckets() {
        return {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    }

    /**
     * @brief Timer for automatic duration recording
     */
    class Timer {
        Metrics& metrics_;
        std::string name_;
        Labels labels_;
        std::vector<double> buckets_;
        std::chrono::steady_clock::time_point start_;
        
    public:
        Timer(
            const std::string& name,
            const Labels& labels = {},
            const std::vector<double>& buckets = default_buckets()
        ) : metrics_(Metrics::instance()),
            name_(name),
            labels_(labels),
            buckets_(buckets),
            start_(std::chrono::steady_clock::now()) {}
        
        ~Timer() {
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration<double>(end - start_).count();
            metrics_.histogram(name_, duration, buckets_, labels_);
        }
        
        // Non-copyable
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
    };

    /**
     * @brief Export in Prometheus text format
     */
    [[nodiscard]] std::string to_prometheus_format() const {
        std::lock_guard lock(mutex_);
        std::ostringstream oss;
        
        // Counters
        for (const auto& [key, counter] : counters_) {
            oss << "# HELP " << counter.name << " Counter\n";
            oss << "# TYPE " << counter.name << " counter\n";
            oss << counter.name << format_labels(counter.labels) 
                << " " << counter.value << "\n";
        }
        
        // Gauges
        for (const auto& [key, gauge] : gauges_) {
            oss << "# HELP " << gauge.name << " Gauge\n";
            oss << "# TYPE " << gauge.name << " gauge\n";
            oss << gauge.name << format_labels(gauge.labels)
                << " " << gauge.value << "\n";
        }
        
        // Histograms
        for (const auto& [key, hist] : histograms_) {
            oss << "# HELP " << hist.name << " Histogram\n";
            oss << "# TYPE " << hist.name << " histogram\n";
            
            for (size_t i = 0; i < hist.buckets.size(); ++i) {
                Labels bucket_labels = hist.labels;
                std::ostringstream le;
                le << hist.buckets[i];
                bucket_labels["le"] = le.str();
                oss << hist.name << "_bucket" << format_labels(bucket_labels)
                    << " " << hist.counts[i] << "\n";
            }
            
            Labels inf_labels = hist.labels;
            inf_labels["le"] = "+Inf";
            oss << hist.name << "_bucket" << format_labels(inf_labels)
                << " " << hist.counts.back() << "\n";
            
            oss << hist.name << "_sum" << format_labels(hist.labels)
                << " " << hist.sum << "\n";
            oss << hist.name << "_count" << format_labels(hist.labels)
                << " " << hist.count << "\n";
        }
        
        return oss.str();
    }

    /**
     * @brief Reset all metrics
     */
    void reset() {
        std::lock_guard lock(mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
    }

    /**
     * @brief Get counter value
     */
    [[nodiscard]] double get_counter(const std::string& name, const Labels& labels = {}) const {
        std::lock_guard lock(mutex_);
        std::string key = name + format_labels(labels);
        auto it = counters_.find(key);
        return it != counters_.end() ? it->second.value : 0.0;
    }

    /**
     * @brief Get gauge value
     */
    [[nodiscard]] double get_gauge(const std::string& name, const Labels& labels = {}) const {
        std::lock_guard lock(mutex_);
        std::string key = name + format_labels(labels);
        auto it = gauges_.find(key);
        return it != gauges_.end() ? it->second.value : 0.0;
    }

private:
    Metrics() = default;
    
    struct Counter {
        std::string name;
        Labels labels;
        double value{0};
    };
    
    struct Gauge {
        std::string name;
        Labels labels;
        double value{0};
    };
    
    struct Histogram {
        std::string name;
        Labels labels;
        std::vector<double> buckets;
        std::vector<size_t> counts;
        double sum{0};
        size_t count{0};
    };
    
    mutable std::mutex mutex_;
    std::map<std::string, Counter> counters_;
    std::map<std::string, Gauge> gauges_;
    std::map<std::string, Histogram> histograms_;
};

/**
 * @brief Convenience macros for metrics
 */
#define METRIC_INCREMENT(name) genie::Metrics::instance().increment(name)
#define METRIC_ADD(name, value) genie::Metrics::instance().add(name, value)
#define METRIC_GAUGE(name, value) genie::Metrics::instance().gauge(name, value)
#define METRIC_HISTOGRAM(name, value) genie::Metrics::instance().histogram(name, value)
#define METRIC_TIMER(name) genie::Metrics::Timer _timer_##__LINE__(name)

} // namespace genie

#endif // GENIE_CORE_METRICS_HPP
