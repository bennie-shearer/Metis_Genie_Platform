/**
 * @file var_engine.hpp
 * @brief Value at Risk engine for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_RISK_VAR_ENGINE_HPP
#define GENIE_RISK_VAR_ENGINE_HPP
#include "../portfolio/portfolio.hpp"
#include "../core/random.hpp"
#include "../core/thread_pool.hpp"

namespace genie::risk {
using namespace genie::portfolio;
using namespace genie::market;

struct VaRConfig { VaRMethod method{VaRMethod::Historical}; double confidence_level{0.95}; int holding_period_days{1}; int historical_window{252}; int monte_carlo_simulations{10000}; };

struct VaRResult {
    double var{0}, cvar{0}, var_percent{0}, portfolio_value{0};
    VaRMethod method{VaRMethod::Historical}; double confidence_level{0.95};
    int holding_period{1}, num_simulations{0};
    std::vector<double> pnl_distribution, percentiles;
    TimePoint calculation_time;
};

class VaREngine {
    VaRConfig config_;
    MarketDataService* market_data_{nullptr};
public:
    void set_config(const VaRConfig& cfg) { config_ = cfg; }
    void set_market_data(MarketDataService* md) { market_data_ = md; }
    [[nodiscard]] VaRResult calculate_var(const Portfolio& portfolio) {
        switch (config_.method) {
            case VaRMethod::Parametric: return calc_parametric(portfolio);
            case VaRMethod::Historical: return calc_historical(portfolio);
            case VaRMethod::MonteCarlo: return calc_monte_carlo(portfolio);
            default: return calc_historical(portfolio);
        }
    }
private:
    [[nodiscard]] VaRResult calc_parametric(const Portfolio& portfolio) {
        VaRResult result; result.method = VaRMethod::Parametric; result.confidence_level = config_.confidence_level;
        result.calculation_time = std::chrono::system_clock::now(); result.portfolio_value = portfolio.nav().amount;
        auto positions = portfolio.get_all_positions();
        if (positions.empty() || !market_data_) return result;
        std::vector<std::vector<double>> returns_matrix; std::vector<double> weights;
        double total = result.portfolio_value;
        for (const auto& pos : positions) {
            auto rets = market_data_->store()->get_return_vector(pos.security_id());
            if (rets.size() >= 20) { returns_matrix.push_back(rets); weights.push_back(pos.market_value().amount / total); }
        }
        if (returns_matrix.empty()) return result;
        auto cov = math::covariance_matrix(returns_matrix);
        double port_var = 0;
        for (size_t i = 0; i < weights.size(); ++i) for (size_t j = 0; j < weights.size(); ++j) port_var += weights[i] * weights[j] * cov[i][j];
        double port_vol = std::sqrt(port_var) * std::sqrt(config_.holding_period_days);
        double z = std::abs(math::normal_inverse_cdf(1.0 - config_.confidence_level));
        result.var = total * port_vol * z; result.cvar = result.var * 1.25; result.var_percent = result.var / total * 100.0;
        return result;
    }
    [[nodiscard]] VaRResult calc_historical(const Portfolio& portfolio) {
        VaRResult result; result.method = VaRMethod::Historical; result.confidence_level = config_.confidence_level;
        result.calculation_time = std::chrono::system_clock::now(); result.portfolio_value = portfolio.nav().amount;
        auto positions = portfolio.get_all_positions();
        if (positions.empty() || !market_data_) return result;
        std::map<SecurityId, std::vector<double>> all_rets; size_t min_len = SIZE_MAX;
        for (const auto& pos : positions) {
            auto rets = market_data_->store()->get_return_vector(pos.security_id());
            if (!rets.empty()) { all_rets[pos.security_id()] = rets; min_len = std::min(min_len, rets.size()); }
        }
        if (min_len < 20 || min_len == SIZE_MAX) return result;
        std::vector<double> pnl; pnl.reserve(min_len);
        for (size_t d = 0; d < min_len; ++d) {
            double daily_pnl = 0;
            for (const auto& pos : positions) { auto it = all_rets.find(pos.security_id()); if (it != all_rets.end() && d < it->second.size()) daily_pnl += pos.market_value().amount * it->second[d]; }
            pnl.push_back(daily_pnl);
        }
        result.pnl_distribution = pnl; result.num_simulations = static_cast<int>(pnl.size());
        std::sort(pnl.begin(), pnl.end());
        size_t var_idx = static_cast<size_t>((1.0 - config_.confidence_level) * pnl.size());
        result.var = -pnl[var_idx];
        double cvar_sum = 0; for (size_t i = 0; i <= var_idx; ++i) cvar_sum += pnl[i];
        result.cvar = -(cvar_sum / (var_idx + 1)); result.var_percent = result.var / result.portfolio_value * 100.0;
        return result;
    }
    [[nodiscard]] VaRResult calc_monte_carlo(const Portfolio& portfolio) {
        VaRResult result; result.method = VaRMethod::MonteCarlo; result.confidence_level = config_.confidence_level;
        result.calculation_time = std::chrono::system_clock::now(); result.portfolio_value = portfolio.nav().amount;
        result.num_simulations = config_.monte_carlo_simulations;
        auto positions = portfolio.get_all_positions();
        if (positions.empty() || !market_data_) return result;
        std::vector<std::vector<double>> returns_matrix; std::vector<double> means, values;
        for (const auto& pos : positions) {
            auto rets = market_data_->store()->get_return_vector(pos.security_id());
            if (rets.size() >= 20) { returns_matrix.push_back(rets); means.push_back(math::mean(rets)); values.push_back(pos.market_value().amount); }
        }
        if (returns_matrix.empty()) return result;
        auto cov = math::covariance_matrix(returns_matrix);
        auto chol = math::cholesky_decomposition(cov);
        RandomGenerator gen;
        std::vector<double> pnl(config_.monte_carlo_simulations);
        for (int sim = 0; sim < config_.monte_carlo_simulations; ++sim) {
            auto corr_rets = gen.correlated_normals(chol);
            double daily_pnl = 0; for (size_t i = 0; i < values.size(); ++i) daily_pnl += values[i] * (means[i] + corr_rets[i]);
            pnl[sim] = daily_pnl;
        }
        result.pnl_distribution = pnl; std::sort(pnl.begin(), pnl.end());
        size_t var_idx = static_cast<size_t>((1.0 - config_.confidence_level) * pnl.size());
        result.var = -pnl[var_idx];
        double cvar_sum = 0; for (size_t i = 0; i <= var_idx; ++i) cvar_sum += pnl[i];
        result.cvar = -(cvar_sum / (var_idx + 1)); result.var_percent = result.var / result.portfolio_value * 100.0;
        return result;
    }
};

struct SimulationResult { std::vector<std::vector<double>> paths; double mean_terminal_price{0}, std_terminal_price{0}, mean_return{0}; std::vector<double> percentiles; };

class MonteCarloEngine {
    int simulations_{10000}, steps_{252};
    bool parallel_{true};
public:
    void set_simulations(int n) { simulations_ = n; }
    void set_steps(int n) { steps_ = n; }
    void set_parallel(bool p) { parallel_ = p; }
    [[nodiscard]] bool is_parallel() const { return parallel_; }

    [[nodiscard]] SimulationResult simulate_gbm(double S0, double drift, double vol, double T) {
        SimulationResult result; result.paths.resize(simulations_);
        std::vector<double> terminals(simulations_);

        if (parallel_ && simulations_ >= 100) {
            // Parallel: per-thread RNGs avoid mutex contention
            thread_pool().parallel_for_chunked(0, static_cast<size_t>(simulations_),
                [&](size_t lo, size_t hi) {
                    RandomGenerator gen; // Thread-local RNG with unique seed
                    for (size_t i = lo; i < hi; ++i) {
                        result.paths[i] = gen.gbm_path(S0, drift, vol, T, steps_);
                        terminals[i] = result.paths[i].back();
                    }
                });
        } else {
            RandomGenerator gen;
            for (int i = 0; i < simulations_; ++i) {
                result.paths[i] = gen.gbm_path(S0, drift, vol, T, steps_);
                terminals[i] = result.paths[i].back();
            }
        }

        result.mean_terminal_price = math::mean(terminals); result.std_terminal_price = math::stddev(terminals);
        result.mean_return = (result.mean_terminal_price - S0) / S0;
        result.percentiles = { math::percentile(terminals, 1), math::percentile(terminals, 5), math::percentile(terminals, 25), math::percentile(terminals, 50), math::percentile(terminals, 75), math::percentile(terminals, 95), math::percentile(terminals, 99) };
        return result;
    }

    [[nodiscard]] SimulationResult simulate_jump_diffusion(double S0, double drift, double vol, double T, double jump_intensity, double jump_mean, double jump_vol) {
        SimulationResult result; std::vector<double> terminals(simulations_); double dt = T / steps_;

        if (parallel_ && simulations_ >= 100) {
            thread_pool().parallel_for_chunked(0, static_cast<size_t>(simulations_),
                [&](size_t lo, size_t hi) {
                    RandomGenerator gen;
                    for (size_t i = lo; i < hi; ++i) {
                        double S = S0;
                        for (int t = 0; t < steps_; ++t) {
                            double dW = gen.normal() * std::sqrt(dt); int jumps = gen.poisson(jump_intensity * dt);
                            double jump_effect = 0; for (int j = 0; j < jumps; ++j) jump_effect += gen.normal(jump_mean, jump_vol);
                            S *= std::exp((drift - 0.5 * vol * vol) * dt + vol * dW + jump_effect);
                        }
                        terminals[i] = S;
                    }
                });
        } else {
            RandomGenerator gen;
            for (int i = 0; i < simulations_; ++i) {
                double S = S0;
                for (int t = 0; t < steps_; ++t) {
                    double dW = gen.normal() * std::sqrt(dt); int jumps = gen.poisson(jump_intensity * dt);
                    double jump_effect = 0; for (int j = 0; j < jumps; ++j) jump_effect += gen.normal(jump_mean, jump_vol);
                    S *= std::exp((drift - 0.5 * vol * vol) * dt + vol * dW + jump_effect);
                }
                terminals[i] = S;
            }
        }

        result.mean_terminal_price = math::mean(terminals); result.std_terminal_price = math::stddev(terminals);
        result.mean_return = (result.mean_terminal_price - S0) / S0; return result;
    }

    [[nodiscard]] SimulationResult simulate_heston(double S0, double v0, double theta, double kappa, double xi, double rho, double T) {
        SimulationResult result; std::vector<double> terminals(simulations_); double dt = T / steps_;

        if (parallel_ && simulations_ >= 100) {
            thread_pool().parallel_for_chunked(0, static_cast<size_t>(simulations_),
                [&](size_t lo, size_t hi) {
                    RandomGenerator gen;
                    for (size_t i = lo; i < hi; ++i) {
                        double S = S0, v = v0;
                        for (int t = 0; t < steps_; ++t) {
                            double z1 = gen.normal(), z2 = gen.normal();
                            double zv = z1, zs = rho * z1 + std::sqrt(1 - rho * rho) * z2;
                            v = std::max(0.0, v + kappa * (theta - v) * dt + xi * std::sqrt(v * dt) * zv);
                            S *= std::exp(-0.5 * v * dt + std::sqrt(v * dt) * zs);
                        }
                        terminals[i] = S;
                    }
                });
        } else {
            RandomGenerator gen;
            for (int i = 0; i < simulations_; ++i) {
                double S = S0, v = v0;
                for (int t = 0; t < steps_; ++t) {
                    double z1 = gen.normal(), z2 = gen.normal();
                    double zv = z1, zs = rho * z1 + std::sqrt(1 - rho * rho) * z2;
                    v = std::max(0.0, v + kappa * (theta - v) * dt + xi * std::sqrt(v * dt) * zv);
                    S *= std::exp(-0.5 * v * dt + std::sqrt(v * dt) * zs);
                }
                terminals[i] = S;
            }
        }

        result.mean_terminal_price = math::mean(terminals); result.std_terminal_price = math::stddev(terminals);
        result.mean_return = (result.mean_terminal_price - S0) / S0; return result;
    }
};
} // namespace genie::risk
#endif
