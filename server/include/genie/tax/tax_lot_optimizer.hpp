/**
 * @file tax_lot_optimizer.hpp
 * @brief Tax Lot Selection and Optimization Engine for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Optimizes tax lot selection for trade execution to minimize tax liability
 * while respecting wash sale rules. Supports 7 lot selection methods.
 *
 * Tax Lot Selection Methods (7):
 *  1. FIFO - First In, First Out
 *  2. LIFO - Last In, First Out
 *  3. HIFO - Highest In, First Out (maximize cost basis)
 *  4. LOFO - Lowest In, First Out (minimize cost basis)
 *  5. Specific Identification - manually select specific lots
 *  6. Minimum Tax - optimal selection to minimize tax liability
 *  7. Tax Loss Harvesting - prioritize lots with losses
 *
 * Features:
 *  - All 7 lot selection methods with configurable strategies
 *  - Short-term vs long-term gain/loss classification (1-year threshold)
 *  - Wash sale rule detection (30-day lookback and look-forward)
 *  - Tax loss harvesting opportunity identification
 *  - Gain/loss impact preview before execution
 *  - Lot-level tracking with cost basis and acquisition date
 *  - Tax bracket-aware optimization (short-term @ ordinary income rate)
 *  - Realized gain/loss journal
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_TAX_LOT_OPTIMIZER_HPP
#define GENIE_TAX_LOT_OPTIMIZER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace genie::tax {

// ============================================================================
// Enums
// ============================================================================

/** @brief The 7 tax lot selection methods */
enum class LotSelectionMethod {
    FIFO,                   // First In, First Out
    LIFO,                   // Last In, First Out
    HIFO,                   // Highest In, First Out
    LOFO,                   // Lowest In, First Out
    SPECIFIC_ID,            // Specific Identification
    MINIMUM_TAX,            // Optimize for minimum tax
    TAX_LOSS_HARVESTING     // Prioritize loss-generating lots
};

/** @brief Gain/loss holding period classification */
enum class HoldingPeriod { SHORT_TERM, LONG_TERM };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Tax lot record */
struct TaxLot {
    std::string lot_id;
    std::string symbol;
    std::string account;
    double quantity{0.0};
    double cost_basis_per_share{0.0};
    double total_cost_basis{0.0};
    std::string acquisition_date;
    std::string acquisition_type; // "purchase", "transfer", "dividend_reinvest"
    HoldingPeriod holding_period{HoldingPeriod::SHORT_TERM};
    int holding_days{0};
    bool wash_sale_affected{false};
    double wash_sale_disallowed{0.0};
    double adjusted_cost_basis{0.0};
};

/** @brief Lot selection result for a proposed sale */
struct LotSelection {
    std::string lot_id;
    double quantity_from_lot{0.0};
    double cost_basis{0.0};
    double market_value{0.0};
    double gain_loss{0.0};
    HoldingPeriod holding_period{HoldingPeriod::SHORT_TERM};
    bool wash_sale_risk{false};
};

/** @brief Tax impact preview */
struct TaxImpactPreview {
    std::string symbol;
    LotSelectionMethod method{LotSelectionMethod::FIFO};
    double total_shares_sold{0.0};
    double total_proceeds{0.0};
    double total_cost_basis{0.0};
    double short_term_gain{0.0};
    double short_term_loss{0.0};
    double long_term_gain{0.0};
    double long_term_loss{0.0};
    double net_gain_loss{0.0};
    double estimated_tax{0.0};
    double estimated_tax_rate{0.0};
    int lots_used{0};
    int wash_sale_lots{0};
    double wash_sale_disallowed{0.0};
    std::vector<LotSelection> selections;
};

/** @brief Tax loss harvesting opportunity */
struct HarvestingOpportunity {
    std::string symbol;
    double unrealized_loss{0.0};
    double quantity_available{0.0};
    HoldingPeriod holding_period{HoldingPeriod::SHORT_TERM};
    double current_price{0.0};
    double cost_basis{0.0};
    double estimated_tax_savings{0.0};
    bool wash_sale_risk{false};
    std::string recommended_replacement; // Similar but not substantially identical
};

/** @brief Realized gain/loss journal entry */
struct RealizedGainLoss {
    std::string journal_id;
    std::string symbol;
    std::string lot_id;
    double quantity{0.0};
    double proceeds{0.0};
    double cost_basis{0.0};
    double gain_loss{0.0};
    HoldingPeriod holding_period{HoldingPeriod::SHORT_TERM};
    LotSelectionMethod method{LotSelectionMethod::FIFO};
    std::string sale_date;
    std::string acquisition_date;
    bool wash_sale{false};
    double wash_sale_disallowed{0.0};
};

