/**
 * @file risk_analytics.hpp
 * @brief Real risk analytics from actual market data
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Production-grade risk calculations using real price data:
 * - Value at Risk (VaR) from historical returns
 * - Correlation matrix from real price series
 * - Beta calculation against real benchmarks
 * - Volatility from actual return distributions
 * - Drawdown tracking from real NAV series
 * - Conditional VaR (CVaR/Expected Shortfall)
 * - Risk decomposition and attribution
 */
#pragma once
#ifndef GENIE_ANALYTICS_RISK_ANALYTICS_HPP
#define GENIE_ANALYTICS_RISK_ANALYTICS_HPP

#include "../market/data_manager.hpp"
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
// Value at Risk (VaR)
// ============================================================================

/**
 * @brief VaR calculation method
 */
enum class VaRMethod {
    Historical,       // Historical simulation
    Parametric,       // Normal distribution assumption
    CornishFisher,    // Adjusted for skewness/kurtosis
    MonteCarlo        // Monte Carlo simulation
};

/**
 * @brief VaR calculation result
 */
struct VaRResult {
    double var{0};                    // Value at Risk (loss amount)
    double var_pct{0};                // VaR as percentage
    double cvar{0};                   // Conditional VaR / Expected Shortfall
    double cvar_pct{0};               // CVaR as percentage
    double confidence{0.95};          // Confidence level
    int horizon_days{1};              // Time horizon
    VaRMethod method;
    
    // Distribution statistics
    double mean_return{0};
    double std_dev{0};
    double skewness{0};
    double kurtosis{0};
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "VaR Analysis (" << (confidence * 100) << "% confidence, " 
            << horizon_days << "-day horizon):\n";
        oss << "  VaR: " << (var_pct * 100) << "% ($" << std::setprecision(2) 
            << var << ")\n";
        oss << "  CVaR/ES: " << std::setprecision(4) << (cvar_pct * 100) << "% ($" 
            << std::setprecision(2) << cvar << ")\n";
        oss << "  Observations: " << observations << "\n";
        oss << "  Volatility: " << std::setprecision(4) << (std_dev * 100 * std::sqrt(252)) 
            << "% (annualized)\n";
        return oss.str();
    }
};

/**
 * @brief VaR calculator
 */
class VaRCalculator {
public:
    /**
     * @brief Calculate VaR from returns
     */
    static VaRResult calculate(
        const std::vector<double>& returns,
        double portfolio_value,
        double confidence = 0.95,
        int horizon_days = 1,
        VaRMethod method = VaRMethod::Historical) {
        
        VaRResult result;
        result.confidence = confidence;
        result.horizon_days = horizon_days;
        result.method = method;
        result.observations = static_cast<int>(returns.size());
        
        if (returns.empty()) return result;
        
        // Calculate statistics
        result.mean_return = mean(returns);
        result.std_dev = std_dev(returns, result.mean_return);
        result.skewness = calc_skewness(returns, result.mean_return, result.std_dev);
        result.kurtosis = calc_kurtosis(returns, result.mean_return, result.std_dev);
        
        // Calculate VaR based on method
        switch (method) {
            case VaRMethod::Historical:
                calc_historical_var(returns, result);
                break;
            case VaRMethod::Parametric:
                calc_parametric_var(result);
                break;
            case VaRMethod::CornishFisher:
                calc_cornish_fisher_var(result);
                break;
            case VaRMethod::MonteCarlo:
                calc_monte_carlo_var(result);
                break;
        }
        
        // Scale to horizon if > 1 day
        if (horizon_days > 1) {
            double scale = std::sqrt(static_cast<double>(horizon_days));
            result.var_pct *= scale;
            result.cvar_pct *= scale;
        }
        
        // Convert to dollar amounts
        result.var = result.var_pct * portfolio_value;
        result.cvar = result.cvar_pct * portfolio_value;
        
        return result;
    }
    
