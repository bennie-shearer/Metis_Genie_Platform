/**
 * @file tail_risk.hpp
 * @brief Tail risk analysis: CVaR, Expected Shortfall, EVT
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Advanced tail risk measurement:
 * - Conditional VaR (CVaR / Expected Shortfall)
 * - Extreme Value Theory (Generalized Pareto Distribution)
 * - Cornish-Fisher expansion for non-normal distributions
 * - Tail index estimation (Hill estimator)
 * - Maximum drawdown distribution
 * - Tail dependence (co-crash probability)
 * - Historical tail loss decomposition
 * - Tail risk budgeting across positions
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_RISK_TAIL_RISK_HPP
#define GENIE_RISK_TAIL_RISK_HPP

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
namespace risk {
namespace tail {

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Tail risk metrics for a portfolio or position
 */
struct TailRiskMetrics {
    std::string entity_id;         // Portfolio or position ID
    double var_95{0};              // VaR at 95%
    double var_99{0};              // VaR at 99%
    double cvar_95{0};             // Expected Shortfall at 95%
    double cvar_99{0};             // Expected Shortfall at 99%
    double max_drawdown{0};
    double skewness{0};
    double kurtosis{0};
    double tail_index{0};          // Hill estimator
    double cornish_fisher_var{0};  // Adjusted VaR
    double peak_to_trough{0};     // Worst historical drawdown
    int observations{0};
    double tail_ratio{0};          // Gain-to-pain ratio in tails

    [[nodiscard]] bool fat_tails() const { return kurtosis > 3.5; }
    [[nodiscard]] bool left_skewed() const { return skewness < -0.5; }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "Tail Risk [" << entity_id << "] (" << observations << " obs)\n";
        oss << "  VaR(95/99):  " << var_95 * 100 << "% / " << var_99 * 100 << "%\n";
        oss << "  CVaR(95/99): " << cvar_95 * 100 << "% / " << cvar_99 * 100 << "%\n";
        oss << "  Skew/Kurt:   " << skewness << " / " << kurtosis;
        if (fat_tails()) oss << " [FAT TAILS]";
        if (left_skewed()) oss << " [LEFT SKEW]";
        oss << "\n  Max DD:      " << max_drawdown * 100 << "%";
        oss << "\n  CF-VaR(99):  " << cornish_fisher_var * 100 << "%";
        oss << "\n  Tail Index:  " << tail_index;
        return oss.str();
    }
};

/**
 * @brief GPD (Generalized Pareto Distribution) fit
 */
struct GPDFit {
    double shape{0};       // xi (shape parameter)
    double scale{0};       // sigma (scale)
    double threshold{0};   // u (threshold)
    int exceedances{0};    // Number of observations above threshold
    double mean_excess{0}; // Mean excess above threshold

    [[nodiscard]] bool heavy_tail() const { return shape > 0; }

    /**
     * @brief Estimate tail probability P(X > x | X > u)
     */
    [[nodiscard]] double tail_prob(double x) const {
        if (scale <= 0 || x <= threshold) return 1.0;
        double z = (x - threshold) / scale;
        if (std::abs(shape) < 1e-10) return std::exp(-z);
        double inner = 1.0 + shape * z;
        if (inner <= 0) return 0.0;
        return std::pow(inner, -1.0 / shape);
    }
};

/**
 * @brief Tail dependence between two assets
 */
struct TailDependence {
    std::string asset_a;
    std::string asset_b;
    double lower_tail{0};  // Co-crash probability
    double upper_tail{0};  // Co-boom probability
    int joint_exceedances{0};

    [[nodiscard]] bool high_co_crash_risk() const { return lower_tail > 0.3; }
};

/**
 * @brief Tail contribution by position
 */
struct TailContribution {
    std::string symbol;
    double weight{0};
    double marginal_cvar{0};       // Contribution to portfolio CVaR
    double component_cvar{0};      // Weight * marginal CVaR
    double pct_of_total_cvar{0};

    [[nodiscard]] bool is_tail_heavy() const {
        return pct_of_total_cvar > weight * 1.5;
    }
};

// ============================================================================
// Tail Risk Engine
// ============================================================================

/**
 * @brief Comprehensive tail risk analysis engine
 */