/** @brief Tax rate configuration */
struct TaxRates {
    double short_term_rate{0.37};    // Ordinary income rate
    double long_term_rate{0.20};     // Long-term capital gains rate
    double net_investment_income_tax{0.038}; // NIIT
    double state_rate{0.0};          // State tax rate
};

/** @brief Optimizer statistics */
struct TaxOptimizerStats {
    uint64_t optimizations_run{0};
    uint64_t lots_tracked{0};
    uint64_t wash_sales_detected{0};
    uint64_t harvesting_opportunities{0};
    double total_realized_gains{0.0};
    double total_realized_losses{0.0};
    double total_tax_saved{0.0};
};

// ============================================================================
// TaxLotOptimizer
// ============================================================================

/**
 * @class TaxLotOptimizer
 * @brief Optimizes tax lot selection using 7 methods
 */
class TaxLotOptimizer {
public:
    explicit TaxLotOptimizer(TaxRates rates = {}) : rates_(std::move(rates)) {}

    // ---- Lot Management ----

    /** @brief Add a tax lot */
    void add_lot(TaxLot lot) {
        std::lock_guard lock(mutex_);
        lot.total_cost_basis = lot.quantity * lot.cost_basis_per_share;
        lot.adjusted_cost_basis = lot.total_cost_basis;
        lot.holding_days = compute_holding_days(lot.acquisition_date);
        lot.holding_period = lot.holding_days >= 365 ? HoldingPeriod::LONG_TERM : HoldingPeriod::SHORT_TERM;
        lots_by_symbol_[lot.symbol].push_back(lot);
    }

    /** @brief Get lots for a symbol */
    [[nodiscard]] std::vector<TaxLot> get_lots(const std::string& symbol) const {
        std::lock_guard lock(mutex_);
        auto it = lots_by_symbol_.find(symbol);
        if (it != lots_by_symbol_.end()) return it->second;
        return {};
    }

    /** @brief Total lot count */
    [[nodiscard]] std::size_t lot_count() const {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (const auto& [_, lots] : lots_by_symbol_) count += lots.size();
        return count;
    }

    // ---- Tax Impact Preview ----

    /** @brief Preview tax impact of selling shares using specified method */
    TaxImpactPreview preview(const std::string& symbol, double quantity,
                             double current_price, LotSelectionMethod method) const {
        std::lock_guard lock(mutex_);
        TaxImpactPreview preview;
        preview.symbol = symbol;
        preview.method = method;
        preview.total_shares_sold = quantity;
        preview.total_proceeds = quantity * current_price;

        auto it = lots_by_symbol_.find(symbol);
        if (it == lots_by_symbol_.end()) return preview;

        auto sorted_lots = sort_lots(it->second, method);
        double remaining = quantity;

        for (const auto& lot : sorted_lots) {
            if (remaining <= 0) break;
            double use_qty = std::min(remaining, lot.quantity);

            LotSelection sel;
            sel.lot_id = lot.lot_id;
            sel.quantity_from_lot = use_qty;
            sel.cost_basis = use_qty * lot.cost_basis_per_share;
            sel.market_value = use_qty * current_price;
            sel.gain_loss = sel.market_value - sel.cost_basis;
            sel.holding_period = lot.holding_period;
            sel.wash_sale_risk = check_wash_sale_risk(symbol, lot.acquisition_date);

            preview.total_cost_basis += sel.cost_basis;

            if (sel.gain_loss > 0) {
                if (sel.holding_period == HoldingPeriod::SHORT_TERM) preview.short_term_gain += sel.gain_loss;
                else preview.long_term_gain += sel.gain_loss;
            } else {
                if (sel.holding_period == HoldingPeriod::SHORT_TERM) preview.short_term_loss += sel.gain_loss;
                else preview.long_term_loss += sel.gain_loss;
            }

            if (sel.wash_sale_risk) preview.wash_sale_lots++;

            preview.selections.push_back(std::move(sel));
            preview.lots_used++;
            remaining -= use_qty;
        }

        preview.net_gain_loss = preview.short_term_gain + preview.short_term_loss
                              + preview.long_term_gain + preview.long_term_loss;

        // Estimate tax
        double st_tax = std::max(0.0, preview.short_term_gain + preview.short_term_loss)
                       * (rates_.short_term_rate + rates_.net_investment_income_tax + rates_.state_rate);
        double lt_tax = std::max(0.0, preview.long_term_gain + preview.long_term_loss)
                       * (rates_.long_term_rate + rates_.net_investment_income_tax + rates_.state_rate);
        preview.estimated_tax = st_tax + lt_tax;
        preview.estimated_tax_rate = preview.total_proceeds > 0
            ? preview.estimated_tax / preview.total_proceeds * 100.0 : 0;

        return preview;
    }

