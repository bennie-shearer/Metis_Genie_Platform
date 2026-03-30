/**
 * @file correlated_mc.hpp
 * @brief Correlated Monte Carlo simulation for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Cholesky decomposition of correlation matrix to generate
 * correlated asset paths for portfolio-level VaR and stress testing.
 */
#pragma once
#ifndef GENIE_RISK_CORRELATED_MC_HPP
#define GENIE_RISK_CORRELATED_MC_HPP

#include "../core/random.hpp"
#include "../core/math_utils.hpp"
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <numeric>

namespace genie::risk {

/** Cholesky decomposition of a symmetric positive-definite matrix */
class CholeskyDecomposition {
    std::vector<std::vector<double>> L_;
    size_t n_{0};

public:
    CholeskyDecomposition() = default;

    /** Decompose correlation/covariance matrix. Returns false if not positive definite. */
    bool decompose(const std::vector<std::vector<double>>& matrix) {
        n_ = matrix.size();
        L_.assign(n_, std::vector<double>(n_, 0.0));

        for (size_t i = 0; i < n_; ++i) {
            for (size_t j = 0; j <= i; ++j) {
                double sum = 0;
                for (size_t k = 0; k < j; ++k)
                    sum += L_[i][k] * L_[j][k];

                if (i == j) {
                    double diag = matrix[i][i] - sum;
                    if (diag <= 0) return false; // Not positive definite
                    L_[i][j] = std::sqrt(diag);
                } else {
                    L_[i][j] = (matrix[i][j] - sum) / L_[j][j];
                }
            }
        }
        return true;
    }

    /** Transform independent standard normals into correlated normals */
    [[nodiscard]] std::vector<double> correlate(const std::vector<double>& z) const {
        std::vector<double> result(n_, 0.0);
        for (size_t i = 0; i < n_; ++i)
            for (size_t j = 0; j <= i; ++j)
                result[i] += L_[i][j] * z[j];
        return result;
    }

    [[nodiscard]] size_t dimension() const { return n_; }
    [[nodiscard]] const std::vector<std::vector<double>>& lower() const { return L_; }
};

/** Asset parameters for correlated simulation */
struct AssetParams {
    std::string id;
    double current_price{100.0};
    double annual_return{0.08};   // expected drift (mu)
    double annual_vol{0.20};      // annualized volatility (sigma)
};

/** Result of a correlated Monte Carlo simulation */
struct CorrelatedMcResult {
    size_t n_assets{0};
    size_t n_paths{0};
    size_t n_steps{0};
    std::vector<std::string> asset_ids;

    // Terminal portfolio values for each path
    std::vector<double> portfolio_values;

    // Per-asset terminal prices (asset x path)
    std::vector<std::vector<double>> terminal_prices;

    // Statistics
    double mean_portfolio_value{0};
    double portfolio_std{0};
    double var_95{0}, var_99{0};
    double cvar_95{0}, cvar_99{0};