    /**
     * @brief Calculate VaR from price bars
     */
    static VaRResult calculate_from_prices(
        const std::vector<market::PriceBar>& bars,
        double portfolio_value,
        double confidence = 0.95,
        int horizon_days = 1,
        VaRMethod method = VaRMethod::Historical) {
        
        auto returns = calculate_returns(bars);
        return calculate(returns, portfolio_value, confidence, horizon_days, method);
    }
    
    /**
     * @brief Calculate portfolio VaR with weights
     */
    static VaRResult calculate_portfolio(
        const std::map<std::string, std::vector<double>>& asset_returns,
        const std::map<std::string, double>& weights,
        double portfolio_value,
        double confidence = 0.95,
        int horizon_days = 1) {
        
        // Calculate portfolio returns
        size_t min_obs = SIZE_MAX;
        for (const auto& [sym, rets] : asset_returns) {
            if (rets.size() < min_obs) min_obs = rets.size();
        }
        
        std::vector<double> portfolio_returns(min_obs, 0);
        
        for (size_t i = 0; i < min_obs; ++i) {
            double port_ret = 0;
            for (const auto& [sym, rets] : asset_returns) {
                auto wit = weights.find(sym);
                if (wit != weights.end() && i < rets.size()) {
                    port_ret += wit->second * rets[i];
                }
            }
            portfolio_returns[i] = port_ret;
        }
        
        return calculate(portfolio_returns, portfolio_value, confidence, horizon_days);
    }

private:
    static double mean(const std::vector<double>& v) {
        if (v.empty()) return 0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
    
    static double std_dev(const std::vector<double>& v, double m) {
        if (v.size() < 2) return 0;
        double sum_sq = 0;
        for (double x : v) sum_sq += (x - m) * (x - m);
        return std::sqrt(sum_sq / (v.size() - 1));
    }
    
    static double calc_skewness(const std::vector<double>& v, double m, double s) {
        if (v.size() < 3 || s == 0) return 0;
        double n = static_cast<double>(v.size());
        double sum = 0;
        for (double x : v) sum += std::pow((x - m) / s, 3);
        return sum * n / ((n - 1) * (n - 2));
    }
    
    static double calc_kurtosis(const std::vector<double>& v, double m, double s) {
        if (v.size() < 4 || s == 0) return 0;
        double n = static_cast<double>(v.size());
        double sum = 0;
        for (double x : v) sum += std::pow((x - m) / s, 4);
        return (n * (n + 1) * sum / ((n - 1) * (n - 2) * (n - 3))) -
               (3 * (n - 1) * (n - 1) / ((n - 2) * (n - 3)));
    }
    
    static double normal_quantile(double p) {
        // Approximation of inverse standard normal CDF
        if (p <= 0) return -8;
        if (p >= 1) return 8;
        
        double t = std::sqrt(-2.0 * std::log(p < 0.5 ? p : 1 - p));
        double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        
        double z = t - (c0 + c1 * t + c2 * t * t) /
                      (1 + d1 * t + d2 * t * t + d3 * t * t * t);
        
        return p < 0.5 ? -z : z;
    }
    
    static void calc_historical_var(const std::vector<double>& returns, VaRResult& result) {
        std::vector<double> sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        
        size_t var_idx = static_cast<size_t>((1 - result.confidence) * sorted.size());
        if (var_idx >= sorted.size()) var_idx = sorted.size() - 1;
        
        result.var_pct = -sorted[var_idx];  // VaR is positive for losses
        
        // CVaR = average of returns worse than VaR
        double sum = 0;
        for (size_t i = 0; i <= var_idx; ++i) {
            sum += sorted[i];
        }
        result.cvar_pct = var_idx > 0 ? -sum / (var_idx + 1) : result.var_pct;
    }
    
    static void calc_parametric_var(VaRResult& result) {
        double z = normal_quantile(1 - result.confidence);
        result.var_pct = -(result.mean_return + z * result.std_dev);
        
        // CVaR for normal distribution
        double pdf_z = std::exp(-z * z / 2) / std::sqrt(2 * M_PI);
        result.cvar_pct = -(result.mean_return - result.std_dev * pdf_z / (1 - result.confidence));
    }
    
    static void calc_cornish_fisher_var(VaRResult& result) {
        double z = normal_quantile(1 - result.confidence);
        double s = result.skewness;
        double k = result.kurtosis;
        
        // Cornish-Fisher expansion
        double z_cf = z + (z * z - 1) * s / 6 +
                      (z * z * z - 3 * z) * k / 24 -
                      (2 * z * z * z - 5 * z) * s * s / 36;
        
        result.var_pct = -(result.mean_return + z_cf * result.std_dev);
        result.cvar_pct = result.var_pct * 1.1;  // Approximation
    }
    
    static void calc_monte_carlo_var(VaRResult& result) {
        const int n_sims = 10000;
        std::vector<double> simulated(n_sims);
        
        // Simple LCG for reproducibility
        uint64_t seed = 42;
        auto lcg = [&seed]() -> double {
            seed = seed * 6364136223846793005ULL + 1;
            return static_cast<double>(seed >> 33) / static_cast<double>(1ULL << 31);
        };
        
        // Box-Muller transform
        for (int i = 0; i < n_sims; i += 2) {
            double u1 = lcg();
            double u2 = lcg();
            double z1 = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
            double z2 = std::sqrt(-2 * std::log(u1)) * std::sin(2 * M_PI * u2);
            
            simulated[i] = result.mean_return + z1 * result.std_dev;
            if (i + 1 < n_sims) {
                simulated[i + 1] = result.mean_return + z2 * result.std_dev;
            }
        }
        
        // Calculate VaR from simulations
        std::sort(simulated.begin(), simulated.end());
        size_t var_idx = static_cast<size_t>((1 - result.confidence) * n_sims);
        result.var_pct = -simulated[var_idx];
        
        double sum = 0;
        for (size_t i = 0; i <= var_idx; ++i) sum += simulated[i];
        result.cvar_pct = -(sum / (var_idx + 1));
    }
    
    static std::vector<double> calculate_returns(const std::vector<market::PriceBar>& bars) {
        std::vector<double> returns;
        if (bars.size() < 2) return returns;
        
        returns.reserve(bars.size() - 1);
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i-1].close > 0) {
                double ret = (bars[i].close - bars[i-1].close) / bars[i-1].close;
                returns.push_back(ret);
            }
        }
        return returns;
    }
};

