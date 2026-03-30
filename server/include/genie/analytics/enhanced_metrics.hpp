/**
 * @file enhanced_metrics.hpp
 * @brief Advanced risk-adjusted performance metrics
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides institutional-grade risk metrics including:
 * - Sortino Ratio (downside deviation)
 * - Calmar Ratio (max drawdown adjusted)
 * - Omega Ratio (gain/loss probability weighted)
 * - Ulcer Index (drawdown pain)
 * - Kappa Ratios (generalized lower partial moments)
 * - Tail Ratios (upside vs downside capture)
 */
#pragma once
#ifndef GENIE_ANALYTICS_ENHANCED_METRICS_HPP
#define GENIE_ANALYTICS_ENHANCED_METRICS_HPP

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <optional>

namespace genie::analytics {

/**
 * @brief Comprehensive risk-adjusted metrics result
 */
struct EnhancedMetrics {
    // Basic statistics
    double mean_return{0};
    double std_deviation{0};
    double annualized_return{0};
    double annualized_volatility{0};
    
    // Traditional ratios
    double sharpe_ratio{0};
    
    // Downside risk ratios
    double sortino_ratio{0};
    double downside_deviation{0};
    
    // Drawdown-based ratios
    double calmar_ratio{0};
    double sterling_ratio{0};
    double burke_ratio{0};
    double ulcer_index{0};
    double pain_index{0};
    double martin_ratio{0};  // Return / Ulcer Index
    
    // Probability-weighted ratios
    double omega_ratio{0};
    double upside_potential_ratio{0};
    
    // Higher moment ratios
    double kappa_2{0};  // Sortino equivalent
    double kappa_3{0};  // Third lower partial moment
    
    // Tail analysis
    double tail_ratio{0};        // 95th percentile / 5th percentile
    double gain_loss_ratio{0};   // Avg gain / Avg loss
    double profit_factor{0};     // Sum gains / Sum losses
    double win_rate{0};          // % positive returns
    
    // Drawdown statistics
    double max_drawdown{0};
    double avg_drawdown{0};
    double max_drawdown_duration{0};  // In periods
    double recovery_factor{0};        // Total return / Max DD
    
    // Risk metrics
    double var_95{0};
    double cvar_95{0};
    double skewness{0};
    double kurtosis{0};
};

/**
 * @brief Configuration for metrics calculation
 */
struct MetricsConfig {
    double risk_free_rate{0.0};      // Annual risk-free rate
    double target_return{0.0};       // MAR for Sortino/Omega
    int periods_per_year{252};       // Trading days for annualization
    double var_confidence{0.95};     // VaR confidence level
    bool use_sample_std{true};       // Sample vs population std
};

/**
 * @brief Advanced risk metrics calculator
 */
class EnhancedMetricsCalculator {
public:
    explicit EnhancedMetricsCalculator(MetricsConfig config = {})
        : config_(config) {}
    
