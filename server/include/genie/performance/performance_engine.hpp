/**
 * @file performance_engine.hpp
 * @brief Performance Attribution Engine for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_PERFORMANCE_ENGINE_HPP
#define GENIE_PERFORMANCE_ENGINE_HPP
#include "../portfolio/portfolio.hpp"

namespace genie::performance {
using namespace genie::portfolio;

struct PerformanceStats {
    double total_return{0}, annualized_return{0}, ytd_return{0};
    double volatility{0}, annualized_volatility{0}, downside_volatility{0};
    double sharpe_ratio{0}, sortino_ratio{0}, calmar_ratio{0}, information_ratio{0};
    double max_drawdown{0}, current_drawdown{0};
    int max_drawdown_days{0}, winning_days{0}, losing_days{0};
    double best_day{0}, worst_day{0}, average_win{0}, average_loss{0};
    double win_rate{0}, profit_factor{0};
    double alpha{0}, beta{0}, r_squared{0}, tracking_error{0};
};

class PerformanceCalculator {
    double risk_free_rate_{0.05};
public:
    void set_risk_free_rate(double r) { risk_free_rate_ = r; }
    [[nodiscard]] PerformanceStats calculate(const std::vector<double>& returns, const std::vector<double>& benchmark = {}) {
        PerformanceStats stats; if (returns.empty()) return stats;
        stats.total_return = math::compound_return(returns);
        double years = returns.size() / 252.0;
        stats.annualized_return = math::annualized_return(stats.total_return, years);
        stats.volatility = math::stddev(returns);
        stats.annualized_volatility = math::annualized_volatility(stats.volatility);
        stats.downside_volatility = math::downside_deviation(returns);
        stats.max_drawdown = math::max_drawdown(returns);
        double ann_downside = math::annualized_volatility(stats.downside_volatility);
        stats.sharpe_ratio = stats.annualized_volatility > 0 ? (stats.annualized_return - risk_free_rate_) / stats.annualized_volatility : 0;
        stats.sortino_ratio = ann_downside > 0 ? (stats.annualized_return - risk_free_rate_) / ann_downside : 0;
        stats.calmar_ratio = stats.max_drawdown > 0 ? stats.annualized_return / stats.max_drawdown : 0;
        double win_sum = 0, loss_sum = 0; stats.best_day = -1e10; stats.worst_day = 1e10;
        for (double r : returns) {
            if (r > 0) { stats.winning_days++; win_sum += r; }
            else if (r < 0) { stats.losing_days++; loss_sum += std::abs(r); }
            stats.best_day = std::max(stats.best_day, r); stats.worst_day = std::min(stats.worst_day, r);
        }
        stats.win_rate = returns.size() > 0 ? (stats.winning_days * 100.0) / returns.size() : 0;
        stats.average_win = stats.winning_days > 0 ? win_sum / stats.winning_days : 0;
        stats.average_loss = stats.losing_days > 0 ? loss_sum / stats.losing_days : 0;
        stats.profit_factor = stats.average_loss > 0 ? stats.average_win / stats.average_loss : 0;
        if (!benchmark.empty() && benchmark.size() == returns.size()) {
            stats.beta = math::beta(returns, benchmark);
            double bench_ret = math::compound_return(benchmark);
            double bench_ann = math::annualized_return(bench_ret, years);
            stats.alpha = stats.annualized_return - (risk_free_rate_ + stats.beta * (bench_ann - risk_free_rate_));
            std::vector<double> excess; for (size_t i = 0; i < returns.size(); ++i) excess.push_back(returns[i] - benchmark[i]);
            stats.tracking_error = math::annualized_volatility(math::stddev(excess));
            stats.information_ratio = stats.tracking_error > 0 ? math::mean(excess) * std::sqrt(252.0) / stats.tracking_error : 0;
            double corr = math::correlation(returns, benchmark); stats.r_squared = corr * corr;
        }
        return stats;
    }
};

struct BrinsonAttribution {
    std::map<std::string, double> allocation_effect, selection_effect, interaction_effect;
    double total_allocation{0}, total_selection{0}, total_interaction{0}, total_active{0};
};

class AttributionEngine {
    PerformanceCalculator perf_calc_;
public:
    [[nodiscard]] PerformanceCalculator& performance() { return perf_calc_; }
    [[nodiscard]] BrinsonAttribution calculate_brinson(
        const std::map<std::string, double>& port_weights, const std::map<std::string, double>& bench_weights,
        const std::map<std::string, double>& port_returns, const std::map<std::string, double>& bench_returns) {
        BrinsonAttribution result;
        std::set<std::string> sectors;
        for (const auto& [s, w] : port_weights) sectors.insert(s);
        for (const auto& [s, w] : bench_weights) sectors.insert(s);
        for (const std::string& s : sectors) {
            double wp = port_weights.count(s) ? port_weights.at(s) : 0;
            double wb = bench_weights.count(s) ? bench_weights.at(s) : 0;
            double rp = port_returns.count(s) ? port_returns.at(s) : 0;
            double rb = bench_returns.count(s) ? bench_returns.at(s) : 0;
            double bench_total = 0; for (const auto& [sec, ret] : bench_returns) { double bw = bench_weights.count(sec) ? bench_weights.at(sec) : 0; bench_total += bw * ret; }
            result.allocation_effect[s] = (wp - wb) * (rb - bench_total);
            result.selection_effect[s] = wb * (rp - rb);
            result.interaction_effect[s] = (wp - wb) * (rp - rb);
            result.total_allocation += result.allocation_effect[s];
            result.total_selection += result.selection_effect[s];
            result.total_interaction += result.interaction_effect[s];
        }
        result.total_active = result.total_allocation + result.total_selection + result.total_interaction;
        return result;
    }
};
} // namespace genie::performance
#endif
