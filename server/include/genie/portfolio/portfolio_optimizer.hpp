/**
 * @file portfolio_optimizer.hpp
 * @brief Portfolio optimization for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_PORTFOLIO_OPTIMIZER_HPP
#define GENIE_PORTFOLIO_OPTIMIZER_HPP
#include "portfolio.hpp"
#include "../core/random.hpp"

namespace genie::portfolio {
struct OptimizationConstraints {
    double min_weight{0.0}, max_weight{1.0}; bool long_only{true}; double max_turnover{1.0};
    std::map<SecurityId, std::pair<double, double>> position_limits;
    void add_max_position(double max) { max_weight = max; }
};

struct OptimizationInputs {
    std::vector<SecurityId> security_ids; std::vector<double> expected_returns;
    math::Matrix covariance_matrix; double risk_free_rate{0.05};
    OptimizationObjective objective{OptimizationObjective::MaxSharpe};
    OptimizationConstraints constraints; double target_return{0.10}, target_risk{0.15};
};

struct OptimizationResult {
    bool success{false}; std::string message; std::map<SecurityId, double> weights;
    double expected_return{0}, expected_volatility{0}, sharpe_ratio{0}; int iterations{0};
};

class PortfolioOptimizer {
    int max_iterations_{10000}; double tolerance_{1e-8};
public:
    void set_max_iterations(int n) { max_iterations_ = n; }
    [[nodiscard]] OptimizationResult optimize(const OptimizationInputs& inputs) {
        switch (inputs.objective) {
            case OptimizationObjective::MaxSharpe: return optimize_max_sharpe(inputs);
            case OptimizationObjective::MinVariance: return optimize_min_variance(inputs);
            case OptimizationObjective::RiskParity: return optimize_risk_parity(inputs);
            default: return optimize_max_sharpe(inputs);
        }
    }
private:
    [[nodiscard]] OptimizationResult optimize_max_sharpe(const OptimizationInputs& inputs) {
        OptimizationResult result; size_t n = inputs.security_ids.size();
        if (n == 0 || inputs.expected_returns.size() != n || inputs.covariance_matrix.size() != n) { result.message = "Invalid input"; return result; }
        RandomGenerator gen;
        std::vector<double> best_weights(n, 1.0 / n);
        double best_sharpe = calc_sharpe(best_weights, inputs);
        for (int iter = 0; iter < max_iterations_; ++iter) {
            std::vector<double> w(n); double sum = 0;
            for (size_t i = 0; i < n; ++i) { w[i] = gen.uniform(0.01, 1.0); sum += w[i]; }
            for (auto& x : w) x /= sum;
            bool valid = true;
            for (size_t i = 0; i < n; ++i) { if (inputs.constraints.long_only && w[i] < 0) valid = false; if (w[i] > inputs.constraints.max_weight) valid = false; }
            if (!valid) continue;
            double sharpe = calc_sharpe(w, inputs);
            if (sharpe > best_sharpe) { best_sharpe = sharpe; best_weights = w; }
        }
        result.success = true;
        for (size_t i = 0; i < n; ++i) result.weights[inputs.security_ids[i]] = best_weights[i];
        result.expected_return = calc_return(best_weights, inputs);
        result.expected_volatility = calc_volatility(best_weights, inputs);
        result.sharpe_ratio = best_sharpe; result.iterations = max_iterations_;
        return result;
    }
    [[nodiscard]] OptimizationResult optimize_min_variance(const OptimizationInputs& inputs) {
        OptimizationResult result; size_t n = inputs.security_ids.size();
        if (n == 0 || inputs.covariance_matrix.size() != n) { result.message = "Invalid inputs"; return result; }
        RandomGenerator gen;
        std::vector<double> best_weights(n, 1.0 / n);
        double best_var = calc_variance(best_weights, inputs);
        for (int iter = 0; iter < max_iterations_; ++iter) {
            std::vector<double> w(n); double sum = 0;
            for (size_t i = 0; i < n; ++i) { w[i] = gen.uniform(0.01, 1.0); sum += w[i]; }
            for (auto& x : w) x /= sum;
            double var = calc_variance(w, inputs);
            if (var < best_var) { best_var = var; best_weights = w; }
        }
        result.success = true;
        for (size_t i = 0; i < n; ++i) result.weights[inputs.security_ids[i]] = best_weights[i];
        result.expected_return = calc_return(best_weights, inputs);
        result.expected_volatility = std::sqrt(best_var);
        result.sharpe_ratio = result.expected_volatility > 0 ? (result.expected_return - inputs.risk_free_rate) / result.expected_volatility : 0;
        return result;
    }
    [[nodiscard]] OptimizationResult optimize_risk_parity(const OptimizationInputs& inputs) {
        OptimizationResult result; size_t n = inputs.security_ids.size();
        if (n == 0 || inputs.covariance_matrix.size() != n) { result.message = "Invalid inputs"; return result; }
        std::vector<double> vols(n);
        for (size_t i = 0; i < n; ++i) vols[i] = std::sqrt(inputs.covariance_matrix[i][i]);
        double sum_inv_vol = 0;
        for (double v : vols) if (v > 0) sum_inv_vol += 1.0 / v;
        std::vector<double> weights(n);
        for (size_t i = 0; i < n; ++i) weights[i] = vols[i] > 0 ? (1.0 / vols[i]) / sum_inv_vol : 0;
        result.success = true;
        for (size_t i = 0; i < n; ++i) result.weights[inputs.security_ids[i]] = weights[i];
        result.expected_return = calc_return(weights, inputs);
        result.expected_volatility = calc_volatility(weights, inputs);
        result.sharpe_ratio = result.expected_volatility > 0 ? (result.expected_return - inputs.risk_free_rate) / result.expected_volatility : 0;
        return result;
    }
    [[nodiscard]] double calc_return(const std::vector<double>& w, const OptimizationInputs& in) const {
        double r = 0; for (size_t i = 0; i < w.size() && i < in.expected_returns.size(); ++i) r += w[i] * in.expected_returns[i]; return r;
    }
    [[nodiscard]] double calc_variance(const std::vector<double>& w, const OptimizationInputs& in) const {
        double v = 0; for (size_t i = 0; i < w.size(); ++i) for (size_t j = 0; j < w.size(); ++j) v += w[i] * w[j] * in.covariance_matrix[i][j]; return v;
    }
    [[nodiscard]] double calc_volatility(const std::vector<double>& w, const OptimizationInputs& in) const { return std::sqrt(calc_variance(w, in)); }
    [[nodiscard]] double calc_sharpe(const std::vector<double>& w, const OptimizationInputs& in) const {
        double ret = calc_return(w, in), vol = calc_volatility(w, in);
        return vol > tolerance_ ? (ret - in.risk_free_rate) / vol : 0;
    }
};

struct RebalanceOrder { SecurityId security_id; double current_weight{0}, target_weight{0}, trade_weight{0}; Money trade_value; Quantity trade_quantity{0}; OrderSide side{OrderSide::Buy}; };
struct RebalanceResult { bool success{false}; std::vector<RebalanceOrder> orders; double estimated_turnover{0}; Money estimated_cost; };

class PortfolioRebalancer {
public:
    [[nodiscard]] RebalanceResult calculate_rebalance(const Portfolio& portfolio, const std::map<SecurityId, double>& target_weights, const std::map<SecurityId, Price>& prices, double min_trade = 0.001) {
        RebalanceResult result;
        auto current = portfolio.get_weights(); double nav = portfolio.nav().amount;
        std::set<SecurityId> all_ids;
        for (const auto& [id, w] : current) all_ids.insert(id);
        for (const auto& [id, w] : target_weights) all_ids.insert(id);
        double turnover = 0;
        for (const SecurityId& id : all_ids) {
            auto cur_it = current.find(id); auto tgt_it = target_weights.find(id);
            double cur = cur_it != current.end() ? cur_it->second : 0.0;
            double tgt = tgt_it != target_weights.end() ? tgt_it->second : 0.0;
            double diff = tgt - cur;
            if (std::abs(diff) >= min_trade) {
                RebalanceOrder order;
                order.security_id = id; order.current_weight = cur; order.target_weight = tgt; order.trade_weight = diff;
                order.trade_value = Money(std::abs(diff * nav), portfolio.base_currency());
                order.side = diff > 0 ? OrderSide::Buy : OrderSide::Sell;
                auto pit = prices.find(id);
                if (pit != prices.end() && pit->second > 0) order.trade_quantity = std::abs(diff * nav) / pit->second;
                result.orders.push_back(order); turnover += std::abs(diff);
            }
        }
        result.success = true; result.estimated_turnover = turnover / 2.0; return result;
    }
};
} // namespace genie::portfolio
#endif
