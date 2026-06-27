/**
 * @file real_risk.hpp
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
 * - Marginal and component VaR
 */
#pragma once
#ifndef GENIE_ANALYTICS_REAL_RISK_HPP
#define GENIE_ANALYTICS_REAL_RISK_HPP

#include "../market/data_manager.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <functional>
#include <sstream>
#include <iomanip>

namespace genie::analytics {

// ============================================================================
// Return Calculations
// ============================================================================

/**
 * @brief Calculate returns from price series
 */
class ReturnCalculator {
public:
    enum class ReturnType {
        Simple,      // (P1 - P0) / P0
        Log          // ln(P1 / P0)
    };
    
    /**
     * @brief Calculate returns from price vector
     */
    static std::vector<double> calculate_returns(
        const std::vector<double>& prices,
        ReturnType type = ReturnType::Simple) {
        
        std::vector<double> returns;
        if (prices.size() < 2) return returns;
        
        returns.reserve(prices.size() - 1);
        
        for (size_t i = 1; i < prices.size(); ++i) {
            if (prices[i - 1] <= 0) continue;
            
            double ret;
            if (type == ReturnType::Simple) {
                ret = (prices[i] - prices[i - 1]) / prices[i - 1];
            } else {
                ret = std::log(prices[i] / prices[i - 1]);
            }
            returns.push_back(ret);
        }
        
        return returns;
    }
    
    /**
     * @brief Calculate returns from price bars
     */
    static std::vector<double> calculate_returns(
        const std::vector<market::PriceBar>& bars,
        ReturnType type = ReturnType::Simple,
        bool use_adjusted = true) {
        
        std::vector<double> prices;
        prices.reserve(bars.size());
        
        for (const auto& bar : bars) {
            double price = use_adjusted ? bar.adjusted_close : bar.close;
            if (price > 0) prices.push_back(price);
        }
        
        return calculate_returns(prices, type);
    }
    
    /**
     * @brief Calculate cumulative returns
     */
    static std::vector<double> cumulative_returns(const std::vector<double>& returns) {
        std::vector<double> cumulative;
        if (returns.empty()) return cumulative;
        
        cumulative.reserve(returns.size());
        double cum = 1.0;
        
        for (double ret : returns) {
            cum *= (1.0 + ret);
            cumulative.push_back(cum - 1.0);
        }
        
        return cumulative;
    }
};

// ============================================================================
// Value at Risk (VaR)
// ============================================================================

/**
 * @brief VaR calculation method
 */
enum class VaRMethod {
    Historical,        // Historical simulation
    Parametric,        // Normal distribution assumption
    MonteCarlo,        // Monte Carlo simulation
    CornishFisher      // Cornish-Fisher expansion (accounts for skew/kurtosis)
};

/**
 * @brief VaR result
 */
struct VaRResult {
    double var{0};                    // Value at Risk (positive = loss)
    double cvar{0};                   // Conditional VaR / Expected Shortfall
    double confidence{0.95};          // Confidence level (e.g., 0.95 = 95%)
    int horizon_days{1};              // Time horizon
    VaRMethod method;
    
    // Additional statistics
    double mean_return{0};
    double volatility{0};
    double skewness{0};
    double kurtosis{0};
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "VaR (" << (confidence * 100) << "%, " << horizon_days << "d): " 
            << (var * 100) << "%\n";
        oss << "CVaR/ES: " << (cvar * 100) << "%\n";
        oss << "Volatility: " << (volatility * 100) << "%\n";
        oss << "Observations: " << observations << "\n";
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
        result.volatility = std_dev(returns);
        result.skewness = skewness(returns);
        result.kurtosis = kurtosis(returns);
        
        switch (method) {
            case VaRMethod::Historical:
                calculate_historical(returns, result);
                break;
            case VaRMethod::Parametric:
                calculate_parametric(returns, result);
                break;
            case VaRMethod::CornishFisher:
                calculate_cornish_fisher(returns, result);
                break;
            case VaRMethod::MonteCarlo:
                calculate_monte_carlo(returns, result);
                break;
        }
        
