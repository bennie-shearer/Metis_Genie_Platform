/**
 * @file rolling_stats.hpp
 * @brief Rolling window statistics for time series analysis
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements rolling window calculations:
 * - Rolling mean, std, variance
 * - Rolling Sharpe, Sortino
 * - Rolling beta, correlation
 * - Rolling min, max
 * - Exponential moving averages
 */

#pragma once
#ifndef GENIE_ANALYTICS_ROLLING_STATS_HPP
#define GENIE_ANALYTICS_ROLLING_STATS_HPP

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <optional>
#include <vector>

namespace genie::analytics {

/**
 * @brief Rolling window statistics calculator
 */
class RollingStats {
public:
    /**
     * @brief Result of rolling calculations
     */
    struct RollingResult {
        std::vector<double> values;
        size_t window_size{0};
        size_t valid_count{0};  // Number of non-NaN values
    };

    /**
     * @brief Calculate rolling mean
     */
    [[nodiscard]] static RollingResult rolling_mean(
        const std::vector<double>& data,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < window || window == 0) return result;
        
        double sum = 0.0;
        for (size_t i = 0; i < window; ++i) {
            sum += data[i];
        }
        result.values[window - 1] = sum / window;
        result.valid_count = 1;
        
        for (size_t i = window; i < data.size(); ++i) {
            sum += data[i] - data[i - window];
            result.values[i] = sum / window;
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling standard deviation
     */
    [[nodiscard]] static RollingResult rolling_std(
        const std::vector<double>& data,
        size_t window,
        bool population = false  // Use N instead of N-1
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < window || window < 2) return result;
        
        for (size_t i = window - 1; i < data.size(); ++i) {
            double sum = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                sum += data[j];
            }
            double mean = sum / window;
            
            double sum_sq = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                double diff = data[j] - mean;
                sum_sq += diff * diff;
            }
            
            double divisor = population ? window : (window - 1);
            result.values[i] = std::sqrt(sum_sq / divisor);
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling variance
     */
    [[nodiscard]] static RollingResult rolling_variance(
        const std::vector<double>& data,
        size_t window,
        bool population = false
    ) {
        auto std_result = rolling_std(data, window, population);
        for (double& v : std_result.values) {
            if (!std::isnan(v)) v = v * v;
        }
        return std_result;
    }

    /**
     * @brief Calculate rolling Sharpe ratio
     */
    [[nodiscard]] static RollingResult rolling_sharpe(
        const std::vector<double>& returns,
        size_t window,
        double risk_free_rate = 0.0,  // Per period
        double annualization = 252.0
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(returns.size(), std::nan(""));
        
        if (returns.size() < window || window < 2) return result;
        
        for (size_t i = window - 1; i < returns.size(); ++i) {
            double sum = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                sum += returns[j];
            }
            double mean = sum / window;
            
            double sum_sq = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                double diff = returns[j] - mean;
                sum_sq += diff * diff;
            }
            double std_dev = std::sqrt(sum_sq / (window - 1));
            
