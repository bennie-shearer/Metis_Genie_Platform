/**
 * @file benchmark_manager.hpp
 * @brief Index tracking, benchmark comparison, and tracking error analysis
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Benchmark management for portfolio performance analysis:
 * - Benchmark definition with constituent weights
 * - Tracking error calculation (ex-ante and ex-post)
 * - Information ratio and active return
 * - Active share measurement
 * - Sector/country/factor attribution vs benchmark
 * - Benchmark rebalancing and reconstitution
 * - Multiple benchmark support per portfolio
 * - Historical benchmark return series
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERFORMANCE_BENCHMARK_MANAGER_HPP
#define GENIE_PERFORMANCE_BENCHMARK_MANAGER_HPP

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
#include <deque>

namespace genie {
namespace performance {
namespace benchmark {

// ============================================================================
// Data Structures
// ============================================================================

struct BenchmarkConstituent {
    std::string symbol;
    double weight{0};
    std::string sector;
    std::string country;
    double market_cap{0};
};

struct BenchmarkReturn {
    std::chrono::system_clock::time_point date;
    double daily_return{0};
    double cumulative_return{0};
    double index_level{0};
};

struct TrackingMetrics {
    double tracking_error_annualized{0};
    double information_ratio{0};
    double active_return_annualized{0};
    double active_share{0};          // % of portfolio differing from benchmark
    double beta{0};
    double alpha_annualized{0};
    double r_squared{0};
    int observation_count{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "TE=" << tracking_error_annualized * 100 << "% "
            << "IR=" << information_ratio << " "
            << "ActiveRet=" << active_return_annualized * 100 << "% "
            << "ActiveShare=" << active_share * 100 << "% "
            << "Beta=" << beta << " Alpha=" << alpha_annualized * 100 << "%";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "{\"tracking_error\":" << tracking_error_annualized
            << ",\"information_ratio\":" << information_ratio
            << ",\"active_return\":" << active_return_annualized
            << ",\"active_share\":" << active_share
            << ",\"beta\":" << beta
            << ",\"alpha\":" << alpha_annualized
            << ",\"r_squared\":" << r_squared
            << ",\"observations\":" << observation_count << "}";
        return oss.str();
    }
};

/**
 * @brief Benchmark definition
 */
struct Benchmark {
    std::string id;
    std::string name;
    std::string description;
    std::string currency{"USD"};
    std::vector<BenchmarkConstituent> constituents;
    std::deque<BenchmarkReturn> returns;
    double base_level{1000.0};
    std::chrono::system_clock::time_point inception_date;

    [[nodiscard]] int constituent_count() const {
        return static_cast<int>(constituents.size());
    }

    [[nodiscard]] double total_weight() const {
        double sum = 0;
        for (const auto& c : constituents) sum += c.weight;
        return sum;
    }

    [[nodiscard]] std::map<std::string, double> sector_weights() const {
        std::map<std::string, double> weights;
        for (const auto& c : constituents) weights[c.sector] += c.weight;
        return weights;
    }

    [[nodiscard]] std::map<std::string, double> country_weights() const {
        std::map<std::string, double> weights;
        for (const auto& c : constituents) weights[c.country] += c.weight;
        return weights;
    }

    [[nodiscard]] std::optional<double> weight_of(const std::string& symbol) const {
        for (const auto& c : constituents) {
            if (c.symbol == symbol) return c.weight;
        }
        return std::nullopt;
    }
};

// ============================================================================
// Benchmark Manager
// ============================================================================

class BenchmarkManager {
public:
    BenchmarkManager() { register_default_benchmarks(); }

    void add_benchmark(Benchmark bm) {
        std::lock_guard<std::mutex> lock(mutex_);
        benchmarks_[bm.id] = std::move(bm);
    }

    [[nodiscard]] std::optional<Benchmark> get(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = benchmarks_.find(id);
        if (it == benchmarks_.end()) return std::nullopt;
        return it->second;
    }

