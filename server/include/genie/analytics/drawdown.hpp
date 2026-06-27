/**
 * @file drawdown.hpp
 * @brief Comprehensive drawdown analysis
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements detailed drawdown analysis:
 * - Maximum drawdown calculation
 * - Drawdown duration and recovery
 * - All drawdown periods
 * - Underwater curve
 */

#pragma once
#ifndef GENIE_ANALYTICS_DRAWDOWN_HPP
#define GENIE_ANALYTICS_DRAWDOWN_HPP

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace genie::analytics {

/**
 * @brief Single drawdown period information
 */
struct DrawdownPeriod {
    size_t start_index{0};      // When drawdown began
    size_t trough_index{0};     // Lowest point
    size_t end_index{0};        // When recovered (or ongoing)
    double peak_value{0};       // Value at start
    double trough_value{0};     // Value at lowest point
    double drawdown_pct{0};     // Percentage decline
    size_t duration_days{0};    // Days from start to trough
    size_t recovery_days{0};    // Days from trough to recovery
    bool recovered{false};      // Whether fully recovered
};

/**
 * @brief Complete drawdown analysis results
 */
struct DrawdownAnalysis {
    double max_drawdown{0};             // Maximum drawdown percentage
    double current_drawdown{0};         // Current drawdown (if in one)
    double average_drawdown{0};         // Average of all drawdowns
    size_t max_drawdown_duration{0};    // Longest drawdown (days)
    size_t average_recovery_time{0};    // Average recovery time
    size_t num_drawdowns{0};            // Count of significant drawdowns
    std::vector<DrawdownPeriod> all_drawdowns;
    std::vector<double> underwater_curve;  // Drawdown at each point
};

/**
 * @brief Drawdown analysis calculator
 */
class DrawdownAnalyzer {
public:
    /**
     * @brief Perform comprehensive drawdown analysis
     * 
     * @param returns Vector of periodic returns
     * @param threshold Minimum drawdown to track (default 5%)
     * @return DrawdownAnalysis Complete analysis results
     */
    [[nodiscard]] static DrawdownAnalysis analyze(
        const std::vector<double>& returns,
        double threshold = 0.05
    ) {
        DrawdownAnalysis result;
        
        if (returns.empty()) return result;
        
        // Convert returns to price series
        std::vector<double> prices(returns.size() + 1);
        prices[0] = 100.0;
        for (size_t i = 0; i < returns.size(); ++i) {
            prices[i + 1] = prices[i] * (1.0 + returns[i]);
        }
        
        // Calculate underwater curve (drawdown at each point)
        result.underwater_curve.resize(prices.size());
        double peak = prices[0];
        
        for (size_t i = 0; i < prices.size(); ++i) {
            if (prices[i] > peak) {
                peak = prices[i];
            }
            result.underwater_curve[i] = (peak - prices[i]) / peak;
            
            if (result.underwater_curve[i] > result.max_drawdown) {
                result.max_drawdown = result.underwater_curve[i];
            }
        }
        
        // Current drawdown is the last value
        result.current_drawdown = result.underwater_curve.back();
        
        // Identify all drawdown periods
        bool in_drawdown = false;
        DrawdownPeriod current_period;
        
        for (size_t i = 0; i < result.underwater_curve.size(); ++i) {
            double dd = result.underwater_curve[i];
            
            if (!in_drawdown && dd > 0) {
                // Start of new drawdown
                in_drawdown = true;
                current_period = DrawdownPeriod{};
                current_period.start_index = i > 0 ? i - 1 : 0;
                current_period.peak_value = prices[current_period.start_index];
                current_period.trough_index = i;
                current_period.trough_value = prices[i];
                current_period.drawdown_pct = dd;
            } else if (in_drawdown) {
                if (dd > current_period.drawdown_pct) {
                    // New trough
                    current_period.trough_index = i;
                    current_period.trough_value = prices[i];
                    current_period.drawdown_pct = dd;
                }
                
                if (dd == 0 || i == result.underwater_curve.size() - 1) {
                    // End of drawdown (recovered) or end of series
                    current_period.end_index = i;
                    current_period.recovered = (dd == 0);
                    current_period.duration_days = current_period.trough_index - current_period.start_index;
                    current_period.recovery_days = current_period.recovered ? 
                        (i - current_period.trough_index) : 0;
                    
                    // Only track significant drawdowns
                    if (current_period.drawdown_pct >= threshold) {
                        result.all_drawdowns.push_back(current_period);
                    }
                    
                    in_drawdown = false;
                }
            }
        }
        
        result.num_drawdowns = result.all_drawdowns.size();
        
        // Calculate statistics
        if (!result.all_drawdowns.empty()) {
            double sum_dd = 0.0;
            size_t sum_recovery = 0;
            size_t recovered_count = 0;
            
            for (const auto& dd : result.all_drawdowns) {
                sum_dd += dd.drawdown_pct;
                if (dd.duration_days > result.max_drawdown_duration) {
                    result.max_drawdown_duration = dd.duration_days + dd.recovery_days;
                }
                if (dd.recovered) {
                    sum_recovery += dd.recovery_days;
                    ++recovered_count;
                }
            }
            
            result.average_drawdown = sum_dd / result.all_drawdowns.size();
            if (recovered_count > 0) {
                result.average_recovery_time = sum_recovery / recovered_count;
            }
        }
        
        return result;
    }

    /**
     * @brief Calculate drawdown series from returns
     * 
     * @param returns Vector of periodic returns
     * @return Vector of drawdown values (0 = no drawdown)
     */
    [[nodiscard]] static std::vector<double> drawdown_series(
        const std::vector<double>& returns
    ) {
        if (returns.empty()) return {};
        
        std::vector<double> dd(returns.size() + 1);
        double value = 1.0;
        double peak = 1.0;
        dd[0] = 0.0;
        
        for (size_t i = 0; i < returns.size(); ++i) {
            value *= (1.0 + returns[i]);
            if (value > peak) peak = value;
            dd[i + 1] = (peak - value) / peak;
        }
        
        return dd;
    }

    /**
     * @brief Calculate maximum drawdown from returns
     */
    [[nodiscard]] static double max_drawdown(const std::vector<double>& returns) {
        if (returns.empty()) return 0.0;
        
        double value = 1.0;
        double peak = 1.0;
        double max_dd = 0.0;
        
        for (double r : returns) {
            value *= (1.0 + r);
            if (value > peak) peak = value;
            double dd = (peak - value) / peak;
            if (dd > max_dd) max_dd = dd;
        }
        
        return max_dd;
    }

    /**
     * @brief Calculate current drawdown from returns
     */
    [[nodiscard]] static double current_drawdown(const std::vector<double>& returns) {
        if (returns.empty()) return 0.0;
        
        double value = 1.0;
        double peak = 1.0;
        
        for (double r : returns) {
            value *= (1.0 + r);
            if (value > peak) peak = value;
        }
        
        return (peak - value) / peak;
    }

    /**
     * @brief Calculate time spent in drawdown
     * @return Percentage of time underwater
     */
    [[nodiscard]] static double underwater_time(const std::vector<double>& returns) {
        if (returns.empty()) return 0.0;
        
        double value = 1.0;
        double peak = 1.0;
        size_t underwater_count = 0;
        
        for (double r : returns) {
            value *= (1.0 + r);
            if (value > peak) {
                peak = value;
            } else {
                ++underwater_count;
            }
        }
        
        return static_cast<double>(underwater_count) / returns.size();
    }

    /**
     * @brief Find the N largest drawdowns
     */
    [[nodiscard]] static std::vector<DrawdownPeriod> top_n_drawdowns(
        const std::vector<double>& returns,
        size_t n = 5
    ) {
        auto analysis = analyze(returns, 0.01);  // Low threshold to catch all
        auto& drawdowns = analysis.all_drawdowns;
        
        // Sort by drawdown magnitude (largest first)
        std::sort(drawdowns.begin(), drawdowns.end(),
            [](const DrawdownPeriod& a, const DrawdownPeriod& b) {
                return a.drawdown_pct > b.drawdown_pct;
            });
        
        if (drawdowns.size() > n) {
            drawdowns.resize(n);
        }
        
        return drawdowns;
    }

    /**
     * @brief Calculate Calmar-style ratio using drawdown
     */
    [[nodiscard]] static double pain_ratio(
        const std::vector<double>& returns,
        double annualization = 252.0
    ) {
        if (returns.size() < 2) return 0.0;
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double ann_return = mean * annualization;
        
        // Pain index: average drawdown
        auto dd_series = drawdown_series(returns);
        double sum_dd = std::accumulate(dd_series.begin(), dd_series.end(), 0.0);
        double pain_index = sum_dd / dd_series.size();
        
        if (pain_index < 1e-10) return std::numeric_limits<double>::infinity();
        
        return ann_return / pain_index;
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_DRAWDOWN_HPP
