/**
 * @file factor_model.hpp
 * @brief Multi-factor risk/return attribution model
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Factor-based portfolio analytics:
 * - Fama-French 3-factor and 5-factor models
 * - Custom factor definitions
 * - Factor exposure calculation
 * - Factor return attribution
 * - Risk decomposition (systematic vs idiosyncratic)
 * - Factor covariance estimation
 * - Factor mimicking portfolio construction
 * - Regression-based factor loading
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_ANALYTICS_FACTOR_MODEL_HPP
#define GENIE_ANALYTICS_FACTOR_MODEL_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cmath>
#include <numeric>

namespace genie {
namespace analytics {
namespace factor {

// ============================================================================
// Data Structures
// ============================================================================

enum class FactorCategory {
    Market,
    Size,
    Value,
    Momentum,
    Quality,
    Volatility,
    Yield,
    Growth,
    Liquidity,
    Sector,
    Country,
    Currency,
    Custom
};

[[nodiscard]] inline std::string factor_category_string(FactorCategory c) {
    switch (c) {
        case FactorCategory::Market:     return "market";
        case FactorCategory::Size:       return "size";
        case FactorCategory::Value:      return "value";
        case FactorCategory::Momentum:   return "momentum";
        case FactorCategory::Quality:    return "quality";
        case FactorCategory::Volatility: return "volatility";
        case FactorCategory::Yield:      return "yield";
        case FactorCategory::Growth:     return "growth";
        case FactorCategory::Liquidity:  return "liquidity";
        case FactorCategory::Sector:     return "sector";
        case FactorCategory::Country:    return "country";
        case FactorCategory::Currency:   return "currency";
        case FactorCategory::Custom:     return "custom";
    }
    return "unknown";
}

/**
 * @brief Factor definition
 */
struct Factor {
    std::string id;
    std::string name;
    FactorCategory category;
    std::string description;
    std::vector<double> returns;       // Historical returns
    double expected_return{0};
    double volatility{0};
    double sharpe_ratio{0};

    [[nodiscard]] double mean_return() const {
        if (returns.empty()) return 0;
        return std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    }

    [[nodiscard]] double std_dev() const {
        if (returns.size() < 2) return 0;
        double mean = mean_return();
        double sq_sum = 0;
        for (double r : returns) sq_sum += (r - mean) * (r - mean);
        return std::sqrt(sq_sum / (returns.size() - 1));
    }
};

/**
 * @brief Asset factor exposures (betas)
 */
struct AssetExposure {
    std::string symbol;
    std::map<std::string, double> betas;   // factor_id -> beta
    double alpha{0};                        // Intercept (idiosyncratic)
    double r_squared{0};                    // Model fit
    double residual_vol{0};                 // Idiosyncratic volatility

    [[nodiscard]] double systematic_risk_pct() const { return r_squared * 100.0; }
    [[nodiscard]] double idiosyncratic_risk_pct() const { return (1.0 - r_squared) * 100.0; }
};

/**
 * @brief Portfolio factor attribution
 */
struct PortfolioAttribution {
    std::map<std::string, double> factor_exposures;   // Weighted portfolio betas
    std::map<std::string, double> factor_contributions; // Return attributed to each factor
    double total_return{0};
    double systematic_return{0};
    double idiosyncratic_return{0};
    double selection_effect{0};            // Stock picking
    double allocation_effect{0};           // Factor timing

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "Portfolio Attribution:\n";
        oss << "  Total Return: " << total_return * 100 << "%\n";
        oss << "  Systematic: " << systematic_return * 100 << "%\n";
        oss << "  Idiosyncratic: " << idiosyncratic_return * 100 << "%\n\n";
        oss << "  Factor Contributions:\n";
        for (const auto& [name, contrib] : factor_contributions) {
            oss << "    " << std::setw(15) << std::left << name
                << " beta=" << std::setw(8) << std::right
                << factor_exposures.at(name)
                << " contrib=" << contrib * 100 << "%\n";
        }
        return oss.str();
    }
};

/**
 * @brief Factor covariance matrix
 */
struct FactorCovariance {
    std::vector<std::string> factor_ids;
    std::vector<std::vector<double>> matrix;

    [[nodiscard]] double get(int i, int j) const {
        if (i < static_cast<int>(matrix.size()) &&
            j < static_cast<int>(matrix[i].size()))
            return matrix[i][j];
        return 0;
    }