    // Per-asset stats
    struct AssetStats {
        std::string id;
        double mean_price{0}, std_price{0};
        double var_95{0}, var_99{0};
    };
    std::vector<AssetStats> asset_stats;
};

/** Correlated Monte Carlo engine */
class CorrelatedMonteCarlo {
public:
    /**
     * Run correlated GBM simulation.
     * @param assets      Vector of asset parameters
     * @param correlation Correlation matrix (n x n)
     * @param weights     Portfolio weights per asset (must sum to ~1)
     * @param n_paths     Number of simulation paths
     * @param n_steps     Number of time steps per path
     * @param dt          Time step in years (e.g., 1/252 for daily)
     * @param seed        RNG seed
     */
    static CorrelatedMcResult simulate(
            const std::vector<AssetParams>& assets,
            const std::vector<std::vector<double>>& correlation,
            const std::vector<double>& weights,
            size_t n_paths = 10000,
            size_t n_steps = 252,
            double dt = 1.0 / 252.0,
            uint64_t seed = 42) {

        size_t n = assets.size();
        if (n == 0) throw std::invalid_argument("No assets");
        if (correlation.size() != n) throw std::invalid_argument("Correlation matrix size mismatch");
        if (weights.size() != n) throw std::invalid_argument("Weights size mismatch");

        // Build covariance matrix from correlation + vols
        std::vector<std::vector<double>> cov(n, std::vector<double>(n));
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                cov[i][j] = correlation[i][j] * assets[i].annual_vol * assets[j].annual_vol;

        CholeskyDecomposition chol;
        if (!chol.decompose(cov))
            throw std::runtime_error("Correlation matrix not positive definite");

        CorrelatedMcResult result;
        result.n_assets = n;
        result.n_paths = n_paths;
        result.n_steps = n_steps;
        for (const auto& a : assets) result.asset_ids.push_back(a.id);

        result.portfolio_values.resize(n_paths);
        result.terminal_prices.assign(n, std::vector<double>(n_paths));

        RandomGenerator rng(seed);

        // Calculate initial portfolio value
        double init_portfolio = 0;
        for (size_t i = 0; i < n; ++i)
            init_portfolio += weights[i] * assets[i].current_price;

        for (size_t p = 0; p < n_paths; ++p) {
            // Simulate each path
            std::vector<double> prices(n);
            for (size_t i = 0; i < n; ++i)
                prices[i] = assets[i].current_price;

            for (size_t t = 0; t < n_steps; ++t) {
                // Generate independent normals
                std::vector<double> z(n);
                for (size_t i = 0; i < n; ++i)
                    z[i] = rng.normal();

                // Correlate
                auto corr_z = chol.correlate(z);

                // GBM step for each asset
                for (size_t i = 0; i < n; ++i) {
                    double mu = assets[i].annual_return;
                    double sigma = assets[i].annual_vol;
                    prices[i] *= std::exp((mu - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * corr_z[i]);
                }
            }

            // Record terminal prices and portfolio value
            double port_val = 0;
            for (size_t i = 0; i < n; ++i) {
                result.terminal_prices[i][p] = prices[i];
                port_val += weights[i] * prices[i];
            }
            result.portfolio_values[p] = port_val;
        }

        // Compute statistics
        compute_stats(result, init_portfolio);
        return result;
    }

private:
    static void compute_stats(CorrelatedMcResult& r, double init_value) {
        // Portfolio stats
        auto& pv = r.portfolio_values;
        r.mean_portfolio_value = std::accumulate(pv.begin(), pv.end(), 0.0) / pv.size();

        double var_sum = 0;
        for (double v : pv) var_sum += (v - r.mean_portfolio_value) * (v - r.mean_portfolio_value);
        r.portfolio_std = std::sqrt(var_sum / pv.size());

        // P&L distribution
        std::vector<double> pnl(pv.size());
        for (size_t i = 0; i < pv.size(); ++i) pnl[i] = pv[i] - init_value;
        std::sort(pnl.begin(), pnl.end());

        size_t idx_5 = static_cast<size_t>(0.05 * pnl.size());
        size_t idx_1 = static_cast<size_t>(0.01 * pnl.size());
        r.var_95 = -pnl[idx_5];
        r.var_99 = -pnl[idx_1];

        // CVaR
        double sum_5 = 0, sum_1 = 0;
        for (size_t i = 0; i <= idx_5; ++i) sum_5 += pnl[i];
        for (size_t i = 0; i <= idx_1; ++i) sum_1 += pnl[i];
        r.cvar_95 = -(sum_5 / (idx_5 + 1));
        r.cvar_99 = -(sum_1 / (idx_1 + 1));

        // Per-asset stats
        r.asset_stats.resize(r.n_assets);
        for (size_t a = 0; a < r.n_assets; ++a) {
            auto& s = r.asset_stats[a];
            s.id = r.asset_ids[a];
            auto& tp = r.terminal_prices[a];
            s.mean_price = std::accumulate(tp.begin(), tp.end(), 0.0) / tp.size();
            double v = 0;
            for (double p : tp) v += (p - s.mean_price) * (p - s.mean_price);
            s.std_price = std::sqrt(v / tp.size());

            std::vector<double> sorted(tp.begin(), tp.end());
            std::sort(sorted.begin(), sorted.end());
            s.var_95 = s.mean_price - sorted[static_cast<size_t>(0.05 * sorted.size())];
            s.var_99 = s.mean_price - sorted[static_cast<size_t>(0.01 * sorted.size())];
        }
    }
};

} // namespace genie::risk

#endif // GENIE_RISK_CORRELATED_MC_HPP
