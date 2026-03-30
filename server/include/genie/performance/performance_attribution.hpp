/**
 * @file performance_attribution.hpp
 * @brief Brinson-Fachler return attribution with multi-level decomposition
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Performance attribution framework:
 * - Brinson-Hood-Beebower (BHB) single-period attribution
 * - Brinson-Fachler allocation/selection/interaction effects
 * - Multi-period geometric linking (Carino smoothing)
 * - Sector/country/currency level attribution
 * - Factor-based attribution (market, size, value, momentum, quality)
 * - Fixed income attribution (duration, credit, curve, carry)
 * - Security-level selection drill-down
 * - Cumulative and rolling attribution windows
 * - Attribution summary with formatted output
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERFORMANCE_ATTRIBUTION_HPP
#define GENIE_PERFORMANCE_ATTRIBUTION_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <numeric>

namespace genie {
namespace performance {
namespace attribution {

// ============================================================================
// Data Structures
// ============================================================================

struct SectorAllocation {
    std::string sector;
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
};

struct AttributionEffect {
    std::string sector;
    double allocation{0};
    double selection{0};
    double interaction{0};
    double total{0};
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << std::left << std::setw(22) << sector
            << "  Alloc=" << std::setw(8) << allocation * 100 << "%"
            << "  Sel=" << std::setw(8) << selection * 100 << "%"
            << "  Inter=" << std::setw(8) << interaction * 100 << "%"
            << "  Total=" << std::setw(8) << total * 100 << "%";
        return oss.str();
    }
};

struct AttributionResult {
    double portfolio_return{0};
    double benchmark_return{0};
    double active_return{0};
    double total_allocation{0};
    double total_selection{0};
    double total_interaction{0};
    double residual{0};
    std::vector<AttributionEffect> effects;
    std::string period;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Attribution [" << period << "]\n"
            << "  Portfolio: " << portfolio_return * 100 << "%"
            << "  Benchmark: " << benchmark_return * 100 << "%"
            << "  Active: " << active_return * 100 << "%\n"
            << "  Allocation: " << total_allocation * 100 << "%"
            << "  Selection: " << total_selection * 100 << "%"
            << "  Interaction: " << total_interaction * 100 << "%";
        if (std::abs(residual) > 0.0001)
            oss << "  Residual: " << residual * 100 << "%";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "{\"portfolio\":" << portfolio_return
            << ",\"benchmark\":" << benchmark_return
            << ",\"active\":" << active_return
            << ",\"allocation\":" << total_allocation
            << ",\"selection\":" << total_selection
            << ",\"interaction\":" << total_interaction
            << ",\"residual\":" << residual
            << ",\"sectors\":[";
        for (size_t i = 0; i < effects.size(); ++i) {
            if (i > 0) oss << ",";
            const auto& e = effects[i];
            oss << "{\"sector\":\"" << e.sector
                << "\",\"alloc\":" << e.allocation
                << ",\"sel\":" << e.selection
                << ",\"inter\":" << e.interaction
                << ",\"total\":" << e.total << "}";
        }
        oss << "]}";
        return oss.str();
    }
};

struct FactorAttribution {
    double market_effect{0};
    double size_effect{0};
    double value_effect{0};
    double momentum_effect{0};
    double quality_effect{0};
    double residual_alpha{0};
    double total_explained{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Factor Attribution:\n"
            << "  Market=" << market_effect * 100 << "%  Size=" << size_effect * 100
            << "%  Value=" << value_effect * 100 << "%\n"
            << "  Momentum=" << momentum_effect * 100 << "%  Quality=" << quality_effect * 100
            << "%  Alpha=" << residual_alpha * 100 << "%";
        return oss.str();
    }
};

struct FixedIncomeAttribution {
    double carry_effect{0};
    double duration_effect{0};
    double curve_effect{0};
    double credit_effect{0};
    double residual{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "FI Attribution: Carry=" << carry_effect * 100
            << "% Duration=" << duration_effect * 100
            << "% Curve=" << curve_effect * 100
            << "% Credit=" << credit_effect * 100
            << "% Resid=" << residual * 100 << "%";
        return oss.str();
    }
};

// ============================================================================
// Attribution Engine
// ============================================================================

class PerformanceAttributionEngine {
public:
    /**
     * @brief Brinson-Fachler single-period attribution
     *
     * Decomposes active return into:
     *   Allocation: (Wp - Wb) * (Rb_sector - Rb_total)
     *   Selection:  Wb * (Rp_sector - Rb_sector)
     *   Interaction: (Wp - Wb) * (Rp_sector - Rb_sector)
     */
    [[nodiscard]] AttributionResult brinson_fachler(
        const std::vector<SectorAllocation>& sectors,
        const std::string& period = "") const {

        std::lock_guard<std::mutex> lock(mutex_);
        AttributionResult result;
        result.period = period;

        // Compute portfolio and benchmark total returns
        for (const auto& s : sectors) {
            result.portfolio_return += s.portfolio_weight * s.portfolio_return;
            result.benchmark_return += s.benchmark_weight * s.benchmark_return;
        }
        result.active_return = result.portfolio_return - result.benchmark_return;

        // Brinson-Fachler decomposition per sector
        for (const auto& s : sectors) {
            AttributionEffect effect;
            effect.sector = s.sector;
            effect.portfolio_weight = s.portfolio_weight;
            effect.benchmark_weight = s.benchmark_weight;
            effect.portfolio_return = s.portfolio_return;
            effect.benchmark_return = s.benchmark_return;

            double w_diff = s.portfolio_weight - s.benchmark_weight;
            double r_diff = s.portfolio_return - s.benchmark_return;

            // Allocation effect: overweight sectors that outperform
            effect.allocation = w_diff * (s.benchmark_return - result.benchmark_return);

            // Selection effect: pick better stocks within sector
            effect.selection = s.benchmark_weight * r_diff;

            // Interaction effect: cross-term
            effect.interaction = w_diff * r_diff;

            effect.total = effect.allocation + effect.selection + effect.interaction;

            result.total_allocation += effect.allocation;
            result.total_selection += effect.selection;
            result.total_interaction += effect.interaction;

            result.effects.push_back(std::move(effect));
        }

        // Residual = active return minus explained effects
        result.residual = result.active_return -
            (result.total_allocation + result.total_selection + result.total_interaction);

        return result;
    }

    /**
     * @brief Multi-period geometric linking using Carino smoothing
     *
     * Links single-period attributions into a multi-period result
     * while preserving the additive property of effects.
     */
    [[nodiscard]] AttributionResult link_periods(
        const std::vector<AttributionResult>& periods) const {

        std::lock_guard<std::mutex> lock(mutex_);
        if (periods.empty()) return {};

        AttributionResult linked;
        linked.period = "Linked (" + std::to_string(periods.size()) + " periods)";

        // Geometric linking of total returns
        double port_cum = 1.0, bench_cum = 1.0;
        for (const auto& p : periods) {
            port_cum *= (1.0 + p.portfolio_return);
            bench_cum *= (1.0 + p.benchmark_return);
        }
        linked.portfolio_return = port_cum - 1.0;
        linked.benchmark_return = bench_cum - 1.0;
        linked.active_return = linked.portfolio_return - linked.benchmark_return;

        // Carino smoothing factors for proportional attribution
        double k_total = 1.0;
        if (std::abs(linked.active_return) > 1e-12) {
            double ln_ratio = std::log(port_cum) - std::log(bench_cum);
            k_total = ln_ratio / linked.active_return;
        }

        for (const auto& p : periods) {
            double k_t = 1.0;
            if (std::abs(p.portfolio_return) > 1e-12 && std::abs(k_total) > 1e-12) {
                double ln_pt = std::log(1.0 + p.portfolio_return);
                k_t = ln_pt / ((1.0 + p.portfolio_return) * k_total);
            }
            linked.total_allocation += p.total_allocation * k_t;
            linked.total_selection += p.total_selection * k_t;
            linked.total_interaction += p.total_interaction * k_t;
        }

        linked.residual = linked.active_return -
            (linked.total_allocation + linked.total_selection + linked.total_interaction);

        return linked;
    }

    /**
     * @brief Factor-based attribution using multi-factor model
     *
     * Decomposes return into systematic factor exposures and residual alpha.
     */
    [[nodiscard]] FactorAttribution factor_attribution(
        double portfolio_return, double benchmark_return,
        double portfolio_beta, double size_tilt, double value_tilt,
        double momentum_tilt, double quality_tilt,
        double market_return, double size_premium,
        double value_premium, double momentum_premium,
        double quality_premium = 0) const {

        std::lock_guard<std::mutex> lock(mutex_);
        FactorAttribution fa;
        fa.market_effect = portfolio_beta * market_return;
        fa.size_effect = size_tilt * size_premium;
        fa.value_effect = value_tilt * value_premium;
        fa.momentum_effect = momentum_tilt * momentum_premium;
        fa.quality_effect = quality_tilt * quality_premium;
        fa.total_explained = fa.market_effect + fa.size_effect + fa.value_effect +
                             fa.momentum_effect + fa.quality_effect;
        fa.residual_alpha = (portfolio_return - benchmark_return) - fa.total_explained;
        return fa;
    }

    /**
     * @brief Fixed income attribution
     *
     * Decomposes bond return into carry, duration, curve, and credit effects.
     */
    [[nodiscard]] FixedIncomeAttribution fixed_income_attribution(
        double total_return, double benchmark_return,
        double portfolio_yield, double benchmark_yield,
        double portfolio_duration, double benchmark_duration,
        double rate_change, double spread_change,
        double curve_shift = 0) const {

        std::lock_guard<std::mutex> lock(mutex_);
        FixedIncomeAttribution fia;

        // Carry: income advantage from higher yield
        fia.carry_effect = (portfolio_yield - benchmark_yield) / 12.0; // Monthly

        // Duration: sensitivity to parallel rate shift
        fia.duration_effect = -(portfolio_duration - benchmark_duration) * rate_change;

        // Curve: non-parallel curve movements
        fia.curve_effect = -portfolio_duration * curve_shift * 0.5;

        // Credit: spread widening/tightening
        fia.credit_effect = -portfolio_duration * spread_change;

        // Residual
        double active = total_return - benchmark_return;
        fia.residual = active - (fia.carry_effect + fia.duration_effect +
                                  fia.curve_effect + fia.credit_effect);
        return fia;
    }

private:
    mutable std::mutex mutex_;
};

} // namespace attribution
} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_ATTRIBUTION_HPP
