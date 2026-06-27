/**
 * @file real_performance.hpp
 * @brief Real performance analytics from actual market data
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Production-grade performance calculations:
 * - Time-Weighted Return (TWR) from daily valuations
 * - Benchmark-relative returns and tracking
 * - Sharpe/Sortino with realized data
 * - Rolling returns from real history
 * - Performance attribution with actual weights
 * - GIPS-compliant calculations
 */
#pragma once
#ifndef GENIE_ANALYTICS_REAL_PERFORMANCE_HPP
#define GENIE_ANALYTICS_REAL_PERFORMANCE_HPP

#include "../market/data_manager.hpp"
#include "real_risk.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace genie::analytics {

// ============================================================================
// Time-Weighted Return (TWR)
// ============================================================================

/**
 * @brief Daily valuation point
 */
struct ValuationPoint {
    std::string date;
    double nav{0};                    // Net Asset Value
    double cash_flow{0};              // External cash flow (+ = deposit, - = withdrawal)
    double market_value{0};           // Market value before cash flow
    double daily_return{0};           // Daily return
    double cumulative_return{0};      // Cumulative return from start
};

/**
 * @brief TWR calculation result
 */
struct TWRResult {
    double total_return{0};           // Total period return
    double annualized_return{0};      // Annualized return
    double cagr{0};                   // Compound Annual Growth Rate
    int trading_days{0};
    int calendar_days{0};
    std::string start_date;
    std::string end_date;
    double start_value{0};
    double end_value{0};
    double net_cash_flows{0};
    
    std::vector<ValuationPoint> daily_values;
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Time-Weighted Return:\n";
        oss << "  Total Return: " << (total_return * 100) << "%\n";
        oss << "  Annualized: " << (annualized_return * 100) << "%\n";
        oss << "  CAGR: " << (cagr * 100) << "%\n";
        oss << "  Period: " << start_date << " to " << end_date << "\n";
        oss << "  Trading Days: " << trading_days << "\n";
        return oss.str();
    }
};

/**
 * @brief TWR calculator (Modified Dietz method for sub-periods)
 */