// ============================================================================
// Correlation Matrix
// ============================================================================

/**
 * @brief Correlation matrix result
 */
struct CorrelationMatrix {
    std::vector<std::string> symbols;
    std::vector<std::vector<double>> matrix;
    int observations{0};
    
    double get(const std::string& sym1, const std::string& sym2) const {
        auto it1 = std::find(symbols.begin(), symbols.end(), sym1);
        auto it2 = std::find(symbols.begin(), symbols.end(), sym2);
        if (it1 == symbols.end() || it2 == symbols.end()) return 0;
        
        size_t i = std::distance(symbols.begin(), it1);
        size_t j = std::distance(symbols.begin(), it2);
        return matrix[i][j];
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        
        oss << "Correlation Matrix (" << observations << " observations):\n";
        
        // Header
        oss << std::setw(8) << "";
        for (const auto& sym : symbols) {
            oss << std::setw(8) << sym.substr(0, 6);
        }
        oss << "\n";
        
        // Data
        for (size_t i = 0; i < symbols.size(); ++i) {
            oss << std::setw(8) << symbols[i].substr(0, 6);
            for (size_t j = 0; j < symbols.size(); ++j) {
                oss << std::setw(8) << matrix[i][j];
            }
            oss << "\n";
        }
        
        return oss.str();
    }
};

/**
 * @brief Correlation calculator
 */
