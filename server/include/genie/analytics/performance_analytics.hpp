/**
 * @file performance_analytics.hpp
 * @brief Real performance analytics from actual market data
 * @version 5.3.1
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
#ifndef GENIE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP
#define GENIE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP

#include "../market/data_manager.hpp"
#include "risk_analytics.hpp"
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
    double cash_flow{0};              // External cash flow
    double market_value{0};           // Before cash flow
    double daily_return{0};
    double cumulative_return{0};
};

/**
 * @brief TWR calculation result
 */
struct TWRResult {
    double total_return{0};           // Total period return
    double annualized_return{0};      // Annualized return
    double cagr{0};                   // CAGR
    int trading_days{0};
    int calendar_days{0};
    std::string start_date;
    std::string end_date;
    double start_value{0};
    double end_value{0};
    double net_cash_flows{0};
    double money_weighted_return{0};  // MWR/IRR
    
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
        oss << "  Net Cash Flows: $" << net_cash_flows << "\n";
        return oss.str();
    }
};

/**
 * @brief TWR calculator (GIPS-compliant)
 */
class TWRCalculator {
public:
    /**
     * @brief Calculate TWR from NAV series
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
        
        // Link daily returns (Modified Dietz for sub-periods with cash flows)
        double cumulative = 1.0;
        result.daily_values.reserve(navs.size());
        
        for (size_t i = 0; i < navs.size(); ++i) {
            ValuationPoint point;
            if (i < dates.size()) point.date = dates[i];
            point.nav = navs[i];
            point.market_value = navs[i];
            
            double cf = 0;
            if (i > 0 && i < cash_flows.size()) {
                cf = cash_flows[i];
                result.net_cash_flows += cf;
            }
            point.cash_flow = cf;
            
            // Daily return with cash flow adjustment
            if (i > 0) {
                double prev_nav = navs[i - 1];
                if (prev_nav > 0) {
                    // Adjust for cash flow (assuming at start of period)
                    point.daily_return = (navs[i] - prev_nav - cf) / prev_nav;
                }
                cumulative *= (1.0 + point.daily_return);
            }
            
            point.cumulative_return = cumulative - 1.0;
            result.daily_values.push_back(point);
        }
        
        result.total_return = cumulative - 1.0;
        
        // Annualize (252 trading days)
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
     * @brief Calculate TWR from price bars
     */
    static TWRResult calculate_from_prices(const std::vector<market::PriceBar>& bars) {
        std::vector<double> prices;
        std::vector<std::string> dates;
        
        for (const auto& bar : bars) {
            double price = bar.adjusted_close > 0 ? bar.adjusted_close : bar.close;
            prices.push_back(price);
            dates.push_back(bar.date);
        }
        
        return calculate(prices, dates);
    }
};

// ============================================================================
// Benchmark Comparison
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
    double alpha{0};                 // Jensen's alpha
    
    // Relative metrics
    double information_ratio{0};
    double tracking_error{0};
    double beta{0};
    double r_squared{0};
    
    // Capture ratios
    double up_capture{0};            // Upside capture
    double down_capture{0};          // Downside capture
    double capture_ratio{0};         // Up/Down
    
    // Consistency
    double win_rate{0};              // % of periods beating benchmark
    double avg_win{0};
    double avg_loss{0};
    
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Benchmark Comparison vs " << benchmark_symbol << ":\n";
        oss << "  Portfolio Return: " << (portfolio_return * 100) << "% (annualized)\n";
        oss << "  Benchmark Return: " << (benchmark_return * 100) << "% (annualized)\n";
        oss << "  Excess Return: " << (excess_return * 100) << "%\n";
        oss << "  Alpha: " << (alpha * 100) << "% (annualized)\n";
        oss << "  Beta: " << std::setprecision(4) << beta << "\n";
        oss << "  Information Ratio: " << information_ratio << "\n";
        oss << "  Tracking Error: " << std::setprecision(2) << (tracking_error * 100) << "%\n";
        oss << "  Up Capture: " << (up_capture * 100) << "%\n";
        oss << "  Down Capture: " << (down_capture * 100) << "%\n";
        oss << "  Win Rate: " << (win_rate * 100) << "%\n";
        return oss.str();
    }
};

