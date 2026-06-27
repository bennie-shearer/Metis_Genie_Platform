/**
 * @file tax_lots.hpp
 * @brief Tax lot management and optimization for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_TAX_LOTS_HPP
#define GENIE_TAX_LOTS_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace genie {
namespace tax {

using TimePoint = std::chrono::system_clock::time_point;

enum class LotSelectionMethod { FIFO, LIFO, HighestCost, LowestCost, SpecificLot, TaxOptimal };

struct TaxLot {
    std::string lot_id;
    std::string security_id;
    TimePoint acquisition_date;
    double quantity;
    double cost_basis;
    double cost_per_share;
    bool is_long_term{false};  // Held > 1 year
    
    double unrealized_gain(double current_price) const {
        return (current_price - cost_per_share) * quantity;
    }
    
    double unrealized_gain_pct(double current_price) const {
        return (current_price - cost_per_share) / cost_per_share;
    }
};

struct LotSaleResult {
    std::string lot_id;
    double quantity_sold;
    double proceeds;
    double cost_basis;
    double realized_gain;
    bool is_long_term;
    bool is_wash_sale{false};
};

struct TaxHarvestOpportunity {
    std::string security_id;
    std::string lot_id;
    double loss_amount;
    bool is_long_term;
    double quantity;
    double days_held;
};

class TaxLotManager {
    std::map<std::string, std::vector<TaxLot>> lots_;  // security_id -> lots
    LotSelectionMethod default_method_{LotSelectionMethod::FIFO};
    double short_term_rate_{0.37};   // Short-term capital gains rate
    double long_term_rate_{0.20};    // Long-term capital gains rate
    int wash_sale_window_{30};       // Days for wash sale rule
    std::vector<LotSaleResult> sale_history_;
    
    std::string generate_lot_id() {
        static int counter = 0;
        return "LOT-" + std::to_string(++counter);
    }
    
public:
    void set_default_method(LotSelectionMethod method) { default_method_ = method; }
    void set_tax_rates(double short_term, double long_term) {
        short_term_rate_ = short_term;
        long_term_rate_ = long_term;
    }
    
    // Add a new lot (purchase)
    std::string add_lot(const std::string& security_id, double quantity, 
                        double cost_per_share, TimePoint acquisition_date = std::chrono::system_clock::now()) {
        TaxLot lot;
        lot.lot_id = generate_lot_id();
        lot.security_id = security_id;
        lot.quantity = quantity;
        lot.cost_per_share = cost_per_share;
        lot.cost_basis = quantity * cost_per_share;
        lot.acquisition_date = acquisition_date;
        
        // Check if long-term
        auto now = std::chrono::system_clock::now();
        auto days = std::chrono::duration_cast<std::chrono::hours>(now - acquisition_date).count() / 24;
        lot.is_long_term = days > 365;
        
        lots_[security_id].push_back(lot);
        return lot.lot_id;
    }
    
    // Get lots for a security
    std::vector<TaxLot> get_lots(const std::string& security_id) const {
        if (lots_.count(security_id)) return lots_.at(security_id);
        return {};
    }
    
    // Select lots for sale based on method
    std::vector<TaxLot*> select_lots(const std::string& security_id, double quantity,
                                      double current_price, LotSelectionMethod method) {
        std::vector<TaxLot*> selected;
        if (!lots_.count(security_id)) return selected;
        
        auto& sec_lots = lots_[security_id];
        std::vector<size_t> indices;
        for (size_t i = 0; i < sec_lots.size(); ++i) {
            if (sec_lots[i].quantity > 0) indices.push_back(i);
        }
        
        // Sort based on method
        switch (method) {
            case LotSelectionMethod::FIFO:
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return sec_lots[a].acquisition_date < sec_lots[b].acquisition_date;
                });
                break;
            case LotSelectionMethod::LIFO:
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return sec_lots[a].acquisition_date > sec_lots[b].acquisition_date;
                });
                break;
            case LotSelectionMethod::HighestCost:
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return sec_lots[a].cost_per_share > sec_lots[b].cost_per_share;
                });
                break;
            case LotSelectionMethod::LowestCost:
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return sec_lots[a].cost_per_share < sec_lots[b].cost_per_share;
                });
                break;
            case LotSelectionMethod::TaxOptimal:
                // Prioritize: losses first, then long-term gains, then short-term gains
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    double gain_a = sec_lots[a].unrealized_gain(current_price);
                    double gain_b = sec_lots[b].unrealized_gain(current_price);
                    if (gain_a < 0 && gain_b >= 0) return true;
                    if (gain_a >= 0 && gain_b < 0) return false;
                    if (gain_a < 0 && gain_b < 0) return gain_a < gain_b;  // Bigger loss first
                    // Both gains: prefer long-term
                    if (sec_lots[a].is_long_term != sec_lots[b].is_long_term)
                        return sec_lots[a].is_long_term;
                    return gain_a < gain_b;  // Smaller gain first
                });
                break;
            default:
                break;
        }
        
        double remaining = quantity;
        for (size_t idx : indices) {
            if (remaining <= 0) break;
            if (sec_lots[idx].quantity > 0) {
                selected.push_back(&sec_lots[idx]);
                remaining -= sec_lots[idx].quantity;
            }
        }
        
        return selected;
    }
    
    // Sell from lots
    std::vector<LotSaleResult> sell(const std::string& security_id, double quantity,
                                     double sale_price, LotSelectionMethod method = LotSelectionMethod::FIFO) {
        std::vector<LotSaleResult> results;
        auto selected = select_lots(security_id, quantity, sale_price, 
                                    method == LotSelectionMethod::SpecificLot ? default_method_ : method);
        
        double remaining = quantity;
        for (auto* lot : selected) {
            if (remaining <= 0) break;
            
            double sell_qty = std::min(remaining, lot->quantity);
            LotSaleResult result;
            result.lot_id = lot->lot_id;
            result.quantity_sold = sell_qty;
            result.proceeds = sell_qty * sale_price;
            result.cost_basis = sell_qty * lot->cost_per_share;
            result.realized_gain = result.proceeds - result.cost_basis;
            result.is_long_term = lot->is_long_term;
            
            // Update lot
            lot->quantity -= sell_qty;
            lot->cost_basis = lot->quantity * lot->cost_per_share;
            
            results.push_back(result);
            sale_history_.push_back(result);
            remaining -= sell_qty;
        }
        
        return results;
    }
    
    // Find tax-loss harvesting opportunities
    std::vector<TaxHarvestOpportunity> find_harvest_opportunities(
            const std::map<std::string, double>& current_prices, double min_loss = 100.0) const {
        std::vector<TaxHarvestOpportunity> opportunities;
        
        for (const auto& [sec_id, sec_lots] : lots_) {
            if (!current_prices.count(sec_id)) continue;
            double price = current_prices.at(sec_id);
            
            for (const auto& lot : sec_lots) {
                if (lot.quantity <= 0) continue;
                double loss = lot.unrealized_gain(price);
                
                if (loss < -min_loss) {
                    TaxHarvestOpportunity opp;
                    opp.security_id = sec_id;
                    opp.lot_id = lot.lot_id;
                    opp.loss_amount = -loss;
                    opp.is_long_term = lot.is_long_term;
                    opp.quantity = lot.quantity;
                    auto now = std::chrono::system_clock::now();
                    opp.days_held = std::chrono::duration_cast<std::chrono::hours>(
                        now - lot.acquisition_date).count() / 24.0;
                    opportunities.push_back(opp);
                }
            }
        }
        
        // Sort by loss amount descending
        std::sort(opportunities.begin(), opportunities.end(),
                  [](const auto& a, const auto& b) { return a.loss_amount > b.loss_amount; });
        
        return opportunities;
    }
    
    // Check for wash sale
    bool check_wash_sale(const std::string& security_id, TimePoint sale_date) const {
        // Check if bought within wash sale window before or after
        if (!lots_.count(security_id)) return false;
        
        for (const auto& lot : lots_.at(security_id)) {
            auto days = std::abs(std::chrono::duration_cast<std::chrono::hours>(
                lot.acquisition_date - sale_date).count() / 24);
            if (days <= wash_sale_window_) return true;
        }
        return false;
    }
    
    // Calculate total unrealized gains/losses
    std::pair<double, double> total_unrealized(const std::map<std::string, double>& prices) const {
        double gains = 0, losses = 0;
        
        for (const auto& [sec_id, sec_lots] : lots_) {
            if (!prices.count(sec_id)) continue;
            double price = prices.at(sec_id);
            
            for (const auto& lot : sec_lots) {
                if (lot.quantity <= 0) continue;
                double gl = lot.unrealized_gain(price);
                if (gl > 0) gains += gl;
                else losses += gl;
            }
        }
        
        return {gains, losses};
    }
    
    // Tax impact estimate
    double estimate_tax_impact(const std::vector<LotSaleResult>& sales) const {
        double tax = 0;
        for (const auto& s : sales) {
            if (s.realized_gain > 0) {
                tax += s.realized_gain * (s.is_long_term ? long_term_rate_ : short_term_rate_);
            }
        }
        return tax;
    }
    
    // Generate report
    std::string report(const std::map<std::string, double>& prices) const {
        std::ostringstream ss;
        ss << "=== TAX LOT REPORT ===\n\n";
        ss << std::fixed << std::setprecision(2);
        
        for (const auto& [sec_id, sec_lots] : lots_) {
            double price = prices.count(sec_id) ? prices.at(sec_id) : 0;
            ss << sec_id << ":\n";
            
            for (const auto& lot : sec_lots) {
                if (lot.quantity <= 0) continue;
                double gain = lot.unrealized_gain(price);
                ss << "  " << lot.lot_id << ": " << lot.quantity << " shares @ $"
                   << lot.cost_per_share << " = $" << lot.cost_basis
                   << " | Unrealized: $" << gain
                   << " (" << (lot.is_long_term ? "LT" : "ST") << ")\n";
            }
        }
        
        auto [gains, losses] = total_unrealized(prices);
        ss << "\nTotal Unrealized Gains: $" << gains << "\n";
        ss << "Total Unrealized Losses: $" << losses << "\n";
        ss << "Net Unrealized: $" << (gains + losses) << "\n";
        
        return ss.str();
    }
};

} // namespace tax
} // namespace genie
#endif // GENIE_TAX_LOTS_HPP