    /**
     * @brief Calculate all enhanced metrics from return series
     * @param returns Vector of periodic returns (not prices)
     * @return Complete metrics structure
     */
    EnhancedMetrics calculate(const std::vector<double>& returns) const {
        if (returns.size() < 2) {
            throw std::invalid_argument("Need at least 2 returns for metrics calculation");
        }
        
        EnhancedMetrics m;
        size_t n = returns.size();
        
        // === Basic Statistics ===
        m.mean_return = mean(returns);
        m.std_deviation = stddev(returns, config_.use_sample_std);
        m.annualized_return = m.mean_return * config_.periods_per_year;
        m.annualized_volatility = m.std_deviation * std::sqrt(config_.periods_per_year);
        
        // === Sharpe Ratio ===
        double excess_return = m.annualized_return - config_.risk_free_rate;
        m.sharpe_ratio = (m.annualized_volatility > 0) ? 
            excess_return / m.annualized_volatility : 0;
        
        // === Downside Metrics ===
        double daily_target = config_.target_return / config_.periods_per_year;
        m.downside_deviation = downside_deviation(returns, daily_target);
        double ann_downside = m.downside_deviation * std::sqrt(config_.periods_per_year);
        m.sortino_ratio = (ann_downside > 0) ? 
            (m.annualized_return - config_.target_return) / ann_downside : 0;
        
        // === Drawdown Analysis ===
        auto dd_result = analyze_drawdowns(returns);
        m.max_drawdown = dd_result.max_drawdown;
        m.avg_drawdown = dd_result.avg_drawdown;
        m.max_drawdown_duration = dd_result.max_duration;
        
        // === Drawdown-Based Ratios ===
        m.calmar_ratio = (m.max_drawdown > 0) ? 
            m.annualized_return / m.max_drawdown : 0;
        
        // Sterling: Return / (Avg of N largest drawdowns - 10%)
        m.sterling_ratio = (dd_result.avg_largest > 0.10) ?
            m.annualized_return / (dd_result.avg_largest - 0.10) : 0;
        
        // Burke: Return / sqrt(sum of squared drawdowns)
        m.burke_ratio = (dd_result.sum_squared > 0) ?
            m.annualized_return / std::sqrt(dd_result.sum_squared) : 0;
        
        // Ulcer Index: RMS of drawdowns
        m.ulcer_index = dd_result.ulcer_index;
        m.pain_index = dd_result.pain_index;
        m.martin_ratio = (m.ulcer_index > 0) ? 
            m.annualized_return / m.ulcer_index : 0;
        
        m.recovery_factor = (m.max_drawdown > 0) ?
            cumulative_return(returns) / m.max_drawdown : 0;
        
        // === Omega Ratio ===
        m.omega_ratio = omega_ratio(returns, daily_target);
        m.upside_potential_ratio = upside_potential_ratio(returns, daily_target);
        
        // === Kappa Ratios ===
        m.kappa_2 = kappa_ratio(returns, daily_target, 2);
        m.kappa_3 = kappa_ratio(returns, daily_target, 3);
        
        // === Tail Analysis ===
        m.tail_ratio = tail_ratio(returns);
        
        auto gl = gain_loss_analysis(returns);
        m.gain_loss_ratio = gl.gain_loss_ratio;
        m.profit_factor = gl.profit_factor;
        m.win_rate = gl.win_rate;
        
        // === Higher Moments ===
        m.skewness = skewness(returns);
        m.kurtosis = kurtosis(returns);
        
        // === VaR/CVaR ===
        auto var_result = calculate_var(returns, config_.var_confidence);
        m.var_95 = var_result.first;
        m.cvar_95 = var_result.second;
        
        return m;
    }
    
    // === Individual Metric Functions ===
    
    /**
     * @brief Sortino Ratio - Uses downside deviation instead of total volatility
     * Higher is better. Penalizes only downside volatility.
     */
    double sortino_ratio(const std::vector<double>& returns, 
                         double mar = 0.0) const {
        double ann_ret = mean(returns) * config_.periods_per_year;
        double daily_mar = mar / config_.periods_per_year;
        double dd = downside_deviation(returns, daily_mar);
        double ann_dd = dd * std::sqrt(config_.periods_per_year);
        return (ann_dd > 0) ? (ann_ret - mar) / ann_dd : 0;
    }
    
    /**
     * @brief Calmar Ratio - Annual return / Max drawdown
     * Higher is better. Measures return per unit of max pain.
     */
    double calmar_ratio(const std::vector<double>& returns) const {
        double ann_ret = mean(returns) * config_.periods_per_year;
        double mdd = max_drawdown(returns);
        return (mdd > 0) ? ann_ret / mdd : 0;
    }
    
    /**
     * @brief Omega Ratio - Probability weighted gains / losses above threshold
     * Higher is better. 1.0 = break even at threshold.
     */
    double omega_ratio(const std::vector<double>& returns, 
                       double threshold = 0.0) const {
        double gains = 0, losses = 0;
        for (double r : returns) {
            if (r > threshold) {
                gains += (r - threshold);
            } else {
                losses += (threshold - r);
            }
        }
        return (losses > 0) ? gains / losses : (gains > 0 ? 999.0 : 1.0);
    }
    
    /**
     * @brief Ulcer Index - Root mean square of drawdowns
     * Lower is better. Measures "pain" of holding through drawdowns.
     */
    double ulcer_index(const std::vector<double>& returns) const {
        auto prices = returns_to_prices(returns);
        double peak = prices[0];
        double sum_sq = 0;
        
        for (size_t i = 1; i < prices.size(); ++i) {
            peak = std::max(peak, prices[i]);
            double dd = (peak - prices[i]) / peak;
            sum_sq += dd * dd;
        }
        
        return std::sqrt(sum_sq / prices.size()) * 100;  // As percentage
    }
    
    /**
     * @brief Kappa Ratio - Generalized downside risk ratio
     * @param order LPM order (2 = Sortino, 3 = emphasizes large losses)
     */
    double kappa_ratio(const std::vector<double>& returns,
                       double threshold,
                       int order) const {
        double lpm = lower_partial_moment(returns, threshold, order);
        if (lpm <= 0) return 0;
        
        double excess = mean(returns) - threshold;
        return excess / std::pow(lpm, 1.0 / order);
    }
    