    [[nodiscard]] double correlation(int i, int j) const {
        double var_i = get(i, i);
        double var_j = get(j, j);
        if (var_i <= 0 || var_j <= 0) return 0;
        return get(i, j) / std::sqrt(var_i * var_j);
    }
};

// ============================================================================
// Factor Model
// ============================================================================

class FactorModel {
public:
    FactorModel() {
        register_fama_french();
    }

    /**
     * @brief Register a factor
     */
    void add_factor(Factor factor) {
        std::lock_guard<std::mutex> lock(mutex_);
        factors_[factor.id] = std::move(factor);
    }

    /**
     * @brief Set asset factor exposures
     */
    void set_exposure(const std::string& symbol,
                        const std::map<std::string, double>& betas,
                        double alpha = 0, double r_squared = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        AssetExposure exp;
        exp.symbol = symbol;
        exp.betas = betas;
        exp.alpha = alpha;
        exp.r_squared = r_squared;
        exposures_[symbol] = std::move(exp);
    }

    /**
     * @brief Get expected return for asset using factor model
     */
    [[nodiscard]] double expected_return(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto exp_it = exposures_.find(symbol);
        if (exp_it == exposures_.end()) return 0;

        double ret = exp_it->second.alpha;
        for (const auto& [fid, beta] : exp_it->second.betas) {
            auto f_it = factors_.find(fid);
            if (f_it != factors_.end()) {
                ret += beta * f_it->second.expected_return;
            }
        }
        return ret;
    }

    /**
     * @brief Portfolio factor attribution
     */
    [[nodiscard]] PortfolioAttribution attribute(
        const std::map<std::string, double>& weights,
        const std::map<std::string, double>& asset_returns = {}) const {
        std::lock_guard<std::mutex> lock(mutex_);
        PortfolioAttribution attr;

        // Calculate weighted portfolio betas
        for (const auto& [symbol, weight] : weights) {
            auto exp_it = exposures_.find(symbol);
            if (exp_it == exposures_.end()) continue;

            for (const auto& [fid, beta] : exp_it->second.betas) {
                attr.factor_exposures[fid] += weight * beta;
            }
        }

        // Calculate factor contributions
        for (const auto& [fid, portfolio_beta] : attr.factor_exposures) {
            auto f_it = factors_.find(fid);
            if (f_it != factors_.end()) {
                double contribution = portfolio_beta * f_it->second.expected_return;
                attr.factor_contributions[fid] = contribution;
                attr.systematic_return += contribution;
            }
        }

        // Total return from asset returns or model
        if (!asset_returns.empty()) {
            for (const auto& [sym, w] : weights) {
                auto r_it = asset_returns.find(sym);
                if (r_it != asset_returns.end())
                    attr.total_return += w * r_it->second;
            }
        } else {
            attr.total_return = attr.systematic_return;
            for (const auto& [sym, w] : weights) {
                auto exp_it = exposures_.find(sym);
                if (exp_it != exposures_.end())
                    attr.total_return += w * exp_it->second.alpha;
            }
        }

        attr.idiosyncratic_return = attr.total_return - attr.systematic_return;
        return attr;
    }

    /**
     * @brief Compute factor covariance matrix
     */
    [[nodiscard]] FactorCovariance compute_covariance() const {
        std::lock_guard<std::mutex> lock(mutex_);
        FactorCovariance cov;
        for (const auto& [id, _] : factors_) cov.factor_ids.push_back(id);
        int n = static_cast<int>(cov.factor_ids.size());
        cov.matrix.resize(n, std::vector<double>(n, 0));

        for (int i = 0; i < n; ++i) {
            const auto& fi = factors_.at(cov.factor_ids[i]);
            for (int j = i; j < n; ++j) {
                const auto& fj = factors_.at(cov.factor_ids[j]);
                double covariance = compute_covariance_pair(fi.returns, fj.returns);
                cov.matrix[i][j] = covariance;
                cov.matrix[j][i] = covariance;
            }
        }
        return cov;
    }