class TailRiskEngine {
public:
    /**
     * @brief Compute full tail risk metrics from return series
     */
    [[nodiscard]] TailRiskMetrics analyze(const std::string& id,
                                            const std::vector<double>& returns) const {
        TailRiskMetrics m;
        m.entity_id = id;
        m.observations = static_cast<int>(returns.size());
        if (returns.size() < 10) return m;

        auto sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        int n = static_cast<int>(sorted.size());

        // VaR (historical percentile)
        m.var_95 = -sorted[static_cast<int>(n * 0.05)];
        m.var_99 = -sorted[static_cast<int>(n * 0.01)];

        // CVaR (Expected Shortfall) - average of losses beyond VaR
        m.cvar_95 = compute_cvar(sorted, 0.05);
        m.cvar_99 = compute_cvar(sorted, 0.01);

        // Moments
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / n;
        double var = 0, skew_sum = 0, kurt_sum = 0;
        for (double r : returns) {
            double d = r - mean;
            var += d * d;
            skew_sum += d * d * d;
            kurt_sum += d * d * d * d;
        }
        double stddev = std::sqrt(var / n);
        if (stddev > 1e-12) {
            m.skewness = (skew_sum / n) / (stddev * stddev * stddev);
            m.kurtosis = (kurt_sum / n) / (stddev * stddev * stddev * stddev);
        }

        // Cornish-Fisher VaR (adjusted for skew/kurtosis)
        m.cornish_fisher_var = cornish_fisher(0.01, mean, stddev, m.skewness, m.kurtosis);

        // Max drawdown
        m.max_drawdown = compute_max_drawdown(returns);
        m.peak_to_trough = m.max_drawdown;

        // Hill tail index
        m.tail_index = hill_estimator(sorted, std::max(10, n / 10));

        // Tail ratio
        double upper_sum = 0, lower_sum = 0;
        int upper_count = 0, lower_count = 0;
        double q95_up = sorted[static_cast<int>(n * 0.95)];
        double q05_dn = sorted[static_cast<int>(n * 0.05)];
        for (double r : returns) {
            if (r >= q95_up) { upper_sum += r; ++upper_count; }
            if (r <= q05_dn) { lower_sum += std::abs(r); ++lower_count; }
        }
        double avg_gain = upper_count > 0 ? upper_sum / upper_count : 0;
        double avg_loss = lower_count > 0 ? lower_sum / lower_count : 1;
        m.tail_ratio = avg_loss > 0 ? avg_gain / avg_loss : 0;

        return m;
    }

    /**
     * @brief Fit Generalized Pareto Distribution to tail
     */
    [[nodiscard]] GPDFit fit_gpd(const std::vector<double>& returns,
                                   double threshold_percentile = 0.10) const {
        auto sorted = returns;
        std::sort(sorted.begin(), sorted.end());
        int n = static_cast<int>(sorted.size());
        int threshold_idx = static_cast<int>(n * threshold_percentile);
        if (threshold_idx < 5) return {};

        GPDFit fit;
        fit.threshold = -sorted[threshold_idx]; // Losses are positive
        fit.exceedances = threshold_idx;

        // Compute exceedances (losses beyond threshold)
        std::vector<double> exceedances;
        for (int i = 0; i < threshold_idx; ++i) {
            exceedances.push_back(-sorted[i] - fit.threshold);
        }

        // Method of moments estimation
        double sum = std::accumulate(exceedances.begin(), exceedances.end(), 0.0);
        fit.mean_excess = sum / exceedances.size();

        double sq_sum = 0;
        for (double e : exceedances) sq_sum += e * e;
        double variance = sq_sum / exceedances.size() - fit.mean_excess * fit.mean_excess;

        if (fit.mean_excess > 0) {
            fit.shape = 0.5 * (fit.mean_excess * fit.mean_excess / variance - 1.0);
            fit.scale = fit.mean_excess * (1.0 + fit.shape) / 2.0;
        }

        return fit;
    }

    /**
     * @brief Compute tail dependence between two return series
     */
    [[nodiscard]] TailDependence tail_dependence(
        const std::string& id_a, const std::vector<double>& returns_a,
        const std::string& id_b, const std::vector<double>& returns_b,
        double quantile = 0.05) const {

        TailDependence td;
        td.asset_a = id_a;
        td.asset_b = id_b;
        int n = static_cast<int>(std::min(returns_a.size(), returns_b.size()));
        if (n < 20) return td;

        auto sa = returns_a; std::sort(sa.begin(), sa.end());
        auto sb = returns_b; std::sort(sb.begin(), sb.end());
        double thresh_a = sa[static_cast<int>(n * quantile)];
        double thresh_b = sb[static_cast<int>(n * quantile)];
        double up_a = sa[static_cast<int>(n * (1.0 - quantile))];
        double up_b = sb[static_cast<int>(n * (1.0 - quantile))];

        int joint_lower = 0, joint_upper = 0;
        int below_a = 0;
        for (int i = 0; i < n; ++i) {
            if (returns_a[i] <= thresh_a) {
                ++below_a;
                if (returns_b[i] <= thresh_b) ++joint_lower;
            }
            if (returns_a[i] >= up_a && returns_b[i] >= up_b) ++joint_upper;
        }

        td.lower_tail = below_a > 0 ? static_cast<double>(joint_lower) / below_a : 0;
        td.upper_tail = below_a > 0 ? static_cast<double>(joint_upper) / below_a : 0;
        td.joint_exceedances = joint_lower;
        return td;
    }

