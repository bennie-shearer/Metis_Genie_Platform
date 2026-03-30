/**
 * @file scenario_analysis.hpp
 * @brief Custom scenario analysis for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_SCENARIO_ANALYSIS_HPP
#define GENIE_SCENARIO_ANALYSIS_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace genie {
namespace scenario {

struct ScenarioShock {
    std::string factor;
    double shock_value;      // Absolute or relative depending on shock_type
    bool is_relative{true};  // true = percentage, false = absolute
};

struct CustomScenario {
    std::string id;
    std::string name;
    std::string description;
    std::vector<ScenarioShock> shocks;
    double probability{0.0};  // Estimated probability of occurrence
};

struct ScenarioResult {
    std::string scenario_id;
    std::string scenario_name;
    double portfolio_pnl{0};
    double portfolio_pnl_pct{0};
    std::map<std::string, double> position_pnl;
    std::map<std::string, double> factor_contributions;
    double var_impact{0};
    double worst_position_pnl{0};
    std::string worst_position_id;
};

struct SensitivityResult {
    std::string factor;
    double delta;      // First derivative (linear sensitivity)
    double gamma;      // Second derivative (convexity)
    double pnl_up;     // P&L if factor moves up by unit
    double pnl_down;   // P&L if factor moves down by unit
};

class ScenarioEngine {
    std::map<std::string, CustomScenario> scenarios_;
    std::map<std::string, std::function<double(double, double)>> factor_models_;
    
public:
    // Add a custom scenario
    void add_scenario(const CustomScenario& scenario) {
        scenarios_[scenario.id] = scenario;
    }
    
    // Create scenario from shocks
    void create_scenario(const std::string& id, const std::string& name,
                         const std::vector<ScenarioShock>& shocks,
                         const std::string& description = "") {
        CustomScenario s;
        s.id = id;
        s.name = name;
        s.description = description;
        s.shocks = shocks;
        scenarios_[id] = s;
    }
    
    // Register factor model (how position reacts to factor)
    void register_factor_model(const std::string& factor,
                               std::function<double(double, double)> model) {
        factor_models_[factor] = model;
    }
    
    // Pre-built scenarios
    static CustomScenario equity_crash_10pct() {
        CustomScenario s;
        s.id = "EQUITY_CRASH_10";
        s.name = "Equity Market -10%";
        s.description = "Broad equity market decline of 10%";
        s.shocks = {{"equity", -0.10, true}};
        s.probability = 0.05;
        return s;
    }
    
    static CustomScenario rate_hike_100bps() {
        CustomScenario s;
        s.id = "RATE_HIKE_100";
        s.name = "Interest Rate +100bps";
        s.description = "Parallel shift in yield curve of 100 basis points";
        s.shocks = {{"interest_rate", 0.01, false}};
        s.probability = 0.10;
        return s;
    }
    
    static CustomScenario dollar_rally() {
        CustomScenario s;
        s.id = "USD_RALLY";
        s.name = "USD Rally 5%";
        s.description = "Broad US dollar appreciation of 5%";
        s.shocks = {{"fx_usd", 0.05, true}};
        s.probability = 0.15;
        return s;
    }
    
    static CustomScenario stagflation() {
        CustomScenario s;
        s.id = "STAGFLATION";
        s.name = "Stagflation";
        s.description = "High inflation with economic slowdown";
        s.shocks = {
            {"equity", -0.15, true},
            {"interest_rate", 0.02, false},
            {"inflation", 0.03, false},
            {"credit_spread", 0.015, false}
        };
        s.probability = 0.03;
        return s;
    }
    
    // Run scenario analysis
    ScenarioResult run_scenario(const std::string& scenario_id,
                                const std::map<std::string, double>& positions,
                                const std::map<std::string, double>& factor_sensitivities,
                                double portfolio_value) const {
        ScenarioResult result;
        
        if (!scenarios_.count(scenario_id)) return result;
        const auto& scenario = scenarios_.at(scenario_id);
        
        result.scenario_id = scenario.id;
        result.scenario_name = scenario.name;
        
        // Calculate factor contributions
        for (const auto& shock : scenario.shocks) {
            double sensitivity = factor_sensitivities.count(shock.factor) ?
                                factor_sensitivities.at(shock.factor) : 0;
            double impact = sensitivity * shock.shock_value * portfolio_value;
            result.factor_contributions[shock.factor] = impact;
            result.portfolio_pnl += impact;
        }
        
        result.portfolio_pnl_pct = result.portfolio_pnl / portfolio_value;
        
        // Calculate position-level P&L (simplified)
        for (const auto& [pos_id, pos_value] : positions) {
            double pos_pnl = 0;
            for (const auto& shock : scenario.shocks) {
                // Assume positions have average sensitivity
                double avg_sensitivity = factor_sensitivities.count(shock.factor) ?
                                        factor_sensitivities.at(shock.factor) : 0;
                pos_pnl += avg_sensitivity * shock.shock_value * pos_value;
            }
            result.position_pnl[pos_id] = pos_pnl;
            
            if (pos_pnl < result.worst_position_pnl) {
                result.worst_position_pnl = pos_pnl;
                result.worst_position_id = pos_id;
            }
        }
        
        return result;
    }
    
    // Run all scenarios
    std::vector<ScenarioResult> run_all_scenarios(
            const std::map<std::string, double>& positions,
            const std::map<std::string, double>& factor_sensitivities,
            double portfolio_value) const {
        std::vector<ScenarioResult> results;
        
        for (const auto& [id, scenario] : scenarios_) {
            results.push_back(run_scenario(id, positions, factor_sensitivities, portfolio_value));
        }
        
        // Sort by P&L impact
        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.portfolio_pnl < b.portfolio_pnl; });
        
        return results;
    }
    
    // Sensitivity analysis (delta/gamma)
    SensitivityResult calculate_sensitivity(const std::string& factor,
                                            double current_value,
                                            double portfolio_value [[maybe_unused]],
                                            std::function<double(double)> valuation_func,
                                            double bump = 0.01) const {
        SensitivityResult result;
        result.factor = factor;
        
        double base_value = valuation_func(current_value);
        double up_value = valuation_func(current_value * (1 + bump));
        double down_value = valuation_func(current_value * (1 - bump));
        
        result.pnl_up = up_value - base_value;
        result.pnl_down = down_value - base_value;
        
        // Delta (first derivative)
        result.delta = (up_value - down_value) / (2 * bump * current_value);
        
        // Gamma (second derivative)
        result.gamma = (up_value - 2 * base_value + down_value) / (bump * bump * current_value * current_value);
        
        return result;
    }
    
    // Get scenario by ID
    const CustomScenario& get_scenario(const std::string& id) const {
        return scenarios_.at(id);
    }
    
    bool has_scenario(const std::string& id) const {
        return scenarios_.count(id) > 0;
    }
    
    std::vector<std::string> list_scenarios() const {
        std::vector<std::string> result;
        for (const auto& [id, s] : scenarios_) result.push_back(id);
        return result;
    }
    
    // Generate report
    std::string report(const std::vector<ScenarioResult>& results) const {
        std::ostringstream ss;
        ss << "=== SCENARIO ANALYSIS REPORT ===\n\n";
        ss << std::fixed << std::setprecision(2);
        
        for (const auto& r : results) {
            ss << r.scenario_name << " (" << r.scenario_id << "):\n";
            ss << "  Portfolio P&L: $" << r.portfolio_pnl 
               << " (" << (r.portfolio_pnl_pct * 100) << "%)\n";
            
            if (!r.factor_contributions.empty()) {
                ss << "  Factor Contributions:\n";
                for (const auto& [f, c] : r.factor_contributions) {
                    ss << "    " << f << ": $" << c << "\n";
                }
            }
            
            if (!r.worst_position_id.empty()) {
                ss << "  Worst Position: " << r.worst_position_id 
                   << " ($" << r.worst_position_pnl << ")\n";
            }
            ss << "\n";
        }
        
        return ss.str();
    }
};

} // namespace scenario
} // namespace genie
#endif // GENIE_SCENARIO_ANALYSIS_HPP