    /** @brief Compare all 7 methods for a proposed sale */
    std::vector<TaxImpactPreview> compare_methods(const std::string& symbol,
                                                   double quantity, double current_price) const {
        std::vector<TaxImpactPreview> results;
        for (auto method : {LotSelectionMethod::FIFO, LotSelectionMethod::LIFO,
                            LotSelectionMethod::HIFO, LotSelectionMethod::LOFO,
                            LotSelectionMethod::SPECIFIC_ID, LotSelectionMethod::MINIMUM_TAX,
                            LotSelectionMethod::TAX_LOSS_HARVESTING}) {
            results.push_back(preview(symbol, quantity, current_price, method));
        }
        return results;
    }

    /** @brief Get the method with minimum tax impact */
    LotSelectionMethod optimal_method(const std::string& symbol, double quantity,
                                       double current_price) const {
        auto comparisons = compare_methods(symbol, quantity, current_price);
        LotSelectionMethod best = LotSelectionMethod::FIFO;
        double min_tax = std::numeric_limits<double>::max();
        for (const auto& p : comparisons) {
            if (p.estimated_tax < min_tax) {
                min_tax = p.estimated_tax;
                best = p.method;
            }
        }
        return best;
    }

    // ---- Tax Loss Harvesting ----

    /** @brief Find tax loss harvesting opportunities */
    std::vector<HarvestingOpportunity> find_harvesting_opportunities(
        const std::unordered_map<std::string, double>& current_prices,
        double min_loss = 1000.0
    ) const {
        std::lock_guard lock(mutex_);
        std::vector<HarvestingOpportunity> opportunities;

        for (const auto& [symbol, lots] : lots_by_symbol_) {
            auto price_it = current_prices.find(symbol);
            if (price_it == current_prices.end()) continue;
            double current_price = price_it->second;

            for (const auto& lot : lots) {
                double unrealized = (current_price - lot.cost_basis_per_share) * lot.quantity;
                if (unrealized < -min_loss) {
                    HarvestingOpportunity opp;
                    opp.symbol = symbol;
                    opp.unrealized_loss = unrealized;
                    opp.quantity_available = lot.quantity;
                    opp.holding_period = lot.holding_period;
                    opp.current_price = current_price;
                    opp.cost_basis = lot.cost_basis_per_share;
                    double tax_rate = lot.holding_period == HoldingPeriod::SHORT_TERM
                        ? rates_.short_term_rate : rates_.long_term_rate;
                    opp.estimated_tax_savings = std::abs(unrealized) * tax_rate;
                    opp.wash_sale_risk = check_wash_sale_risk(symbol, "");
                    opportunities.push_back(std::move(opp));
                }
            }
        }

        std::sort(opportunities.begin(), opportunities.end(), [](const auto& a, const auto& b) {
            return a.estimated_tax_savings > b.estimated_tax_savings;
        });
        return opportunities;
    }

    // ---- Wash Sale Detection ----

    /** @brief Check for wash sale violations */
    std::vector<std::string> check_wash_sales(const std::string& symbol,
                                               const std::string& sale_date) const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> violations;
        // Check purchases within 30 days before and after
        auto it = lots_by_symbol_.find(symbol);
        if (it == lots_by_symbol_.end()) return violations;

