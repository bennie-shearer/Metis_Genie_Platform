/**
 * @file advanced_metrics.hpp
 * @brief Advanced risk-adjusted performance metrics
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements institutional-grade performance metrics:
 * - Sortino Ratio (downside deviation)
 * - Calmar Ratio (max drawdown adjusted)
 * - Omega Ratio (gain/loss probability weighted)
 * - Ulcer Index (drawdown duration weighted)
 * - Information Ratio (tracking error adjusted)
 * - Treynor Ratio (systematic risk adjusted)
 */

#pragma once
#ifndef GENIE_ANALYTICS_ADVANCED_METRICS_HPP
#define GENIE_ANALYTICS_ADVANCED_METRICS_HPP

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace genie::analytics {

/**
 * @brief Advanced risk-adjusted performance metrics
 */
class AdvancedMetrics {
public:
    /**
     * @brief Calculate Sortino Ratio
     * 
     * Sortino = (Return - MAR) / Downside Deviation
     * Unlike Sharpe, only penalizes downside volatility
     *
     * @param returns Vector of periodic returns
     * @param mar Minimum Acceptable Return (default 0)
     * @param annualization_factor Periods per year (252 for daily)
     * @return Annualized Sortino Ratio
     */
    [[nodiscard]] static double sortino_ratio(
        const std::vector<double>& returns,
        double mar = 0.0,
        double annualization_factor = 252.0
    ) {
        if (returns.size() < 2) return 0.0;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        // Calculate downside deviation (only negative deviations from MAR)
        double sum_sq_downside = 0.0;
        size_t downside_count = 0;
        for (double r : returns) {
            if (r < mar) {
                sum_sq_downside += (r - mar) * (r - mar);
                ++downside_count;
            }
        }
        
        if (downside_count == 0) return std::numeric_limits<double>::infinity();
        
        double downside_dev = std::sqrt(sum_sq_downside / returns.size());
        if (downside_dev < 1e-10) return std::numeric_limits<double>::infinity();
        
        // Annualize
        double ann_return = mean * annualization_factor;
        double ann_downside = downside_dev * std::sqrt(annualization_factor);
        double ann_mar = mar * annualization_factor;
        
        return (ann_return - ann_mar) / ann_downside;
    }

    /**
     * @brief Calculate Calmar Ratio
     * 
     * Calmar = Annualized Return / Max Drawdown
     * Measures return per unit of drawdown risk
     *
     * @param returns Vector of periodic returns
     * @param annualization_factor Periods per year
     * @return Calmar Ratio
     */
    [[nodiscard]] static double calmar_ratio(
        const std::vector<double>& returns,
        double annualization_factor = 252.0
    ) {
        if (returns.size() < 2) return 0.0;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double ann_return = mean * annualization_factor;
        
        double max_dd = max_drawdown(returns);
        if (max_dd < 1e-10) return std::numeric_limits<double>::infinity();
        
        return ann_return / max_dd;
    }

    /**
     * @brief Calculate Omega Ratio
     * 
     * Omega = Probability-weighted gains / Probability-weighted losses
     * Considers entire return distribution, not just mean/variance
     *
     * @param returns Vector of periodic returns
     * @param threshold Threshold return (default 0)
     * @return Omega Ratio (>1 is good, <1 is bad)
     */
    [[nodiscard]] static double omega_ratio(
        const std::vector<double>& returns,
        double threshold = 0.0
    ) {
        if (returns.empty()) return 0.0;

        double gains = 0.0;
        double losses = 0.0;
        
        for (double r : returns) {
            if (r > threshold) {
                gains += (r - threshold);
            } else {
                losses += (threshold - r);
            }
        }
        
        if (losses < 1e-10) return std::numeric_limits<double>::infinity();
        
        return gains / losses;
    }

    /**
     * @brief Calculate Ulcer Index
     * 
     * Measures depth and duration of drawdowns
     * Lower is better (less "ulcer-inducing")
     *
     * @param returns Vector of periodic returns
     * @return Ulcer Index (0-100 scale)
     */
    [[nodiscard]] static double ulcer_index(const std::vector<double>& returns) {
        if (returns.size() < 2) return 0.0;

        // Convert returns to price series
        std::vector<double> prices(returns.size() + 1);
        prices[0] = 100.0;
        for (size_t i = 0; i < returns.size(); ++i) {
            prices[i + 1] = prices[i] * (1.0 + returns[i]);
        }
        
        // Calculate percentage drawdowns from peak
        double peak = prices[0];
        double sum_sq_dd = 0.0;
        
        for (size_t i = 1; i < prices.size(); ++i) {
            if (prices[i] > peak) peak = prices[i];
            double dd_pct = 100.0 * (peak - prices[i]) / peak;
            sum_sq_dd += dd_pct * dd_pct;
        }
        
        return std::sqrt(sum_sq_dd / prices.size());
    }

    /**
     * @brief Calculate Information Ratio
     * 
     * IR = Active Return / Tracking Error
     * Measures skill of active management
     *
     * @param portfolio_returns Portfolio returns
     * @param benchmark_returns Benchmark returns
     * @param annualization_factor Periods per year
     * @return Annualized Information Ratio
     */
    [[nodiscard]] static double information_ratio(
        const std::vector<double>& portfolio_returns,
        const std::vector<double>& benchmark_returns,
        double annualization_factor = 252.0
    ) {
        size_t n = std::min(portfolio_returns.size(), benchmark_returns.size());
        if (n < 2) return 0.0;

        // Calculate active returns
        std::vector<double> active(n);
        for (size_t i = 0; i < n; ++i) {
            active[i] = portfolio_returns[i] - benchmark_returns[i];
        }
        
        double mean_active = std::accumulate(active.begin(), active.end(), 0.0) / n;
        
        // Tracking error (std of active returns)
        double sum_sq = 0.0;
        for (double a : active) {
            sum_sq += (a - mean_active) * (a - mean_active);
        }
        double tracking_error = std::sqrt(sum_sq / (n - 1));
        
        if (tracking_error < 1e-10) return 0.0;
        
        // Annualize
        double ann_active = mean_active * annualization_factor;
        double ann_te = tracking_error * std::sqrt(annualization_factor);
        
        return ann_active / ann_te;
    }

    /**
     * @brief Calculate Treynor Ratio
     * 
     * Treynor = (Return - Rf) / Beta
     * Measures return per unit of systematic risk
     *
     * @param portfolio_returns Portfolio returns
     * @param market_returns Market returns
     * @param risk_free_rate Risk-free rate (annualized)
     * @param annualization_factor Periods per year
     * @return Treynor Ratio
     */
    [[nodiscard]] static double treynor_ratio(
        const std::vector<double>& portfolio_returns,
        const std::vector<double>& market_returns,
        double risk_free_rate = 0.02,
        double annualization_factor = 252.0
    ) {
        size_t n = std::min(portfolio_returns.size(), market_returns.size());
        if (n < 2) return 0.0;

        double beta = calculate_beta(portfolio_returns, market_returns);
        if (std::abs(beta) < 1e-10) return 0.0;
        
        double mean_port = std::accumulate(portfolio_returns.begin(), 
                                           portfolio_returns.begin() + n, 0.0) / n;
        double ann_return = mean_port * annualization_factor;
        
        return (ann_return - risk_free_rate) / beta;
    }

    /**
     * @brief Calculate Maximum Drawdown
     * 
     * @param returns Vector of periodic returns
     * @return Maximum drawdown as positive decimal (0.20 = 20%)
     */
    [[nodiscard]] static double max_drawdown(const std::vector<double>& returns) {
        if (returns.empty()) return 0.0;

        double peak = 1.0;
        double value = 1.0;
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
     * @brief Calculate Beta
     * 
     * @param asset_returns Asset returns
     * @param market_returns Market returns
     * @return Beta coefficient
     */
    [[nodiscard]] static double calculate_beta(
        const std::vector<double>& asset_returns,
        const std::vector<double>& market_returns
    ) {
        size_t n = std::min(asset_returns.size(), market_returns.size());
        if (n < 2) return 1.0;

        double mean_a = std::accumulate(asset_returns.begin(), 
                                        asset_returns.begin() + n, 0.0) / n;
        double mean_m = std::accumulate(market_returns.begin(), 
                                        market_returns.begin() + n, 0.0) / n;
        
        double cov = 0.0;
        double var_m = 0.0;
        
        for (size_t i = 0; i < n; ++i) {
            double da = asset_returns[i] - mean_a;
            double dm = market_returns[i] - mean_m;
            cov += da * dm;
            var_m += dm * dm;
        }
        
        if (var_m < 1e-10) return 1.0;
        
        return cov / var_m;
    }

    /**
     * @brief Calculate Sharpe Ratio
     * 
     * @param returns Vector of periodic returns
     * @param risk_free_rate Risk-free rate (annualized)
     * @param annualization_factor Periods per year
     * @return Annualized Sharpe Ratio
     */
    [[nodiscard]] static double sharpe_ratio(
        const std::vector<double>& returns,
        double risk_free_rate = 0.02,
        double annualization_factor = 252.0
    ) {
        if (returns.size() < 2) return 0.0;

        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        double sum_sq = 0.0;
        for (double r : returns) {
            sum_sq += (r - mean) * (r - mean);
        }
        double std_dev = std::sqrt(sum_sq / (returns.size() - 1));
        
        if (std_dev < 1e-10) return 0.0;
        
        double ann_return = mean * annualization_factor;
        double ann_std = std_dev * std::sqrt(annualization_factor);
        
        return (ann_return - risk_free_rate) / ann_std;
    }

    /**
     * @brief Calculate all metrics at once
     */
    struct MetricsSummary {
        double sharpe_ratio{0};
        double sortino_ratio{0};
        double calmar_ratio{0};
        double omega_ratio{0};
        double ulcer_index{0};
        double information_ratio{0};
        double treynor_ratio{0};
        double max_drawdown{0};
        double beta{0};
        double annualized_return{0};
        double annualized_volatility{0};
    };

    [[nodiscard]] static MetricsSummary calculate_all(
        const std::vector<double>& portfolio_returns,
        const std::vector<double>& benchmark_returns = {},
        double risk_free_rate = 0.02,
        double annualization_factor = 252.0
    ) {
        MetricsSummary s;
        
        if (portfolio_returns.size() < 2) return s;
        
        double mean = std::accumulate(portfolio_returns.begin(), 
                                      portfolio_returns.end(), 0.0) / portfolio_returns.size();
        
        double sum_sq = 0.0;
        for (double r : portfolio_returns) {
            sum_sq += (r - mean) * (r - mean);
        }
        double std_dev = std::sqrt(sum_sq / (portfolio_returns.size() - 1));
        
        s.annualized_return = mean * annualization_factor;
        s.annualized_volatility = std_dev * std::sqrt(annualization_factor);
        s.sharpe_ratio = sharpe_ratio(portfolio_returns, risk_free_rate, annualization_factor);
        s.sortino_ratio = sortino_ratio(portfolio_returns, 0.0, annualization_factor);
        s.calmar_ratio = calmar_ratio(portfolio_returns, annualization_factor);
        s.omega_ratio = omega_ratio(portfolio_returns);
        s.ulcer_index = ulcer_index(portfolio_returns);
        s.max_drawdown = max_drawdown(portfolio_returns);
        
        if (!benchmark_returns.empty()) {
            s.information_ratio = information_ratio(portfolio_returns, benchmark_returns, annualization_factor);
            s.treynor_ratio = treynor_ratio(portfolio_returns, benchmark_returns, risk_free_rate, annualization_factor);
            s.beta = calculate_beta(portfolio_returns, benchmark_returns);
        }
        
        return s;
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_ADVANCED_METRICS_HPP
