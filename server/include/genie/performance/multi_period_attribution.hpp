/**
 * @file multi_period_attribution.hpp
 * @brief Brinson-Fachler multi-period performance attribution for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Brinson-Fachler single-period and Carino multi-period linking.
 * Sector/country/factor decomposition of active returns.
 */
#pragma once
#ifndef GENIE_PERFORMANCE_MULTI_PERIOD_ATTRIBUTION_HPP
#define GENIE_PERFORMANCE_MULTI_PERIOD_ATTRIBUTION_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace genie::performance {

/** Single-period Brinson-Fachler attribution for one segment */
struct SegmentAttribution {
    std::string segment;              // sector/country/factor name
    double portfolio_weight{0};       // w_p
    double benchmark_weight{0};       // w_b
    double portfolio_return{0};       // r_p
    double benchmark_return{0};       // r_b
    double allocation_effect{0};      // (w_p - w_b) * (r_b - R_b)
    double selection_effect{0};       // w_b * (r_p - r_b)
    double interaction_effect{0};     // (w_p - w_b) * (r_p - r_b)
    double total_effect{0};           // allocation + selection + interaction
};

/** Single-period total attribution */
struct PeriodAttribution {
    std::string period_label;
    double portfolio_return{0};       // R_p
    double benchmark_return{0};       // R_b
    double active_return{0};          // R_p - R_b
    double total_allocation{0};
    double total_selection{0};
    double total_interaction{0};
    std::vector<SegmentAttribution> segments;
};

/** Multi-period linked attribution */
struct MultiPeriodAttribution {
    double cumulative_portfolio_return{0};
    double cumulative_benchmark_return{0};
    double cumulative_active_return{0};
    double linked_allocation{0};
    double linked_selection{0};
    double linked_interaction{0};
    std::vector<PeriodAttribution> periods;
    std::map<std::string, SegmentAttribution> cumulative_segments;
};

/** Segment data for a single period */
struct SegmentData {
    std::string name;
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
};

/** Brinson-Fachler attribution engine */
class BrinsonFachler {
public:
    /** Single-period Brinson-Fachler decomposition */
    [[nodiscard]] static PeriodAttribution single_period(
            const std::string& label,
            const std::vector<SegmentData>& segments) {

        PeriodAttribution pa;
        pa.period_label = label;

        // Calculate total returns
        for (const auto& s : segments) {
            pa.portfolio_return += s.portfolio_weight * s.portfolio_return;
            pa.benchmark_return += s.benchmark_weight * s.benchmark_return;
        }
        pa.active_return = pa.portfolio_return - pa.benchmark_return;

        // Brinson-Fachler decomposition per segment
        for (const auto& s : segments) {
            SegmentAttribution sa;
            sa.segment = s.name;
            sa.portfolio_weight = s.portfolio_weight;
            sa.benchmark_weight = s.benchmark_weight;
            sa.portfolio_return = s.portfolio_return;
            sa.benchmark_return = s.benchmark_return;

            double dw = s.portfolio_weight - s.benchmark_weight;
            double dr = s.portfolio_return - s.benchmark_return;

            // Brinson-Fachler allocation uses (r_b_i - R_b) not just r_b_i
            sa.allocation_effect = dw * (s.benchmark_return - pa.benchmark_return);
            sa.selection_effect = s.benchmark_weight * dr;
            sa.interaction_effect = dw * dr;
            sa.total_effect = sa.allocation_effect + sa.selection_effect + sa.interaction_effect;

            pa.total_allocation += sa.allocation_effect;
            pa.total_selection += sa.selection_effect;
            pa.total_interaction += sa.interaction_effect;
            pa.segments.push_back(sa);
        }
        return pa;
    }

    /** Multi-period attribution with Carino linking */
    [[nodiscard]] static MultiPeriodAttribution multi_period(
            const std::vector<PeriodAttribution>& periods) {

        MultiPeriodAttribution mpa;
        mpa.periods = periods;

        if (periods.empty()) return mpa;

        // Compound returns
        double cum_port = 1.0, cum_bench = 1.0;
        for (const auto& p : periods) {
            cum_port *= (1.0 + p.portfolio_return);
            cum_bench *= (1.0 + p.benchmark_return);
        }
        mpa.cumulative_portfolio_return = cum_port - 1.0;
        mpa.cumulative_benchmark_return = cum_bench - 1.0;
        mpa.cumulative_active_return = mpa.cumulative_portfolio_return - mpa.cumulative_benchmark_return;

        // Carino linking factors
        double R_p = mpa.cumulative_portfolio_return;
        double R_b = mpa.cumulative_benchmark_return;

        // Carino smoothing factor for the total period
        double k_total = 1.0;
        if (std::abs(R_p - R_b) > 1e-12) {
            double ln_ratio = std::log((1.0 + R_p) / (1.0 + R_b));
            k_total = (R_p - R_b) / ln_ratio;
        }

        // Per-period Carino factors and linked effects
        double linked_alloc = 0, linked_sel = 0, linked_inter = 0;

        for (const auto& p : periods) {
            double k_t = 1.0;
            double r_p = p.portfolio_return;
            double r_b = p.benchmark_return;

            if (std::abs(r_p - r_b) > 1e-12) {
                k_t = (std::log(1.0 + r_p) - std::log(1.0 + r_b)) / (r_p - r_b);
            } else if (std::abs(r_p) > 1e-12) {
                k_t = std::log(1.0 + r_p) / r_p;
            }

            double scale = (std::abs(k_total) > 1e-12) ? k_t / k_total : 1.0;

            linked_alloc += scale * p.total_allocation;
            linked_sel += scale * p.total_selection;
            linked_inter += scale * p.total_interaction;

            // Accumulate per-segment
            for (const auto& sa : p.segments) {
                auto& cum = mpa.cumulative_segments[sa.segment];
                cum.segment = sa.segment;
                cum.allocation_effect += scale * sa.allocation_effect;
                cum.selection_effect += scale * sa.selection_effect;
                cum.interaction_effect += scale * sa.interaction_effect;
                cum.total_effect += scale * sa.total_effect;
            }
        }

        mpa.linked_allocation = linked_alloc;
        mpa.linked_selection = linked_sel;
        mpa.linked_interaction = linked_inter;

        return mpa;
    }
};

} // namespace genie::performance

#endif // GENIE_PERFORMANCE_MULTI_PERIOD_ATTRIBUTION_HPP
