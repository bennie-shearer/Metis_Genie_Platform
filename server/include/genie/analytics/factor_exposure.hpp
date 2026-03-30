/**
 * @file factor_exposure.hpp
 * @brief Factor Exposure Analysis
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements multi-factor exposure analysis:
 * - Factor regression (OLS)
 * - Rolling factor exposures
 * - Factor attribution
 * - Style analysis
 */

#pragma once
#ifndef GENIE_ANALYTICS_FACTOR_EXPOSURE_HPP
#define GENIE_ANALYTICS_FACTOR_EXPOSURE_HPP

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace genie::analytics {

/**
 * @brief Factor exposure results
 */
struct FactorExposures {
    // Standard Fama-French + Carhart factors
    double market_beta{0};          // MKT-RF exposure
    double size_exposure{0};        // SMB (Small Minus Big)
    double value_exposure{0};       // HML (High Minus Low)
    double momentum_exposure{0};    // UMD (Up Minus Down)
    double quality_exposure{0};     // QMJ (Quality)
    double volatility_exposure{0};  // BAB (Betting Against Beta)
    
    // Regression statistics
    double alpha{0};                // Unexplained return (annualized)
    double r_squared{0};            // Explained variance
    double adjusted_r_squared{0};   // Adjusted R-squared
    double residual_std{0};         // Std of residuals
    
    // T-statistics for significance
    double alpha_tstat{0};
    double beta_tstat{0};
    
    // Custom factor exposures
    std::map<std::string, double> custom_exposures;
};

/**
 * @brief Factor return attribution
 */
struct FactorAttribution {
    double total_return{0};
    double factor_return{0};        // Return explained by factors
    double specific_return{0};      // Alpha + residual
    
    std::map<std::string, double> factor_contributions;
    
    [[nodiscard]] double explained_ratio() const {
        if (std::abs(total_return) < 1e-10) return 0.0;
        return factor_return / total_return;
    }
};

/**
 * @brief Factor exposure analysis engine
 */
class FactorExposureAnalyzer {
public:
    /**
     * @brief Analyze factor exposures using regression
     * 
     * @param portfolio_returns Portfolio returns (excess of risk-free)
     * @param factor_returns Map of factor name to return series
     * @param annualization_factor Periods per year (252 for daily)
     * @return FactorExposures regression results
     */
    [[nodiscard]] FactorExposures analyze(
        const std::vector<double>& portfolio_returns,
        const std::map<std::string, std::vector<double>>& factor_returns,
        double annualization_factor = 252.0
    ) const {
        FactorExposures result;
        
        if (portfolio_returns.empty()) return result;
        
        // Find minimum length
        size_t n = portfolio_returns.size();
        for (const auto& [name, returns] : factor_returns) {
            n = std::min(n, returns.size());
        }
        
        if (n < 10) return result;  // Need sufficient data
        
        // Build factor matrix
        std::vector<std::string> factor_names;
        std::vector<std::vector<double>> X;
        
        for (const auto& [name, returns] : factor_returns) {
            factor_names.push_back(name);
            X.push_back(std::vector<double>(returns.begin(), returns.begin() + n));
        }
        
        std::vector<double> y(portfolio_returns.begin(), portfolio_returns.begin() + n);
        
        // Run OLS regression
        auto [coefficients, stats] = ols_regression(y, X);
        
        // Extract results
        result.alpha = coefficients[0] * annualization_factor;  // Annualize alpha
        result.r_squared = stats.r_squared;
        result.adjusted_r_squared = stats.adjusted_r_squared;
        result.residual_std = stats.residual_std * std::sqrt(annualization_factor);
        result.alpha_tstat = stats.t_stats[0];
        
        // Map coefficients to standard factors
        for (size_t i = 0; i < factor_names.size(); ++i) {
            const std::string& name = factor_names[i];
            double exposure = coefficients[i + 1];  // +1 because [0] is alpha
            double tstat = stats.t_stats[i + 1];
            
            if (name == "MKT" || name == "MKT-RF" || name == "Market") {
                result.market_beta = exposure;
                result.beta_tstat = tstat;
            } else if (name == "SMB" || name == "Size") {
                result.size_exposure = exposure;
            } else if (name == "HML" || name == "Value") {
                result.value_exposure = exposure;
            } else if (name == "UMD" || name == "MOM" || name == "Momentum") {
                result.momentum_exposure = exposure;
            } else if (name == "QMJ" || name == "Quality") {
                result.quality_exposure = exposure;
            } else if (name == "BAB" || name == "LowVol" || name == "Volatility") {
                result.volatility_exposure = exposure;
            } else {
                result.custom_exposures[name] = exposure;
            }
        }
        
        return result;
    }

    /**
     * @brief Attribute portfolio return to factors
     */
    [[nodiscard]] FactorAttribution attribute_return(
        double portfolio_return,
        const FactorExposures& exposures,
        const std::map<std::string, double>& period_factor_returns
    ) const {
        FactorAttribution result;
        result.total_return = portfolio_return;
        
        // Calculate factor contribution
        auto add_contribution = [&](const std::string& name, double exposure) {
            auto it = period_factor_returns.find(name);
            if (it != period_factor_returns.end()) {
                double contribution = exposure * it->second;
                result.factor_contributions[name] = contribution;
                result.factor_return += contribution;
            }
        };
        
        add_contribution("MKT", exposures.market_beta);
        add_contribution("SMB", exposures.size_exposure);
        add_contribution("HML", exposures.value_exposure);
        add_contribution("UMD", exposures.momentum_exposure);
        add_contribution("QMJ", exposures.quality_exposure);
        add_contribution("BAB", exposures.volatility_exposure);
        
        for (const auto& [name, exposure] : exposures.custom_exposures) {
            add_contribution(name, exposure);
        }
        
        result.specific_return = result.total_return - result.factor_return;
        
        return result;
    }

    /**
     * @brief Calculate rolling factor exposures
     */
    [[nodiscard]] std::vector<FactorExposures> rolling_exposures(
        const std::vector<double>& portfolio_returns,
        const std::map<std::string, std::vector<double>>& factor_returns,
        size_t window = 60
    ) const {
        std::vector<FactorExposures> results;
        
        if (portfolio_returns.size() < window) return results;
        
        results.reserve(portfolio_returns.size() - window + 1);
        
        for (size_t i = window; i <= portfolio_returns.size(); ++i) {
            std::vector<double> window_returns(
                portfolio_returns.begin() + i - window,
                portfolio_returns.begin() + i
            );
            
            std::map<std::string, std::vector<double>> window_factors;
            for (const auto& [name, returns] : factor_returns) {
                if (returns.size() >= i) {
                    window_factors[name] = std::vector<double>(
                        returns.begin() + i - window,
                        returns.begin() + i
                    );
                }
            }
            
            results.push_back(analyze(window_returns, window_factors, 252.0));
        }
        
        return results;
    }

    /**
     * @brief Style analysis (constrained regression with weights summing to 1)
     */
    [[nodiscard]] std::map<std::string, double> style_analysis(
        const std::vector<double>& portfolio_returns,
        const std::map<std::string, std::vector<double>>& benchmark_returns
    ) const {
        std::map<std::string, double> weights;
        
        if (portfolio_returns.empty() || benchmark_returns.empty()) {
            return weights;
        }
        
        // Simple correlation-based style weights (approximation)
        // Full style analysis would use quadratic programming
        
        std::map<std::string, double> correlations;
        double sum_corr = 0.0;
        
        for (const auto& [name, returns] : benchmark_returns) {
            double corr = calculate_correlation(portfolio_returns, returns);
            corr = std::max(0.0, corr);  // Only positive weights
            correlations[name] = corr;
            sum_corr += corr;
        }
        
        // Normalize to sum to 1
        if (sum_corr > 1e-10) {
            for (const auto& [name, corr] : correlations) {
                weights[name] = corr / sum_corr;
            }
        }
        
        return weights;
    }

private:
    struct RegressionStats {
        double r_squared{0};
        double adjusted_r_squared{0};
        double residual_std{0};
        std::vector<double> t_stats;
    };

    /**
     * @brief Simple OLS regression
     */
    [[nodiscard]] std::pair<std::vector<double>, RegressionStats> ols_regression(
        const std::vector<double>& y,
        const std::vector<std::vector<double>>& X
    ) const {
        size_t n = y.size();
        size_t k = X.size() + 1;  // +1 for intercept
        
        std::vector<double> coefficients(k, 0.0);
        RegressionStats stats;
        stats.t_stats.resize(k, 0.0);
        
        if (n < k + 1) return {coefficients, stats};
        
        // Build design matrix with intercept
        std::vector<std::vector<double>> design(n, std::vector<double>(k, 1.0));
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < X.size(); ++j) {
                design[i][j + 1] = X[j][i];
            }
        }
        
        // Solve using normal equations: (X'X)^-1 X'y
        // This is a simplified implementation
        
        // X'X
        std::vector<std::vector<double>> XtX(k, std::vector<double>(k, 0.0));
        for (size_t i = 0; i < k; ++i) {
            for (size_t j = 0; j < k; ++j) {
                for (size_t t = 0; t < n; ++t) {
                    XtX[i][j] += design[t][i] * design[t][j];
                }
            }
        }
        
        // X'y
        std::vector<double> Xty(k, 0.0);
        for (size_t i = 0; i < k; ++i) {
            for (size_t t = 0; t < n; ++t) {
                Xty[i] += design[t][i] * y[t];
            }
        }
        
        // Solve using Gaussian elimination (simplified)
        coefficients = solve_linear_system(XtX, Xty);
        
        // Calculate R-squared and residuals
        double y_mean = std::accumulate(y.begin(), y.end(), 0.0) / n;
        double ss_tot = 0.0;
        double ss_res = 0.0;
        
        for (size_t i = 0; i < n; ++i) {
            double y_pred = 0.0;
            for (size_t j = 0; j < k; ++j) {
                y_pred += coefficients[j] * design[i][j];
            }
            ss_res += (y[i] - y_pred) * (y[i] - y_pred);
            ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
        }
        
        stats.r_squared = 1.0 - ss_res / ss_tot;
        stats.adjusted_r_squared = 1.0 - (1.0 - stats.r_squared) * (n - 1) / (n - k);
        stats.residual_std = std::sqrt(ss_res / (n - k));
        
        // T-statistics (simplified)
        for (size_t i = 0; i < k; ++i) {
            double se = stats.residual_std / std::sqrt(XtX[i][i] > 0 ? XtX[i][i] : 1.0);
            stats.t_stats[i] = coefficients[i] / (se > 0 ? se : 1.0);
        }
        
        return {coefficients, stats};
    }

    /**
     * @brief Solve linear system using Gaussian elimination
     */
    [[nodiscard]] std::vector<double> solve_linear_system(
        std::vector<std::vector<double>> A,
        std::vector<double> b
    ) const {
        size_t n = b.size();
        std::vector<double> x(n, 0.0);
        
        // Forward elimination
        for (size_t i = 0; i < n; ++i) {
            // Find pivot
            size_t max_row = i;
            for (size_t k = i + 1; k < n; ++k) {
                if (std::abs(A[k][i]) > std::abs(A[max_row][i])) {
                    max_row = k;
                }
            }
            std::swap(A[i], A[max_row]);
            std::swap(b[i], b[max_row]);
            
            if (std::abs(A[i][i]) < 1e-10) continue;
            
            for (size_t k = i + 1; k < n; ++k) {
                double factor = A[k][i] / A[i][i];
                for (size_t j = i; j < n; ++j) {
                    A[k][j] -= factor * A[i][j];
                }
                b[k] -= factor * b[i];
            }
        }
        
        // Back substitution
        for (int i = n - 1; i >= 0; --i) {
            x[i] = b[i];
            for (size_t j = i + 1; j < n; ++j) {
                x[i] -= A[i][j] * x[j];
            }
            if (std::abs(A[i][i]) > 1e-10) {
                x[i] /= A[i][i];
            }
        }
        
        return x;
    }

    [[nodiscard]] double calculate_correlation(
        const std::vector<double>& x,
        const std::vector<double>& y
    ) const {
        size_t n = std::min(x.size(), y.size());
        if (n < 2) return 0.0;
        
        double sum_x = 0, sum_y = 0;
        for (size_t i = 0; i < n; ++i) {
            sum_x += x[i];
            sum_y += y[i];
        }
        double mean_x = sum_x / n;
        double mean_y = sum_y / n;
        
        double cov = 0, var_x = 0, var_y = 0;
        for (size_t i = 0; i < n; ++i) {
            double dx = x[i] - mean_x;
            double dy = y[i] - mean_y;
            cov += dx * dy;
            var_x += dx * dx;
            var_y += dy * dy;
        }
        
        double denom = std::sqrt(var_x * var_y);
        return denom > 1e-10 ? cov / denom : 0.0;
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_FACTOR_EXPOSURE_HPP