/**
 * @brief Benchmark calculator
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
        
        // Calculate cumulative returns
        double port_cum = 1.0, bench_cum = 1.0;
        for (size_t i = 0; i < n; ++i) {
            port_cum *= (1.0 + portfolio_returns[i]);
            bench_cum *= (1.0 + benchmark_returns[i]);
        }
        
        // Annualize
        double years = n / 252.0;
        result.portfolio_return = std::pow(port_cum, 1.0 / years) - 1.0;
        result.benchmark_return = std::pow(bench_cum, 1.0 / years) - 1.0;
        result.excess_return = result.portfolio_return - result.benchmark_return;
        
        // Active returns for tracking error
        std::vector<double> active(n);
        double sum_active = 0;
        for (size_t i = 0; i < n; ++i) {
            active[i] = portfolio_returns[i] - benchmark_returns[i];
            sum_active += active[i];
        }
        double mean_active = sum_active / n;
        
        double var_active = 0;
        for (double ar : active) {
            var_active += (ar - mean_active) * (ar - mean_active);
        }
        result.tracking_error = std::sqrt(var_active / (n - 1)) * std::sqrt(252);
        
        // Information ratio
        if (result.tracking_error > 0) {
            result.information_ratio = result.excess_return / result.tracking_error;
        }
        
        // Beta and alpha
        auto beta_result = BetaCalculator::calculate(
            portfolio_returns, benchmark_returns, "", benchmark_symbol);
        result.beta = beta_result.beta;
        result.alpha = beta_result.alpha;
        result.r_squared = beta_result.r_squared;
        
        // Up/Down capture
        double up_port = 0, up_bench = 0;
        double down_port = 0, down_bench = 0;
        int up_count = 0, down_count = 0;
        int wins = 0;
        double win_sum = 0, loss_sum = 0;
        int loss_count = 0;
        
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
            
            // Win/loss tracking
            if (portfolio_returns[i] > benchmark_returns[i]) {
                wins++;
                win_sum += active[i];
            } else {
                loss_sum += active[i];
                loss_count++;
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
        
        result.win_rate = static_cast<double>(wins) / n;
        result.avg_win = wins > 0 ? win_sum / wins : 0;
        result.avg_loss = loss_count > 0 ? std::abs(loss_sum / loss_count) : 0;
        
        return result;
    }
};

// ============================================================================
// Sharpe and Sortino Ratios
// ============================================================================

/**
 * @brief Risk-adjusted metrics
 */
struct RiskAdjustedMetrics {
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};
    double treynor_ratio{0};
    double omega_ratio{0};
    double information_ratio{0};
    
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
        oss << "  Omega Ratio: " << omega_ratio << "\n";
        return oss.str();
    }
};

/**
 * @brief Calculate risk-adjusted metrics
 */
class RiskAdjustedCalculator {
public:
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
        
        double daily_rf = risk_free_rate / 252.0;
        
        // Mean return
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        result.annual_return = mean * 252;
        
        // Volatility and downside deviation
        double sum_sq = 0, sum_down = 0;
        int down_count = 0;
        double gain_sum = 0, loss_sum = 0;
        
        for (double ret : returns) {
            sum_sq += (ret - mean) * (ret - mean);
            
            double excess = ret - daily_rf;
            if (excess < 0) {
                sum_down += excess * excess;
                down_count++;
                loss_sum += std::abs(excess);
            } else {
                gain_sum += excess;
            }
        }
        
