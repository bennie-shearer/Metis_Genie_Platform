/**
 * @file stress_testing.hpp
 * @brief Stress testing framework for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_RISK_STRESS_TESTING_HPP
#define GENIE_RISK_STRESS_TESTING_HPP
#include "var_engine.hpp"

namespace genie::risk {
struct StressScenario { std::string name, description; double equity_shock{0}, rate_shock{0}, credit_shock{0}, vol_shock{0}, fx_shock{0}; };

struct StressTestResult {
    std::string scenario_name; double portfolio_value_before{0}, portfolio_value_after{0}, absolute_pnl{0}, percentage_pnl{0};
    std::map<SecurityId, double> position_pnl; std::map<AssetClass, double> asset_class_pnl; TimePoint calculation_time;
};

class StressScenarioLibrary {
public:
    static StressScenario market_crash_2008() { StressScenario s; s.name = "2008 Financial Crisis"; s.equity_shock = -0.40; s.credit_shock = 0.03; s.vol_shock = 0.80; return s; }
    static StressScenario interest_rate_rise() { StressScenario s; s.name = "Interest Rate Shock +200bp"; s.rate_shock = 0.02; s.equity_shock = -0.10; return s; }
    static StressScenario credit_crisis() { StressScenario s; s.name = "Credit Crisis"; s.credit_shock = 0.05; s.equity_shock = -0.15; return s; }
    static StressScenario volatility_spike() { StressScenario s; s.name = "Volatility Spike"; s.vol_shock = 1.0; s.equity_shock = -0.08; return s; }
    static StressScenario currency_crisis() { StressScenario s; s.name = "USD Strength"; s.fx_shock = 0.15; s.equity_shock = -0.05; return s; }
    static StressScenario black_swan() { StressScenario s; s.name = "Black Swan Event"; s.equity_shock = -0.30; s.credit_shock = 0.04; s.vol_shock = 1.5; s.rate_shock = -0.01; return s; }
    static std::vector<StressScenario> all_scenarios() { return { market_crash_2008(), interest_rate_rise(), credit_crisis(), volatility_spike(), currency_crisis(), black_swan() }; }
};

class StressTestEngine {
    MarketDataService* market_data_{nullptr};
public:
    void set_market_data(MarketDataService* md) { market_data_ = md; }
    [[nodiscard]] StressTestResult run_stress_test(const Portfolio& portfolio, const StressScenario& scenario) {
        StressTestResult result; result.scenario_name = scenario.name;
        result.calculation_time = std::chrono::system_clock::now(); result.portfolio_value_before = portfolio.nav().amount;
        auto positions = portfolio.get_all_positions(); double total_pnl = 0;
        for (const auto& pos : positions) {
            double pnl = 0, mv = pos.market_value().amount;
            if (pos.security()) {
                AssetClass ac = pos.security()->asset_class();
                switch (ac) {
                    case AssetClass::Equity: pnl = mv * scenario.equity_shock; break;
                    case AssetClass::FixedIncome: pnl = mv * (-scenario.rate_shock * 5.0 + scenario.credit_shock * -2.0); break;
                    case AssetClass::Derivative: pnl = mv * (scenario.equity_shock * 1.5 + scenario.vol_shock * 0.1); break;
                    default: pnl = mv * scenario.equity_shock * 0.5;
                }
                result.asset_class_pnl[ac] += pnl;
            }
            result.position_pnl[pos.security_id()] = pnl; total_pnl += pnl;
        }
        result.absolute_pnl = total_pnl; result.portfolio_value_after = result.portfolio_value_before + total_pnl;
        result.percentage_pnl = result.portfolio_value_before > 0 ? (total_pnl / result.portfolio_value_before) * 100.0 : 0.0;
        return result;
    }
    [[nodiscard]] std::vector<StressTestResult> run_all_scenarios(const Portfolio& portfolio) {
        std::vector<StressTestResult> results;
        for (const auto& scenario : StressScenarioLibrary::all_scenarios()) results.push_back(run_stress_test(portfolio, scenario));
        return results;
    }
};

struct RiskFactor { std::string name; double loading{0}, volatility{0}; };
struct FactorRiskDecomposition { double systematic_risk{0}, specific_risk{0}, total_risk{0}; std::map<std::string, double> factor_contributions; double r_squared{0}; };

class FactorRiskModel {
    std::vector<RiskFactor> factors_;
public:
    void add_factor(const RiskFactor& f) { factors_.push_back(f); }
    [[nodiscard]] const std::vector<RiskFactor>& factors() const { return factors_; }
    static FactorRiskModel create_equity_model() {
        FactorRiskModel m;
        m.add_factor({"Market", 1.0, 0.16}); m.add_factor({"Size", 0.0, 0.08}); m.add_factor({"Value", 0.0, 0.06});
        m.add_factor({"Momentum", 0.0, 0.10}); m.add_factor({"Quality", 0.0, 0.05}); m.add_factor({"Technology", 0.0, 0.12});
        m.add_factor({"Financials", 0.0, 0.10}); m.add_factor({"Healthcare", 0.0, 0.08});
        return m;
    }
};

struct CorrelationResult {
    math::Matrix correlation_matrix; std::vector<SecurityId> security_ids; double average_correlation{0};
    std::pair<SecurityId, SecurityId> most_correlated, least_correlated; double max_correlation{0}, min_correlation{0};
};

class CorrelationAnalyzer {
    MarketDataService* market_data_{nullptr};
public:
    void set_market_data(MarketDataService* md) { market_data_ = md; }
    [[nodiscard]] CorrelationResult analyze(const Portfolio& portfolio) {
        CorrelationResult result; auto positions = portfolio.get_all_positions();
        std::vector<std::vector<double>> returns;
        for (const auto& pos : positions) {
            auto rets = market_data_->store()->get_return_vector(pos.security_id());
            if (rets.size() >= 20) { returns.push_back(rets); result.security_ids.push_back(pos.security_id()); }
        }
        if (returns.size() < 2) return result;
        result.correlation_matrix = math::correlation_matrix(returns);
        double sum = 0, count = 0; result.max_correlation = -1; result.min_correlation = 1;
        for (size_t i = 0; i < result.security_ids.size(); ++i) {
            for (size_t j = i + 1; j < result.security_ids.size(); ++j) {
                double c = result.correlation_matrix[i][j]; sum += c; ++count;
                if (c > result.max_correlation) { result.max_correlation = c; result.most_correlated = {result.security_ids[i], result.security_ids[j]}; }
                if (c < result.min_correlation) { result.min_correlation = c; result.least_correlated = {result.security_ids[i], result.security_ids[j]}; }
            }
        }
        result.average_correlation = count > 0 ? sum / count : 0; return result;
    }
};
} // namespace genie::risk
#endif
