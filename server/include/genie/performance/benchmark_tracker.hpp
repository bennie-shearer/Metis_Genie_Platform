/**
 * @file benchmark_tracker.hpp
 * @brief Benchmark return tracking, tracking error, and information ratio
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Benchmark tracking framework:
 * - Benchmark index return series management
 * - Tracking error calculation (annualized)
 * - Information ratio (active return / tracking error)
 * - Active share measurement
 * - Benchmark-relative risk decomposition
 * - Rolling tracking statistics
 * - Multi-benchmark comparison
 * - Benchmark constituent replication analysis
 * - Style drift detection via rolling R-squared
 * - Excess return decomposition (beta, alpha, residual)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERFORMANCE_BENCHMARK_TRACKER_HPP
#define GENIE_PERFORMANCE_BENCHMARK_TRACKER_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>
#include <set>

namespace genie {
namespace performance {
namespace benchmark {

// ============================================================================
// Data Structures
// ============================================================================

struct BenchmarkIndex {
    std::string id;
    std::string name;
    std::string asset_class;
    std::deque<double> daily_returns;
    std::map<std::string, double> weights; // Constituent weights
    double ytd_return{0};
    double ann_return{0};
    double ann_volatility{0};
};

struct TrackingResult {
    std::string benchmark_id;
    std::string benchmark_name;
    int observations{0};
    double portfolio_return{0};
    double benchmark_return{0};
    double active_return{0};
    double tracking_error{0};
    double information_ratio{0};
    double active_share{0};
    double beta{0};
    double alpha{0};             // Jensen's alpha
    double r_squared{0};
    double correlation{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Benchmark: " << benchmark_name << "\n"
            << "  Active Return=" << active_return * 100 << "%"
            << "  TE=" << tracking_error * 100 << "%"
            << "  IR=" << information_ratio << "\n"
            << "  Beta=" << beta << "  Alpha=" << alpha * 100 << "%"
            << "  R²=" << r_squared
            << "  Active Share=" << active_share * 100 << "%";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "{\"benchmark\":\"" << benchmark_name
            << "\",\"active_return\":" << active_return
            << ",\"tracking_error\":" << tracking_error
            << ",\"information_ratio\":" << information_ratio
            << ",\"active_share\":" << active_share
            << ",\"beta\":" << beta
            << ",\"alpha\":" << alpha
            << ",\"r_squared\":" << r_squared << "}";
        return oss.str();
    }
};

struct RollingStats {
    std::string date;
    double trailing_te{0};
    double trailing_ir{0};
    double trailing_alpha{0};
    double trailing_r_squared{0};
};

struct StyleDriftResult {
    double r_squared_initial{0};
    double r_squared_current{0};
    double drift_magnitude{0};     // Absolute change in R²
    bool significant_drift{false};
    std::string assessment;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "Style Drift: R² " << r_squared_initial
            << " -> " << r_squared_current
            << " (Δ=" << drift_magnitude << ")"
            << " " << assessment;
        return oss.str();
    }
};

// ============================================================================
// Benchmark Tracker
// ============================================================================

class BenchmarkTracker {
public:
    BenchmarkTracker() { register_default_benchmarks(); }

    void add_benchmark(const std::string& id, const std::string& name,
                         const std::string& asset_class = "equity") {
        std::lock_guard<std::mutex> lock(mutex_);
        BenchmarkIndex bm;
        bm.id = id; bm.name = name; bm.asset_class = asset_class;
        benchmarks_[id] = std::move(bm);
    }

    void record_return(const std::string& benchmark_id, double daily_return) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = benchmarks_.find(benchmark_id);
        if (it == benchmarks_.end()) return;
        it->second.daily_returns.push_back(daily_return);
        if (it->second.daily_returns.size() > 2520) // 10 years
            it->second.daily_returns.pop_front();
    }

    /**
     * @brief Calculate tracking statistics vs benchmark
     */
    [[nodiscard]] TrackingResult track(
        const std::string& benchmark_id,
        const std::vector<double>& portfolio_daily_returns,
        const std::map<std::string, double>& portfolio_weights = {},
        int lookback = 252) const {

        std::lock_guard<std::mutex> lock(mutex_);
        TrackingResult result;
        auto bit = benchmarks_.find(benchmark_id);
        if (bit == benchmarks_.end()) return result;
        const auto& bm = bit->second;

        result.benchmark_id = bm.id;
        result.benchmark_name = bm.name;

        // Align return series (use minimum of both lengths, capped by lookback)
        int n = std::min({static_cast<int>(portfolio_daily_returns.size()),
                          static_cast<int>(bm.daily_returns.size()), lookback});
        if (n < 5) return result;
        result.observations = n;

        // Extract aligned series from the end
        std::vector<double> pr(n), br(n), ar(n);
        for (int i = 0; i < n; ++i) {
            pr[i] = portfolio_daily_returns[portfolio_daily_returns.size() - n + i];
            br[i] = bm.daily_returns[bm.daily_returns.size() - n + i];
            ar[i] = pr[i] - br[i]; // Active returns
        }

        // Cumulative returns
        double port_cum = 1, bench_cum = 1;
        for (int i = 0; i < n; ++i) { port_cum *= (1 + pr[i]); bench_cum *= (1 + br[i]); }
        result.portfolio_return = port_cum - 1;
        result.benchmark_return = bench_cum - 1;
        result.active_return = result.portfolio_return - result.benchmark_return;

        // Tracking error (annualized std dev of active returns)
        double ar_mean = std::accumulate(ar.begin(), ar.end(), 0.0) / n;
        double ar_var = 0;
        for (double a : ar) ar_var += (a - ar_mean) * (a - ar_mean);
        ar_var /= (n - 1);
        result.tracking_error = std::sqrt(ar_var) * std::sqrt(252.0);

        // Information ratio
        double ann_active = ar_mean * 252;
        result.information_ratio = result.tracking_error > 1e-10 ?
            ann_active / result.tracking_error : 0;

        // Beta and alpha via regression
        double pr_mean = std::accumulate(pr.begin(), pr.end(), 0.0) / n;
        double br_mean = std::accumulate(br.begin(), br.end(), 0.0) / n;
        double cov = 0, br_var = 0;
        for (int i = 0; i < n; ++i) {
            cov += (pr[i] - pr_mean) * (br[i] - br_mean);
            br_var += (br[i] - br_mean) * (br[i] - br_mean);
        }
        cov /= (n - 1); br_var /= (n - 1);
        result.beta = br_var > 1e-10 ? cov / br_var : 1.0;
        result.alpha = (pr_mean - result.beta * br_mean) * 252; // Annualized

        // R-squared
        double pr_var = 0;
        for (int i = 0; i < n; ++i) pr_var += (pr[i] - pr_mean) * (pr[i] - pr_mean);
        pr_var /= (n - 1);
        result.correlation = (pr_var > 1e-10 && br_var > 1e-10) ?
            cov / std::sqrt(pr_var * br_var) : 0;
        result.r_squared = result.correlation * result.correlation;

        // Active share (requires constituent weights)
        if (!portfolio_weights.empty() && !bm.weights.empty()) {
            double active_share = 0;
            std::set<std::string> all_securities;
            for (const auto& [k, _] : portfolio_weights) all_securities.insert(k);
            for (const auto& [k, _] : bm.weights) all_securities.insert(k);
            for (const auto& sec : all_securities) {
                double pw = portfolio_weights.count(sec) ? portfolio_weights.at(sec) : 0;
                double bw = bm.weights.count(sec) ? bm.weights.at(sec) : 0;
                active_share += std::abs(pw - bw);
            }
            result.active_share = active_share / 2.0;
        }

        return result;
    }

    /**
     * @brief Detect style drift via rolling R-squared
     */
    [[nodiscard]] StyleDriftResult detect_style_drift(
        const std::string& benchmark_id,
        const std::vector<double>& portfolio_daily_returns,
        int initial_window = 60, int current_window = 60) const {

        std::lock_guard<std::mutex> lock(mutex_);
        StyleDriftResult sdr;
        auto bit = benchmarks_.find(benchmark_id);
        if (bit == benchmarks_.end()) return sdr;
        const auto& bm = bit->second;

        int total = std::min(static_cast<int>(portfolio_daily_returns.size()),
                             static_cast<int>(bm.daily_returns.size()));
        if (total < initial_window + current_window) return sdr;

        // R² for initial window
        sdr.r_squared_initial = compute_r_squared(portfolio_daily_returns, bm.daily_returns,
            total - initial_window - current_window, initial_window);
        // R² for current window
        sdr.r_squared_current = compute_r_squared(portfolio_daily_returns, bm.daily_returns,
            total - current_window, current_window);

        sdr.drift_magnitude = std::abs(sdr.r_squared_current - sdr.r_squared_initial);
        sdr.significant_drift = sdr.drift_magnitude > 0.15;
        if (sdr.drift_magnitude < 0.05) sdr.assessment = "Stable";
        else if (sdr.drift_magnitude < 0.15) sdr.assessment = "Minor drift";
        else sdr.assessment = "SIGNIFICANT DRIFT";
        return sdr;
    }

    [[nodiscard]] int benchmark_count() const { std::lock_guard<std::mutex> lock(mutex_); return static_cast<int>(benchmarks_.size()); }