    /**
     * @brief Tail Ratio - Upside tail / Downside tail
     * > 1 means larger gains than losses at extremes
     */
    double tail_ratio(const std::vector<double>& returns,
                      double percentile = 0.05) const {
        auto sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        
        size_t n = sorted.size();
        size_t lower_idx = static_cast<size_t>(percentile * n);
        size_t upper_idx = static_cast<size_t>((1.0 - percentile) * n);
        
        double lower = std::abs(sorted[lower_idx]);
        double upper = std::abs(sorted[upper_idx]);
        
        return (lower > 0) ? upper / lower : 0;
    }
    
    /**
     * @brief Maximum Drawdown - Largest peak-to-trough decline
     */
    double max_drawdown(const std::vector<double>& returns) const {
        auto prices = returns_to_prices(returns);
        double peak = prices[0];
        double max_dd = 0;
        
        for (size_t i = 1; i < prices.size(); ++i) {
            peak = std::max(peak, prices[i]);
            double dd = (peak - prices[i]) / peak;
            max_dd = std::max(max_dd, dd);
        }
        
        return max_dd;
    }
    
    /**
     * @brief Downside Deviation - Std dev of returns below target
     */
    double downside_deviation(const std::vector<double>& returns,
                              double target = 0.0) const {
        double sum_sq = 0;
        size_t count = 0;
        
        for (double r : returns) {
            if (r < target) {
                double diff = target - r;
                sum_sq += diff * diff;
                count++;
            }
        }
        
        return (count > 0) ? std::sqrt(sum_sq / returns.size()) : 0;
    }
    
    /**
     * @brief Upside Potential Ratio - Upside potential / Downside risk
     */
    double upside_potential_ratio(const std::vector<double>& returns,
                                   double threshold = 0.0) const {
        double upside = 0;
        for (double r : returns) {
            if (r > threshold) upside += (r - threshold);
        }
        upside /= returns.size();
        
        double dd = downside_deviation(returns, threshold);
        return (dd > 0) ? upside / dd : 0;
    }

private:
    MetricsConfig config_;
    