class TWRCalculator {
public:
    /**
     * @brief Calculate TWR from daily NAVs
     */
    static TWRResult calculate(
        const std::vector<double>& navs,
        const std::vector<std::string>& dates,
        const std::vector<double>& cash_flows = {}) {
        
        TWRResult result;
        if (navs.size() < 2) return result;
        
        result.trading_days = static_cast<int>(navs.size());
        result.start_value = navs.front();
        result.end_value = navs.back();
        
        if (!dates.empty()) {
            result.start_date = dates.front();
            result.end_date = dates.back();
        }
        
        // Calculate daily returns
        result.daily_values.reserve(navs.size());
        double cumulative = 1.0;
        
        for (size_t i = 0; i < navs.size(); ++i) {
            ValuationPoint point;
            if (i < dates.size()) point.date = dates[i];
            point.nav = navs[i];
            point.market_value = navs[i];
            
            // Cash flow (if provided)
            if (i < cash_flows.size()) {
                point.cash_flow = cash_flows[i];
                result.net_cash_flows += cash_flows[i];
            }
            
            // Daily return
            if (i > 0) {
                double prev_nav = navs[i - 1];
                double cf = (i < cash_flows.size()) ? cash_flows[i] : 0;
                
                // Adjust for cash flow (assuming flow at end of day)
                if (prev_nav > 0) {
                    point.daily_return = (navs[i] - cf - prev_nav) / prev_nav;
                }
                
                cumulative *= (1.0 + point.daily_return);
            }
            
            point.cumulative_return = cumulative - 1.0;
            result.daily_values.push_back(point);
        }
        
        result.total_return = cumulative - 1.0;
        
        // Annualize (assuming 252 trading days)
        if (result.trading_days > 0) {
            double years = result.trading_days / 252.0;
            if (years > 0 && cumulative > 0) {
                result.annualized_return = std::pow(cumulative, 1.0 / years) - 1.0;
                result.cagr = result.annualized_return;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Calculate TWR with sub-period linking (for cash flows)
     */
    static TWRResult calculate_linked(
        const std::vector<ValuationPoint>& valuations) {
        
        TWRResult result;
        if (valuations.size() < 2) return result;
        
        result.trading_days = static_cast<int>(valuations.size());
        result.start_value = valuations.front().nav;
        result.end_value = valuations.back().nav;
        result.start_date = valuations.front().date;
        result.end_date = valuations.back().date;
        
        // Link sub-period returns
        double linked_return = 1.0;
        
        for (size_t i = 1; i < valuations.size(); ++i) {
            const auto& prev = valuations[i - 1];
            const auto& curr = valuations[i];
            
            // Modified Dietz for sub-period
            double sub_return;
            double denominator = prev.nav + prev.cash_flow;
            
            if (denominator > 0) {
                sub_return = (curr.nav - prev.nav - prev.cash_flow) / denominator;
            } else {
                sub_return = 0;
            }
            
            linked_return *= (1.0 + sub_return);
            result.net_cash_flows += curr.cash_flow;
        }
        
        result.total_return = linked_return - 1.0;
        
        // Annualize
        double years = result.trading_days / 252.0;
        if (years > 0 && linked_return > 0) {
            result.annualized_return = std::pow(linked_return, 1.0 / years) - 1.0;
            result.cagr = result.annualized_return;
        }
        
        result.daily_values = valuations;
        
        return result;
    }
};

// ============================================================================
// Benchmark Relative Returns
// ============================================================================

/**
 * @brief Benchmark comparison result
 */
struct BenchmarkComparison {
    std::string benchmark_symbol;
    
    // Returns
    double portfolio_return{0};
    double benchmark_return{0};
    double excess_return{0};         // Active return
    double alpha{0};                 // Risk-adjusted excess
    
    // Relative metrics
    double information_ratio{0};
    double tracking_error{0};
    double beta{0};
    double r_squared{0};
    
    // Up/Down capture
    double up_capture{0};            // % of benchmark upside captured
    double down_capture{0};          // % of benchmark downside captured
    double capture_ratio{0};         // Up capture / Down capture
    
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Benchmark Comparison vs " << benchmark_symbol << ":\n";
        oss << "  Portfolio Return: " << (portfolio_return * 100) << "%\n";
        oss << "  Benchmark Return: " << (benchmark_return * 100) << "%\n";
        oss << "  Excess Return: " << (excess_return * 100) << "%\n";
        oss << "  Alpha: " << (alpha * 100) << "%\n";
        oss << "  Information Ratio: " << information_ratio << "\n";
        oss << "  Tracking Error: " << (tracking_error * 100) << "%\n";
        oss << "  Beta: " << beta << "\n";
        oss << "  Up Capture: " << (up_capture * 100) << "%\n";
        oss << "  Down Capture: " << (down_capture * 100) << "%\n";
        return oss.str();
    }
};

/**
 * @brief Benchmark comparison calculator
 */
class BenchmarkCalculator {
public:
    /**
     * @brief Compare portfolio to benchmark
     */
    static BenchmarkComparison compare(
        const std::vector<double>& portfolio_returns,
        const std::vector<double>& benchmark_returns,
        const std::string& benchmark_symbol = "SPY") {
        
        BenchmarkComparison result;
        result.benchmark_symbol = benchmark_symbol;
        
        size_t n = std::min(portfolio_returns.size(), benchmark_returns.size());
        if (n < 2) return result;
        
        result.observations = static_cast<int>(n);
        
        // Total returns
        double port_cum = 1.0, bench_cum = 1.0;
        for (size_t i = 0; i < n; ++i) {
            port_cum *= (1.0 + portfolio_returns[i]);
            bench_cum *= (1.0 + benchmark_returns[i]);
        }
        
        result.portfolio_return = std::pow(port_cum, 252.0 / n) - 1.0;
        result.benchmark_return = std::pow(bench_cum, 252.0 / n) - 1.0;
        result.excess_return = result.portfolio_return - result.benchmark_return;
        
        // Active returns for tracking error
        std::vector<double> active_returns(n);
        for (size_t i = 0; i < n; ++i) {
            active_returns[i] = portfolio_returns[i] - benchmark_returns[i];
        }
        
        // Tracking error
        double mean_active = std::accumulate(active_returns.begin(), 
                                             active_returns.end(), 0.0) / n;
        double sum_sq = 0;
        for (double ar : active_returns) {
            sum_sq += (ar - mean_active) * (ar - mean_active);
        }
        result.tracking_error = std::sqrt(sum_sq / (n - 1)) * std::sqrt(252);
        
        // Information ratio
        if (result.tracking_error > 0) {
            result.information_ratio = result.excess_return / result.tracking_error;
        }
        
        // Beta and R-squared
        auto beta_result = BetaCalculator::calculate(
            portfolio_returns, benchmark_returns, "", benchmark_symbol);
        result.beta = beta_result.beta;
        result.alpha = beta_result.alpha;
        result.r_squared = beta_result.r_squared;
        
        // Up/Down capture
        double up_port = 0, up_bench = 0, up_count = 0;
        double down_port = 0, down_bench = 0, down_count = 0;
        
        for (size_t i = 0; i < n; ++i) {
            if (benchmark_returns[i] > 0) {
                up_port += portfolio_returns[i];
                up_bench += benchmark_returns[i];
                up_count++;
            } else if (benchmark_returns[i] < 0) {
                down_port += portfolio_returns[i];
                down_bench += benchmark_returns[i];
                down_count++;
            }
        }
        
        if (up_count > 0 && up_bench != 0) {
            result.up_capture = (up_port / up_count) / (up_bench / up_count);
        }
        if (down_count > 0 && down_bench != 0) {
            result.down_capture = (down_port / down_count) / (down_bench / down_count);
        }
        if (result.down_capture != 0) {
            result.capture_ratio = result.up_capture / result.down_capture;
        }
        
        return result;
    }
};

// ============================================================================
// Sharpe and Sortino Ratios
// ============================================================================

/**
 * @brief Risk-adjusted return metrics
 */
struct RiskAdjustedMetrics {
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};
    double treynor_ratio{0};
    double omega_ratio{0};
    
    // Components
    double annual_return{0};
    double risk_free_rate{0};
    double volatility{0};
    double downside_deviation{0};
    double max_drawdown{0};
    double beta{0};
    
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Risk-Adjusted Metrics:\n";
        oss << "  Annual Return: " << (annual_return * 100) << "%\n";
        oss << "  Volatility: " << (volatility * 100) << "%\n";
        oss << "  Sharpe Ratio: " << sharpe_ratio << "\n";
        oss << "  Sortino Ratio: " << sortino_ratio << "\n";
        oss << "  Calmar Ratio: " << calmar_ratio << "\n";
        if (beta > 0) {
            oss << "  Treynor Ratio: " << treynor_ratio << "\n";
        }
        return oss.str();
    }
};

