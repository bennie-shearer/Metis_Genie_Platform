/**
 * @file currency_hedging.hpp
 * @brief Currency hedging and FX exposure management for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CURRENCY_HEDGING_HPP
#define GENIE_CURRENCY_HEDGING_HPP

#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace genie {
namespace fx {

struct CurrencyExposure {
    std::string currency;
    double exposure_value;       // In base currency
    double exposure_pct;         // As % of portfolio
    double hedged_amount;
    double unhedged_amount;
    double hedge_ratio;
};

struct ForwardContract {
    std::string contract_id;
    std::string base_currency;
    std::string quote_currency;
    double notional_amount;
    double forward_rate;
    double spot_rate_at_inception;
    int days_to_maturity;
    double mark_to_market{0};
};

struct HedgeRecommendation {
    std::string currency;
    double current_hedge_ratio;
    double recommended_hedge_ratio;
    double notional_to_hedge;
    double estimated_cost_bps;
    std::string rationale;
};

class CurrencyHedgingEngine {
    std::string base_currency_{"USD"};
    std::map<std::string, double> spot_rates_;          // vs base currency
    std::map<std::string, double> forward_points_;      // 3M forward points
    std::map<std::string, double> interest_rates_;      // Annualized
    std::map<std::string, double> target_hedge_ratios_;
    std::vector<ForwardContract> active_forwards_;
    double hedge_tolerance_{0.05};  // 5% drift before rehedge
    
public:
    void set_base_currency(const std::string& ccy) { base_currency_ = ccy; }
    const std::string& base_currency() const { return base_currency_; }
    
    void set_spot_rate(const std::string& ccy, double rate) { spot_rates_[ccy] = rate; }
    void set_forward_points(const std::string& ccy, double points) { forward_points_[ccy] = points; }
    void set_interest_rate(const std::string& ccy, double rate) { interest_rates_[ccy] = rate; }
    void set_target_hedge_ratio(const std::string& ccy, double ratio) { target_hedge_ratios_[ccy] = ratio; }
    void set_hedge_tolerance(double tol) { hedge_tolerance_ = tol; }
    
    double get_spot(const std::string& ccy) const {
        return spot_rates_.count(ccy) ? spot_rates_.at(ccy) : 1.0;
    }
    
    // Calculate forward rate using interest rate parity
    double calculate_forward_rate(const std::string& ccy, int days) const {
        double spot = get_spot(ccy);
        double base_rate = interest_rates_.count(base_currency_) ? interest_rates_.at(base_currency_) : 0.02;
        double foreign_rate = interest_rates_.count(ccy) ? interest_rates_.at(ccy) : 0.02;
        
        double years = days / 365.0;
        // Forward = Spot * (1 + r_domestic)^t / (1 + r_foreign)^t
        return spot * std::pow(1 + base_rate, years) / std::pow(1 + foreign_rate, years);
    }
    
    // Calculate forward points (in pips)
    double calculate_forward_points(const std::string& ccy, int days) const {
        double spot = get_spot(ccy);
        double forward = calculate_forward_rate(ccy, days);
        return (forward - spot) * 10000;  // In pips
    }
    
    // Analyze currency exposure
    std::vector<CurrencyExposure> analyze_exposure(
            const std::map<std::string, double>& position_values_by_currency,
            double total_portfolio_value) const {
        std::vector<CurrencyExposure> exposures;
        
        // Calculate hedged amounts from active forwards
        std::map<std::string, double> hedged_by_ccy;
        for (const auto& fwd : active_forwards_) {
            std::string ccy = (fwd.base_currency == base_currency_) ? fwd.quote_currency : fwd.base_currency;
            hedged_by_ccy[ccy] += fwd.notional_amount;
        }
        
        for (const auto& [ccy, value] : position_values_by_currency) {
            if (ccy == base_currency_) continue;
            
            CurrencyExposure exp;
            exp.currency = ccy;
            exp.exposure_value = value;
            exp.exposure_pct = value / total_portfolio_value;
            exp.hedged_amount = hedged_by_ccy.count(ccy) ? hedged_by_ccy[ccy] : 0;
            exp.unhedged_amount = value - exp.hedged_amount;
            exp.hedge_ratio = (value > 0) ? exp.hedged_amount / value : 0;
            exposures.push_back(exp);
        }
        
        return exposures;
    }
    
    // Generate hedge recommendations
    std::vector<HedgeRecommendation> generate_recommendations(
            const std::vector<CurrencyExposure>& exposures) const {
        std::vector<HedgeRecommendation> recs;
        
        for (const auto& exp : exposures) {
            double target = target_hedge_ratios_.count(exp.currency) ? 
                           target_hedge_ratios_.at(exp.currency) : 0.5;  // Default 50%
            
            double diff = std::abs(exp.hedge_ratio - target);
            if (diff > hedge_tolerance_) {
                HedgeRecommendation rec;
                rec.currency = exp.currency;
                rec.current_hedge_ratio = exp.hedge_ratio;
                rec.recommended_hedge_ratio = target;
                rec.notional_to_hedge = (target - exp.hedge_ratio) * exp.exposure_value;
                
                // Estimate cost (forward points as bps per year, adjusted for 3 months)
                double fwd_points = forward_points_.count(exp.currency) ? 
                                   forward_points_.at(exp.currency) : 0;
                rec.estimated_cost_bps = std::abs(fwd_points) / 4.0;  // Quarterly
                
                if (rec.notional_to_hedge > 0) {
                    rec.rationale = "Increase hedge - underhedged by " + 
                                    std::to_string(int((target - exp.hedge_ratio) * 100)) + "%";
                } else {
                    rec.rationale = "Decrease hedge - overhedged by " + 
                                    std::to_string(int((exp.hedge_ratio - target) * 100)) + "%";
                }
                
                recs.push_back(rec);
            }
        }
        
        return recs;
    }
    
    // Add forward contract
    std::string add_forward(const std::string& base_ccy, const std::string& quote_ccy,
                            double notional, int days) {
        ForwardContract fwd;
        fwd.contract_id = "FWD-" + std::to_string(active_forwards_.size() + 1);
        fwd.base_currency = base_ccy;
        fwd.quote_currency = quote_ccy;
        fwd.notional_amount = notional;
        fwd.spot_rate_at_inception = get_spot(quote_ccy);
        fwd.forward_rate = calculate_forward_rate(quote_ccy, days);
        fwd.days_to_maturity = days;
        
        active_forwards_.push_back(fwd);
        return fwd.contract_id;
    }
    
    // Mark-to-market all forwards
    void mark_to_market() {
        for (auto& fwd : active_forwards_) {
            std::string ccy = (fwd.base_currency == base_currency_) ? fwd.quote_currency : fwd.base_currency;
            double current_fwd = calculate_forward_rate(ccy, fwd.days_to_maturity);
            fwd.mark_to_market = fwd.notional_amount * (current_fwd - fwd.forward_rate);
        }
    }
    
    // Get total hedge P&L
    double total_hedge_pnl() const {
        double total = 0;
        for (const auto& fwd : active_forwards_) {
            total += fwd.mark_to_market;
        }
        return total;
    }
    
    // Get active forwards
    const std::vector<ForwardContract>& active_forwards() const { return active_forwards_; }
    
    // Clear expired forwards
    void clear_expired() {
        active_forwards_.erase(
            std::remove_if(active_forwards_.begin(), active_forwards_.end(),
                          [](const ForwardContract& f) { return f.days_to_maturity <= 0; }),
            active_forwards_.end());
    }
    
    // Generate report
    std::string report(const std::map<std::string, double>& position_values,
                       double portfolio_value) const {
        std::ostringstream ss;
        ss << "=== CURRENCY HEDGING REPORT ===\n\n";
        ss << "Base Currency: " << base_currency_ << "\n\n";
        ss << std::fixed << std::setprecision(2);
        
        auto exposures = analyze_exposure(position_values, portfolio_value);
        
        ss << "Currency Exposures:\n";
        for (const auto& exp : exposures) {
            ss << "  " << exp.currency << ": $" << exp.exposure_value
               << " (" << (exp.exposure_pct * 100) << "% of portfolio)"
               << " | Hedge Ratio: " << (exp.hedge_ratio * 100) << "%\n";
        }
        
        ss << "\nActive Forward Contracts: " << active_forwards_.size() << "\n";
        for (const auto& fwd : active_forwards_) {
            ss << "  " << fwd.contract_id << ": " << fwd.base_currency << "/" << fwd.quote_currency
               << " $" << fwd.notional_amount << " @ " << fwd.forward_rate
               << " | MTM: $" << fwd.mark_to_market << "\n";
        }
        
        ss << "\nTotal Hedge P&L: $" << total_hedge_pnl() << "\n";
        
        return ss.str();
    }
};

} // namespace fx
} // namespace genie
#endif // GENIE_CURRENCY_HEDGING_HPP