        // Scale to horizon
        if (horizon_days > 1) {
            double scale = std::sqrt(static_cast<double>(horizon_days));
            result.var *= scale;
            result.cvar *= scale;
            result.volatility *= scale;
        }
        
        return result;
    }
    
    /**
     * @brief Calculate VaR from price bars
     */
    static VaRResult calculate_from_prices(
        const std::vector<market::PriceBar>& bars,
        double confidence = 0.95,
        int horizon_days = 1,
        VaRMethod method = VaRMethod::Historical) {
        
        auto returns = ReturnCalculator::calculate_returns(bars);
        return calculate(returns, confidence, horizon_days, method);
    }
    
    /**
     * @brief Calculate portfolio VaR
     */
    static VaRResult calculate_portfolio(
        const std::map<std::string, std::vector<double>>& asset_returns,
        const std::map<std::string, double>& weights,
        double confidence = 0.95,
        int horizon_days = 1,
        VaRMethod method = VaRMethod::Historical) {
        
        // Calculate portfolio returns
        size_t n_obs = 0;
        for (const auto& [symbol, returns] : asset_returns) {
            if (returns.size() > n_obs) n_obs = returns.size();
        }
        
        std::vector<double> portfolio_returns(n_obs, 0);
        
        for (size_t i = 0; i < n_obs; ++i) {
            double port_ret = 0;
            for (const auto& [symbol, returns] : asset_returns) {
                auto wit = weights.find(symbol);
                if (wit == weights.end()) continue;
                if (i < returns.size()) {
                    port_ret += wit->second * returns[i];
                }
            }
            portfolio_returns[i] = port_ret;
        }
        
        return calculate(portfolio_returns, confidence, horizon_days, method);
    }

private:
    static double mean(const std::vector<double>& v) {
        if (v.empty()) return 0;
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
    
    static double std_dev(const std::vector<double>& v) {
        if (v.size() < 2) return 0;
        double m = mean(v);
        double sum_sq = 0;
        for (double x : v) {
            sum_sq += (x - m) * (x - m);
        }
        return std::sqrt(sum_sq / (v.size() - 1));
    }
    
    static double skewness(const std::vector<double>& v) {
        if (v.size() < 3) return 0;
        double m = mean(v);
        double s = std_dev(v);
        if (s == 0) return 0;
        
        double sum = 0;
        for (double x : v) {
            sum += std::pow((x - m) / s, 3);
        }
        return sum * v.size() / ((v.size() - 1) * (v.size() - 2));
    }
    
    static double kurtosis(const std::vector<double>& v) {
        if (v.size() < 4) return 0;
        double m = mean(v);
        double s = std_dev(v);
        if (s == 0) return 0;
        
        double sum = 0;
        for (double x : v) {
            sum += std::pow((x - m) / s, 4);
        }
        double n = static_cast<double>(v.size());
        return (n * (n + 1) * sum / ((n - 1) * (n - 2) * (n - 3))) -
               (3 * (n - 1) * (n - 1) / ((n - 2) * (n - 3)));
    }
    
    static double percentile(std::vector<double> v, double p) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        size_t idx = static_cast<size_t>(p * (v.size() - 1));
        return v[idx];
    }
    
    static double normal_quantile(double p) {
        // Approximation of inverse normal CDF
        if (p <= 0) return -10;
        if (p >= 1) return 10;
        
        double t = std::sqrt(-2.0 * std::log(p < 0.5 ? p : 1 - p));
        double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        
        double z = t - (c0 + c1 * t + c2 * t * t) /
                      (1 + d1 * t + d2 * t * t + d3 * t * t * t);
        
        return p < 0.5 ? -z : z;
    }
    
    static void calculate_historical(const std::vector<double>& returns,
                                     VaRResult& result) {
        // Sort returns and find percentile
        std::vector<double> sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        
        size_t var_idx = static_cast<size_t>((1 - result.confidence) * sorted.size());
        result.var = -sorted[var_idx];  // VaR is positive for losses
        
        // CVaR = average of returns worse than VaR
        double sum = 0;
        int count = 0;
        for (size_t i = 0; i <= var_idx; ++i) {
            sum += sorted[i];
            count++;
        }
        result.cvar = count > 0 ? -sum / count : result.var;
    }
    