/**
 * @brief Risk-adjusted metrics calculator
 */
class RiskAdjustedCalculator {
public:
    /**
     * @brief Calculate all risk-adjusted metrics
     */
    static RiskAdjustedMetrics calculate(
        const std::vector<double>& returns,
        double risk_free_rate = 0.02,
        double beta = 0,
        double max_drawdown = 0) {
        
        RiskAdjustedMetrics result;
        if (returns.empty()) return result;
        
        result.observations = static_cast<int>(returns.size());
        result.risk_free_rate = risk_free_rate;
        result.beta = beta;
        result.max_drawdown = max_drawdown;
        
        // Daily risk-free rate
        double daily_rf = risk_free_rate / 252.0;
        
        // Mean return
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        result.annual_return = mean * 252;
        
        // Volatility
        double sum_sq = 0;
        double sum_down = 0;
        int down_count = 0;
        
        for (double ret : returns) {
            sum_sq += (ret - mean) * (ret - mean);
            
            // Downside deviation (vs risk-free)
            double excess = ret - daily_rf;
            if (excess < 0) {
                sum_down += excess * excess;
                down_count++;
            }
        }
        
        result.volatility = std::sqrt(sum_sq / (returns.size() - 1)) * std::sqrt(252);
        
        // Downside deviation
        if (down_count > 0) {
            result.downside_deviation = std::sqrt(sum_down / down_count) * std::sqrt(252);
        }
        
        // Sharpe ratio
        if (result.volatility > 0) {
            result.sharpe_ratio = (result.annual_return - risk_free_rate) / result.volatility;
        }
        
        // Sortino ratio
        if (result.downside_deviation > 0) {
            result.sortino_ratio = (result.annual_return - risk_free_rate) / 
                                   result.downside_deviation;
        }
        
        // Calmar ratio
        if (max_drawdown > 0) {
            result.max_drawdown = max_drawdown;
            result.calmar_ratio = result.annual_return / max_drawdown;
        }
        
        // Treynor ratio
        if (beta > 0) {
            result.treynor_ratio = (result.annual_return - risk_free_rate) / beta;
        }
        
        // Omega ratio
        double gain_sum = 0, loss_sum = 0;
        for (double ret : returns) {
            double excess = ret - daily_rf;
            if (excess > 0) gain_sum += excess;
            else loss_sum += std::abs(excess);
        }
        if (loss_sum > 0) {
            result.omega_ratio = gain_sum / loss_sum;
        }
        
        return result;
    }
};