    /**
     * @brief Simple OLS regression to estimate betas
     */
    [[nodiscard]] AssetExposure regress(const std::string& symbol,
                                          const std::vector<double>& asset_returns) const {
        std::lock_guard<std::mutex> lock(mutex_);
        AssetExposure exp;
        exp.symbol = symbol;

        // Simple single-factor regression against market
        auto mkt_it = factors_.find("MKT");
        if (mkt_it == factors_.end() || asset_returns.empty()) return exp;

        const auto& mkt_returns = mkt_it->second.returns;
        int n = std::min(static_cast<int>(asset_returns.size()),
                         static_cast<int>(mkt_returns.size()));
        if (n < 3) return exp;

        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
        for (int i = 0; i < n; ++i) {
            sum_x += mkt_returns[i];
            sum_y += asset_returns[i];
            sum_xy += mkt_returns[i] * asset_returns[i];
            sum_xx += mkt_returns[i] * mkt_returns[i];
        }

        double mean_x = sum_x / n;
        double mean_y = sum_y / n;
        double beta = (sum_xy - n * mean_x * mean_y) / (sum_xx - n * mean_x * mean_x);
        double alpha = mean_y - beta * mean_x;

        exp.betas["MKT"] = beta;
        exp.alpha = alpha;

        // R-squared
        double ss_res = 0, ss_tot = 0;
        for (int i = 0; i < n; ++i) {
            double predicted = alpha + beta * mkt_returns[i];
            ss_res += (asset_returns[i] - predicted) * (asset_returns[i] - predicted);
            ss_tot += (asset_returns[i] - mean_y) * (asset_returns[i] - mean_y);
        }
        exp.r_squared = ss_tot > 0 ? 1.0 - (ss_res / ss_tot) : 0;
        exp.residual_vol = n > 2 ? std::sqrt(ss_res / (n - 2)) : 0;

        return exp;
    }

    [[nodiscard]] int factor_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(factors_.size());
    }

    [[nodiscard]] int asset_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(exposures_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Factor> factors_;
    std::map<std::string, AssetExposure> exposures_;

    double compute_covariance_pair(const std::vector<double>& a,
                                     const std::vector<double>& b) const {
        int n = std::min(static_cast<int>(a.size()), static_cast<int>(b.size()));
        if (n < 2) return 0;
        double mean_a = std::accumulate(a.begin(), a.begin() + n, 0.0) / n;
        double mean_b = std::accumulate(b.begin(), b.begin() + n, 0.0) / n;
        double cov = 0;
        for (int i = 0; i < n; ++i) {
            cov += (a[i] - mean_a) * (b[i] - mean_b);
        }
        return cov / (n - 1);
    }

    void register_fama_french() {
        Factor mkt;
        mkt.id = "MKT";
        mkt.name = "Market Risk Premium";
        mkt.category = FactorCategory::Market;
        mkt.description = "Excess return of market portfolio over risk-free rate";
        mkt.expected_return = 0.065;
        mkt.volatility = 0.18;
        factors_[mkt.id] = mkt;

        Factor smb;
        smb.id = "SMB";
        smb.name = "Size (Small Minus Big)";
        smb.category = FactorCategory::Size;
        smb.description = "Return spread between small and large cap stocks";
        smb.expected_return = 0.025;
        smb.volatility = 0.10;
        factors_[smb.id] = smb;

        Factor hml;
        hml.id = "HML";
        hml.name = "Value (High Minus Low)";
        hml.category = FactorCategory::Value;
        hml.description = "Return spread between value and growth stocks";
        hml.expected_return = 0.035;
        hml.volatility = 0.11;
        factors_[hml.id] = hml;

        Factor rmw;
        rmw.id = "RMW";
        rmw.name = "Profitability (Robust Minus Weak)";
        rmw.category = FactorCategory::Quality;
        rmw.description = "Return spread by operating profitability";
        rmw.expected_return = 0.030;
        rmw.volatility = 0.08;
        factors_[rmw.id] = rmw;

        Factor cma;
        cma.id = "CMA";
        cma.name = "Investment (Conservative Minus Aggressive)";
        cma.category = FactorCategory::Growth;
        cma.description = "Return spread by asset growth";
        cma.expected_return = 0.028;
        cma.volatility = 0.07;
        factors_[cma.id] = cma;

        Factor mom;
        mom.id = "MOM";
        mom.name = "Momentum";
        mom.category = FactorCategory::Momentum;
        mom.description = "12-1 month momentum factor";
        mom.expected_return = 0.040;
        mom.volatility = 0.16;
        factors_[mom.id] = mom;
    }
};

} // namespace factor
} // namespace analytics
} // namespace genie

#endif // GENIE_ANALYTICS_FACTOR_MODEL_HPP