class CorrelationCalculator {
public:
    /**
     * @brief Calculate correlation between two series
     */
    static double correlation(const std::vector<double>& x, const std::vector<double>& y) {
        size_t n = std::min(x.size(), y.size());
        if (n < 2) return 0;
        
        double sum_x = 0, sum_y = 0, sum_xy = 0;
        double sum_x2 = 0, sum_y2 = 0;
        
        for (size_t i = 0; i < n; ++i) {
            sum_x += x[i];
            sum_y += y[i];
            sum_xy += x[i] * y[i];
            sum_x2 += x[i] * x[i];
            sum_y2 += y[i] * y[i];
        }
        
        double num = n * sum_xy - sum_x * sum_y;
        double den = std::sqrt((n * sum_x2 - sum_x * sum_x) *
                              (n * sum_y2 - sum_y * sum_y));
        
        return den != 0 ? num / den : 0;
    }
    
    /**
     * @brief Calculate covariance
     */
    static double covariance(const std::vector<double>& x, const std::vector<double>& y) {
        size_t n = std::min(x.size(), y.size());
        if (n < 2) return 0;
        
        double mean_x = std::accumulate(x.begin(), x.begin() + n, 0.0) / n;
        double mean_y = std::accumulate(y.begin(), y.begin() + n, 0.0) / n;
        
        double cov = 0;
        for (size_t i = 0; i < n; ++i) {
            cov += (x[i] - mean_x) * (y[i] - mean_y);
        }
        
        return cov / (n - 1);
    }
    
    /**
     * @brief Build correlation matrix from returns
     */
    static CorrelationMatrix build_matrix(
        const std::map<std::string, std::vector<double>>& returns) {
        
        CorrelationMatrix result;
        
        // Get symbols
        for (const auto& [sym, rets] : returns) {
            result.symbols.push_back(sym);
        }
        
        // Initialize matrix
        size_t n = result.symbols.size();
        result.matrix.resize(n, std::vector<double>(n, 0));
        
        // Calculate correlations
        for (size_t i = 0; i < n; ++i) {
            result.matrix[i][i] = 1.0;
            
            for (size_t j = i + 1; j < n; ++j) {
                auto it_i = returns.find(result.symbols[i]);
                auto it_j = returns.find(result.symbols[j]);
                
                if (it_i != returns.end() && it_j != returns.end()) {
                    double corr = correlation(it_i->second, it_j->second);
                    result.matrix[i][j] = corr;
                    result.matrix[j][i] = corr;
                }
            }
        }
        
        // Track observations
        if (!returns.empty()) {
            result.observations = static_cast<int>(returns.begin()->second.size());
        }
        
        return result;
    }
};

// ============================================================================
// Beta Calculation
// ============================================================================

/**
 * @brief Beta analysis result
 */
struct BetaResult {
    std::string symbol;
    std::string benchmark{"SPY"};
    double beta{0};
    double alpha{0};                  // Jensen's alpha (annualized)
    double r_squared{0};
    double correlation{0};
    double tracking_error{0};
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << symbol << " vs " << benchmark << ":\n";
        oss << "  Beta: " << beta << "\n";
        oss << "  Alpha: " << (alpha * 100) << "% (annualized)\n";
        oss << "  R-squared: " << r_squared << "\n";
        oss << "  Correlation: " << correlation << "\n";
        oss << "  Tracking Error: " << (tracking_error * 100) << "%\n";
        return oss.str();
    }
};

/**
 * @brief Beta calculator
 */
