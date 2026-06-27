/**
 * @file incremental_var.hpp
 * @brief Incremental and Component VaR analysis
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements VaR decomposition:
 * - Marginal VaR: Sensitivity of VaR to position changes
 * - Component VaR: Each position's contribution to total VaR
 * - Incremental VaR: Change in VaR from adding a position
 * - Diversification analysis
 */

#pragma once
#ifndef GENIE_RISK_INCREMENTAL_VAR_HPP
#define GENIE_RISK_INCREMENTAL_VAR_HPP

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace genie::risk {

using SecurityId = std::string;

/**
 * @brief Incremental VaR result for a single position
 */
struct IncrementalVaR {
    SecurityId security;
    double position_value{0};       // Current position value
    double weight{0};               // Portfolio weight
    double marginal_var{0};         // dVaR/dposition (sensitivity)
    double component_var{0};        // position x marginal_var
    double percent_contribution{0}; // % of total VaR
    double standalone_var{0};       // VaR if only this position
    double diversification_benefit{0}; // standalone - component
};

/**
 * @brief Complete component VaR analysis result
 */
struct ComponentVaRResult {
    double portfolio_var{0};
    double portfolio_value{0};
    double confidence_level{0.95};
    int holding_period{1};
    
    std::vector<IncrementalVaR> components;
    
    double sum_component_var{0};    // Should equal portfolio_var
    double sum_standalone_var{0};   // Sum of individual VaRs
    double diversification_ratio{0}; // portfolio_var / sum_standalone
    double diversification_benefit{0}; // 1 - diversification_ratio
    
    /**
     * @brief Get component by security
     */
    [[nodiscard]] const IncrementalVaR* get_component(const SecurityId& id) const {
        for (const auto& c : components) {
            if (c.security == id) return &c;
        }
        return nullptr;
    }
    
    /**
     * @brief Get top N risk contributors
     */
    [[nodiscard]] std::vector<IncrementalVaR> top_contributors(size_t n = 5) const {
        auto sorted = components;
        std::sort(sorted.begin(), sorted.end(),
            [](const IncrementalVaR& a, const IncrementalVaR& b) {
                return std::abs(a.component_var) > std::abs(b.component_var);
            });
        if (sorted.size() > n) sorted.resize(n);
        return sorted;
    }
};

/**
 * @brief Position data for VaR calculation
 */
struct VaRPosition {
    SecurityId security;
    double value{0};            // Position market value
    double volatility{0};       // Annualized volatility
    double weight{0};           // Portfolio weight
};

/**
 * @brief Covariance matrix for VaR calculation
 */
class CovarianceMatrix {
    std::vector<SecurityId> securities_;
    std::vector<std::vector<double>> matrix_;
    std::map<SecurityId, size_t> index_map_;

public:
    CovarianceMatrix() = default;
    
    explicit CovarianceMatrix(const std::vector<SecurityId>& securities)
        : securities_(securities) {
        size_t n = securities.size();
        matrix_.resize(n, std::vector<double>(n, 0.0));
        for (size_t i = 0; i < n; ++i) {
            index_map_[securities[i]] = i;
        }
    }
    
    void set(const SecurityId& a, const SecurityId& b, double cov) {
        auto it_a = index_map_.find(a);
        auto it_b = index_map_.find(b);
        if (it_a != index_map_.end() && it_b != index_map_.end()) {
            matrix_[it_a->second][it_b->second] = cov;
            matrix_[it_b->second][it_a->second] = cov;
        }
    }
    
    [[nodiscard]] double get(const SecurityId& a, const SecurityId& b) const {
        auto it_a = index_map_.find(a);
        auto it_b = index_map_.find(b);
        if (it_a != index_map_.end() && it_b != index_map_.end()) {
            return matrix_[it_a->second][it_b->second];
        }
        return 0.0;
    }
    
    [[nodiscard]] size_t size() const { return securities_.size(); }
    [[nodiscard]] const std::vector<SecurityId>& securities() const { return securities_; }
};

/**
 * @brief Incremental VaR calculation engine
 */
class IncrementalVaREngine {
    double confidence_{0.95};
    int holding_period_{1};

public:
    void set_confidence(double c) { confidence_ = c; }
    void set_holding_period(int days) { holding_period_ = days; }

    /**
     * @brief Calculate component VaR for portfolio
     * 
     * Uses variance-covariance (parametric) approach:
     * - Portfolio variance = w' * Sigma * w
     * - Marginal VaR = dVaR/dw_i = z * sigma_p * (Sigma * w)_i / sigma_p
     * - Component VaR = w_i * Marginal VaR_i
     * 
     * @param positions Portfolio positions
     * @param cov_matrix Covariance matrix
     * @return ComponentVaRResult Complete decomposition
     */
    [[nodiscard]] ComponentVaRResult calculate(
        const std::vector<VaRPosition>& positions,
        const CovarianceMatrix& cov_matrix
    ) const {
        ComponentVaRResult result;
        result.confidence_level = confidence_;
        result.holding_period = holding_period_;
        
        if (positions.empty()) return result;
        
        size_t n = positions.size();
        
        // Calculate portfolio value and weights
        result.portfolio_value = 0.0;
        for (const auto& pos : positions) {
            result.portfolio_value += pos.value;
        }
        
        std::vector<double> weights(n);
        std::vector<double> values(n);
        for (size_t i = 0; i < n; ++i) {
            values[i] = positions[i].value;
            weights[i] = positions[i].value / result.portfolio_value;
        }
        
        // Calculate portfolio variance: w' * Sigma * w
        double portfolio_variance = 0.0;
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                double cov = cov_matrix.get(positions[i].security, positions[j].security);
                portfolio_variance += weights[i] * weights[j] * cov;
            }
        }
        
        double portfolio_std = std::sqrt(portfolio_variance);
        
        // Z-score for confidence level
        double z = std::abs(normal_inverse_cdf(1.0 - confidence_));
        
        // Adjust for holding period
        double sqrt_t = std::sqrt(static_cast<double>(holding_period_));
        
        // Portfolio VaR
        result.portfolio_var = z * portfolio_std * sqrt_t * result.portfolio_value;
        
        // Calculate (Sigma * w) for marginal VaR
        std::vector<double> sigma_w(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                double cov = cov_matrix.get(positions[i].security, positions[j].security);
                sigma_w[i] += cov * weights[j];
            }
        }
        
        // Calculate component VaR for each position
        result.components.resize(n);
        result.sum_component_var = 0.0;
        result.sum_standalone_var = 0.0;
        
        for (size_t i = 0; i < n; ++i) {
            auto& comp = result.components[i];
            comp.security = positions[i].security;
            comp.position_value = positions[i].value;
            comp.weight = weights[i];
            
            // Marginal VaR: dVaR/dw_i
            if (portfolio_std > 1e-10) {
                comp.marginal_var = z * sqrt_t * result.portfolio_value * sigma_w[i] / portfolio_std;
            }
            
            // Component VaR: w_i * Marginal VaR
            comp.component_var = weights[i] * comp.marginal_var;
            
            // Percent contribution
            if (result.portfolio_var > 1e-10) {
                comp.percent_contribution = comp.component_var / result.portfolio_var * 100.0;
            }
            
            // Standalone VaR (if only this position)
            double standalone_std = std::sqrt(cov_matrix.get(positions[i].security, positions[i].security));
            comp.standalone_var = z * standalone_std * sqrt_t * positions[i].value;
            
            // Diversification benefit
            comp.diversification_benefit = comp.standalone_var - comp.component_var;
            
            result.sum_component_var += comp.component_var;
            result.sum_standalone_var += comp.standalone_var;
        }
        
        // Diversification metrics
        if (result.sum_standalone_var > 1e-10) {
            result.diversification_ratio = result.portfolio_var / result.sum_standalone_var;
            result.diversification_benefit = 1.0 - result.diversification_ratio;
        }
        
        return result;
    }

    /**
     * @brief Calculate marginal VaR for adding/removing a position
     * 
     * @param current_positions Current portfolio
     * @param security Security to analyze
     * @param position_delta Change in position value
     * @param cov_matrix Covariance matrix
     * @return Change in VaR
     */
    [[nodiscard]] double marginal_var(
        const std::vector<VaRPosition>& current_positions,
        const SecurityId& security,
        double position_delta,
        const CovarianceMatrix& cov_matrix
    ) const {
        // Calculate current VaR
        auto current_result = calculate(current_positions, cov_matrix);
        
        // Create modified positions
        std::vector<VaRPosition> modified = current_positions;
        bool found = false;
        for (auto& pos : modified) {
            if (pos.security == security) {
                pos.value += position_delta;
                found = true;
                break;
            }
        }
        
        if (!found) {
            // Add new position
            VaRPosition new_pos;
            new_pos.security = security;
            new_pos.value = position_delta;
            modified.push_back(new_pos);
        }
        
        // Calculate new VaR
        auto new_result = calculate(modified, cov_matrix);
        
        return new_result.portfolio_var - current_result.portfolio_var;
    }

private:
    /**
     * @brief Inverse of standard normal CDF (approximation)
     */
    [[nodiscard]] static double normal_inverse_cdf(double p) {
        // Rational approximation for inverse normal
        if (p <= 0.0) return -std::numeric_limits<double>::infinity();
        if (p >= 1.0) return std::numeric_limits<double>::infinity();
        if (p == 0.5) return 0.0;
        
        double t = std::sqrt(-2.0 * std::log(p < 0.5 ? p : 1.0 - p));
        
        // Coefficients
        const double c0 = 2.515517;
        const double c1 = 0.802853;
        const double c2 = 0.010328;
        const double d1 = 1.432788;
        const double d2 = 0.189269;
        const double d3 = 0.001308;
        
        double result = t - (c0 + c1*t + c2*t*t) / (1 + d1*t + d2*t*t + d3*t*t*t);
        
        return p < 0.5 ? -result : result;
    }
};

} // namespace genie::risk

#endif // GENIE_RISK_INCREMENTAL_VAR_HPP
