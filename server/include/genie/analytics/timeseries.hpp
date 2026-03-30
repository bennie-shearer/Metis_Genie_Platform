/**
 * @file timeseries.hpp
 * @brief Time series analysis for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_TIMESERIES_HPP
#define GENIE_TIMESERIES_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace genie {
namespace timeseries {

// Simple Moving Average
[[nodiscard]] inline std::vector<double> sma(const std::vector<double>& data, int period) {
    std::vector<double> result;
    if (data.size() < static_cast<size_t>(period)) return result;
    
    double sum = 0;
    for (int i = 0; i < period; ++i) sum += data[i];
    result.push_back(sum / period);
    
    for (size_t i = period; i < data.size(); ++i) {
        sum = sum - data[i - period] + data[i];
        result.push_back(sum / period);
    }
    return result;
}

// Exponential Moving Average
[[nodiscard]] inline std::vector<double> ema(const std::vector<double>& data, int period) {
    std::vector<double> result;
    if (data.empty()) return result;
    
    double multiplier = 2.0 / (period + 1);
    result.push_back(data[0]);
    
    for (size_t i = 1; i < data.size(); ++i) {
        double val = (data[i] - result.back()) * multiplier + result.back();
        result.push_back(val);
    }
    return result;
}

// Weighted Moving Average
[[nodiscard]] inline std::vector<double> wma(const std::vector<double>& data, int period) {
    std::vector<double> result;
    if (data.size() < static_cast<size_t>(period)) return result;
    
    double weight_sum = period * (period + 1) / 2.0;
    
    for (size_t i = period - 1; i < data.size(); ++i) {
        double weighted_sum = 0;
        for (int j = 0; j < period; ++j) {
            weighted_sum += data[i - period + 1 + j] * (j + 1);
        }
        result.push_back(weighted_sum / weight_sum);
    }
    return result;
}

// Bollinger Bands
struct BollingerBands {
    std::vector<double> upper;
    std::vector<double> middle;
    std::vector<double> lower;
    std::vector<double> bandwidth;
};

[[nodiscard]] inline BollingerBands bollinger(const std::vector<double>& data, int period = 20, double num_std = 2.0) {
    BollingerBands bb;
    bb.middle = sma(data, period);
    
    for (size_t i = 0; i < bb.middle.size(); ++i) {
        double sum_sq = 0;
        for (int j = 0; j < period; ++j) {
            double diff = data[i + j] - bb.middle[i];
            sum_sq += diff * diff;
        }
        double std_dev = std::sqrt(sum_sq / period);
        bb.upper.push_back(bb.middle[i] + num_std * std_dev);
        bb.lower.push_back(bb.middle[i] - num_std * std_dev);
        bb.bandwidth.push_back((bb.upper.back() - bb.lower.back()) / bb.middle[i]);
    }
    return bb;
}

// Relative Strength Index (RSI)
[[nodiscard]] inline std::vector<double> rsi(const std::vector<double>& data, int period = 14) {
    std::vector<double> result;
    if (data.size() < static_cast<size_t>(period + 1)) return result;
    
    std::vector<double> gains, losses;
    for (size_t i = 1; i < data.size(); ++i) {
        double change = data[i] - data[i - 1];
        gains.push_back(change > 0 ? change : 0);
        losses.push_back(change < 0 ? -change : 0);
    }
    
    // Initial average
    double avg_gain = 0, avg_loss = 0;
    for (int i = 0; i < period; ++i) {
        avg_gain += gains[i];
        avg_loss += losses[i];
    }
    avg_gain /= period;
    avg_loss /= period;
    
    double rs = (avg_loss > 0.0001) ? avg_gain / avg_loss : 100;
    result.push_back(100 - (100 / (1 + rs)));
    
    // Subsequent values using smoothing
    for (size_t i = period; i < gains.size(); ++i) {
        avg_gain = (avg_gain * (period - 1) + gains[i]) / period;
        avg_loss = (avg_loss * (period - 1) + losses[i]) / period;
        rs = (avg_loss > 0.0001) ? avg_gain / avg_loss : 100;
        result.push_back(100 - (100 / (1 + rs)));
    }
    return result;
}

// MACD (Moving Average Convergence Divergence)
struct MACD {
    std::vector<double> macd_line;
    std::vector<double> signal_line;
    std::vector<double> histogram;
};

[[nodiscard]] inline MACD macd(const std::vector<double>& data, int fast = 12, int slow = 26, int signal = 9) {
    MACD result;
    auto fast_ema = ema(data, fast);
    auto slow_ema = ema(data, slow);
    
    size_t start = slow - 1;
    for (size_t i = start; i < data.size(); ++i) {
        result.macd_line.push_back(fast_ema[i] - slow_ema[i]);
    }
    
    result.signal_line = ema(result.macd_line, signal);
    
    for (size_t i = 0; i < result.signal_line.size(); ++i) {
        size_t macd_idx = result.macd_line.size() - result.signal_line.size() + i;
        result.histogram.push_back(result.macd_line[macd_idx] - result.signal_line[i]);
    }
    
    return result;
}

// Trend Detection
enum class Trend { StrongUp, Up, Neutral, Down, StrongDown };

[[nodiscard]] inline Trend detect_trend(const std::vector<double>& data, int lookback = 20) {
    if (data.size() < static_cast<size_t>(lookback)) return Trend::Neutral;
    
    // Linear regression slope
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    size_t start = data.size() - lookback;
    
    for (int i = 0; i < lookback; ++i) {
        sum_x += i;
        sum_y += data[start + i];
        sum_xy += i * data[start + i];
        sum_x2 += i * i;
    }
    
    double slope = (lookback * sum_xy - sum_x * sum_y) / (lookback * sum_x2 - sum_x * sum_x);
    double avg_price = sum_y / lookback;
    double normalized_slope = slope / avg_price * 100;  // As percentage
    
    if (normalized_slope > 0.5) return Trend::StrongUp;
    if (normalized_slope > 0.1) return Trend::Up;
    if (normalized_slope < -0.5) return Trend::StrongDown;
    if (normalized_slope < -0.1) return Trend::Down;
    return Trend::Neutral;
}

[[nodiscard]] inline std::string trend_string(Trend t) {
    switch (t) {
        case Trend::StrongUp: return "Strong Uptrend";
        case Trend::Up: return "Uptrend";
        case Trend::Neutral: return "Neutral";
        case Trend::Down: return "Downtrend";
        case Trend::StrongDown: return "Strong Downtrend";
        default: return "Unknown";
    }
}

// Autocorrelation
[[nodiscard]] inline std::vector<double> autocorrelation(const std::vector<double>& data, int max_lag = 20) {
    std::vector<double> result;
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    
    double var = 0;
    for (double v : data) var += (v - mean) * (v - mean);
    
    for (int lag = 0; lag <= max_lag && lag < static_cast<int>(data.size()); ++lag) {
        double covar = 0;
        for (size_t i = 0; i < data.size() - lag; ++i) {
            covar += (data[i] - mean) * (data[i + lag] - mean);
        }
        result.push_back(covar / var);
    }
    return result;
}

// Volatility (rolling standard deviation)
[[nodiscard]] inline std::vector<double> rolling_volatility(const std::vector<double>& returns, int window = 20) {
    std::vector<double> result;
    if (returns.size() < static_cast<size_t>(window)) return result;
    
    for (size_t i = window - 1; i < returns.size(); ++i) {
        double sum = 0, sum_sq = 0;
        for (int j = 0; j < window; ++j) {
            sum += returns[i - j];
            sum_sq += returns[i - j] * returns[i - j];
        }
        double mean = sum / window;
        double variance = (sum_sq / window) - (mean * mean);
        result.push_back(std::sqrt(variance) * std::sqrt(252.0));  // Annualized
    }
    return result;
}

// Drawdown series
[[nodiscard]] inline std::vector<double> drawdown_series(const std::vector<double>& prices) {
    std::vector<double> result;
    double peak = prices[0];
    
    for (double p : prices) {
        peak = std::max(peak, p);
        double dd = (peak - p) / peak;
        result.push_back(dd);
    }
    return result;
}

// Returns from prices
[[nodiscard]] inline std::vector<double> returns_from_prices(const std::vector<double>& prices) {
    std::vector<double> result;
    for (size_t i = 1; i < prices.size(); ++i) {
        result.push_back((prices[i] - prices[i - 1]) / prices[i - 1]);
    }
    return result;
}

// Log returns
[[nodiscard]] inline std::vector<double> log_returns(const std::vector<double>& prices) {
    std::vector<double> result;
    for (size_t i = 1; i < prices.size(); ++i) {
        result.push_back(std::log(prices[i] / prices[i - 1]));
    }
    return result;
}

} // namespace timeseries
} // namespace genie
#endif // GENIE_TIMESERIES_HPP
