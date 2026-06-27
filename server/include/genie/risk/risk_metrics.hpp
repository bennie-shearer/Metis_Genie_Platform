/**
 * @file risk_metrics.hpp
 * @brief Extended risk metrics for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_RISK_METRICS_HPP
#define GENIE_RISK_METRICS_HPP

#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>
#include <string>

namespace genie {
namespace risk {

struct RiskMetrics {
    // Return metrics
    double total_return{0};
    double annualized_return{0};
    double arithmetic_mean{0};
    double geometric_mean{0};
    
    // Volatility metrics
    double volatility{0};
    double downside_deviation{0};
    double upside_deviation{0};
    double semi_deviation{0};
    
    // Risk-adjusted returns
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double treynor_ratio{0};
    double calmar_ratio{0};
    double omega_ratio{0};
    double information_ratio{0};
    
    // Regression metrics
    double alpha{0};           // Jensen's Alpha
    double beta{0};
    double r_squared{0};
    double tracking_error{0};
    
    // Drawdown metrics
    double max_drawdown{0};
    double avg_drawdown{0};
    double max_drawdown_duration{0};
    double ulcer_index{0};
    
    // Tail risk
    double var_95{0};
    double var_99{0};
    double cvar_95{0};
    double cvar_99{0};
    double skewness{0};
    double kurtosis{0};
    
    // Win/Loss
    double win_rate{0};
    double profit_factor{0};
    double avg_win{0};
    double avg_loss{0};
    double gain_to_pain_ratio{0};
};

class RiskAnalyzer {
    double risk_free_rate_{0.02};
    double target_return_{0.0};
    
public:
    void set_risk_free_rate(double rate) { risk_free_rate_ = rate; }
    void set_target_return(double target) { target_return_ = target; }
    
    RiskMetrics analyze(const std::vector<double>& returns,
                        const std::vector<double>& benchmark_returns = {}) const {
        RiskMetrics m;
        if (returns.empty()) return m;
        
        size_t n = returns.size();
        
        // Basic statistics
        m.arithmetic_mean = std::accumulate(returns.begin(), returns.end(), 0.0) / n;
        
        // Geometric mean
        double product = 1.0;
        for (double r : returns) product *= (1.0 + r);
        m.geometric_mean = std::pow(product, 1.0 / n) - 1.0;
        
        // Total and annualized return
        m.total_return = product - 1.0;
        m.annualized_return = std::pow(1.0 + m.total_return, 252.0 / n) - 1.0;
        
        // Volatility
        double var = 0;
        for (double r : returns) var += (r - m.arithmetic_mean) * (r - m.arithmetic_mean);
        var /= (n - 1);
        m.volatility = std::sqrt(var) * std::sqrt(252.0);
        
        // Downside/Upside deviation
        double down_var = 0, up_var = 0;
        int down_n = 0, up_n = 0;
        double mar = target_return_ / 252.0;  // Minimum acceptable return (daily)
        
        for (double r : returns) {
            if (r < mar) { down_var += (r - mar) * (r - mar); ++down_n; }
            if (r > mar) { up_var += (r - mar) * (r - mar); ++up_n; }
            if (r < m.arithmetic_mean) m.semi_deviation += (r - m.arithmetic_mean) * (r - m.arithmetic_mean);
        }
        
        m.downside_deviation = (down_n > 0) ? std::sqrt(down_var / down_n) * std::sqrt(252.0) : 0.0001;
        m.upside_deviation = (up_n > 0) ? std::sqrt(up_var / up_n) * std::sqrt(252.0) : 0.0001;
        m.semi_deviation = std::sqrt(m.semi_deviation / n) * std::sqrt(252.0);
        
        // Sharpe Ratio
        m.sharpe_ratio = (m.volatility > 0.0001) ? 
            (m.annualized_return - risk_free_rate_) / m.volatility : 0;
        
        // Sortino Ratio
        m.sortino_ratio = (m.downside_deviation > 0.0001) ?
            (m.annualized_return - risk_free_rate_) / m.downside_deviation : 0;
        
        // Omega Ratio
        double gains = 0, losses = 0;
        for (double r : returns) {
            if (r > mar) gains += (r - mar);
            else losses += (mar - r);
        }
        m.omega_ratio = (losses > 0.0001) ? gains / losses : (gains > 0 ? 999.0 : 1.0);
        
        // Drawdown analysis
        double peak = 0, cumulative = 0;
        double dd_sum = 0;
        int dd_count = 0;
        std::vector<double> drawdowns;
        
        for (double r : returns) {
            cumulative = (1.0 + cumulative) * (1.0 + r) - 1.0;
            peak = std::max(peak, cumulative);
            double dd = (peak > 0) ? (peak - cumulative) / (1.0 + peak) : 0;
            if (dd > 0) { dd_sum += dd; ++dd_count; }
            drawdowns.push_back(dd);
            m.max_drawdown = std::max(m.max_drawdown, dd);
        }
        
        m.avg_drawdown = (dd_count > 0) ? dd_sum / dd_count : 0;
        m.calmar_ratio = (m.max_drawdown > 0.0001) ? m.annualized_return / m.max_drawdown : 0;
        
        // Ulcer Index
        double ulcer_sum = 0;
        for (double dd : drawdowns) ulcer_sum += dd * dd;
        m.ulcer_index = std::sqrt(ulcer_sum / n);
        
        // VaR and CVaR
        std::vector<double> sorted_returns = returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());
        
        size_t var95_idx = static_cast<size_t>(n * 0.05);
        size_t var99_idx = static_cast<size_t>(n * 0.01);
        
        m.var_95 = -sorted_returns[var95_idx] * std::sqrt(252.0);
        m.var_99 = -sorted_returns[var99_idx] * std::sqrt(252.0);
        
        double cvar95_sum = 0;
        for (size_t i = 0; i <= var95_idx; ++i) cvar95_sum += sorted_returns[i];
        m.cvar_95 = -(cvar95_sum / (var95_idx + 1)) * std::sqrt(252.0);
        
        double cvar99_sum = 0;
        for (size_t i = 0; i <= var99_idx; ++i) cvar99_sum += sorted_returns[i];
        m.cvar_99 = -(cvar99_sum / (var99_idx + 1)) * std::sqrt(252.0);
        
        // Skewness and Kurtosis
        double skew_sum = 0, kurt_sum = 0;
        double std_dev = std::sqrt(var);
        for (double r : returns) {
            double z = (r - m.arithmetic_mean) / std_dev;
            skew_sum += z * z * z;
            kurt_sum += z * z * z * z;
        }
        m.skewness = skew_sum / n;
        m.kurtosis = kurt_sum / n - 3.0;  // Excess kurtosis
        
        // Win/Loss analysis
        double total_wins = 0, total_losses = 0;
        int win_count = 0, loss_count = 0;
        
        for (double r : returns) {
            if (r > 0) { total_wins += r; ++win_count; }
            else if (r < 0) { total_losses += std::abs(r); ++loss_count; }
        }
        
        m.win_rate = static_cast<double>(win_count) / n;
        m.avg_win = (win_count > 0) ? total_wins / win_count : 0;
        m.avg_loss = (loss_count > 0) ? total_losses / loss_count : 0;
        m.profit_factor = (total_losses > 0.0001) ? total_wins / total_losses : 
                          (total_wins > 0 ? 999.0 : 1.0);
        m.gain_to_pain_ratio = (total_losses > 0.0001) ? 
            (total_wins - total_losses) / total_losses : 0;
        
        // Benchmark analysis (if provided)
        if (!benchmark_returns.empty() && benchmark_returns.size() == returns.size()) {
            // Beta and Alpha
            double bench_mean = std::accumulate(benchmark_returns.begin(), 
                                                benchmark_returns.end(), 0.0) / n;
            
            double covar = 0, bench_var = 0;
            for (size_t i = 0; i < n; ++i) {
                covar += (returns[i] - m.arithmetic_mean) * (benchmark_returns[i] - bench_mean);
                bench_var += (benchmark_returns[i] - bench_mean) * (benchmark_returns[i] - bench_mean);
            }
            covar /= (n - 1);
            bench_var /= (n - 1);
            
            m.beta = (bench_var > 0.0001) ? covar / bench_var : 1.0;
            
            // Jensen's Alpha (annualized)
            double bench_annual = std::pow(std::accumulate(benchmark_returns.begin(),
                                                            benchmark_returns.end(), 1.0,
                                                            [](double a, double b) { return a * (1 + b); }),
                                            252.0 / n) - 1.0;
            m.alpha = m.annualized_return - (risk_free_rate_ + m.beta * (bench_annual - risk_free_rate_));
            
            // Treynor Ratio
            m.treynor_ratio = (std::abs(m.beta) > 0.0001) ?
                (m.annualized_return - risk_free_rate_) / m.beta : 0;
            
            // Tracking Error
            double te_var = 0;
            for (size_t i = 0; i < n; ++i) {
                double excess = returns[i] - benchmark_returns[i];
                te_var += excess * excess;
            }
            m.tracking_error = std::sqrt(te_var / (n - 1)) * std::sqrt(252.0);
            
            // Information Ratio
            double excess_mean = m.arithmetic_mean - bench_mean;
            m.information_ratio = (m.tracking_error > 0.0001) ?
                (excess_mean * 252.0) / m.tracking_error : 0;
            
            // R-squared
            double ss_res = 0, ss_tot = 0;
            for (size_t i = 0; i < n; ++i) {
                double predicted = m.arithmetic_mean + m.beta * (benchmark_returns[i] - bench_mean);
                ss_res += (returns[i] - predicted) * (returns[i] - predicted);
                ss_tot += (returns[i] - m.arithmetic_mean) * (returns[i] - m.arithmetic_mean);
            }
            m.r_squared = (ss_tot > 0.0001) ? 1.0 - (ss_res / ss_tot) : 0;
        }
        
        return m;
    }
    
    // Quick Sharpe calculation
    double sharpe(const std::vector<double>& returns) const {
        if (returns.size() < 2) return 0;
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double var = 0;
        for (double r : returns) var += (r - mean) * (r - mean);
        var /= (returns.size() - 1);
        double ann_ret = mean * 252.0;
        double ann_vol = std::sqrt(var) * std::sqrt(252.0);
        return (ann_vol > 0.0001) ? (ann_ret - risk_free_rate_) / ann_vol : 0;
    }
    
    // Rolling Sharpe
    std::vector<double> rolling_sharpe(const std::vector<double>& returns, int window = 60) const {
        std::vector<double> result;
        for (size_t i = window; i <= returns.size(); ++i) {
            std::vector<double> window_returns(returns.begin() + i - window, returns.begin() + i);
            result.push_back(sharpe(window_returns));
        }
        return result;
    }
};

// Exposure Analysis
struct ExposureReport {
    std::map<std::string, double> sector_exposure;
    std::map<std::string, double> currency_exposure;
    std::map<std::string, double> asset_class_exposure;
    std::map<std::string, double> country_exposure;
    double gross_exposure{0};
    double net_exposure{0};
    double long_exposure{0};
    double short_exposure{0};
};

class ExposureAnalyzer {
public:
    ExposureReport analyze(const std::map<std::string, double>& positions,
                          const std::map<std::string, std::string>& sectors,
                          const std::map<std::string, std::string>& currencies,
                          const std::map<std::string, std::string>& asset_classes,
                          double portfolio_value) const {
        ExposureReport report;
        
        for (const auto& [id, value] : positions) {
            double weight = value / portfolio_value;
            
            // Track gross/net
            if (value > 0) report.long_exposure += weight;
            else report.short_exposure += std::abs(weight);
            
            // Sector exposure
            if (sectors.count(id)) {
                report.sector_exposure[sectors.at(id)] += weight;
            }
            
            // Currency exposure
            if (currencies.count(id)) {
                report.currency_exposure[currencies.at(id)] += weight;
            }
            
            // Asset class exposure
            if (asset_classes.count(id)) {
                report.asset_class_exposure[asset_classes.at(id)] += weight;
            }
        }
        
        report.gross_exposure = report.long_exposure + report.short_exposure;
        report.net_exposure = report.long_exposure - report.short_exposure;
        
        return report;
    }
};

// Liquidity Risk
struct LiquidityMetrics {
    double days_to_liquidate{0};
    double liquidation_cost{0};
    double liquidity_score{0};      // 0-100
    std::map<std::string, double> position_liquidity;
};

class LiquidityAnalyzer {
public:
    LiquidityMetrics analyze(const std::map<std::string, double>& positions,
                            const std::map<std::string, double>& avg_daily_volumes,
                            const std::map<std::string, double>& spreads,
                            double max_daily_participation = 0.1) const {
        LiquidityMetrics m;
        double total_value = 0;
        double weighted_days = 0;
        double weighted_cost = 0;
        
        for (const auto& [id, value] : positions) {
            total_value += std::abs(value);
            
            double adv = avg_daily_volumes.count(id) ? avg_daily_volumes.at(id) : value;
            double spread = spreads.count(id) ? spreads.at(id) : 0.001;
            
            // Days to liquidate this position
            double days = std::abs(value) / (adv * max_daily_participation);
            m.position_liquidity[id] = days;
            weighted_days += days * std::abs(value);
            
            // Cost to liquidate (spread + market impact)
            double market_impact = 0.1 * spread * std::sqrt(days);
            weighted_cost += (spread / 2.0 + market_impact) * std::abs(value);
        }
        
        m.days_to_liquidate = (total_value > 0) ? weighted_days / total_value : 0;
        m.liquidation_cost = weighted_cost;
        
        // Score: 100 = highly liquid (< 1 day), 0 = illiquid (> 30 days)
        m.liquidity_score = std::max(0.0, std::min(100.0, 100.0 * (1.0 - m.days_to_liquidate / 30.0)));
        
        return m;
    }
};

} // namespace risk
} // namespace genie
#endif // GENIE_RISK_METRICS_HPP