class BetaCalculator {
public:
    /**
     * @brief Calculate beta against benchmark
     */
    static BetaResult calculate(
        const std::vector<double>& asset_returns,
        const std::vector<double>& benchmark_returns,
        const std::string& symbol = "",
        const std::string& benchmark = "SPY") {
        
        BetaResult result;
        result.symbol = symbol;
        result.benchmark = benchmark;
        
        size_t n = std::min(asset_returns.size(), benchmark_returns.size());
        if (n < 2) return result;
        
        result.observations = static_cast<int>(n);
        
        // Calculate means
        double mean_asset = 0, mean_bench = 0;
        for (size_t i = 0; i < n; ++i) {
            mean_asset += asset_returns[i];
            mean_bench += benchmark_returns[i];
        }
        mean_asset /= n;
        mean_bench /= n;
        
        // Calculate covariance and variances
        double cov = 0, var_bench = 0, var_asset = 0;
        for (size_t i = 0; i < n; ++i) {
            double da = asset_returns[i] - mean_asset;
            double db = benchmark_returns[i] - mean_bench;
            cov += da * db;
            var_bench += db * db;
            var_asset += da * da;
        }
        cov /= (n - 1);
        var_bench /= (n - 1);
        var_asset /= (n - 1);
        
        // Beta = Cov(asset, bench) / Var(bench)
        result.beta = var_bench != 0 ? cov / var_bench : 0;
        
        // Alpha (annualized)
        result.alpha = (mean_asset - result.beta * mean_bench) * 252;
        
        // R-squared from regression
        double ss_res = 0, ss_tot = 0;
        for (size_t i = 0; i < n; ++i) {
            double predicted = mean_asset + result.beta * 
                              (benchmark_returns[i] - mean_bench);
            ss_res += std::pow(asset_returns[i] - predicted, 2);
            ss_tot += std::pow(asset_returns[i] - mean_asset, 2);
        }
        result.r_squared = ss_tot != 0 ? 1 - ss_res / ss_tot : 0;
        
        // Correlation
        double std_asset = std::sqrt(var_asset);
        double std_bench = std::sqrt(var_bench);
        result.correlation = (std_asset * std_bench != 0) ? cov / (std_asset * std_bench) : 0;
        
        // Tracking error
        std::vector<double> tracking(n);
        for (size_t i = 0; i < n; ++i) {
            tracking[i] = asset_returns[i] - benchmark_returns[i];
        }
        double mean_track = std::accumulate(tracking.begin(), tracking.end(), 0.0) / n;
        double var_track = 0;
        for (double t : tracking) var_track += (t - mean_track) * (t - mean_track);
        result.tracking_error = std::sqrt(var_track / (n - 1)) * std::sqrt(252);
        
        return result;
    }
};

// ============================================================================
// Volatility Analysis
// ============================================================================

/**
 * @brief Volatility result
 */
struct VolatilityResult {
    double daily{0};
    double annualized{0};
    double downside{0};              // Downside deviation
    double upside{0};                // Upside deviation
    double parkinson{0};             // Parkinson (high-low) volatility
    double garman_klass{0};          // Garman-Klass volatility
    double ewma{0};                  // Exponentially weighted
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "Volatility Analysis:\n";
        oss << "  Daily: " << (daily * 100) << "%\n";
        oss << "  Annualized: " << (annualized * 100) << "%\n";
        oss << "  Downside: " << (downside * 100) << "% (annualized)\n";
        oss << "  Upside: " << (upside * 100) << "% (annualized)\n";
        if (parkinson > 0) {
            oss << "  Parkinson: " << (parkinson * 100) << "% (annualized)\n";
        }
        if (garman_klass > 0) {
            oss << "  Garman-Klass: " << (garman_klass * 100) << "% (annualized)\n";
        }
        return oss.str();
    }
};

/**
 * @brief Volatility calculator
 */
