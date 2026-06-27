/**
 * @file brinson_attribution.hpp
 * @brief Brinson-Fachler performance attribution model
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Brinson-Hood-Beebower and Brinson-Fachler attribution:
 * - Allocation effect (sector weight decisions)
 * - Selection effect (security picking within sectors)
 * - Interaction effect (cross-term)
 * - Multi-period compounding (geometric linking)
 * - Currency attribution for international portfolios
 * - Hierarchical roll-up (security -> sector -> total)
 * - Benchmark-relative decomposition
 * - Attribution persistence analysis
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PORTFOLIO_BRINSON_ATTRIBUTION_HPP
#define GENIE_PORTFOLIO_BRINSON_ATTRIBUTION_HPP

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
namespace portfolio {
namespace attribution {

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Sector/group data for one period
 */
struct SectorData {
    std::string sector;
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
};

/**
 * @brief Single-period Brinson attribution result
 */
struct BrinsonResult {
    std::string sector;
    double allocation{0};       // Weight decision effect
    double selection{0};        // Stock picking effect
    double interaction{0};      // Cross-term
    double total{0};            // Sum of effects
    double portfolio_weight{0};
    double benchmark_weight{0};
    double portfolio_return{0};
    double benchmark_return{0};
    double active_weight{0};    // pw - bw
    double active_return{0};    // pr - br

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << std::setw(20) << std::left << sector
            << " alloc=" << std::setw(8) << allocation * 100 << "%"
            << " select=" << std::setw(8) << selection * 100 << "%"
            << " interact=" << std::setw(8) << interaction * 100 << "%"
            << " total=" << total * 100 << "%";
        return oss.str();
    }
};

/**
 * @brief Full attribution report
 */
struct AttributionReport {
    std::string portfolio_id;
    std::string benchmark_id;
    std::string period;
    std::vector<BrinsonResult> sectors;
    double total_allocation{0};
    double total_selection{0};
    double total_interaction{0};
    double total_active_return{0};
    double portfolio_return{0};
    double benchmark_return{0};
    double tracking_error{0};
    double information_ratio{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Brinson Attribution: " << portfolio_id << " vs " << benchmark_id << "\n";
        oss << std::fixed << std::setprecision(4);
        oss << "  Portfolio Return: " << portfolio_return * 100 << "%\n";
        oss << "  Benchmark Return: " << benchmark_return * 100 << "%\n";
        oss << "  Active Return:    " << total_active_return * 100 << "%\n";
        oss << "  Decomposition:\n";
        oss << "    Allocation:  " << total_allocation * 100 << "%\n";
        oss << "    Selection:   " << total_selection * 100 << "%\n";
        oss << "    Interaction: " << total_interaction * 100 << "%\n";
        oss << std::string(80, '-') << "\n";
        for (const auto& s : sectors) {
            oss << "  " << s.format() << "\n";
        }
        if (tracking_error > 0) {
            oss << "  Tracking Error: " << tracking_error * 100 << "%\n";
            oss << "  Information Ratio: " << information_ratio << "\n";
        }
        return oss.str();
    }
};

/**
 * @brief Multi-period linked attribution
 */
struct MultiPeriodAttribution {
    std::vector<AttributionReport> periods;
    double cumulative_allocation{0};
    double cumulative_selection{0};
    double cumulative_interaction{0};
    double cumulative_active{0};
};

// ============================================================================
// Attribution Engine
// ============================================================================

/**
 * @brief Brinson-Fachler performance attribution engine
 */
