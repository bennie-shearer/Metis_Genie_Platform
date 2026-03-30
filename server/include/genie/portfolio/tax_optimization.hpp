/**
 * @file tax_optimization.hpp
 * @brief Tax-loss harvesting and optimization for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Wash sale detection (30-day rule), lot selection strategies,
 * harvest candidate identification, and after-tax return calculation.
 */
#pragma once
#ifndef GENIE_PORTFOLIO_TAX_OPTIMIZATION_HPP
#define GENIE_PORTFOLIO_TAX_OPTIMIZATION_HPP

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <ctime>
#include <chrono>

namespace genie::portfolio {

/** Tax lot for specific identification */
struct HarvestLot {
    std::string lot_id;
    std::string security_id;
    double quantity{0};
    double cost_basis{0};        // per share
    double total_cost{0};
    std::string acquisition_date;
    bool long_term{false};       // held > 1 year
};

/** Lot selection strategies */
enum class HarvestStrategy { FIFO, LIFO, HIFO, LOFO, SpecificID, MinTax };

inline std::string strategy_name(HarvestStrategy s) {
    switch (s) {
        case HarvestStrategy::FIFO: return "FIFO";
        case HarvestStrategy::LIFO: return "LIFO";
        case HarvestStrategy::HIFO: return "HIFO (Highest In, First Out)";
        case HarvestStrategy::LOFO: return "LOFO (Lowest In, First Out)";
        case HarvestStrategy::SpecificID: return "Specific ID";
        case HarvestStrategy::MinTax: return "Min Tax";
        default: return "Unknown";
    }
}

/** Harvest candidate - a position with unrealized losses */
struct HarvestCandidate {
    std::string security_id;
    double quantity{0};
    double cost_basis{0};
    double current_price{0};
    double unrealized_loss{0};  // negative = loss
    double tax_savings{0};      // estimated at given tax rate
    bool wash_sale_risk{false}; // true if recently sold/bought
    std::string last_trade_date;
    std::vector<std::string> substitutes; // similar securities for replacement
};

/** Wash sale detection result */
struct WashSaleCheck {
    bool is_wash_sale{false};
    std::string security_id;
    std::string sell_date;
    std::string triggering_date;  // buy within 30 days before/after
    double disallowed_loss{0};
    std::string reason;
};

/** Tax-aware sell result */
struct TaxSellResult {
    std::vector<HarvestLot> lots_sold;
    double total_proceeds{0};
    double total_cost_basis{0};
    double realized_gain_loss{0};
    double short_term_gain{0};
    double long_term_gain{0};
    double estimated_tax{0};
};

/** Tax optimization engine */
class TaxOptimizer {
    double short_term_rate_{0.37};  // Federal + state
    double long_term_rate_{0.20};   // Federal + state
    int wash_sale_window_{30};      // days

public:
    TaxOptimizer() = default;
    TaxOptimizer(double st_rate, double lt_rate)
        : short_term_rate_(st_rate), long_term_rate_(lt_rate) {}

    void set_rates(double st, double lt) { short_term_rate_ = st; long_term_rate_ = lt; }
    [[nodiscard]] double short_term_rate() const { return short_term_rate_; }
    [[nodiscard]] double long_term_rate() const { return long_term_rate_; }

    /** Select lots for selling based on strategy */
    [[nodiscard]] std::vector<HarvestLot> select_lots(
            std::vector<HarvestLot> available_lots,
            double sell_quantity,
            HarvestStrategy strategy = HarvestStrategy::HIFO) const {

        // Sort based on strategy
        switch (strategy) {
            case HarvestStrategy::FIFO:
                std::sort(available_lots.begin(), available_lots.end(),
                    [](const auto& a, const auto& b) { return a.acquisition_date < b.acquisition_date; });
                break;
            case HarvestStrategy::LIFO:
                std::sort(available_lots.begin(), available_lots.end(),
                    [](const auto& a, const auto& b) { return a.acquisition_date > b.acquisition_date; });
                break;
            case HarvestStrategy::HIFO:
                std::sort(available_lots.begin(), available_lots.end(),
                    [](const auto& a, const auto& b) { return a.cost_basis > b.cost_basis; });
                break;
            case HarvestStrategy::LOFO:
                std::sort(available_lots.begin(), available_lots.end(),
                    [](const auto& a, const auto& b) { return a.cost_basis < b.cost_basis; });
                break;
            case HarvestStrategy::MinTax:
                // Sell long-term gains first (lower rate), then short-term losses
                std::sort(available_lots.begin(), available_lots.end(),
                    [](const auto& a, const auto& b) {
                        if (a.long_term != b.long_term) return a.long_term; // LT first
                        return a.cost_basis > b.cost_basis; // highest cost first
                    });
                break;
            default: break;
        }

        // Select lots to fill quantity
        std::vector<HarvestLot> selected;
        double remaining = sell_quantity;
        for (auto& lot : available_lots) {
            if (remaining <= 0) break;
            HarvestLot sell_lot = lot;
            if (lot.quantity <= remaining) {
                selected.push_back(sell_lot);
                remaining -= lot.quantity;
            } else {
                sell_lot.quantity = remaining;
                sell_lot.total_cost = remaining * lot.cost_basis;
                selected.push_back(sell_lot);
                remaining = 0;
            }
        }
        return selected;
    }