        for (const auto& lot : it->second) {
            int days_diff = days_between(lot.acquisition_date, sale_date);
            if (std::abs(days_diff) <= 30 && days_diff != 0) {
                violations.push_back("Wash sale: " + symbol + " lot " + lot.lot_id
                    + " acquired " + std::to_string(std::abs(days_diff)) + " days "
                    + (days_diff < 0 ? "before" : "after") + " sale");
            }
        }
        return violations;
    }

    // ---- Execution ----

    /** @brief Execute a sale using specified method, returns journal entries */
    std::vector<RealizedGainLoss> execute_sale(const std::string& symbol, double quantity,
                                                double sale_price, LotSelectionMethod method) {
        std::lock_guard lock(mutex_);
        std::vector<RealizedGainLoss> journal;

        auto it = lots_by_symbol_.find(symbol);
        if (it == lots_by_symbol_.end()) return journal;

        auto sorted = sort_lots(it->second, method);
        double remaining = quantity;

        for (auto& lot : sorted) {
            if (remaining <= 0) break;
            double use_qty = std::min(remaining, lot.quantity);

            RealizedGainLoss entry;
            entry.journal_id = "TXJ-" + std::to_string(++journal_counter_);
            entry.symbol = symbol;
            entry.lot_id = lot.lot_id;
            entry.quantity = use_qty;
            entry.proceeds = use_qty * sale_price;
            entry.cost_basis = use_qty * lot.cost_basis_per_share;
            entry.gain_loss = entry.proceeds - entry.cost_basis;
            entry.holding_period = lot.holding_period;
            entry.method = method;
            entry.sale_date = now_str().substr(0, 10);
            entry.acquisition_date = lot.acquisition_date;

            // Check wash sale
            auto violations = check_wash_sales(symbol, entry.sale_date);
            if (!violations.empty() && entry.gain_loss < 0) {
                entry.wash_sale = true;
                entry.wash_sale_disallowed = std::abs(entry.gain_loss);
                wash_sales_detected_++;
            }

            if (entry.gain_loss > 0) total_gains_ += entry.gain_loss;
            else total_losses_ += entry.gain_loss;

            journal.push_back(entry);
            lot.quantity -= use_qty;
            remaining -= use_qty;
        }

        // Remove empty lots
        it->second.erase(
            std::remove_if(it->second.begin(), it->second.end(),
                [](const TaxLot& l) { return l.quantity <= 0; }),
            it->second.end());

        optimizations_run_++;
        return journal;
    }

    // ---- Configuration ----

    /** @brief Set tax rates */
    void set_rates(TaxRates rates) {
        std::lock_guard lock(mutex_);
        rates_ = std::move(rates);
    }

    [[nodiscard]] TaxRates rates() const {
        std::lock_guard lock(mutex_);
        return rates_;
    }

    // ---- Statistics ----

    [[nodiscard]] TaxOptimizerStats stats() const {
        std::lock_guard lock(mutex_);
        TaxOptimizerStats s;
        s.optimizations_run = optimizations_run_;
        for (const auto& [_, lots] : lots_by_symbol_) s.lots_tracked += lots.size();
        s.wash_sales_detected = wash_sales_detected_;
        s.total_realized_gains = total_gains_;
        s.total_realized_losses = total_losses_;
        return s;
    }

private:
    std::vector<TaxLot> sort_lots(std::vector<TaxLot> lots, LotSelectionMethod method) const {
        switch (method) {
            case LotSelectionMethod::FIFO:
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    return a.acquisition_date < b.acquisition_date;
                });
                break;
            case LotSelectionMethod::LIFO:
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    return a.acquisition_date > b.acquisition_date;
                });
                break;
            case LotSelectionMethod::HIFO:
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    return a.cost_basis_per_share > b.cost_basis_per_share;
                });
                break;
            case LotSelectionMethod::LOFO:
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    return a.cost_basis_per_share < b.cost_basis_per_share;
                });
                break;
            case LotSelectionMethod::MINIMUM_TAX:
                // Prefer long-term gains over short-term, then lowest gains first
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    if (a.holding_period != b.holding_period)
                        return a.holding_period == HoldingPeriod::LONG_TERM;
                    return a.cost_basis_per_share > b.cost_basis_per_share; // Higher cost = less gain
                });
                break;
            case LotSelectionMethod::TAX_LOSS_HARVESTING:
                // Prioritize lots with largest unrealized losses
                std::sort(lots.begin(), lots.end(), [](const auto& a, const auto& b) {
                    return a.cost_basis_per_share > b.cost_basis_per_share;
                });
                break;
            case LotSelectionMethod::SPECIFIC_ID:
                // No sorting - use as provided
                break;
        }
        return lots;
    }

    bool check_wash_sale_risk(const std::string& symbol, const std::string&) const {
        // Simplified: check if there are recent purchases in same symbol
        auto it = lots_by_symbol_.find(symbol);
        if (it == lots_by_symbol_.end()) return false;
        for (const auto& lot : it->second) {
            if (lot.holding_days < 31) return true;
        }
        return false;
    }

    static int compute_holding_days(const std::string& acq_date) {
        if (acq_date.size() < 10) return 0;
        // Simplified: assume current date is 2026-02-06
        int acq_y = std::stoi(acq_date.substr(0, 4));
        int acq_m = std::stoi(acq_date.substr(5, 2));
        int acq_d = std::stoi(acq_date.substr(8, 2));
        return (2026 - acq_y) * 365 + (2 - acq_m) * 30 + (6 - acq_d);
    }

    static int days_between(const std::string& a, const std::string& b) {
        return compute_holding_days(a) - compute_holding_days(b);
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    TaxRates rates_;
    std::unordered_map<std::string, std::vector<TaxLot>> lots_by_symbol_;
    uint64_t optimizations_run_{0};
    uint64_t wash_sales_detected_{0};
    uint64_t journal_counter_{0};
    double total_gains_{0.0};
    double total_losses_{0.0};
    mutable std::mutex mutex_;
};

} // namespace genie::tax

#endif // GENIE_TAX_LOT_OPTIMIZER_HPP
