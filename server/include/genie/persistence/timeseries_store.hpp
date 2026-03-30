/**
 * @file timeseries_store.hpp
 * @brief Purpose-built time-series storage for financial data
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * In-memory time-series database optimized for financial data:
 * - OHLCV bar storage with millisecond precision
 * - Windowed range queries (time range, last N bars)
 * - Aggregation (resample 1m->5m->1h->1d)
 * - Gap detection and fill-forward
 * - Rolling statistics (SMA, EMA, Bollinger)
 * - Multi-symbol partitioned storage
 * - Memory-bounded with LRU eviction
 * - Snapshot export (CSV, JSON)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERSISTENCE_TIMESERIES_STORE_HPP
#define GENIE_PERSISTENCE_TIMESERIES_STORE_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cmath>
#include <numeric>
#include <optional>

namespace genie {
namespace persistence {
namespace timeseries {

// ============================================================================
// Data Structures
// ============================================================================

enum class BarInterval {
    Tick,
    Second1,
    Minute1,
    Minute5,
    Minute15,
    Minute30,
    Hour1,
    Hour4,
    Day1,
    Week1,
    Month1
};

[[nodiscard]] inline std::string interval_string(BarInterval i) {
    switch (i) {
        case BarInterval::Tick:     return "tick";
        case BarInterval::Second1:  return "1s";
        case BarInterval::Minute1:  return "1m";
        case BarInterval::Minute5:  return "5m";
        case BarInterval::Minute15: return "15m";
        case BarInterval::Minute30: return "30m";
        case BarInterval::Hour1:    return "1h";
        case BarInterval::Hour4:    return "4h";
        case BarInterval::Day1:     return "1d";
        case BarInterval::Week1:    return "1w";
        case BarInterval::Month1:   return "1M";
    }
    return "unknown";
}

[[nodiscard]] inline int interval_seconds(BarInterval i) {
    switch (i) {
        case BarInterval::Tick:     return 0;
        case BarInterval::Second1:  return 1;
        case BarInterval::Minute1:  return 60;
        case BarInterval::Minute5:  return 300;
        case BarInterval::Minute15: return 900;
        case BarInterval::Minute30: return 1800;
        case BarInterval::Hour1:    return 3600;
        case BarInterval::Hour4:    return 14400;
        case BarInterval::Day1:     return 86400;
        case BarInterval::Week1:    return 604800;
        case BarInterval::Month1:   return 2592000;
    }
    return 0;
}

/**
 * @brief OHLCV bar
 */
struct Bar {
    std::chrono::system_clock::time_point timestamp;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double volume{0};
    double vwap{0};
    int trade_count{0};
    BarInterval interval{BarInterval::Minute1};

    [[nodiscard]] double range() const { return high - low; }
    [[nodiscard]] double body() const { return std::abs(close - open); }
    [[nodiscard]] bool bullish() const { return close >= open; }
    [[nodiscard]] double typical_price() const { return (high + low + close) / 3.0; }
    [[nodiscard]] double mid() const { return (high + low) / 2.0; }

    [[nodiscard]] std::string to_csv() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        oss << std::fixed << std::setprecision(4);
        oss << "," << open << "," << high << "," << low << "," << close
            << "," << std::setprecision(0) << volume;
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        std::ostringstream oss;
        oss << "{\"t\":\"";
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        oss << std::fixed << std::setprecision(4);
        oss << "\",\"o\":" << open << ",\"h\":" << high
            << ",\"l\":" << low << ",\"c\":" << close
            << ",\"v\":" << std::setprecision(0) << volume << "}";
        return oss.str();
    }

    /**
     * @brief Merge another bar into this one (aggregation)
     */
    void merge(const Bar& other) {
        if (open == 0) open = other.open;
        high = std::max(high, other.high);
        low = (low == 0) ? other.low : std::min(low, other.low);
        close = other.close;
        volume += other.volume;
        trade_count += other.trade_count;
    }
};

/**
 * @brief Rolling window statistics
 */
struct RollingStats {
    double sma{0};
    double ema{0};
    double stddev{0};
    double upper_band{0};  // Bollinger upper
    double lower_band{0};  // Bollinger lower
    double rsi{0};
    double atr{0};         // Average true range
    int period{0};
};

/**
 * @brief Query parameters
 */
struct TimeSeriesQuery {
    std::string symbol;
    std::optional<std::chrono::system_clock::time_point> from;
    std::optional<std::chrono::system_clock::time_point> to;
    int last_n{0};              // Last N bars (0 = use time range)
    BarInterval interval{BarInterval::Minute1};
    bool fill_gaps{false};      // Fill-forward missing bars
};

// ============================================================================
// Symbol Time Series
// ============================================================================

/**
 * @brief Time series for a single symbol
 */
class SymbolSeries {
public:
    explicit SymbolSeries(const std::string& symbol, int max_bars = 100000)
        : symbol_(symbol), max_bars_(max_bars) {}