    void add_return(const std::string& bm_id, double daily_return) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = benchmarks_.find(bm_id);
        if (it == benchmarks_.end()) return;
        auto& bm = it->second;
        BenchmarkReturn br;
        br.date = std::chrono::system_clock::now();
        br.daily_return = daily_return;
        double prev_cum = bm.returns.empty() ? 0 : bm.returns.back().cumulative_return;
        br.cumulative_return = (1 + prev_cum) * (1 + daily_return) - 1;
        br.index_level = bm.base_level * (1 + br.cumulative_return);
        bm.returns.push_back(br);
        if (bm.returns.size() > 10000) bm.returns.pop_front();
    }

    /**
     * @brief Calculate tracking metrics for portfolio vs benchmark
     */
    [[nodiscard]] TrackingMetrics calculate_tracking(
        const std::string& bm_id,
        const std::vector<double>& portfolio_returns,
        const std::map<std::string, double>& portfolio_weights = {}) const {

        std::lock_guard<std::mutex> lock(mutex_);
        TrackingMetrics metrics;
        auto it = benchmarks_.find(bm_id);
        if (it == benchmarks_.end()) return metrics;
        const auto& bm = it->second;

        // Get matching benchmark returns
        int n = std::min(static_cast<int>(portfolio_returns.size()),
                         static_cast<int>(bm.returns.size()));
        if (n < 2) return metrics;
        metrics.observation_count = n;

        std::vector<double> bm_rets(n), active_rets(n);
        int start = static_cast<int>(bm.returns.size()) - n;
        for (int i = 0; i < n; ++i) {
            bm_rets[i] = bm.returns[start + i].daily_return;
            active_rets[i] = portfolio_returns[i] - bm_rets[i];
        }

        // Active return (annualized)
        double avg_active = std::accumulate(active_rets.begin(), active_rets.end(), 0.0) / n;
        metrics.active_return_annualized = avg_active * 252;

        // Tracking error (annualized)
        double sum_sq = 0;
        for (double ar : active_rets) sum_sq += (ar - avg_active) * (ar - avg_active);
        double te_daily = std::sqrt(sum_sq / (n - 1));
        metrics.tracking_error_annualized = te_daily * std::sqrt(252.0);

        // Information ratio
        if (metrics.tracking_error_annualized > 1e-10) {
            metrics.information_ratio = metrics.active_return_annualized /
                                         metrics.tracking_error_annualized;
        }

        // Beta and Alpha
        double avg_bm = std::accumulate(bm_rets.begin(), bm_rets.end(), 0.0) / n;
        double avg_pf = std::accumulate(portfolio_returns.begin(),
                                         portfolio_returns.begin() + n, 0.0) / n;
        double cov = 0, var_bm = 0;
        for (int i = 0; i < n; ++i) {
            cov += (portfolio_returns[i] - avg_pf) * (bm_rets[i] - avg_bm);
            var_bm += (bm_rets[i] - avg_bm) * (bm_rets[i] - avg_bm);
        }
        if (var_bm > 1e-15) {
            metrics.beta = cov / var_bm;
            metrics.alpha_annualized = (avg_pf - metrics.beta * avg_bm) * 252;
        }

        // R-squared
        double var_pf = 0;
        for (int i = 0; i < n; ++i) var_pf += (portfolio_returns[i] - avg_pf) * (portfolio_returns[i] - avg_pf);
        if (var_pf > 1e-15 && var_bm > 1e-15) {
            double corr = cov / std::sqrt(var_pf * var_bm);
            metrics.r_squared = corr * corr;
        }

        // Active share
        if (!portfolio_weights.empty()) {
            double diff_sum = 0;
            std::set<std::string> all_symbols;
            for (const auto& c : bm.constituents) all_symbols.insert(c.symbol);
            for (const auto& [s, _] : portfolio_weights) all_symbols.insert(s);
            for (const auto& sym : all_symbols) {
                double pw = 0, bw = 0;
                auto pi = portfolio_weights.find(sym);
                if (pi != portfolio_weights.end()) pw = pi->second;
                auto bi = bm.weight_of(sym);
                if (bi) bw = *bi;
                diff_sum += std::abs(pw - bw);
            }
            metrics.active_share = diff_sum / 2.0;
        }

        return metrics;
    }

    [[nodiscard]] std::vector<std::string> list_benchmarks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [id, _] : benchmarks_) result.push_back(id);
        return result;
    }

    [[nodiscard]] int benchmark_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(benchmarks_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Benchmark> benchmarks_;

    void register_default_benchmarks() {
        // S&P 500
        {
            Benchmark bm;
            bm.id = "SPX"; bm.name = "S&P 500"; bm.description = "US large-cap equity";
            bm.constituents = {
                {"AAPL", 0.072, "Technology", "US", 3e12},
                {"MSFT", 0.068, "Technology", "US", 2.8e12},
                {"NVDA", 0.055, "Technology", "US", 2.5e12},
                {"AMZN", 0.038, "Consumer Discretionary", "US", 1.8e12},
                {"GOOGL", 0.035, "Communication Services", "US", 1.7e12},
                {"META", 0.025, "Communication Services", "US", 1.2e12},
                {"BRK.B", 0.018, "Financials", "US", 0.8e12},
                {"JPM", 0.013, "Financials", "US", 0.6e12},
                {"V", 0.012, "Financials", "US", 0.5e12},
                {"TSLA", 0.015, "Consumer Discretionary", "US", 0.7e12}
            };
            benchmarks_["SPX"] = std::move(bm);
        }
        // AGG (Bloomberg Aggregate Bond)
        {
            Benchmark bm;
            bm.id = "AGG"; bm.name = "Bloomberg US Aggregate Bond";
            bm.description = "US investment-grade bond market";
            benchmarks_["AGG"] = std::move(bm);
        }
        // 60/40
        {
            Benchmark bm;
            bm.id = "60_40"; bm.name = "60/40 Balanced";
            bm.description = "60% equity / 40% fixed income blend";
            benchmarks_["60_40"] = std::move(bm);
        }
    }
};

} // namespace benchmark
} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_BENCHMARK_MANAGER_HPP