            if (std_dev > 1e-10) {
                double ann_return = mean * annualization;
                double ann_std = std_dev * std::sqrt(annualization);
                result.values[i] = (ann_return - risk_free_rate) / ann_std;
            } else {
                result.values[i] = 0.0;
            }
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling Sortino ratio
     */
    [[nodiscard]] static RollingResult rolling_sortino(
        const std::vector<double>& returns,
        size_t window,
        double mar = 0.0,  // Minimum acceptable return per period
        double annualization = 252.0
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(returns.size(), std::nan(""));
        
        if (returns.size() < window || window < 2) return result;
        
        for (size_t i = window - 1; i < returns.size(); ++i) {
            double sum = 0.0;
            double sum_sq_downside = 0.0;
            
            for (size_t j = i - window + 1; j <= i; ++j) {
                sum += returns[j];
                if (returns[j] < mar) {
                    double diff = returns[j] - mar;
                    sum_sq_downside += diff * diff;
                }
            }
            
            double mean = sum / window;
            double downside_dev = std::sqrt(sum_sq_downside / window);
            
            if (downside_dev > 1e-10) {
                double ann_return = mean * annualization;
                double ann_downside = downside_dev * std::sqrt(annualization);
                result.values[i] = ann_return / ann_downside;
            } else {
                result.values[i] = std::numeric_limits<double>::infinity();
            }
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling beta
     */
    [[nodiscard]] static RollingResult rolling_beta(
        const std::vector<double>& asset_returns,
        const std::vector<double>& market_returns,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        
        size_t n = std::min(asset_returns.size(), market_returns.size());
        result.values.resize(n, std::nan(""));
        
        if (n < window || window < 2) return result;
        
        for (size_t i = window - 1; i < n; ++i) {
            double sum_a = 0.0, sum_m = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                sum_a += asset_returns[j];
                sum_m += market_returns[j];
            }
            double mean_a = sum_a / window;
            double mean_m = sum_m / window;
            
            double cov = 0.0, var_m = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                double da = asset_returns[j] - mean_a;
                double dm = market_returns[j] - mean_m;
                cov += da * dm;
                var_m += dm * dm;
            }
            
            if (var_m > 1e-10) {
                result.values[i] = cov / var_m;
            } else {
                result.values[i] = 1.0;
            }
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling correlation
     */
    [[nodiscard]] static RollingResult rolling_correlation(
        const std::vector<double>& x,
        const std::vector<double>& y,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        
        size_t n = std::min(x.size(), y.size());
        result.values.resize(n, std::nan(""));
        
        if (n < window || window < 2) return result;
        
        for (size_t i = window - 1; i < n; ++i) {
            double sum_x = 0.0, sum_y = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                sum_x += x[j];
                sum_y += y[j];
            }
            double mean_x = sum_x / window;
            double mean_y = sum_y / window;
            
            double cov = 0.0, var_x = 0.0, var_y = 0.0;
            for (size_t j = i - window + 1; j <= i; ++j) {
                double dx = x[j] - mean_x;
                double dy = y[j] - mean_y;
                cov += dx * dy;
                var_x += dx * dx;
                var_y += dy * dy;
            }
            
            double denom = std::sqrt(var_x * var_y);
            if (denom > 1e-10) {
                result.values[i] = cov / denom;
            } else {
                result.values[i] = 0.0;
            }
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling min
     */
    [[nodiscard]] static RollingResult rolling_min(
        const std::vector<double>& data,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < window || window == 0) return result;
        
        std::deque<size_t> dq;  // Monotonic deque for O(1) min
        
        for (size_t i = 0; i < data.size(); ++i) {
            // Remove elements outside window
            while (!dq.empty() && dq.front() <= i - window) {
                dq.pop_front();
            }
            
            // Remove larger elements
            while (!dq.empty() && data[dq.back()] >= data[i]) {
                dq.pop_back();
            }
            
            dq.push_back(i);
            
            if (i >= window - 1) {
                result.values[i] = data[dq.front()];
                ++result.valid_count;
            }
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling max
     */
    [[nodiscard]] static RollingResult rolling_max(
        const std::vector<double>& data,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < window || window == 0) return result;
        
        std::deque<size_t> dq;
        
        for (size_t i = 0; i < data.size(); ++i) {
            while (!dq.empty() && dq.front() <= i - window) {
                dq.pop_front();
            }
            
            while (!dq.empty() && data[dq.back()] <= data[i]) {
                dq.pop_back();
            }
            
            dq.push_back(i);
            
            if (i >= window - 1) {
                result.values[i] = data[dq.front()];
                ++result.valid_count;
            }
        }
        
        return result;
    }

    /**
     * @brief Calculate exponential moving average
     */
    [[nodiscard]] static RollingResult ema(
        const std::vector<double>& data,
        size_t span
    ) {
        RollingResult result;
        result.window_size = span;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.empty() || span == 0) return result;
        
        double alpha = 2.0 / (span + 1);
        result.values[0] = data[0];
        result.valid_count = 1;
        
        for (size_t i = 1; i < data.size(); ++i) {
            result.values[i] = alpha * data[i] + (1 - alpha) * result.values[i - 1];
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate exponential weighted standard deviation
     */
    [[nodiscard]] static RollingResult ewm_std(
        const std::vector<double>& data,
        size_t span
    ) {
        RollingResult result;
        result.window_size = span;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < 2 || span == 0) return result;
        
        double alpha = 2.0 / (span + 1);
        double ewma = data[0];
        double ewmvar = 0.0;
        
        result.values[0] = 0.0;
        result.valid_count = 1;
        
        for (size_t i = 1; i < data.size(); ++i) {
            double diff = data[i] - ewma;
            double incr = alpha * diff;
            ewma += incr;
            ewmvar = (1 - alpha) * (ewmvar + diff * incr);
            result.values[i] = std::sqrt(ewmvar);
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Calculate rolling sum
     */
    [[nodiscard]] static RollingResult rolling_sum(
        const std::vector<double>& data,
        size_t window
    ) {
        RollingResult result;
        result.window_size = window;
        result.values.resize(data.size(), std::nan(""));
        
        if (data.size() < window || window == 0) return result;
        
        double sum = 0.0;
        for (size_t i = 0; i < window; ++i) {
            sum += data[i];
        }
        result.values[window - 1] = sum;
        result.valid_count = 1;
        
        for (size_t i = window; i < data.size(); ++i) {
            sum += data[i] - data[i - window];
            result.values[i] = sum;
            ++result.valid_count;
        }
        
        return result;
    }

    /**
     * @brief Full rolling statistics summary
     */
    struct RollingSummary {
        RollingResult mean;
        RollingResult std;
        RollingResult sharpe;
        RollingResult sortino;
        RollingResult min;
        RollingResult max;
    };

    [[nodiscard]] static RollingSummary calculate_all(
        const std::vector<double>& returns,
        size_t window,
        double risk_free_rate = 0.0
    ) {
        RollingSummary s;
        s.mean = rolling_mean(returns, window);
        s.std = rolling_std(returns, window);
        s.sharpe = rolling_sharpe(returns, window, risk_free_rate);
        s.sortino = rolling_sortino(returns, window);
        s.min = rolling_min(returns, window);
        s.max = rolling_max(returns, window);
        return s;
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_ROLLING_STATS_HPP