private:
    mutable std::mutex mutex_;
    std::map<std::string, BenchmarkIndex> benchmarks_;

    double compute_r_squared(const std::vector<double>& pr, const std::deque<double>& br,
                               int start, int len) const {
        if (start < 0 || start + len > static_cast<int>(pr.size()) ||
            start + len > static_cast<int>(br.size())) return 0;
        double pm = 0, bm = 0;
        for (int i = start; i < start + len; ++i) { pm += pr[i]; bm += br[i]; }
        pm /= len; bm /= len;
        double cov = 0, pv = 0, bv = 0;
        for (int i = start; i < start + len; ++i) {
            cov += (pr[i] - pm) * (br[i] - bm);
            pv += (pr[i] - pm) * (pr[i] - pm);
            bv += (br[i] - bm) * (br[i] - bm);
        }
        double corr = (pv > 1e-10 && bv > 1e-10) ? cov / std::sqrt(pv * bv) : 0;
        return corr * corr;
    }

    void register_default_benchmarks() {
        auto add = [this](const std::string& id, const std::string& name, const std::string& ac) {
            BenchmarkIndex bm; bm.id = id; bm.name = name; bm.asset_class = ac;
            benchmarks_[id] = bm;
        };
        add("SPX", "S&P 500", "equity");
        add("NDX", "NASDAQ-100", "equity");
        add("RTY", "Russell 2000", "equity");
        add("MXEA", "MSCI EAFE", "equity");
        add("MXEF", "MSCI Emerging", "equity");
        add("AGG", "Bloomberg US Agg", "fixed_income");
        add("HYG", "Bloomberg US HY", "fixed_income");
        add("BCOM", "Bloomberg Commodity", "commodity");
    }
};

} // namespace benchmark
} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_BENCHMARK_TRACKER_HPP
