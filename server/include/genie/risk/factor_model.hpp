/**
 * @file factor_model.hpp
 * @brief Multi-factor risk model (Barra-style) for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Factor exposures, covariance estimation, marginal/component VaR,
 * and factor-based return attribution.
 *
 * Factors: Market, Size, Value, Momentum, Quality, Volatility
 */
#pragma once
#ifndef GENIE_RISK_FACTOR_MODEL_HPP
#define GENIE_RISK_FACTOR_MODEL_HPP

#include "../core/math_utils.hpp"
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace genie::risk {

enum class Factor { Market, Size, Value, Momentum, Quality, Volatility };

inline std::string factor_name(Factor f) {
    switch (f) {
        case Factor::Market:     return "Market";
        case Factor::Size:       return "Size";
        case Factor::Value:      return "Value";
        case Factor::Momentum:   return "Momentum";
        case Factor::Quality:    return "Quality";
        case Factor::Volatility: return "Volatility";
        default: return "Unknown";
    }
}

static constexpr size_t NUM_FACTORS = 6;
static const Factor ALL_FACTORS[] = {
    Factor::Market, Factor::Size, Factor::Value,
    Factor::Momentum, Factor::Quality, Factor::Volatility
};

/** Factor exposures for a single security */
struct FactorExposure {
    std::string security_id;
    double exposures[NUM_FACTORS]{1.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // default: market beta=1

    double& operator[](Factor f) { return exposures[static_cast<size_t>(f)]; }
    double operator[](Factor f) const { return exposures[static_cast<size_t>(f)]; }
};

/** Factor covariance matrix (symmetric NxN) */
class FactorCovariance {
    double cov_[NUM_FACTORS][NUM_FACTORS]{};

public:
    FactorCovariance() {
        // Default: diagonal with typical annual factor volatilities
        double vols[] = {0.16, 0.08, 0.06, 0.10, 0.05, 0.12}; // annualized
        for (size_t i = 0; i < NUM_FACTORS; ++i)
            cov_[i][i] = vols[i] * vols[i];
        // Add some correlations
        set_correlation(Factor::Market, Factor::Size, -0.3);
        set_correlation(Factor::Market, Factor::Volatility, -0.5);
        set_correlation(Factor::Value, Factor::Momentum, -0.2);
        set_correlation(Factor::Quality, Factor::Volatility, -0.3);
    }

    void set(Factor i, Factor j, double val) {
        cov_[static_cast<size_t>(i)][static_cast<size_t>(j)] = val;
        cov_[static_cast<size_t>(j)][static_cast<size_t>(i)] = val;
    }

    void set_correlation(Factor i, Factor j, double rho) {
        size_t a = static_cast<size_t>(i), b = static_cast<size_t>(j);
        double val = rho * std::sqrt(cov_[a][a] * cov_[b][b]);
        cov_[a][b] = val; cov_[b][a] = val;
    }

    [[nodiscard]] double get(Factor i, Factor j) const {
        return cov_[static_cast<size_t>(i)][static_cast<size_t>(j)];
    }

    [[nodiscard]] double factor_vol(Factor f) const {
        return std::sqrt(cov_[static_cast<size_t>(f)][static_cast<size_t>(f)]);
    }
};

/** Factor return attribution for a single period */
struct FactorAttribution {
    double factor_returns[NUM_FACTORS]{};
    double total_factor_return{0};
    double specific_return{0};
    double total_return{0};
};

/** Multi-factor risk model */
class FactorModel {
    FactorCovariance covariance_;
    std::map<std::string, FactorExposure> exposures_;
    std::map<std::string, double> specific_risk_; // idiosyncratic risk per security

public:
    FactorModel() = default;
    explicit FactorModel(const FactorCovariance& cov) : covariance_(cov) {}

    void set_covariance(const FactorCovariance& cov) { covariance_ = cov; }
    [[nodiscard]] const FactorCovariance& covariance() const { return covariance_; }

    void set_exposure(const FactorExposure& exp) { exposures_[exp.security_id] = exp; }
    void set_specific_risk(const std::string& id, double risk) { specific_risk_[id] = risk; }

    [[nodiscard]] const FactorExposure& exposure(const std::string& id) const {
        static FactorExposure default_exp;
        auto it = exposures_.find(id);
        return (it != exposures_.end()) ? it->second : default_exp;
    }

    [[nodiscard]] double specific_risk(const std::string& id) const {
        auto it = specific_risk_.find(id);
        return (it != specific_risk_.end()) ? it->second : 0.20; // default 20% idio vol
    }

    /** Portfolio factor exposure (weight-averaged) */
    [[nodiscard]] FactorExposure portfolio_exposure(
            const std::map<std::string, double>& weights) const {
        FactorExposure port;
        port.security_id = "PORTFOLIO";
        for (const auto& [id, w] : weights) {
            const auto& exp = exposure(id);
            for (size_t f = 0; f < NUM_FACTORS; ++f)
                port.exposures[f] += w * exp.exposures[f];
        }
        return port;
    }

    /** Portfolio systematic (factor) variance */
    [[nodiscard]] double systematic_variance(
            const std::map<std::string, double>& weights) const {
        auto port = portfolio_exposure(weights);
        double var = 0;
        for (size_t i = 0; i < NUM_FACTORS; ++i)
            for (size_t j = 0; j < NUM_FACTORS; ++j)
                var += port.exposures[i] * port.exposures[j]
                     * covariance_.get(ALL_FACTORS[i], ALL_FACTORS[j]);
        return var;
    }

    /** Portfolio specific (idiosyncratic) variance */
    [[nodiscard]] double specific_variance(
            const std::map<std::string, double>& weights) const {
        double var = 0;
        for (const auto& [id, w] : weights) {
            double sr = specific_risk(id);
            var += w * w * sr * sr;
        }
        return var;
    }

    /** Total portfolio variance = systematic + specific */
    [[nodiscard]] double total_variance(
            const std::map<std::string, double>& weights) const {
        return systematic_variance(weights) + specific_variance(weights);
    }

    /** Total portfolio volatility */
    [[nodiscard]] double total_volatility(
            const std::map<std::string, double>& weights) const {
        return std::sqrt(total_variance(weights));
    }

    /** Risk decomposition: % systematic vs % specific */
    struct RiskDecomposition {
        double total_vol{0};
        double systematic_vol{0}, specific_vol{0};
        double systematic_pct{0}, specific_pct{0};
        double factor_contributions[NUM_FACTORS]{};
    };

    [[nodiscard]] RiskDecomposition decompose_risk(
            const std::map<std::string, double>& weights) const {
        RiskDecomposition rd;
        double sys_var = systematic_variance(weights);
        double spc_var = specific_variance(weights);
        double tot_var = sys_var + spc_var;
        rd.total_vol = std::sqrt(tot_var);
        rd.systematic_vol = std::sqrt(sys_var);
        rd.specific_vol = std::sqrt(spc_var);
        rd.systematic_pct = (tot_var > 0) ? sys_var / tot_var * 100 : 0;
        rd.specific_pct = (tot_var > 0) ? spc_var / tot_var * 100 : 0;

        // Per-factor marginal contribution
        auto port = portfolio_exposure(weights);
        for (size_t f = 0; f < NUM_FACTORS; ++f) {
            double contrib = 0;
            for (size_t j = 0; j < NUM_FACTORS; ++j)
                contrib += port.exposures[f] * port.exposures[j]
                         * covariance_.get(ALL_FACTORS[f], ALL_FACTORS[j]);
            rd.factor_contributions[f] = (tot_var > 0) ? contrib / tot_var * 100 : 0;
        }
        return rd;
    }

    /** Marginal contribution to risk of adding a security */
    [[nodiscard]] double marginal_risk(const std::string& security_id,
                                        const std::map<std::string, double>& weights) const {
        double base_vol = total_volatility(weights);
        auto modified = weights;
        double bump = 0.001; // 0.1% weight bump
        modified[security_id] = modified[security_id] + bump;
        // Normalize
        double total = 0;
        for (auto& [k, v] : modified) total += v;
        for (auto& [k, v] : modified) v /= total;
        double bumped_vol = total_volatility(modified);
        return (bumped_vol - base_vol) / bump;
    }

    /** Factor return attribution */
    [[nodiscard]] FactorAttribution attribute_returns(
            const std::map<std::string, double>& weights,
            const std::map<std::string, double>& security_returns,
            const double factor_returns_arr[NUM_FACTORS]) const {
        FactorAttribution attr;
        auto port = portfolio_exposure(weights);

        for (size_t f = 0; f < NUM_FACTORS; ++f) {
            attr.factor_returns[f] = port.exposures[f] * factor_returns_arr[f];
            attr.total_factor_return += attr.factor_returns[f];
        }

        // Total portfolio return
        attr.total_return = 0;
        for (const auto& [id, w] : weights) {
            auto it = security_returns.find(id);
            if (it != security_returns.end())
                attr.total_return += w * it->second;
        }
        attr.specific_return = attr.total_return - attr.total_factor_return;
        return attr;
    }

    [[nodiscard]] size_t security_count() const { return exposures_.size(); }

    /** Factory: create with default equity factor covariance */
    static FactorModel create_equity_model() {
        FactorModel m;
        return m;
    }
};

} // namespace genie::risk

#endif // GENIE_RISK_FACTOR_MODEL_HPP