    /**
     * @brief Decompose portfolio CVaR into position contributions
     */
    [[nodiscard]] std::vector<TailContribution> cvar_decomposition(
        const std::map<std::string, double>& weights,
        const std::map<std::string, std::vector<double>>& position_returns,
        const std::vector<double>& portfolio_returns,
        double alpha = 0.05) const {

        std::vector<TailContribution> result;
        auto sorted_port = portfolio_returns;
        std::sort(sorted_port.begin(), sorted_port.end());
        int n = static_cast<int>(sorted_port.size());
        int cutoff = std::max(1, static_cast<int>(n * alpha));
        double port_cvar = compute_cvar(sorted_port, alpha);

        for (const auto& [sym, weight] : weights) {
            TailContribution tc;
            tc.symbol = sym;
            tc.weight = weight;

            auto pr_it = position_returns.find(sym);
            if (pr_it == position_returns.end() || pr_it->second.size() != portfolio_returns.size()) {
                result.push_back(tc);
                continue;
            }

            // Marginal CVaR: average position return on portfolio's worst days
            double sum = 0;
            std::vector<std::pair<double, int>> indexed;
            for (int i = 0; i < n; ++i) {
                indexed.emplace_back(portfolio_returns[i], i);
            }
            std::sort(indexed.begin(), indexed.end());
            for (int i = 0; i < cutoff; ++i) {
                int idx = indexed[i].second;
                sum += pr_it->second[idx];
            }
            tc.marginal_cvar = -(sum / cutoff);
            tc.component_cvar = weight * tc.marginal_cvar;
            tc.pct_of_total_cvar = port_cvar > 0 ? tc.component_cvar / port_cvar : 0;
            result.push_back(tc);
        }
        return result;
    }

private:
    [[nodiscard]] static double compute_cvar(const std::vector<double>& sorted_returns,
                                               double alpha) {
        int n = static_cast<int>(sorted_returns.size());
        int cutoff = std::max(1, static_cast<int>(n * alpha));
        double sum = 0;
        for (int i = 0; i < cutoff; ++i) sum += sorted_returns[i];
        return -(sum / cutoff);
    }

    [[nodiscard]] static double compute_max_drawdown(const std::vector<double>& returns) {
        double peak = 1.0, max_dd = 0;
        double cumulative = 1.0;
        for (double r : returns) {
            cumulative *= (1.0 + r);
            peak = std::max(peak, cumulative);
            double dd = (peak - cumulative) / peak;
            max_dd = std::max(max_dd, dd);
        }
        return max_dd;
    }

    [[nodiscard]] static double hill_estimator(const std::vector<double>& sorted, int k) {
        int n = static_cast<int>(sorted.size());
        if (k < 2 || k >= n) return 0;
        // Use absolute values of losses (left tail)
        double log_sum = 0;
        double log_k = std::log(std::abs(sorted[k - 1]) + 1e-15);
        for (int i = 0; i < k - 1; ++i) {
            double val = std::abs(sorted[i]) + 1e-15;
            log_sum += std::log(val) - log_k;
        }
        return (k - 1) > 0 ? log_sum / (k - 1) : 0;
    }

    [[nodiscard]] static double cornish_fisher(double alpha, double mean,
                                                 double sigma, double skew, double kurt) {
        // Z-score for alpha
        double z = -2.326; // ~99% (approx)
        if (alpha >= 0.05) z = -1.645;

        // Cornish-Fisher expansion
        double cf = z + (z * z - 1) * skew / 6.0
                      + (z * z * z - 3 * z) * (kurt - 3) / 24.0
                      - (2 * z * z * z - 5 * z) * skew * skew / 36.0;

        return -(mean + cf * sigma);
    }
};

} // namespace tail
} // namespace risk
} // namespace genie

#endif // GENIE_RISK_TAIL_RISK_HPP