// ============================================================================
// Rolling Returns
// ============================================================================

/**
 * @brief Rolling return point
 */
struct RollingReturn {
    std::string end_date;
    int window_days{0};
    double return_value{0};
    double annualized{0};
    double volatility{0};
    double sharpe{0};
};

/**
 * @brief Rolling returns result
 */
struct RollingReturnsResult {
    int window_days{0};
    std::vector<RollingReturn> returns;
    
    // Statistics across rolling windows
    double avg_return{0};
    double best_return{0};
    double worst_return{0};
    double pct_positive{0};          // % of windows with positive return
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Rolling " << window_days << "-Day Returns:\n";
        oss << "  Average: " << (avg_return * 100) << "%\n";
        oss << "  Best: " << (best_return * 100) << "%\n";
        oss << "  Worst: " << (worst_return * 100) << "%\n";
        oss << "  % Positive: " << (pct_positive * 100) << "%\n";
        oss << "  Windows: " << returns.size() << "\n";
        return oss.str();
    }
};

/**
 * @brief Rolling returns calculator
 */
class RollingReturnsCalculator {
public:
    /**
     * @brief Calculate rolling returns
     */
    static RollingReturnsResult calculate(
        const std::vector<double>& daily_returns,
        const std::vector<std::string>& dates,
        int window_days = 252) {
        
        RollingReturnsResult result;
        result.window_days = window_days;
        
        if (daily_returns.size() < static_cast<size_t>(window_days)) {
            return result;
        }
        
        size_t n = daily_returns.size();
        double sum_returns = 0;
        double best = -1e10, worst = 1e10;
        int positive_count = 0;
        
        for (size_t i = window_days; i <= n; ++i) {
            RollingReturn rr;
            rr.window_days = window_days;
            if (i <= dates.size()) rr.end_date = dates[i - 1];
            
            // Calculate return for window
            double cum = 1.0;
            double sum = 0;
            std::vector<double> window_rets;
            window_rets.reserve(window_days);
            
            for (size_t j = i - window_days; j < i; ++j) {
                cum *= (1.0 + daily_returns[j]);
                sum += daily_returns[j];
                window_rets.push_back(daily_returns[j]);
            }
            
            rr.return_value = cum - 1.0;
            rr.annualized = std::pow(cum, 252.0 / window_days) - 1.0;
            
            // Rolling volatility
            double mean = sum / window_days;
            double sum_sq = 0;
            for (double ret : window_rets) {
                sum_sq += (ret - mean) * (ret - mean);
            }
            rr.volatility = std::sqrt(sum_sq / (window_days - 1)) * std::sqrt(252);
            
            // Rolling Sharpe (assuming 2% RF)
            if (rr.volatility > 0) {
                rr.sharpe = (rr.annualized - 0.02) / rr.volatility;
            }
            
            result.returns.push_back(rr);
            
            // Track statistics
            sum_returns += rr.annualized;
            if (rr.annualized > best) best = rr.annualized;
            if (rr.annualized < worst) worst = rr.annualized;
            if (rr.return_value > 0) positive_count++;
        }
        
        if (!result.returns.empty()) {
            result.avg_return = sum_returns / result.returns.size();
            result.best_return = best;
            result.worst_return = worst;
            result.pct_positive = static_cast<double>(positive_count) / result.returns.size();
        }
        
        return result;
    }
    