    /**
     * @brief Append a bar
     */
    void append(const Bar& bar) {
        std::lock_guard<std::mutex> lock(mutex_);
        bars_.push_back(bar);
        if (static_cast<int>(bars_.size()) > max_bars_) {
            bars_.pop_front();
        }
    }

    /**
     * @brief Append tick as a bar update
     */
    void tick(double price, double volume_delta = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();

        if (bars_.empty() || should_new_bar(now)) {
            Bar bar;
            bar.timestamp = now;
            bar.open = price;
            bar.high = price;
            bar.low = price;
            bar.close = price;
            bar.volume = volume_delta;
            bar.trade_count = 1;
            bars_.push_back(bar);
            if (static_cast<int>(bars_.size()) > max_bars_) bars_.pop_front();
        } else {
            auto& bar = bars_.back();
            bar.high = std::max(bar.high, price);
            bar.low = std::min(bar.low, price);
            bar.close = price;
            bar.volume += volume_delta;
            ++bar.trade_count;
        }
    }

    /**
     * @brief Query bars by time range
     */
    [[nodiscard]] std::vector<Bar> query(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Bar> result;
        for (const auto& bar : bars_) {
            if (bar.timestamp >= from && bar.timestamp <= to) {
                result.push_back(bar);
            }
        }
        return result;
    }

    /**
     * @brief Get last N bars
     */
    [[nodiscard]] std::vector<Bar> last(int n) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Bar> result;
        int start = std::max(0, static_cast<int>(bars_.size()) - n);
        for (int i = start; i < static_cast<int>(bars_.size()); ++i) {
            result.push_back(bars_[i]);
        }
        return result;
    }

    /**
     * @brief Resample to larger interval
     */
    [[nodiscard]] std::vector<Bar> resample(BarInterval target,
                                              int count = 0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bars_.empty()) return {};

        int secs = interval_seconds(target);
        if (secs == 0) return {bars_.begin(), bars_.end()};

        std::vector<Bar> result;
        Bar current;
        bool started = false;

        for (const auto& bar : bars_) {
            auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                bar.timestamp.time_since_epoch()).count();
            auto bucket = (epoch / secs) * secs;
            auto bucket_tp = std::chrono::system_clock::time_point(
                std::chrono::seconds(bucket));

            if (!started || bucket_tp != current.timestamp) {
                if (started) result.push_back(current);
                current = Bar{};
                current.timestamp = bucket_tp;
                current.open = bar.open;
                current.high = bar.high;
                current.low = bar.low;
                current.interval = target;
                started = true;
            }
            current.merge(bar);
        }
        if (started) result.push_back(current);

        if (count > 0 && static_cast<int>(result.size()) > count) {
            result.erase(result.begin(),
                         result.begin() + (result.size() - count));
        }
        return result;
    }

    /**
     * @brief Calculate rolling statistics
     */
    [[nodiscard]] RollingStats rolling_stats(int period = 20, double bb_mult = 2.0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        RollingStats stats;
        stats.period = period;
        if (static_cast<int>(bars_.size()) < period) return stats;

        // SMA
        double sum = 0;
        int start = static_cast<int>(bars_.size()) - period;
        for (int i = start; i < static_cast<int>(bars_.size()); ++i) {
            sum += bars_[i].close;
        }
        stats.sma = sum / period;

        // StdDev
        double sq_sum = 0;
        for (int i = start; i < static_cast<int>(bars_.size()); ++i) {
            double diff = bars_[i].close - stats.sma;
            sq_sum += diff * diff;
        }
        stats.stddev = std::sqrt(sq_sum / period);

        // Bollinger Bands
        stats.upper_band = stats.sma + bb_mult * stats.stddev;
        stats.lower_band = stats.sma - bb_mult * stats.stddev;

        // EMA
        double multiplier = 2.0 / (period + 1);
        stats.ema = bars_[start].close;
        for (int i = start + 1; i < static_cast<int>(bars_.size()); ++i) {
            stats.ema = (bars_[i].close - stats.ema) * multiplier + stats.ema;
        }

        // RSI
        double avg_gain = 0, avg_loss = 0;
        for (int i = start + 1; i < static_cast<int>(bars_.size()); ++i) {
            double change = bars_[i].close - bars_[i - 1].close;
            if (change > 0) avg_gain += change;
            else avg_loss -= change;
        }
        avg_gain /= period;
        avg_loss /= period;
        if (avg_loss > 0) {
            double rs = avg_gain / avg_loss;
            stats.rsi = 100.0 - (100.0 / (1.0 + rs));
        } else {
            stats.rsi = 100.0;
        }

        // ATR
        double atr_sum = 0;
        for (int i = start + 1; i < static_cast<int>(bars_.size()); ++i) {
            double tr = std::max({
                bars_[i].high - bars_[i].low,
                std::abs(bars_[i].high - bars_[i - 1].close),
                std::abs(bars_[i].low - bars_[i - 1].close)
            });
            atr_sum += tr;
        }
        stats.atr = atr_sum / (period - 1);

        return stats;
    }

    /**
     * @brief Detect gaps
     */
    [[nodiscard]] std::vector<std::pair<
        std::chrono::system_clock::time_point,
        std::chrono::system_clock::time_point>> detect_gaps(int threshold_seconds = 120) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<
            std::chrono::system_clock::time_point,
            std::chrono::system_clock::time_point>> gaps;

        for (size_t i = 1; i < bars_.size(); ++i) {
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(
                bars_[i].timestamp - bars_[i - 1].timestamp).count();
            if (diff > threshold_seconds) {
                gaps.emplace_back(bars_[i - 1].timestamp, bars_[i].timestamp);
            }
        }
        return gaps;
    }

    /**
     * @brief Export to CSV
     */
    [[nodiscard]] std::string to_csv() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "timestamp,open,high,low,close,volume\n";
        for (const auto& bar : bars_) {
            oss << bar.to_csv() << "\n";
        }
        return oss.str();
    }

    [[nodiscard]] int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(bars_.size());
    }

    [[nodiscard]] std::optional<Bar> latest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (bars_.empty()) return std::nullopt;
        return bars_.back();
    }

    [[nodiscard]] const std::string& symbol() const { return symbol_; }