class VolatilityCalculator {
public:
    /**
     * @brief Calculate volatility from returns
     */
    static VolatilityResult calculate(const std::vector<double>& returns) {
        VolatilityResult result;
        if (returns.size() < 2) return result;
        
        result.observations = static_cast<int>(returns.size());
        
        // Mean
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        // Standard deviation, downside, upside
        double sum_sq = 0, sum_down = 0, sum_up = 0;
        int down_count = 0, up_count = 0;
        
        for (double ret : returns) {
            double diff = ret - mean;
            sum_sq += diff * diff;
            
            if (ret < 0) {
                sum_down += ret * ret;
                down_count++;
            } else {
                sum_up += ret * ret;
                up_count++;
            }
        }
        
        result.daily = std::sqrt(sum_sq / (returns.size() - 1));
        result.annualized = result.daily * std::sqrt(252);
        
        result.downside = down_count > 1 ? 
            std::sqrt(sum_down / down_count) * std::sqrt(252) : 0;
        result.upside = up_count > 1 ?
            std::sqrt(sum_up / up_count) * std::sqrt(252) : 0;
        
        // EWMA volatility
        result.ewma = ewma_volatility(returns, 0.94);
        
        return result;
    }
    
    /**
     * @brief Calculate from OHLC data
     */
    static VolatilityResult calculate_from_ohlc(const std::vector<market::PriceBar>& bars) {
        VolatilityResult result;
        if (bars.size() < 2) return result;
        
        // Close-to-close returns
        std::vector<double> returns;
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i-1].close > 0) {
                returns.push_back(std::log(bars[i].close / bars[i-1].close));
            }
        }
        
        result = calculate(returns);
        
        // Parkinson volatility (high-low)
        double parkinson_sum = 0;
        int parkinson_count = 0;
        for (const auto& bar : bars) {
            if (bar.high > 0 && bar.low > 0 && bar.high > bar.low) {
                double hl = std::log(bar.high / bar.low);
                parkinson_sum += hl * hl;
                parkinson_count++;
            }
        }
        if (parkinson_count > 0) {
            result.parkinson = std::sqrt(parkinson_sum / (4 * std::log(2) * parkinson_count)) *
                              std::sqrt(252);
        }
        
        // Garman-Klass volatility
        double gk_sum = 0;
        int gk_count = 0;
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i].high > 0 && bars[i].low > 0 && 
                bars[i].open > 0 && bars[i].close > 0 && bars[i-1].close > 0) {
                double hl = std::log(bars[i].high / bars[i].low);
                double co = std::log(bars[i].close / bars[i].open);
                gk_sum += 0.5 * hl * hl - (2 * std::log(2) - 1) * co * co;
                gk_count++;
            }
        }
        if (gk_count > 0) {
            result.garman_klass = std::sqrt(gk_sum / gk_count * 252);
        }
        
        return result;
    }
    
    /**
     * @brief EWMA volatility
     */
    static double ewma_volatility(const std::vector<double>& returns, double lambda = 0.94) {
        if (returns.empty()) return 0;
        
        double variance = returns[0] * returns[0];
        for (size_t i = 1; i < returns.size(); ++i) {
            variance = lambda * variance + (1 - lambda) * returns[i] * returns[i];
        }
        
        return std::sqrt(variance * 252);
    }
};

// ============================================================================
// Drawdown Tracking
// ============================================================================

/**
 * @brief Drawdown result
 */
struct DrawdownResult {
    double max_drawdown{0};          // Maximum drawdown (positive = loss)
    double max_drawdown_pct{0};
    std::string max_dd_start_date;
    std::string max_dd_end_date;
    std::string max_dd_recovery_date;
    int max_dd_duration{0};          // Days from peak to trough
    int max_dd_recovery_days{0};     // Days from trough to recovery
    
    double current_drawdown{0};
    double current_drawdown_pct{0};
    int days_in_current_dd{0};
    
    double avg_drawdown{0};
    int drawdown_periods{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Drawdown Analysis:\n";
        oss << "  Max Drawdown: " << (max_drawdown_pct * 100) << "% ($" << max_drawdown << ")\n";
        if (!max_dd_start_date.empty()) {
            oss << "    Peak: " << max_dd_start_date << "\n";
            oss << "    Trough: " << max_dd_end_date << "\n";
            oss << "    Duration: " << max_dd_duration << " days\n";
            if (!max_dd_recovery_date.empty()) {
                oss << "    Recovery: " << max_dd_recovery_date << " (" 
                    << max_dd_recovery_days << " days)\n";
            } else {
                oss << "    Recovery: Not yet recovered\n";
            }
        }
        oss << "  Current Drawdown: " << (current_drawdown_pct * 100) << "%\n";
        oss << "  Avg Drawdown: " << (avg_drawdown * 100) << "%\n";
        oss << "  Drawdown Periods: " << drawdown_periods << "\n";
        return oss.str();
    }
};