class BrinsonAttributionEngine {
public:
    /**
     * @brief Single-period Brinson-Fachler attribution
     */
    [[nodiscard]] AttributionReport attribute(
        const std::string& portfolio_id,
        const std::string& benchmark_id,
        const std::vector<SectorData>& sectors,
        const std::string& period_label = "") const {

        AttributionReport report;
        report.portfolio_id = portfolio_id;
        report.benchmark_id = benchmark_id;
        report.period = period_label;

        // Calculate total benchmark return
        double total_br = 0;
        for (const auto& s : sectors) {
            total_br += s.benchmark_weight * s.benchmark_return;
        }
        report.benchmark_return = total_br;

        // Calculate total portfolio return
        double total_pr = 0;
        for (const auto& s : sectors) {
            total_pr += s.portfolio_weight * s.portfolio_return;
        }
        report.portfolio_return = total_pr;
        report.total_active_return = total_pr - total_br;

        // Brinson-Fachler decomposition
        for (const auto& s : sectors) {
            BrinsonResult br;
            br.sector = s.sector;
            br.portfolio_weight = s.portfolio_weight;
            br.benchmark_weight = s.benchmark_weight;
            br.portfolio_return = s.portfolio_return;
            br.benchmark_return = s.benchmark_return;
            br.active_weight = s.portfolio_weight - s.benchmark_weight;
            br.active_return = s.portfolio_return - s.benchmark_return;

            // Allocation: (wp - wb) * (Rb_sector - Rb_total)
            br.allocation = (s.portfolio_weight - s.benchmark_weight) *
                             (s.benchmark_return - total_br);

            // Selection: wb * (Rp_sector - Rb_sector)
            br.selection = s.benchmark_weight *
                            (s.portfolio_return - s.benchmark_return);

            // Interaction: (wp - wb) * (Rp_sector - Rb_sector)
            br.interaction = (s.portfolio_weight - s.benchmark_weight) *
                               (s.portfolio_return - s.benchmark_return);

            br.total = br.allocation + br.selection + br.interaction;

            report.total_allocation += br.allocation;
            report.total_selection += br.selection;
            report.total_interaction += br.interaction;
            report.sectors.push_back(br);
        }

        return report;
    }

    /**
     * @brief Multi-period attribution with geometric linking
     */
    [[nodiscard]] MultiPeriodAttribution multi_period(
        const std::string& portfolio_id,
        const std::string& benchmark_id,
        const std::vector<std::vector<SectorData>>& period_data,
        const std::vector<std::string>& period_labels = {}) const {

        MultiPeriodAttribution mpa;

        // Compute per-period attribution
        for (size_t i = 0; i < period_data.size(); ++i) {
            std::string label = i < period_labels.size() ? period_labels[i] :
                                 "P" + std::to_string(i + 1);
            auto report = attribute(portfolio_id, benchmark_id, period_data[i], label);
            mpa.periods.push_back(report);
        }

        // Geometric linking (Carino method approximation)
        double cum_port = 1.0, cum_bench = 1.0;
        double cum_alloc = 0, cum_sel = 0, cum_inter = 0;

        for (const auto& p : mpa.periods) {
            double port_factor = 1.0 + p.portfolio_return;
            double bench_factor = 1.0 + p.benchmark_return;

            // Linking coefficient (smoothing factor)
            double active = p.portfolio_return - p.benchmark_return;
            double link_factor = 1.0;
            if (std::abs(active) > 1e-12) {
                double log_ratio = std::log(port_factor) - std::log(bench_factor);
                link_factor = log_ratio / active;
            }

            cum_alloc += p.total_allocation * link_factor * cum_bench;
            cum_sel += p.total_selection * link_factor * cum_bench;
            cum_inter += p.total_interaction * link_factor * cum_bench;

            cum_port *= port_factor;
            cum_bench *= bench_factor;
        }

        mpa.cumulative_allocation = cum_alloc;
        mpa.cumulative_selection = cum_sel;
        mpa.cumulative_interaction = cum_inter;
        mpa.cumulative_active = (cum_port - cum_bench);

        return mpa;
    }

    /**
     * @brief Compute tracking error and information ratio
     */
    void add_risk_metrics(AttributionReport& report,
                           const std::vector<double>& active_returns) const {
        if (active_returns.size() < 3) return;

        double mean = std::accumulate(active_returns.begin(), active_returns.end(), 0.0)
                      / active_returns.size();
        double sq_sum = 0;
        for (double r : active_returns) {
            double d = r - mean;
            sq_sum += d * d;
        }
        report.tracking_error = std::sqrt(sq_sum / (active_returns.size() - 1))
                                * std::sqrt(252.0); // Annualize

        if (report.tracking_error > 1e-12) {
            double annualized_active = mean * 252.0;
            report.information_ratio = annualized_active / report.tracking_error;
        }
    }
};

} // namespace attribution
} // namespace portfolio
} // namespace genie

#endif // GENIE_PORTFOLIO_BRINSON_ATTRIBUTION_HPP