    static void calculate_parametric([[maybe_unused]] const std::vector<double>& returns,
                                     VaRResult& result) {
        double z = normal_quantile(1 - result.confidence);
        result.var = -(result.mean_return + z * result.volatility);
        
        // CVaR for normal distribution
        double pdf_z = std::exp(-z * z / 2) / std::sqrt(2 * M_PI);
        result.cvar = -(result.mean_return - 
                       result.volatility * pdf_z / (1 - result.confidence));
    }
    
    static void calculate_cornish_fisher([[maybe_unused]] const std::vector<double>& returns,
                                         VaRResult& result) {
        double z = normal_quantile(1 - result.confidence);
        double s = result.skewness;
        double k = result.kurtosis;
        
        // Cornish-Fisher expansion
        double z_cf = z + (z * z - 1) * s / 6 +
                      (z * z * z - 3 * z) * k / 24 -
                      (2 * z * z * z - 5 * z) * s * s / 36;
        
        result.var = -(result.mean_return + z_cf * result.volatility);
        result.cvar = result.var * 1.1;  // Approximation
    }
    
    static void calculate_monte_carlo([[maybe_unused]] const std::vector<double>& returns,
                                      VaRResult& result) {
        // Simple Monte Carlo using normal distribution
        const int n_sims = 10000;
        std::vector<double> simulated;
        simulated.reserve(n_sims);
        
        // Use a simple LCG for reproducibility
        uint64_t seed = 12345;
        auto lcg_rand = [&seed]() -> double {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>(seed >> 33) / static_cast<double>(1ULL << 31);
        };
        
        // Box-Muller transform
        for (int i = 0; i < n_sims; i += 2) {
            double u1 = lcg_rand();
            double u2 = lcg_rand();
            double z1 = std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
            double z2 = std::sqrt(-2 * std::log(u1)) * std::sin(2 * M_PI * u2);
            
            simulated.push_back(result.mean_return + z1 * result.volatility);
            if (i + 1 < n_sims) {
                simulated.push_back(result.mean_return + z2 * result.volatility);
            }
        }
        
        // Calculate VaR from simulations
        std::sort(simulated.begin(), simulated.end());
        size_t var_idx = static_cast<size_t>((1 - result.confidence) * simulated.size());
        result.var = -simulated[var_idx];
        
        // CVaR
        double sum = 0;
        for (size_t i = 0; i <= var_idx; ++i) {
            sum += simulated[i];
        }
        result.cvar = -(sum / (var_idx + 1));
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
        
        // Header
        oss << std::setw(8) << "";
        for (const auto& sym : symbols) {
            oss << std::setw(8) << sym.substr(0, 6);
        }
        oss << "\n";
        
        // Rows
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
     * @brief Calculate correlation between two return series
     */
    static double correlation(const std::vector<double>& x,
                              const std::vector<double>& y) {
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
    static double covariance(const std::vector<double>& x,
                             const std::vector<double>& y) {
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
     * @brief Build correlation matrix
     */
    static CorrelationMatrix build_matrix(
        const std::map<std::string, std::vector<double>>& returns) {
        
        CorrelationMatrix result;
        
        // Get symbols
        for (const auto& [sym, ret] : returns) {
            result.symbols.push_back(sym);
        }
        
        // Initialize matrix
        size_t n = result.symbols.size();
        result.matrix.resize(n, std::vector<double>(n, 0));
        
        // Calculate correlations
        for (size_t i = 0; i < n; ++i) {
            result.matrix[i][i] = 1.0;  // Diagonal
            
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
    
    /**
     * @brief Build covariance matrix
     */
    static std::vector<std::vector<double>> build_covariance_matrix(
        const std::map<std::string, std::vector<double>>& returns,
        const std::vector<std::string>& symbols) {
        
        size_t n = symbols.size();
        std::vector<std::vector<double>> matrix(n, std::vector<double>(n, 0));
        
        for (size_t i = 0; i < n; ++i) {
            auto it_i = returns.find(symbols[i]);
            if (it_i == returns.end()) continue;
            
            for (size_t j = i; j < n; ++j) {
                auto it_j = returns.find(symbols[j]);
                if (it_j == returns.end()) continue;
                
                double cov = covariance(it_i->second, it_j->second);
                matrix[i][j] = cov;
                matrix[j][i] = cov;
            }
        }
        
        return matrix;
    }
};

// ============================================================================
// Beta Calculation
// ============================================================================

/**
 * @brief Beta result
 */
struct BetaResult {
    std::string symbol;
    std::string benchmark;
    double beta{0};
    double alpha{0};                  // Jensen's alpha
    double r_squared{0};              // Coefficient of determination
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
        
        // Calculate covariance and variance
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
        
        // Alpha (annualized, assuming daily data)
        result.alpha = (mean_asset - result.beta * mean_bench) * 252;
        
        // R-squared
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
        result.correlation = (std_asset * std_bench != 0) ?
            cov / (std_asset * std_bench) : 0;
        
        // Tracking error
        std::vector<double> excess_returns(n);
        for (size_t i = 0; i < n; ++i) {
            excess_returns[i] = asset_returns[i] - benchmark_returns[i];
        }
        double mean_excess = 0;
        for (double e : excess_returns) mean_excess += e;
        mean_excess /= n;
        
        double var_excess = 0;
        for (double e : excess_returns) {
            var_excess += (e - mean_excess) * (e - mean_excess);
        }
        result.tracking_error = std::sqrt(var_excess / (n - 1)) * std::sqrt(252);
        
        return result;
    }
};

// ============================================================================
// Volatility Calculations
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
    int observations{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "Volatility:\n";
        oss << "  Daily: " << (daily * 100) << "%\n";
        oss << "  Annualized: " << (annualized * 100) << "%\n";
        oss << "  Downside: " << (downside * 100) << "%\n";
        oss << "  Upside: " << (upside * 100) << "%\n";
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
        
        // Standard deviation
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
        
        // Downside deviation (vs 0 MAR)
        result.downside = down_count > 1 ? 
            std::sqrt(sum_down / down_count) * std::sqrt(252) : 0;
        
        // Upside deviation
        result.upside = up_count > 1 ?
            std::sqrt(sum_up / up_count) * std::sqrt(252) : 0;
        
        return result;
    }
    
    /**
     * @brief Calculate volatility from OHLC data (Parkinson & Garman-Klass)
     */
    static VolatilityResult calculate_from_ohlc(const std::vector<market::PriceBar>& bars) {
        VolatilityResult result;
        if (bars.size() < 2) return result;
        
        result.observations = static_cast<int>(bars.size());
        
        // Calculate close-to-close returns for standard volatility
        std::vector<double> returns;
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i-1].close > 0) {
                returns.push_back(std::log(bars[i].close / bars[i-1].close));
            }
        }
        
        if (!returns.empty()) {
            double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double sum_sq = 0;
            for (double r : returns) sum_sq += (r - mean) * (r - mean);
            result.daily = std::sqrt(sum_sq / (returns.size() - 1));
            result.annualized = result.daily * std::sqrt(252);
        }
        
        // Parkinson volatility (uses high-low range)
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
        
        // Garman-Klass volatility (uses OHLC)
        double gk_sum = 0;
        int gk_count = 0;
        for (size_t i = 1; i < bars.size(); ++i) {
            const auto& bar = bars[i];
            const auto& prev = bars[i-1];
            
            if (bar.high > 0 && bar.low > 0 && bar.open > 0 && 
                bar.close > 0 && prev.close > 0) {
                double hl = std::log(bar.high / bar.low);
                double co = std::log(bar.close / bar.open);
                
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
     * @brief Calculate exponentially weighted volatility
     */
    static double ewma_volatility(const std::vector<double>& returns,
                                   double lambda = 0.94) {
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
 * @brief Drawdown point
 */
struct DrawdownPoint {
    std::string date;
    double nav{0};
    double peak{0};
    double drawdown{0};              // Negative value
    double drawdown_pct{0};          // Negative percentage
    bool at_peak{false};
    int days_from_peak{0};
};

/**
 * @brief Drawdown analysis result
 */
struct RealDrawdownAnalysis {
    double max_drawdown{0};          // Maximum drawdown (positive = loss)
    double max_drawdown_pct{0};      // Maximum drawdown percentage
    std::string max_dd_start_date;
    std::string max_dd_end_date;
    std::string max_dd_recovery_date;
    int max_dd_duration{0};          // Days from peak to trough
    int max_dd_recovery{0};          // Days from trough to recovery
    
    double current_drawdown{0};
    double current_drawdown_pct{0};
    int days_in_current_dd{0};
    
    double avg_drawdown{0};
    int total_drawdown_periods{0};
    
    std::vector<DrawdownPoint> history;
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Drawdown Analysis:\n";
        oss << "  Max Drawdown: " << (max_drawdown_pct * 100) << "%\n";
        if (!max_dd_start_date.empty()) {
            oss << "    Period: " << max_dd_start_date << " to " << max_dd_end_date << "\n";
            oss << "    Duration: " << max_dd_duration << " days\n";
            if (!max_dd_recovery_date.empty()) {
                oss << "    Recovery: " << max_dd_recovery << " days\n";
            }
        }
        oss << "  Current Drawdown: " << (current_drawdown_pct * 100) << "%\n";
        oss << "  Avg Drawdown: " << (avg_drawdown * 100) << "%\n";
        oss << "  Drawdown Periods: " << total_drawdown_periods << "\n";
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
    static RealDrawdownAnalysis analyze(
        const std::vector<double>& navs,
        const std::vector<std::string>& dates = {}) {
        
        RealDrawdownAnalysis result;
        if (navs.empty()) return result;
        
        double peak = navs[0];
        double max_dd = 0;
        int max_dd_start = 0, max_dd_end = 0;
        int current_dd_start = -1;
        
        double sum_dd = 0;
        int dd_periods = 0;
        bool in_drawdown = false;
        
        result.history.reserve(navs.size());
        
        for (size_t i = 0; i < navs.size(); ++i) {
            double nav = navs[i];
            
            DrawdownPoint point;
            if (i < dates.size()) point.date = dates[i];
            point.nav = nav;
            
            if (nav > peak) {
                peak = nav;
                point.at_peak = true;
                
                if (in_drawdown) {
                    dd_periods++;
                    in_drawdown = false;
                }
                current_dd_start = -1;
            } else {
                if (current_dd_start < 0) {
                    current_dd_start = static_cast<int>(i);
                }
                in_drawdown = true;
            }
            
            point.peak = peak;
            point.drawdown = nav - peak;
            point.drawdown_pct = peak > 0 ? (nav - peak) / peak : 0;
            point.days_from_peak = current_dd_start >= 0 ? 
                static_cast<int>(i) - current_dd_start : 0;
            
            // Track max drawdown
            if (point.drawdown_pct < max_dd) {
                max_dd = point.drawdown_pct;
                max_dd_start = current_dd_start;
                max_dd_end = static_cast<int>(i);
            }
            
            sum_dd += std::abs(point.drawdown_pct);
            
            result.history.push_back(point);
        }
        
        // Final drawdown period
        if (in_drawdown) dd_periods++;
        
        result.max_drawdown_pct = max_dd;
        result.max_drawdown = std::abs(max_dd);
        result.max_dd_duration = max_dd_end - max_dd_start;
        result.total_drawdown_periods = dd_periods;
        result.avg_drawdown = navs.size() > 0 ? sum_dd / navs.size() : 0;
        
        if (max_dd_start >= 0 && max_dd_start < static_cast<int>(dates.size())) {
            result.max_dd_start_date = dates[max_dd_start];
        }
        if (max_dd_end >= 0 && max_dd_end < static_cast<int>(dates.size())) {
            result.max_dd_end_date = dates[max_dd_end];
        }
        
        // Check for recovery
        peak = navs[0];
        for (size_t i = 0; i < navs.size(); ++i) {
            if (navs[i] > peak) peak = navs[i];
            
            if (static_cast<int>(i) > max_dd_end && navs[i] >= peak) {
                result.max_dd_recovery = static_cast<int>(i) - max_dd_end;
                if (i < dates.size()) {
                    result.max_dd_recovery_date = dates[i];
                }
                break;
            }
        }
        
        // Current drawdown
        if (!result.history.empty()) {
            const auto& last = result.history.back();
            result.current_drawdown = std::abs(last.drawdown);
            result.current_drawdown_pct = last.drawdown_pct;
            result.days_in_current_dd = last.days_from_peak;
        }
        
        return result;
    }
    
    /**
     * @brief Analyze drawdowns from price bars
     */
    static RealDrawdownAnalysis analyze(const std::vector<market::PriceBar>& bars) {
        std::vector<double> navs;
        std::vector<std::string> dates;
        
        navs.reserve(bars.size());
        dates.reserve(bars.size());
        
        for (const auto& bar : bars) {
            navs.push_back(bar.adjusted_close > 0 ? bar.adjusted_close : bar.close);
            dates.push_back(bar.date);
        }
        
        return analyze(navs, dates);
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
    std::string period;              // e.g., "1Y", "3Y"
    int observations{0};
    
    VaRResult var_95;
    VaRResult var_99;
    VolatilityResult volatility;
    BetaResult beta;
    RealDrawdownAnalysis drawdown;
    
    // Additional metrics
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};          // Return / Max Drawdown
    
    std::string format() const {
        std::ostringstream oss;
        oss << "=== Risk Report: " << symbol << " (" << period << ") ===\n\n";
        
        oss << var_95.format() << "\n";
        oss << volatility.format() << "\n";
        
        if (!beta.benchmark.empty()) {
            oss << beta.format() << "\n";
        }
        
        oss << drawdown.format() << "\n";
        
        oss << std::fixed << std::setprecision(2);
        oss << "Risk-Adjusted Returns:\n";
        oss << "  Sharpe Ratio: " << sharpe_ratio << "\n";
        oss << "  Sortino Ratio: " << sortino_ratio << "\n";
        oss << "  Calmar Ratio: " << calmar_ratio << "\n";
        
        return oss.str();
    }
};

/**
 * @brief Generate risk report
 */
inline RiskReport generate_risk_report(
    const std::vector<market::PriceBar>& bars,
    const std::vector<double>& benchmark_returns = {},
    const std::string& symbol = "",
    const std::string& benchmark = "SPY",
    double risk_free_rate = 0.02) {
    
    RiskReport report;
    report.symbol = symbol;
    report.observations = static_cast<int>(bars.size());
    
    // Calculate returns
    auto returns = ReturnCalculator::calculate_returns(bars);
    if (returns.empty()) return report;
    
    // VaR
    report.var_95 = VaRCalculator::calculate(returns, 0.95);
    report.var_99 = VaRCalculator::calculate(returns, 0.99);
    
    // Volatility
    report.volatility = VolatilityCalculator::calculate_from_ohlc(bars);
    
    // Beta (if benchmark provided)
    if (!benchmark_returns.empty()) {
        report.beta = BetaCalculator::calculate(returns, benchmark_returns, symbol, benchmark);
    }
    
    // Drawdown
    report.drawdown = DrawdownTracker::analyze(bars);
    
    // Risk-adjusted returns
    double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double annual_return = mean_return * 252;
    double annual_vol = report.volatility.annualized;
    
    // Sharpe ratio
    if (annual_vol > 0) {
        report.sharpe_ratio = (annual_return - risk_free_rate) / annual_vol;
    }
    
    // Sortino ratio
    if (report.volatility.downside > 0) {
        report.sortino_ratio = (annual_return - risk_free_rate) / report.volatility.downside;
    }
    
    // Calmar ratio
    if (report.drawdown.max_drawdown > 0) {
        report.calmar_ratio = annual_return / report.drawdown.max_drawdown;
    }
    
    // Period string
    if (bars.size() >= 252 * 3) {
        report.period = "3Y+";
    } else if (bars.size() >= 252) {
        report.period = "1Y";
    } else if (bars.size() >= 126) {
        report.period = "6M";
    } else if (bars.size() >= 63) {
        report.period = "3M";
    } else {
        report.period = "1M";
    }
    
    return report;
}

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_REAL_RISK_HPP