    /** Calculate tax impact of selling lots at a given price */
    [[nodiscard]] TaxSellResult calculate_tax(
            const std::vector<HarvestLot>& lots, double sell_price) const {
        TaxSellResult result;
        for (const auto& lot : lots) {
            double proceeds = lot.quantity * sell_price;
            double cost = lot.quantity * lot.cost_basis;
            double gain = proceeds - cost;
            result.total_proceeds += proceeds;
            result.total_cost_basis += cost;
            result.realized_gain_loss += gain;
            if (lot.long_term)
                result.long_term_gain += gain;
            else
                result.short_term_gain += gain;
            result.lots_sold.push_back(lot);
        }
        result.estimated_tax = result.short_term_gain * short_term_rate_
                             + result.long_term_gain * long_term_rate_;
        return result;
    }

    /** Check for wash sale violation */
    [[nodiscard]] WashSaleCheck check_wash_sale(
            const std::string& security_id,
            const std::string& sell_date,
            const std::vector<std::pair<std::string, std::string>>& trade_history) const {
        // trade_history: vector of (date, "Buy"/"Sell")
        WashSaleCheck result;
        result.security_id = security_id;
        result.sell_date = sell_date;

        for (const auto& [date, side] : trade_history) {
            if (side != "Buy") continue;
            int days = date_diff_days(sell_date, date);
            if (std::abs(days) <= wash_sale_window_) {
                result.is_wash_sale = true;
                result.triggering_date = date;
                result.reason = "Buy on " + date + " is within " + std::to_string(wash_sale_window_)
                              + " days of sell on " + sell_date;
                break;
            }
        }
        return result;
    }

    /** Identify harvest candidates from positions */
    [[nodiscard]] std::vector<HarvestCandidate> find_harvest_candidates(
            const std::map<std::string, std::vector<HarvestLot>>& positions,
            const std::map<std::string, double>& current_prices,
            double min_loss = 100.0) const {

        std::vector<HarvestCandidate> candidates;
        for (const auto& [sec_id, lots] : positions) {
            auto price_it = current_prices.find(sec_id);
            if (price_it == current_prices.end()) continue;
            double current_px = price_it->second;

            double total_qty = 0, total_cost = 0;
            for (const auto& lot : lots) {
                total_qty += lot.quantity;
                total_cost += lot.quantity * lot.cost_basis;
            }
            double current_value = total_qty * current_px;
            double unrealized = current_value - total_cost;

            if (unrealized < -min_loss) {
                HarvestCandidate hc;
                hc.security_id = sec_id;
                hc.quantity = total_qty;
                hc.cost_basis = (total_qty > 0) ? total_cost / total_qty : 0;
                hc.current_price = current_px;
                hc.unrealized_loss = unrealized;
                hc.tax_savings = -unrealized * short_term_rate_; // max savings at ST rate
                candidates.push_back(hc);
            }
        }

        // Sort by largest tax savings
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.tax_savings > b.tax_savings; });
        return candidates;
    }

    /** Calculate after-tax return */
    [[nodiscard]] double after_tax_return(double pre_tax_return, double holding_period_years) const {
        double rate = (holding_period_years >= 1.0) ? long_term_rate_ : short_term_rate_;
        if (pre_tax_return > 0) return pre_tax_return * (1.0 - rate);
        return pre_tax_return; // losses not reduced by tax
    }

private:
    static int date_diff_days(const std::string& d1, const std::string& d2) {
        int y1, m1, day1, y2, m2, day2;
        std::sscanf(d1.c_str(), "%d-%d-%d", &y1, &m1, &day1);
        std::sscanf(d2.c_str(), "%d-%d-%d", &y2, &m2, &day2);

        std::tm t1{}, t2{};
        t1.tm_year = y1 - 1900; t1.tm_mon = m1 - 1; t1.tm_mday = day1;
        t2.tm_year = y2 - 1900; t2.tm_mon = m2 - 1; t2.tm_mday = day2;
        auto tp1 = std::chrono::system_clock::from_time_t(std::mktime(&t1));
        auto tp2 = std::chrono::system_clock::from_time_t(std::mktime(&t2));
        auto diff = std::chrono::duration_cast<std::chrono::hours>(tp2 - tp1).count();
        return static_cast<int>(diff / 24);
    }
};

} // namespace genie::portfolio

#endif // GENIE_PORTFOLIO_TAX_OPTIMIZATION_HPP
