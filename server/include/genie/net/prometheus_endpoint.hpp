/**
 * @file prometheus_endpoint.hpp
 * @brief Prometheus-compatible /metrics endpoint (text exposition format)
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Exposes server metrics in the Prometheus text format (version 0.0.4).
 * Scrapeable by Prometheus, Grafana Agent, Victoria Metrics, and any
 * OpenMetrics-compatible scraper. No external library required.
 *
 * Endpoint: GET /metrics   (no auth -- like /api/v1/health)
 *           GET /api/v1/metrics/prometheus  (same, auth optional)
 *
 * Format example:
 *   # HELP http_requests_total Total HTTP requests served
 *   # TYPE http_requests_total counter
 *   http_requests_total{method="GET",status="200"} 48291 1706000000000
 *
 * config.pson:
 *   "prometheus": {
 *       "enabled": true,
 *       "path": "/metrics",
 *       "include_go_metrics": false,
 *       "namespace": "metis_genie"
 *   }
 *
 * Zero external dependencies. Cross-platform: Windows/Linux/macOS.
 */
#pragma once
#ifndef GENIE_NET_PROMETHEUS_ENDPOINT_HPP
#define GENIE_NET_PROMETHEUS_ENDPOINT_HPP

#include <string>
#include <sstream>
#include <chrono>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <iomanip>

namespace genie::net {

// ============================================================================
// Prometheus metric types
// ============================================================================

enum class PrometheusType { COUNTER, GAUGE, HISTOGRAM, SUMMARY };

struct PrometheusMetric {
    std::string name;
    std::string help;
    PrometheusType type;
    std::map<std::string, std::string> labels; // label_name -> label_value
    double value = 0.0;
    int64_t timestamp_ms = 0; // 0 = omit
};

// ============================================================================
// Prometheus registry -- thread-safe metric accumulator
// ============================================================================

class PrometheusRegistry {
public:
    static PrometheusRegistry& instance() {
        static PrometheusRegistry inst;
        return inst;
    }

    void configure(bool enabled, const std::string& ns) {
        enabled_   = enabled;
        namespace_ = ns;
    }

    bool is_enabled() const { return enabled_; }

    /** Register or update a counter (monotonically increasing) */
    void counter(const std::string& name, const std::string& help,
                 double increment = 1.0,
                 const std::map<std::string, std::string>& labels = {}) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto key = make_key(name, labels);
        auto& m = metrics_[key];
        m.name   = namespace_.empty() ? name : namespace_ + "_" + name;
        m.help   = help;
        m.type   = PrometheusType::COUNTER;
        m.labels = labels;
        m.value += increment;
        m.timestamp_ms = now_ms();
    }

    /** Register or update a gauge (arbitrary value) */
    void gauge(const std::string& name, const std::string& help,
               double value,
               const std::map<std::string, std::string>& labels = {}) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto key = make_key(name, labels);
        auto& m = metrics_[key];
        m.name   = namespace_.empty() ? name : namespace_ + "_" + name;
        m.help   = help;
        m.type   = PrometheusType::GAUGE;
        m.labels = labels;
        m.value  = value;
        m.timestamp_ms = now_ms();
    }

    /** Generate Prometheus text format (version 0.0.4) */
    [[nodiscard]] std::string text_exposition() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::ostringstream oss;

        // Track which (name, type) combos we've written HELP/TYPE for
        std::map<std::string, bool> seen;

        for (const auto& [key, m] : metrics_) {
            std::string base = m.name;
            if (!seen[base]) {
                seen[base] = true;
                oss << "# HELP " << base << " " << m.help << "\n";
                oss << "# TYPE " << base << " " << type_name(m.type) << "\n";
            }
            oss << base;
            if (!m.labels.empty()) {
                oss << "{";
                bool first = true;
                for (const auto& [k, v] : m.labels) {
                    if (!first) oss << ",";
                    oss << k << "=\"" << escape_label(v) << "\"";
                    first = false;
                }
                oss << "}";
            }
            oss << " " << std::scientific << std::setprecision(6) << m.value;
            if (m.timestamp_ms > 0) oss << " " << m.timestamp_ms;
            oss << "\n";
        }
        return oss.str();
    }

    /** Generate built-in server metrics snapshot */
    void record_server_metrics(int64_t total_requests, int64_t active_conns,
                                int64_t bytes_sent, int64_t bytes_recv,
                                int64_t errors_4xx, int64_t errors_5xx,
                                double uptime_sec, size_t cache_hits,
                                size_t cache_misses) {
        gauge("http_active_connections",
              "Number of currently active HTTP connections", static_cast<double>(active_conns));
        counter("http_requests_total",
                "Total HTTP requests received since startup",
                0.0); // no increment -- set absolute
        // Set absolute counter value directly
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto key = make_key("http_requests_total", {});
            auto& m = metrics_[key];
            m.name  = (namespace_.empty() ? "http" : namespace_) + "_requests_total";
            m.help  = "Total HTTP requests";
            m.type  = PrometheusType::COUNTER;
            m.value = static_cast<double>(total_requests);
            m.timestamp_ms = now_ms();
        }
        gauge("http_errors_4xx_total",  "Total 4xx responses", static_cast<double>(errors_4xx));
        gauge("http_errors_5xx_total",  "Total 5xx responses", static_cast<double>(errors_5xx));
        gauge("http_bytes_sent_total",  "Total bytes sent",    static_cast<double>(bytes_sent));
        gauge("http_bytes_recv_total",  "Total bytes received",static_cast<double>(bytes_recv));
        gauge("process_uptime_seconds", "Server uptime in seconds", uptime_sec);
        gauge("cache_hits_total",   "Response cache hits",   static_cast<double>(cache_hits));
        gauge("cache_misses_total", "Response cache misses", static_cast<double>(cache_misses));
        double cache_rate = (cache_hits + cache_misses > 0)
            ? (100.0 * static_cast<double>(cache_hits) / static_cast<double>(cache_hits + cache_misses)) : 0.0;
        gauge("cache_hit_rate_pct", "Response cache hit rate percentage", cache_rate);
    }

private:
    PrometheusRegistry() = default;
    PrometheusRegistry(const PrometheusRegistry&) = delete;
    PrometheusRegistry& operator=(const PrometheusRegistry&) = delete;

    bool        enabled_{true};
    std::string namespace_{"metis_genie"};
    mutable std::mutex mtx_;
    std::map<std::string, PrometheusMetric> metrics_;

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static std::string make_key(const std::string& name,
                                 const std::map<std::string, std::string>& labels) {
        std::string k = name;
        for (const auto& [lk, lv] : labels) k += "|" + lk + "=" + lv;
        return k;
    }

    static std::string type_name(PrometheusType t) {
        switch (t) {
            case PrometheusType::COUNTER:   return "counter";
            case PrometheusType::GAUGE:     return "gauge";
            case PrometheusType::HISTOGRAM: return "histogram";
            case PrometheusType::SUMMARY:   return "summary";
        }
        return "untyped";
    }

    static std::string escape_label(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    }
};

} // namespace genie::net

#endif // GENIE_NET_PROMETHEUS_ENDPOINT_HPP