    /**
     * @brief Calculate multiple rolling windows
     */
    static std::map<int, RollingReturnsResult> calculate_multiple(
        const std::vector<double>& daily_returns,
        const std::vector<std::string>& dates,
        const std::vector<int>& windows = {21, 63, 126, 252}) {
        
        std::map<int, RollingReturnsResult> results;
        
        for (int window : windows) {
            results[window] = calculate(daily_returns, dates, window);
        }
        
        return results;
    }
};

// ============================================================================
// Performance Attribution
// ============================================================================

/**
 * @brief Attribution effect for a sector/factor
 */
struct AttributionEffect {
    std::string name;
    double allocation_effect{0};     // Weight decision effect
    double selection_effect{0};      // Security selection effect
    double interaction_effect{0};    // Combined effect
    double total_effect{0};          // Sum of all effects
    
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
};

/**
 * @brief Attribution analysis result
 */
struct AttributionResult {
    double total_return{0};
    double benchmark_return{0};
    double active_return{0};
    
    double total_allocation{0};
    double total_selection{0};
    double total_interaction{0};
    
    std::vector<AttributionEffect> effects;
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        
        oss << "Performance Attribution:\n";
        oss << "  Portfolio Return: " << (total_return * 100) << "%\n";
        oss << "  Benchmark Return: " << (benchmark_return * 100) << "%\n";
        oss << "  Active Return: " << (active_return * 100) << "%\n\n";
        
        oss << "Effect Decomposition:\n";
        oss << "  Allocation Effect: " << (total_allocation * 100) << "%\n";
        oss << "  Selection Effect: " << (total_selection * 100) << "%\n";
        oss << "  Interaction Effect: " << (total_interaction * 100) << "%\n\n";
        
        if (!effects.empty()) {
            oss << std::left << std::setw(12) << "Sector"
                << std::right << std::setw(10) << "Alloc"
                << std::setw(10) << "Select"
                << std::setw(10) << "Total"
                << "\n";
            oss << std::string(42, '-') << "\n";
            
            for (const auto& e : effects) {
                oss << std::left << std::setw(12) << e.name.substr(0, 10)
                    << std::right << std::setw(9) << (e.allocation_effect * 100) << "%"
                    << std::setw(9) << (e.selection_effect * 100) << "%"
                    << std::setw(9) << (e.total_effect * 100) << "%"
                    << "\n";
            }
        }
        
        return oss.str();
    }
};

/**
 * @brief Brinson-Fachler attribution calculator
 */