        result.volatility = std::sqrt(sum_sq / (returns.size() - 1)) * std::sqrt(252);
        
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
            result.calmar_ratio = result.annual_return / max_drawdown;
        }
        
        // Treynor ratio
        if (beta > 0) {
            result.treynor_ratio = (result.annual_return - risk_free_rate) / beta;
        }
        
        // Omega ratio
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
    double total_return{0};
    double annualized_return{0};
    double volatility{0};
    double sharpe{0};
};

/**
 * @brief Rolling returns result
 */
struct RollingReturnsResult {
    int window_days{0};
    std::vector<RollingReturn> returns;
    
    // Statistics
    double avg_return{0};
    double best_return{0};
    double worst_return{0};
    double pct_positive{0};
    double percentile_25{0};
    double percentile_75{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Rolling " << window_days << "-Day Returns:\n";
        oss << "  Average: " << (avg_return * 100) << "%\n";
        oss << "  Best: " << (best_return * 100) << "%\n";
        oss << "  Worst: " << (worst_return * 100) << "%\n";
        oss << "  25th Pctl: " << (percentile_25 * 100) << "%\n";
        oss << "  75th Pctl: " << (percentile_75 * 100) << "%\n";
        oss << "  % Positive: " << (pct_positive * 100) << "%\n";
        oss << "  Observations: " << returns.size() << "\n";
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
        
        std::vector<double> annualized_returns;
        double sum_ret = 0;
        int positive_count = 0;
        
        for (size_t i = window_days; i <= daily_returns.size(); ++i) {
            RollingReturn rr;
            rr.window_days = window_days;
            if (i <= dates.size()) rr.end_date = dates[i - 1];
            
            // Calculate window return
            double cum = 1.0;
            double sum = 0;
            std::vector<double> window_rets;
            
            for (size_t j = i - window_days; j < i; ++j) {
                cum *= (1.0 + daily_returns[j]);
                sum += daily_returns[j];
                window_rets.push_back(daily_returns[j]);
            }
            
            rr.total_return = cum - 1.0;
            rr.annualized_return = std::pow(cum, 252.0 / window_days) - 1.0;
            
            // Window volatility
            double mean = sum / window_days;
            double var = 0;
            for (double r : window_rets) var += (r - mean) * (r - mean);
            rr.volatility = std::sqrt(var / (window_days - 1)) * std::sqrt(252);
            
            // Window Sharpe (assuming 2% RF)
            if (rr.volatility > 0) {
                rr.sharpe = (rr.annualized_return - 0.02) / rr.volatility;
            }
            
            result.returns.push_back(rr);
            annualized_returns.push_back(rr.annualized_return);
            sum_ret += rr.annualized_return;
            if (rr.total_return > 0) positive_count++;
        }
        
        if (!result.returns.empty()) {
            result.avg_return = sum_ret / result.returns.size();
            result.pct_positive = static_cast<double>(positive_count) / result.returns.size();
            
            // Sort for percentiles
            std::sort(annualized_returns.begin(), annualized_returns.end());
            result.worst_return = annualized_returns.front();
            result.best_return = annualized_returns.back();
            
            size_t n = annualized_returns.size();
            result.percentile_25 = annualized_returns[n / 4];
            result.percentile_75 = annualized_returns[3 * n / 4];
        }
        
        return result;
    }
    
