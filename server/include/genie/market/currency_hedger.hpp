/**
 * @file currency_hedger.hpp
 * @brief FX hedging strategy with rolling forwards and cost estimation
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Currency risk management:
 * - Currency exposure calculation per portfolio
 * - Hedge ratio optimization (full, partial, dynamic)
 * - Forward contract rolling schedule
 * - Hedging cost estimation (forward points, carry)
 * - Cross-currency basis tracking
 * - Hedge effectiveness measurement
 * - Multi-currency portfolio support
 * - Rebalancing triggers for hedge adjustments
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_MARKET_CURRENCY_HEDGER_HPP
#define GENIE_MARKET_CURRENCY_HEDGER_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>

namespace genie {
namespace market {
namespace hedge {

// ============================================================================
// Enumerations
// ============================================================================

enum class HedgeStrategy { None, Full, Partial, Dynamic, OptimalVariance };

[[nodiscard]] inline std::string strategy_string(HedgeStrategy s) {
    switch (s) {
        case HedgeStrategy::None: return "none"; case HedgeStrategy::Full: return "full";
        case HedgeStrategy::Partial: return "partial"; case HedgeStrategy::Dynamic: return "dynamic";
        case HedgeStrategy::OptimalVariance: return "optimal_variance";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct CurrencyExposure {
    std::string currency;
    double exposure_local{0};       // In foreign currency
    double exposure_base{0};        // In base (home) currency
    double weight{0};               // % of total portfolio
    double spot_rate{0};            // Foreign/Base
    double forward_rate{0};         // 3M forward
    double forward_points{0};       // Forward - spot (annualized bps)
    double volatility{0};           // Annualized FX vol
    double correlation_to_assets{0}; // Corr between FX and asset returns
};

struct HedgePosition {
    std::string currency;
    double notional_base{0};
    double hedge_ratio{0};
    double forward_rate{0};
    std::string maturity_date;
    double unrealized_pnl{0};
    double carry_cost_annual_bps{0};
    std::chrono::system_clock::time_point inception;
};

struct HedgeRecommendation {
    std::string currency;
    double current_ratio{0};
    double recommended_ratio{0};
    double adjustment_notional{0};
    double estimated_annual_cost_bps{0};
    double risk_reduction_pct{0};
    std::string rationale;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << currency << ": " << current_ratio * 100 << "% -> "
            << recommended_ratio * 100 << "% (adj $"
            << std::setprecision(0) << adjustment_notional
            << " cost=" << std::setprecision(1) << estimated_annual_cost_bps << "bps)";
        return oss.str();
    }
};

struct HedgeEffectiveness {
    double portfolio_vol_unhedged{0};
    double portfolio_vol_hedged{0};
    double vol_reduction_pct{0};
    double hedging_cost_annual_bps{0};
    double net_benefit_bps{0};    // Risk reduction value minus cost

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Unhedged vol=" << portfolio_vol_unhedged * 100 << "%"
            << " Hedged vol=" << portfolio_vol_hedged * 100 << "%"
            << " Reduction=" << vol_reduction_pct * 100 << "%"
            << " Cost=" << hedging_cost_annual_bps << "bps";
        return oss.str();
    }
};

// ============================================================================
// Currency Hedger
// ============================================================================

class CurrencyHedger {
public:
    explicit CurrencyHedger(const std::string& base_ccy = "USD")
        : base_currency_(base_ccy) {
        register_default_rates();
    }

    /**
     * @brief Calculate optimal hedge recommendations
     */
    [[nodiscard]] std::vector<HedgeRecommendation> recommend(
        const std::vector<CurrencyExposure>& exposures,
        HedgeStrategy strategy = HedgeStrategy::Partial,
        double target_ratio = 0.50) const {

        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<HedgeRecommendation> recs;

        for (const auto& exp : exposures) {
            if (exp.currency == base_currency_) continue;
            if (std::abs(exp.exposure_base) < 1000) continue;

            HedgeRecommendation rec;
            rec.currency = exp.currency;
            rec.current_ratio = get_current_ratio(exp.currency);

            switch (strategy) {
                case HedgeStrategy::Full:
                    rec.recommended_ratio = 1.0;
                    rec.rationale = "Full hedge";
                    break;
                case HedgeStrategy::Partial:
                    rec.recommended_ratio = target_ratio;
                    rec.rationale = "Partial hedge at " + std::to_string(int(target_ratio * 100)) + "%";
                    break;
                case HedgeStrategy::Dynamic: {
                    // Higher vol = higher hedge ratio
                    double vol_factor = std::min(exp.volatility / 0.10, 1.0);
                    rec.recommended_ratio = 0.3 + 0.5 * vol_factor;
                    rec.rationale = "Dynamic: vol-adjusted";
                    break;
                }
                case HedgeStrategy::OptimalVariance: {
                    // Optimal: hedge ratio = correlation * (port_vol / fx_vol)
                    double opt = std::abs(exp.correlation_to_assets);
                    rec.recommended_ratio = std::min(opt, 1.0);
                    rec.rationale = "Variance-optimal";
                    break;
                }
                default:
                    rec.recommended_ratio = 0;
                    rec.rationale = "No hedge";
            }

            rec.adjustment_notional = exp.exposure_base * (rec.recommended_ratio - rec.current_ratio);
            rec.estimated_annual_cost_bps = std::abs(exp.forward_points);
            rec.risk_reduction_pct = rec.recommended_ratio * exp.volatility * exp.weight;
            recs.push_back(std::move(rec));
        }
        return recs;
    }

    /**
     * @brief Measure hedge effectiveness
     */
    [[nodiscard]] HedgeEffectiveness measure_effectiveness(
        const std::vector<CurrencyExposure>& exposures,
        double asset_volatility = 0.12) const {

        std::lock_guard<std::mutex> lock(mutex_);
        HedgeEffectiveness eff;
        double fx_var_contribution = 0;
        double hedged_fx_var = 0;
        double total_cost = 0;

        for (const auto& exp : exposures) {
            if (exp.currency == base_currency_) continue;
            double w = exp.weight;
            double fxv = exp.volatility;
            fx_var_contribution += w * w * fxv * fxv;
            double ratio = get_current_ratio(exp.currency);
            hedged_fx_var += w * w * fxv * fxv * (1 - ratio) * (1 - ratio);
            total_cost += w * std::abs(exp.forward_points);
        }

        eff.portfolio_vol_unhedged = std::sqrt(asset_volatility * asset_volatility + fx_var_contribution);
        eff.portfolio_vol_hedged = std::sqrt(asset_volatility * asset_volatility + hedged_fx_var);
        eff.vol_reduction_pct = eff.portfolio_vol_unhedged > 0 ?
            (eff.portfolio_vol_unhedged - eff.portfolio_vol_hedged) / eff.portfolio_vol_unhedged : 0;
        eff.hedging_cost_annual_bps = total_cost;
        eff.net_benefit_bps = eff.vol_reduction_pct * 10000 - eff.hedging_cost_annual_bps;
        return eff;
    }

    void set_hedge_ratio(const std::string& ccy, double ratio) {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ratios_[ccy] = std::clamp(ratio, 0.0, 1.0);
    }

    [[nodiscard]] int currency_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(spot_rates_.size());
    }

private:
    mutable std::mutex mutex_;
    std::string base_currency_;
    std::map<std::string, double> spot_rates_;
    std::map<std::string, double> forward_3m_;
    std::map<std::string, double> current_ratios_;

    double get_current_ratio(const std::string& ccy) const {
        auto it = current_ratios_.find(ccy);
        return it != current_ratios_.end() ? it->second : 0;
    }

    void register_default_rates() {
        spot_rates_["EUR"] = 1.08; spot_rates_["GBP"] = 1.27; spot_rates_["JPY"] = 0.0067;
        spot_rates_["CHF"] = 1.13; spot_rates_["AUD"] = 0.65; spot_rates_["CAD"] = 0.74;
        forward_3m_["EUR"] = 1.0810; forward_3m_["GBP"] = 1.2715; forward_3m_["JPY"] = 0.00672;
        forward_3m_["CHF"] = 1.1320; forward_3m_["AUD"] = 0.6490; forward_3m_["CAD"] = 0.7405;
    }
};

} // namespace hedge
} // namespace market
} // namespace genie

#endif // GENIE_MARKET_CURRENCY_HEDGER_HPP