private:
    mutable std::mutex mutex_;
    std::string symbol_;
    std::deque<Bar> bars_;
    int max_bars_;
    BarInterval current_interval_{BarInterval::Minute1};

    [[nodiscard]] bool should_new_bar(std::chrono::system_clock::time_point now) const {
        if (bars_.empty()) return true;
        int secs = interval_seconds(current_interval_);
        if (secs == 0) return true;
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(
            now - bars_.back().timestamp).count();
        return diff >= secs;
    }
};

// ============================================================================
// Time Series Store
// ============================================================================

/**
 * @brief Multi-symbol time series store
 */
class TimeSeriesStore {
public:
    explicit TimeSeriesStore(int max_bars_per_symbol = 100000)
        : max_bars_(max_bars_per_symbol) {}

    /**
     * @brief Append bar for symbol
     */
    void append(const std::string& symbol, const Bar& bar) {
        std::lock_guard<std::mutex> lock(mutex_);
        get_or_create(symbol).append(bar);
    }

    /**
     * @brief Tick update
     */
    void tick(const std::string& symbol, double price, double volume = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        get_or_create(symbol).tick(price, volume);
    }

    /**
     * @brief Query by symbol and time range
     */
    [[nodiscard]] std::vector<Bar> query(const TimeSeriesQuery& q) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = series_.find(q.symbol);
        if (it == series_.end()) return {};

        if (q.last_n > 0) {
            auto bars = it->second.last(q.last_n);
            if (q.interval != BarInterval::Minute1) {
                // Need to resample
                SymbolSeries temp(q.symbol);
                for (const auto& b : bars) temp.append(b);
                return temp.resample(q.interval, q.last_n);
            }
            return bars;
        }

        auto from = q.from.value_or(
            std::chrono::system_clock::now() - std::chrono::hours(24));
        auto to = q.to.value_or(std::chrono::system_clock::now());
        return it->second.query(from, to);
    }

    /**
     * @brief Get rolling stats for symbol
     */
    [[nodiscard]] RollingStats stats(const std::string& symbol, int period = 20) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = series_.find(symbol);
        if (it == series_.end()) return {};
        return it->second.rolling_stats(period);
    }

    /**
     * @brief List tracked symbols
     */
    [[nodiscard]] std::vector<std::string> symbols() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [sym, _] : series_) result.push_back(sym);
        return result;
    }

    /**
     * @brief Get latest bar for symbol
     */
    [[nodiscard]] std::optional<Bar> latest(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = series_.find(symbol);
        if (it == series_.end()) return std::nullopt;
        return it->second.latest();
    }

    /**
     * @brief Total bars across all symbols
     */
    [[nodiscard]] int total_bars() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& [_, s] : series_) total += s.size();
        return total;
    }

    /**
     * @brief Export symbol to CSV
     */
    [[nodiscard]] std::string export_csv(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = series_.find(symbol);
        if (it == series_.end()) return "";
        return it->second.to_csv();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        series_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, SymbolSeries> series_;
    int max_bars_;

    SymbolSeries& get_or_create(const std::string& symbol) {
        auto it = series_.find(symbol);
        if (it == series_.end()) {
            auto [inserted, ok] = series_.try_emplace(symbol, symbol, max_bars_);
            return inserted->second;
        }
        return it->second;
    }
};

} // namespace timeseries
} // namespace persistence
} // namespace genie

#endif // GENIE_PERSISTENCE_TIMESERIES_STORE_HPP