    double mean(const std::vector<double>& v) const {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
    
    double stddev(const std::vector<double>& v, bool sample = true) const {
        double m = mean(v);
        double sum_sq = 0;
        for (double x : v) sum_sq += (x - m) * (x - m);
        size_t denom = sample ? v.size() - 1 : v.size();
        return std::sqrt(sum_sq / denom);
    }
    
    double cumulative_return(const std::vector<double>& returns) const {
        double cum = 1.0;
        for (double r : returns) cum *= (1 + r);
        return cum - 1;
    }
    
    std::vector<double> returns_to_prices(const std::vector<double>& returns,
                                          double start = 100.0) const {
        std::vector<double> prices;
        prices.reserve(returns.size() + 1);
        prices.push_back(start);
        for (double r : returns) {
            prices.push_back(prices.back() * (1 + r));
        }
        return prices;
    }
    
    double lower_partial_moment(const std::vector<double>& returns,
                                double threshold, int order) const {
        double sum = 0;
        for (double r : returns) {
            if (r < threshold) {
                sum += std::pow(threshold - r, order);
            }
        }
        return sum / returns.size();
    }
    
    double skewness(const std::vector<double>& v) const {
        double m = mean(v);
        double s = stddev(v, false);
        if (s == 0) return 0;
        
        double sum = 0;
        for (double x : v) sum += std::pow((x - m) / s, 3);
        return sum / v.size();
    }
    
    double kurtosis(const std::vector<double>& v) const {
        double m = mean(v);
        double s = stddev(v, false);
        if (s == 0) return 0;
        
        double sum = 0;
        for (double x : v) sum += std::pow((x - m) / s, 4);
        return sum / v.size() - 3;  // Excess kurtosis
    }
    
    std::pair<double, double> calculate_var(const std::vector<double>& returns,
                                            double confidence) const {
        auto sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        
        size_t var_idx = static_cast<size_t>((1 - confidence) * sorted.size());
        double var = -sorted[var_idx];
        
        double cvar = 0;
        for (size_t i = 0; i <= var_idx; ++i) cvar += sorted[i];
        cvar = -cvar / (var_idx + 1);
        
        return {var, cvar};
    }
    
    struct DrawdownResult {
        double max_drawdown{0};
        double avg_drawdown{0};
        double max_duration{0};
        double avg_largest{0};
        double sum_squared{0};
        double ulcer_index{0};
        double pain_index{0};
    };
    
    DrawdownResult analyze_drawdowns(const std::vector<double>& returns) const {
        DrawdownResult result;
        auto prices = returns_to_prices(returns);
        
        double peak = prices[0];
        double current_dd = 0;
        size_t dd_start = 0;
        std::vector<double> all_drawdowns;
        std::vector<size_t> dd_durations;
        double sum_sq = 0;
        double sum_dd = 0;
        
        for (size_t i = 1; i < prices.size(); ++i) {
            if (prices[i] >= peak) {
                // Recovery
                if (current_dd > 0) {
                    all_drawdowns.push_back(current_dd);
                    dd_durations.push_back(i - dd_start);
                }
                peak = prices[i];
                current_dd = 0;
            } else {
                if (current_dd == 0) dd_start = i;
                current_dd = (peak - prices[i]) / peak;
            }
            
            double dd = (peak - prices[i]) / peak;
            sum_sq += dd * dd;
            sum_dd += dd;
        }
        
        // Handle ongoing drawdown
        if (current_dd > 0) {
            all_drawdowns.push_back(current_dd);
            dd_durations.push_back(prices.size() - dd_start);
        }
        
        result.sum_squared = sum_sq;
        result.ulcer_index = std::sqrt(sum_sq / prices.size()) * 100;
        result.pain_index = (sum_dd / prices.size()) * 100;
        
        if (!all_drawdowns.empty()) {
            result.max_drawdown = *std::max_element(all_drawdowns.begin(), all_drawdowns.end());
            result.avg_drawdown = std::accumulate(all_drawdowns.begin(), all_drawdowns.end(), 0.0) / all_drawdowns.size();
            result.max_duration = *std::max_element(dd_durations.begin(), dd_durations.end());
            
            // Average of 5 largest drawdowns
            std::sort(all_drawdowns.rbegin(), all_drawdowns.rend());
            size_t n_largest = std::min(size_t(5), all_drawdowns.size());
            result.avg_largest = std::accumulate(all_drawdowns.begin(), 
                all_drawdowns.begin() + n_largest, 0.0) / n_largest;
        }
        
        return result;
    }
    
    struct GainLossResult {
        double gain_loss_ratio{0};
        double profit_factor{0};
        double win_rate{0};
    };
    
    GainLossResult gain_loss_analysis(const std::vector<double>& returns) const {
        GainLossResult result;
        double sum_gains = 0, sum_losses = 0;
        size_t wins = 0, losses = 0;
        
        for (double r : returns) {
            if (r > 0) {
                sum_gains += r;
                wins++;
            } else if (r < 0) {
                sum_losses += std::abs(r);
                losses++;
            }
        }
        
        result.win_rate = static_cast<double>(wins) / returns.size();
        result.profit_factor = (sum_losses > 0) ? sum_gains / sum_losses : 0;
        
        double avg_gain = (wins > 0) ? sum_gains / wins : 0;
        double avg_loss = (losses > 0) ? sum_losses / losses : 0;
        result.gain_loss_ratio = (avg_loss > 0) ? avg_gain / avg_loss : 0;
        
        return result;
    }
};

// === Convenience Functions ===

/**
 * @brief Calculate Sortino ratio from returns
 */
inline double sortino_ratio(const std::vector<double>& returns, 
                            double mar = 0.0,
                            int periods_per_year = 252) {
    MetricsConfig cfg;
    cfg.target_return = mar;
    cfg.periods_per_year = periods_per_year;
    return EnhancedMetricsCalculator(cfg).sortino_ratio(returns, mar);
}

/**
 * @brief Calculate Calmar ratio from returns
 */
inline double calmar_ratio(const std::vector<double>& returns,
                           int periods_per_year = 252) {
    MetricsConfig cfg;
    cfg.periods_per_year = periods_per_year;
    return EnhancedMetricsCalculator(cfg).calmar_ratio(returns);
}

/**
 * @brief Calculate Omega ratio from returns
 */
inline double omega_ratio(const std::vector<double>& returns,
                          double threshold = 0.0) {
    return EnhancedMetricsCalculator().omega_ratio(returns, threshold);
}

/**
 * @brief Calculate Ulcer Index from returns
 */
inline double ulcer_index(const std::vector<double>& returns) {
    return EnhancedMetricsCalculator().ulcer_index(returns);
}

/**
 * @brief Calculate maximum drawdown from returns
 */
inline double max_drawdown(const std::vector<double>& returns) {
    return EnhancedMetricsCalculator().max_drawdown(returns);
}

/**
 * @brief Calculate all enhanced metrics
 */
inline EnhancedMetrics calculate_enhanced_metrics(
    const std::vector<double>& returns,
    const MetricsConfig& config = {}) {
    return EnhancedMetricsCalculator(config).calculate(returns);
}

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_ENHANCED_METRICS_HPP