    /**
     * @brief Calculate multiple windows
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
 * @brief Attribution effect
 */
struct AttributionEffect {
    std::string name;
    double allocation_effect{0};     // Weight decision
    double selection_effect{0};      // Security selection
    double interaction_effect{0};    // Combined effect
    double total_effect{0};
    
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
};

/**
 * @brief Attribution result
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
        oss << "  Allocation: " << (total_allocation * 100) << "%\n";
        oss << "  Selection: " << (total_selection * 100) << "%\n";
        oss << "  Interaction: " << (total_interaction * 100) << "%\n\n";
        
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
     * @brief Calculate single-period attribution
     */
    static AttributionResult calculate(
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
            
            // Weights
            auto pw = portfolio_weights.find(sector);
            auto bw = benchmark_weights.find(sector);
            effect.portfolio_weight = pw != portfolio_weights.end() ? pw->second : 0;
            effect.benchmark_weight = bw != benchmark_weights.end() ? bw->second : 0;
            
            // Returns
            auto pr = portfolio_returns.find(sector);
            auto br = benchmark_returns.find(sector);
            effect.portfolio_return = pr != portfolio_returns.end() ? pr->second : 0;
            effect.benchmark_return = br != benchmark_returns.end() ? br->second : 0;
            
            // Brinson-Fachler effects
            double weight_diff = effect.portfolio_weight - effect.benchmark_weight;
            double return_diff = effect.portfolio_return - effect.benchmark_return;
            
            effect.allocation_effect = weight_diff * 
                (effect.benchmark_return - result.benchmark_return);
            effect.selection_effect = effect.benchmark_weight * return_diff;
            effect.interaction_effect = weight_diff * return_diff;
            effect.total_effect = effect.allocation_effect + 
                                  effect.selection_effect + 
                                  effect.interaction_effect;
            
            result.effects.push_back(effect);
            
            result.total_allocation += effect.allocation_effect;
            result.total_selection += effect.selection_effect;
            result.total_interaction += effect.interaction_effect;
            result.total_return += effect.portfolio_weight * effect.portfolio_return;
        }
        
        result.active_return = result.total_return - result.benchmark_return;
        
        return result;
    }
    
    /**
     * @brief Link multi-period attribution
     */
    static AttributionResult link_periods(
        const std::vector<AttributionResult>& periods) {
        
        AttributionResult result;
        if (periods.empty()) return result;
        
        // Geometric linking of returns
        double port_cum = 1.0, bench_cum = 1.0;
        for (const auto& p : periods) {
            port_cum *= (1.0 + p.total_return);
            bench_cum *= (1.0 + p.benchmark_return);
        }
        
        result.total_return = port_cum - 1.0;
        result.benchmark_return = bench_cum - 1.0;
        result.active_return = result.total_return - result.benchmark_return;
        
        // Sum effects (approximation for short periods)
        for (const auto& p : periods) {
            result.total_allocation += p.total_allocation;
            result.total_selection += p.total_selection;
            result.total_interaction += p.total_interaction;
        }
        
        // Aggregate sector effects
        std::map<std::string, AttributionEffect> agg;
        for (const auto& p : periods) {
            for (const auto& e : p.effects) {
                auto& a = agg[e.name];
                a.name = e.name;
                a.allocation_effect += e.allocation_effect;
                a.selection_effect += e.selection_effect;
                a.interaction_effect += e.interaction_effect;
                a.total_effect += e.total_effect;
            }
        }
        
        for (const auto& [name, effect] : agg) {
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
    
    TWRResult twr;
    BenchmarkComparison benchmark;
    RiskAdjustedMetrics risk_metrics;
    std::map<int, RollingReturnsResult> rolling;
    AttributionResult attribution;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "=== Performance Report: " << portfolio_name << " ===\n";
        oss << "Period: " << period << "\n";
        oss << "Benchmark: " << benchmark_name << "\n\n";
        
        oss << twr.format() << "\n";
        oss << benchmark.format() << "\n";
        oss << risk_metrics.format() << "\n";
        
        // Rolling returns summary
        oss << "Rolling Returns:\n";
        for (const auto& [window, rr] : rolling) {
            oss << "  " << window << "-day: avg " << std::fixed << std::setprecision(1)
                << (rr.avg_return * 100) << "%, positive " 
                << (rr.pct_positive * 100) << "%\n";
        }
        
        if (!attribution.effects.empty()) {
            oss << "\n" << attribution.format();
        }
        
        return oss.str();
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_PERFORMANCE_ANALYTICS_HPP