/**
 * @brief Drawdown tracker
 */
class DrawdownTracker {
public:
    /**
     * @brief Analyze drawdowns from NAV series
     */
    static DrawdownResult analyze(
        const std::vector<double>& navs,
        const std::vector<std::string>& dates = {}) {
        
        DrawdownResult result;
        if (navs.empty()) return result;
        
        double peak = navs[0];
        int peak_idx = 0;
        double max_dd = 0;
        int max_dd_peak_idx = 0, max_dd_trough_idx = 0;
        
        double sum_dd = 0;
        int dd_count = 0;
        bool in_drawdown = false;
        int current_dd_start = -1;
        
        for (size_t i = 0; i < navs.size(); ++i) {
            if (navs[i] > peak) {
                peak = navs[i];
                peak_idx = static_cast<int>(i);
                
                if (in_drawdown) {
                    dd_count++;
                    in_drawdown = false;
                }
                current_dd_start = -1;
            } else {
                if (current_dd_start < 0) {
                    current_dd_start = peak_idx;
                }
                in_drawdown = true;
            }
            
            double dd = (navs[i] - peak) / peak;  // Negative for drawdown
            sum_dd += std::abs(dd);
            
            if (dd < max_dd) {
                max_dd = dd;
                max_dd_peak_idx = current_dd_start >= 0 ? current_dd_start : peak_idx;
                max_dd_trough_idx = static_cast<int>(i);
            }
        }
        
        if (in_drawdown) dd_count++;
        
        result.max_drawdown_pct = -max_dd;
        result.max_drawdown = result.max_drawdown_pct * navs[max_dd_peak_idx];
        result.max_dd_duration = max_dd_trough_idx - max_dd_peak_idx;
        result.drawdown_periods = dd_count;
        result.avg_drawdown = navs.size() > 0 ? sum_dd / navs.size() : 0;
        
        if (!dates.empty()) {
            if (max_dd_peak_idx < static_cast<int>(dates.size())) {
                result.max_dd_start_date = dates[max_dd_peak_idx];
            }
            if (max_dd_trough_idx < static_cast<int>(dates.size())) {
                result.max_dd_end_date = dates[max_dd_trough_idx];
            }
        }
        
        // Check for recovery
        double recovery_target = navs[max_dd_peak_idx];
        for (size_t i = max_dd_trough_idx + 1; i < navs.size(); ++i) {
            if (navs[i] >= recovery_target) {
                result.max_dd_recovery_days = static_cast<int>(i) - max_dd_trough_idx;
                if (i < dates.size()) {
                    result.max_dd_recovery_date = dates[i];
                }
                break;
            }
        }
        
        // Current drawdown
        double current_peak = navs[0];
        int current_peak_idx = 0;
        for (size_t i = 0; i < navs.size(); ++i) {
            if (navs[i] > current_peak) {
                current_peak = navs[i];
                current_peak_idx = static_cast<int>(i);
            }
        }
        
        result.current_drawdown_pct = (navs.back() - current_peak) / current_peak;
        result.current_drawdown = std::abs(result.current_drawdown_pct) * current_peak;
        result.current_drawdown_pct = std::abs(result.current_drawdown_pct);
        result.days_in_current_dd = static_cast<int>(navs.size()) - 1 - current_peak_idx;
        
        return result;
    }
    