class AttributionCalculator {
public:
    /**
     * @brief Calculate single-period Brinson-Fachler attribution
     */
    static AttributionResult calculate_brinson(
        const std::map<std::string, double>& portfolio_weights,
        const std::map<std::string, double>& benchmark_weights,
        const std::map<std::string, double>& portfolio_returns,
        const std::map<std::string, double>& benchmark_returns) {
        
        AttributionResult result;
        
        // Get all sectors
        std::set<std::string> sectors;
        for (const auto& [k, v] : portfolio_weights) sectors.insert(k);
        for (const auto& [k, v] : benchmark_weights) sectors.insert(k);
        
        // Calculate benchmark total return
        for (const auto& [sector, weight] : benchmark_weights) {
            auto it = benchmark_returns.find(sector);
            if (it != benchmark_returns.end()) {
                result.benchmark_return += weight * it->second;
            }
        }
        
        // Calculate attribution for each sector
        for (const auto& sector : sectors) {
            AttributionEffect effect;
            effect.name = sector;
            
            // Get weights (default to 0 if missing)
            auto pw_it = portfolio_weights.find(sector);
            auto bw_it = benchmark_weights.find(sector);
            effect.portfolio_weight = pw_it != portfolio_weights.end() ? pw_it->second : 0;
            effect.benchmark_weight = bw_it != benchmark_weights.end() ? bw_it->second : 0;
            
            // Get returns
            auto pr_it = portfolio_returns.find(sector);
            auto br_it = benchmark_returns.find(sector);
            effect.portfolio_return = pr_it != portfolio_returns.end() ? pr_it->second : 0;
            effect.benchmark_return = br_it != benchmark_returns.end() ? br_it->second : 0;
            
            // Brinson-Fachler effects
            double weight_diff = effect.portfolio_weight - effect.benchmark_weight;
            double return_diff = effect.portfolio_return - effect.benchmark_return;
            
            // Allocation effect
            effect.allocation_effect = weight_diff * 
                (effect.benchmark_return - result.benchmark_return);
            
            // Selection effect
            effect.selection_effect = effect.benchmark_weight * return_diff;
            
            // Interaction effect
            effect.interaction_effect = weight_diff * return_diff;
            
            // Total
            effect.total_effect = effect.allocation_effect + 
                                  effect.selection_effect + 
                                  effect.interaction_effect;
            
            result.effects.push_back(effect);
            
            // Accumulate totals
            result.total_allocation += effect.allocation_effect;
            result.total_selection += effect.selection_effect;
            result.total_interaction += effect.interaction_effect;
            result.total_return += effect.portfolio_weight * effect.portfolio_return;
        }
        
        result.active_return = result.total_return - result.benchmark_return;
        
        return result;
    }
    
    /**
     * @brief Calculate multi-period attribution (linking)
     */
    static AttributionResult calculate_linked(
        const std::vector<AttributionResult>& period_results) {
        
        AttributionResult result;
        if (period_results.empty()) return result;
        
        // Geometric linking of returns
        double port_cum = 1.0, bench_cum = 1.0;
        
        for (const auto& pr : period_results) {
            port_cum *= (1.0 + pr.total_return);
            bench_cum *= (1.0 + pr.benchmark_return);
        }
        
        result.total_return = port_cum - 1.0;
        result.benchmark_return = bench_cum - 1.0;
        result.active_return = result.total_return - result.benchmark_return;
        
        // Simple sum of effects (approximation)
        for (const auto& pr : period_results) {
            result.total_allocation += pr.total_allocation;
            result.total_selection += pr.total_selection;
            result.total_interaction += pr.total_interaction;
        }
        
        // Aggregate sector effects
        std::map<std::string, AttributionEffect> sector_effects;
        
        for (const auto& pr : period_results) {
            for (const auto& e : pr.effects) {
                auto& agg = sector_effects[e.name];
                agg.name = e.name;
                agg.allocation_effect += e.allocation_effect;
                agg.selection_effect += e.selection_effect;
                agg.interaction_effect += e.interaction_effect;
                agg.total_effect += e.total_effect;
            }
        }
        
        for (auto& [name, effect] : sector_effects) {
            result.effects.push_back(effect);
        }
        
        return result;
    }
};

// ============================================================================
// Performance Report
// ============================================================================

/**
 * @brief Comprehensive performance report
 */
struct PerformanceReport {
    std::string portfolio_name;
    std::string benchmark_name;
    std::string period;
    
    TWRResult returns;
    BenchmarkComparison benchmark;
    RiskAdjustedMetrics risk_metrics;
    std::map<int, RollingReturnsResult> rolling_returns;
    AttributionResult attribution;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "=== Performance Report: " << portfolio_name << " ===\n";
        oss << "Period: " << period << "\n";
        oss << "Benchmark: " << benchmark_name << "\n\n";
        
        oss << returns.format() << "\n";
        oss << benchmark.format() << "\n";
        oss << risk_metrics.format() << "\n";
        
        // Rolling returns summary
        oss << "Rolling Returns:\n";
        for (const auto& [window, rr] : rolling_returns) {
            oss << "  " << window << "-day: avg " 
                << std::fixed << std::setprecision(1) 
                << (rr.avg_return * 100) << "%, "
                << "positive " << (rr.pct_positive * 100) << "%\n";
        }
        
        if (!attribution.effects.empty()) {
            oss << "\n" << attribution.format();
        }
        
        return oss.str();
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_REAL_PERFORMANCE_HPP
