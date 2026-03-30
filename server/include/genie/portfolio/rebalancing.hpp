/**
 * @file rebalancing.hpp
 * @brief Portfolio rebalancing engine for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_REBALANCING_HPP
#define GENIE_REBALANCING_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <chrono>
#include <functional>

namespace genie {
namespace portfolio {

enum class RebalanceTrigger { Threshold, Calendar, Drift, Manual };
enum class RebalanceFrequency { Daily, Weekly, BiWeekly, Monthly, Quarterly, Annually, OnDrift };

struct TargetAllocation {
    std::string security_id;
    double target_weight;
    double tolerance;  // Allowed deviation before rebalance
};

struct RebalanceTrade {
    std::string security_id;
    double current_weight;
    double target_weight;
    double trade_weight;  // Positive = buy, negative = sell
    double estimated_shares;
    double estimated_value;
};

struct RebalancingResult {
    bool rebalance_needed{false};
    double max_drift{0};
    double total_drift{0};
    std::vector<RebalanceTrade> trades;
    double estimated_turnover{0};
    double estimated_cost{0};
    std::string trigger_reason;
};

struct DriftReport {
    std::string security_id;
    double current_weight;
    double target_weight;
    double drift;
    double absolute_drift;
    bool exceeds_tolerance;
};

class RebalancingEngine {
    std::vector<TargetAllocation> targets_;
    RebalanceTrigger trigger_type_{RebalanceTrigger::Threshold};
    RebalanceFrequency frequency_{RebalanceFrequency::Quarterly};
    double default_tolerance_{0.05};  // 5% default
    double min_trade_size_{100.0};    // Minimum trade value
    double transaction_cost_bps_{10.0};  // 10 bps
    std::chrono::system_clock::time_point last_rebalance_;
    
public:
    void set_trigger(RebalanceTrigger trigger) { trigger_type_ = trigger; }
    void set_frequency(RebalanceFrequency freq) { frequency_ = freq; }
    void set_default_tolerance(double tol) { default_tolerance_ = tol; }
    void set_min_trade_size(double size) { min_trade_size_ = size; }
    void set_transaction_cost(double bps) { transaction_cost_bps_ = bps; }
    
    void set_targets(const std::vector<TargetAllocation>& targets) {
        targets_ = targets;
    }
    
    void add_target(const std::string& security_id, double weight, double tolerance = -1) {
        TargetAllocation t;
        t.security_id = security_id;
        t.target_weight = weight;
        t.tolerance = (tolerance < 0) ? default_tolerance_ : tolerance;
        targets_.push_back(t);
    }
    
    void clear_targets() { targets_.clear(); }
    
    // Calculate drift for each position
    std::vector<DriftReport> calculate_drift(const std::map<std::string, double>& current_weights) const {
        std::vector<DriftReport> report;
        
        for (const auto& target : targets_) {
            DriftReport dr;
            dr.security_id = target.security_id;
            dr.target_weight = target.target_weight;
            dr.current_weight = current_weights.count(target.security_id) ? 
                                current_weights.at(target.security_id) : 0.0;
            dr.drift = dr.current_weight - dr.target_weight;
            dr.absolute_drift = std::abs(dr.drift);
            dr.exceeds_tolerance = dr.absolute_drift > target.tolerance;
            report.push_back(dr);
        }
        
        // Check for positions not in targets
        for (const auto& [sec_id, weight] : current_weights) {
            bool found = false;
            for (const auto& t : targets_) {
                if (t.security_id == sec_id) { found = true; break; }
            }
            if (!found && weight > 0.001) {
                DriftReport dr;
                dr.security_id = sec_id;
                dr.target_weight = 0;
                dr.current_weight = weight;
                dr.drift = weight;
                dr.absolute_drift = weight;
                dr.exceeds_tolerance = true;
                report.push_back(dr);
            }
        }
        
        return report;
    }
    
    // Check if rebalance is needed
    RebalancingResult check_rebalance(const std::map<std::string, double>& current_weights,
                                    double portfolio_value,
                                    const std::map<std::string, double>& prices) const {
        RebalancingResult result;
        auto drift = calculate_drift(current_weights);
        
        for (const auto& d : drift) {
            result.total_drift += d.absolute_drift;
            result.max_drift = std::max(result.max_drift, d.absolute_drift);
            
            if (d.exceeds_tolerance) {
                result.rebalance_needed = true;
            }
        }
        
        if (result.rebalance_needed) {
            result.trigger_reason = "Threshold exceeded (max drift: " + 
                                     std::to_string(result.max_drift * 100) + "%)";
            
            // Generate trades
            for (const auto& d : drift) {
                if (std::abs(d.drift) > 0.001) {
                    RebalanceTrade trade;
                    trade.security_id = d.security_id;
                    trade.current_weight = d.current_weight;
                    trade.target_weight = d.target_weight;
                    trade.trade_weight = d.target_weight - d.current_weight;
                    trade.estimated_value = trade.trade_weight * portfolio_value;
                    
                    if (std::abs(trade.estimated_value) >= min_trade_size_) {
                        if (prices.count(d.security_id) && prices.at(d.security_id) > 0) {
                            trade.estimated_shares = trade.estimated_value / prices.at(d.security_id);
                        }
                        result.trades.push_back(trade);
                        result.estimated_turnover += std::abs(trade.estimated_value);
                    }
                }
            }
            
            result.estimated_cost = result.estimated_turnover * transaction_cost_bps_ / 10000.0;
        }
        
        return result;
    }
    
    // Check calendar-based rebalance
    bool check_calendar_rebalance() const {
        auto now = std::chrono::system_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::hours>(now - last_rebalance_);
        
        switch (frequency_) {
            case RebalanceFrequency::Daily: return diff.count() >= 24;
            case RebalanceFrequency::Weekly: return diff.count() >= 168;
            case RebalanceFrequency::Monthly: return diff.count() >= 720;
            case RebalanceFrequency::Quarterly: return diff.count() >= 2160;
            case RebalanceFrequency::Annually: return diff.count() >= 8760;
            default: return false;
        }
    }
    
    void record_rebalance() {
        last_rebalance_ = std::chrono::system_clock::now();
    }
    
    // Generate equal-weight targets
    void set_equal_weight(const std::vector<std::string>& security_ids) {
        targets_.clear();
        double weight = 1.0 / security_ids.size();
        for (const auto& id : security_ids) {
            add_target(id, weight);
        }
    }
    
    // Get current targets
    const std::vector<TargetAllocation>& targets() const { return targets_; }
    
    // Summary report
    std::string drift_summary(const std::map<std::string, double>& current_weights) const {
        auto drift = calculate_drift(current_weights);
        std::ostringstream ss;
        ss << "=== DRIFT SUMMARY ===\n";
        ss << std::fixed << std::setprecision(2);
        
        for (const auto& d : drift) {
            ss << d.security_id << ": " 
               << (d.current_weight * 100) << "% current, "
               << (d.target_weight * 100) << "% target, "
               << (d.drift * 100) << "% drift"
               << (d.exceeds_tolerance ? " [EXCEEDS]" : "") << "\n";
        }
        
        return ss.str();
    }
};

} // namespace portfolio
} // namespace genie
#endif // GENIE_REBALANCING_HPP