    /**
     * @brief Analyze from price bars
     */
    static DrawdownResult analyze(const std::vector<market::PriceBar>& bars) {
        std::vector<double> prices;
        std::vector<std::string> dates;
        
        for (const auto& bar : bars) {
            prices.push_back(bar.adjusted_close > 0 ? bar.adjusted_close : bar.close);
            dates.push_back(bar.date);
        }
        
        return analyze(prices, dates);
    }
};

// ============================================================================
// Risk Report
// ============================================================================

/**
 * @brief Comprehensive risk report
 */
struct RiskReport {
    std::string symbol;
    double portfolio_value{0};
    int trading_days{0};
    
    VaRResult var_95;
    VaRResult var_99;
    VolatilityResult volatility;
    BetaResult beta;
    DrawdownResult drawdown;
    
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << "=== Risk Report: " << symbol << " ===\n";
        oss << "Portfolio Value: $" << std::fixed << std::setprecision(2) 
            << portfolio_value << "\n";
        oss << "Trading Days: " << trading_days << "\n\n";
        
        oss << var_95.format() << "\n";
        oss << volatility.format() << "\n";
        
        if (!beta.benchmark.empty() && beta.observations > 0) {
            oss << beta.format() << "\n";
        }
        
        oss << drawdown.format() << "\n";
        
        oss << std::fixed << std::setprecision(2);
        oss << "Risk-Adjusted Metrics:\n";
        oss << "  Sharpe Ratio: " << sharpe_ratio << "\n";
        oss << "  Sortino Ratio: " << sortino_ratio << "\n";
        oss << "  Calmar Ratio: " << calmar_ratio << "\n";
        
        return oss.str();
    }
};

/**
 * @brief Generate comprehensive risk report
 */
inline RiskReport generate_risk_report(
    const std::vector<market::PriceBar>& bars,
    const std::vector<double>& benchmark_returns = {},
    double portfolio_value = 100000,
    const std::string& symbol = "",
    const std::string& benchmark = "SPY",
    double risk_free_rate = 0.02) {
    
    RiskReport report;
    report.symbol = symbol;
    report.portfolio_value = portfolio_value;
    report.trading_days = static_cast<int>(bars.size());
    
    // Calculate returns
    std::vector<double> returns;
    for (size_t i = 1; i < bars.size(); ++i) {
        if (bars[i-1].close > 0) {
            returns.push_back((bars[i].close - bars[i-1].close) / bars[i-1].close);
        }
    }
    
    if (returns.empty()) return report;
    
    // VaR calculations
    report.var_95 = VaRCalculator::calculate(returns, portfolio_value, 0.95);
    report.var_99 = VaRCalculator::calculate(returns, portfolio_value, 0.99);
    
    // Volatility
    report.volatility = VolatilityCalculator::calculate_from_ohlc(bars);
    
    // Beta (if benchmark provided)
    if (!benchmark_returns.empty()) {
        report.beta = BetaCalculator::calculate(returns, benchmark_returns, symbol, benchmark);
    }
    
    // Drawdown
    std::vector<double> prices;
    std::vector<std::string> dates;
    for (const auto& bar : bars) {
        prices.push_back(bar.close);
        dates.push_back(bar.date);
    }
    report.drawdown = DrawdownTracker::analyze(prices, dates);
    
    // Risk-adjusted metrics
    double mean_ret = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double annual_ret = mean_ret * 252;
    double daily_rf = risk_free_rate / 252;
    
    // Sharpe ratio
    if (report.volatility.annualized > 0) {
        report.sharpe_ratio = (annual_ret - risk_free_rate) / report.volatility.annualized;
    }
    
    // Sortino ratio
    if (report.volatility.downside > 0) {
        report.sortino_ratio = (annual_ret - risk_free_rate) / report.volatility.downside;
    }
    
    // Calmar ratio
    if (report.drawdown.max_drawdown_pct > 0) {
        report.calmar_ratio = annual_ret / report.drawdown.max_drawdown_pct;
    }
    
    return report;
}

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_RISK_ANALYTICS_HPP
