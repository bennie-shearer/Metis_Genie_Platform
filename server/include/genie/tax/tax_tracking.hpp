/**
 * @file tax_tracking.hpp
 * @brief Tax lot tracking and cost basis calculations
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Comprehensive tax tracking functionality:
 * - FIFO cost basis calculation
 * - Wash sale detection (30-day window)
 * - Tax lot inventory management
 * - Realized gain/loss tracking
 * - Holding period classification (short/long-term)
 * - Form 8949 data preparation
 */
#pragma once
#ifndef GENIE_TAX_TAX_TRACKING_HPP
#define GENIE_TAX_TAX_TRACKING_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace genie::tax {

// ============================================================================
// Date Utilities
// ============================================================================

inline std::tm make_tm(int year, int month, int day) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12; // Noon to avoid DST edge cases
    t.tm_isdst = -1;
    return t;
}

inline bool parse_ymd(const std::string& d, int& y, int& m, int& day) {
    if (d.size() < 10) return false;
    y = std::stoi(d.substr(0, 4));
    m = std::stoi(d.substr(5, 2));
    day = std::stoi(d.substr(8, 2));
    return true;
}

inline int days_between(const std::string& date1, const std::string& date2) {
    // Proper date difference using std::mktime (YYYY-MM-DD format)
    int y1, m1, d1, y2, m2, d2;
    if (!parse_ymd(date1, y1, m1, d1) || !parse_ymd(date2, y2, m2, d2)) return 0;
    
    std::tm t1 = make_tm(y1, m1, d1);
    std::tm t2 = make_tm(y2, m2, d2);
    
    std::time_t time1 = std::mktime(&t1);
    std::time_t time2 = std::mktime(&t2);
    
    if (time1 == -1 || time2 == -1) return 0;
    
    double diff_secs = std::difftime(time2, time1);
    return static_cast<int>(std::round(diff_secs / 86400.0));
}

/**
 * @brief Check if sale_date is more than one calendar year after acquisition_date.
 * Uses calendar comparison (not day count) per IRS rules.
 * Acquired Jan 15 2024, sold Jan 16 2025 = long-term (> 1 year).
 * Acquired Jan 15 2024, sold Jan 15 2025 = long-term (>= 1 year from day after).
 * 
 * IRS rule: holding period begins day after acquisition. Long-term if held
 * more than one year. In practice, selling on the same date the following
 * year qualifies as long-term.
 */
inline bool is_more_than_one_year(const std::string& acquisition_date, 
                                   const std::string& sale_date) {
    int ay, am, ad, sy, sm, sd;
    if (!parse_ymd(acquisition_date, ay, am, ad) || 
        !parse_ymd(sale_date, sy, sm, sd)) return false;
    
    // Compare sale date against acquisition date + 1 year
    // Long-term if sale_date >= (acquisition_year+1, acquisition_month, acquisition_day)
    int threshold_y = ay + 1;
    int threshold_m = am;
    int threshold_d = ad;
    
    if (sy > threshold_y) return true;
    if (sy < threshold_y) return false;
    // sy == threshold_y
    if (sm > threshold_m) return true;
    if (sm < threshold_m) return false;
    // sm == threshold_m
    return sd >= threshold_d;
}

inline std::string add_days(const std::string& date, int days) {
    if (date.size() < 10) return date;
    
    int y, m, d;
    if (!parse_ymd(date, y, m, d)) return date;
    
    std::tm t = make_tm(y, m, d);
    t.tm_mday += days;
    
    // mktime normalizes overflow/underflow (e.g., day 35 of January -> Feb 4)
    std::time_t result = std::mktime(&t);
    if (result == -1) return date;
    
    std::ostringstream oss;
    oss << std::put_time(&t, "%Y-%m-%d");
    return oss.str();
}

inline std::string today_string() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d");
    return oss.str();
}

// ============================================================================
// Tax Lot
// ============================================================================

/**
 * @brief Holding period classification
 */
enum class HoldingPeriod {
    ShortTerm,        // <= 1 year
    LongTerm          // > 1 year
};

inline std::string holding_period_to_string(HoldingPeriod hp) {
    return hp == HoldingPeriod::ShortTerm ? "Short-Term" : "Long-Term";
}

/**
 * @brief Wash sale adjustment type
 */
enum class WashSaleType {
    None,
    Full,             // Entire loss disallowed
    Partial           // Partial loss disallowed
};

/**
 * @brief Individual tax lot
 */
struct TaxLotRecord {
    std::string id;
    std::string symbol;
    std::string acquisition_date;
    double shares{0};
    double cost_per_share{0};
    double total_cost{0};
    
    // Wash sale adjustments
    double wash_sale_adjustment{0};   // Amount added to cost basis
    bool has_wash_sale{false};
    std::string wash_sale_from_lot;   // Lot that triggered wash sale
    
    // Status
    bool is_open{true};               // Still held
    std::string close_date;
    double proceeds_per_share{0};
    double total_proceeds{0};
    
    // Calculated
    double adjusted_cost() const {
        return total_cost + wash_sale_adjustment;
    }
    
    double adjusted_cost_per_share() const {
        return shares > 0 ? adjusted_cost() / shares : 0;
    }
    
    HoldingPeriod holding_period(const std::string& sale_date = "") const {
        std::string ref_date = sale_date.empty() ? today_string() : sale_date;
        return is_more_than_one_year(acquisition_date, ref_date) ? 
            HoldingPeriod::LongTerm : HoldingPeriod::ShortTerm;
    }
    
    double gain_loss() const {
        if (!is_open && total_proceeds > 0) {
            return total_proceeds - adjusted_cost();
        }
        return 0;
    }
    
    double gain_loss_pct() const {
        if (adjusted_cost() > 0) {
            return (gain_loss() / adjusted_cost()) * 100;
        }
        return 0;
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Lot " << id.substr(0, 8) << ": " << shares << " shares @ $" << cost_per_share;
        oss << " (" << acquisition_date << ")";
        if (has_wash_sale) {
            oss << " [Wash +$" << wash_sale_adjustment << "]";
        }
        if (!is_open) {
            oss << " -> CLOSED $" << gain_loss();
        }
        return oss.str();
    }
};

// ============================================================================
// Realized Gain/Loss Record
// ============================================================================

/**
 * @brief Realized gain/loss from a sale
 */
struct RealizedGainLoss {
    std::string id;
    std::string symbol;
    std::string lot_id;
    std::string acquisition_date;
    std::string sale_date;
    double shares{0};
    double cost_basis{0};
    double proceeds{0};
    double gain_loss{0};
    HoldingPeriod holding_period;
    
    // Wash sale
    WashSaleType wash_sale_type{WashSaleType::None};
    double wash_sale_disallowed{0};   // Loss amount disallowed
    double wash_sale_allowed{0};      // Loss amount allowed
    std::string wash_sale_lot_id;     // New lot with adjusted basis
    
    // For Form 8949
    std::string adjustment_code;
    double adjustment_amount{0};
    
    double taxable_gain() const {
        if (wash_sale_type != WashSaleType::None) {
            return gain_loss + wash_sale_disallowed;  // Reduces loss
        }
        return gain_loss;
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << symbol << " " << shares << " shares: ";
        oss << "Acquired " << acquisition_date << ", Sold " << sale_date << "\n";
        oss << "  Cost: $" << cost_basis << ", Proceeds: $" << proceeds << "\n";
        oss << "  " << holding_period_to_string(holding_period) << " ";
        oss << (gain_loss >= 0 ? "Gain" : "Loss") << ": $" << std::abs(gain_loss);
        if (wash_sale_type != WashSaleType::None) {
            oss << "\n  Wash Sale: $" << wash_sale_disallowed << " disallowed";
        }
        return oss.str();
    }
};

// ============================================================================
// Cost Basis Method
// ============================================================================

/**
 * @brief Cost basis calculation method
 */
enum class CostBasisMethod {
    FIFO,             // First In, First Out (default)
    LIFO,             // Last In, First Out
    SpecificID,       // Specific lot identification
    HighCost,         // Highest cost first
    LowCost,          // Lowest cost first
    AverageCost       // Average cost (for mutual funds)
};

inline std::string cost_basis_method_to_string(CostBasisMethod method) {
    switch (method) {
        case CostBasisMethod::FIFO: return "FIFO";
        case CostBasisMethod::LIFO: return "LIFO";
        case CostBasisMethod::SpecificID: return "Specific ID";
        case CostBasisMethod::HighCost: return "Highest Cost";
        case CostBasisMethod::LowCost: return "Lowest Cost";
        case CostBasisMethod::AverageCost: return "Average Cost";
        default: return "Unknown";
    }
}

// ============================================================================
// Tax Lot Inventory
// ============================================================================

/**
 * @brief Manages tax lots for a symbol
 */
class TaxLotInventory {
public:
    explicit TaxLotInventory(const std::string& symbol)
        : symbol_(symbol) {}
    
    /**
     * @brief Add shares (purchase)
     */
    TaxLotRecord add_lot(double shares, double price_per_share,
                   const std::string& date,
                   double wash_sale_adjustment = 0) {
        
        TaxLotRecord lot;
        lot.id = generate_lot_id();
        lot.symbol = symbol_;
        lot.acquisition_date = date;
        lot.shares = shares;
        lot.cost_per_share = price_per_share;
        lot.total_cost = shares * price_per_share;
        lot.wash_sale_adjustment = wash_sale_adjustment;
        lot.has_wash_sale = wash_sale_adjustment > 0;
        lot.is_open = true;
        
        lots_.push_back(lot);
        
        return lot;
    }
    
    /**
     * @brief Remove shares (sale) using specified method
     */
    std::vector<RealizedGainLoss> remove_shares(
        double shares,
        double price_per_share,
        const std::string& date,
        CostBasisMethod method = CostBasisMethod::FIFO) {
        
        std::vector<RealizedGainLoss> realized;
        double remaining = shares;
        
        // Sort lots based on method
        auto sorted_lots = get_sorted_lots(method);
        
        for (auto& lot : sorted_lots) {
            if (remaining <= 0) break;
            if (!lot->is_open || lot->shares <= 0) continue;
            
            double to_sell = std::min(remaining, lot->shares);
            
            // Create realized gain/loss record
            RealizedGainLoss gl;
            gl.id = generate_gl_id();
            gl.symbol = symbol_;
            gl.lot_id = lot->id;
            gl.acquisition_date = lot->acquisition_date;
            gl.sale_date = date;
            gl.shares = to_sell;
            gl.cost_basis = to_sell * lot->adjusted_cost_per_share();
            gl.proceeds = to_sell * price_per_share;
            gl.gain_loss = gl.proceeds - gl.cost_basis;
            gl.holding_period = lot->holding_period(date);
            
            realized.push_back(gl);
            
            // Update lot
            if (to_sell >= lot->shares) {
                lot->is_open = false;
                lot->close_date = date;
                lot->proceeds_per_share = price_per_share;
                lot->total_proceeds = lot->shares * price_per_share;
            } else {
                // Partial sale - split the lot
                double remaining_shares = lot->shares - to_sell;
                lot->shares = remaining_shares;
                lot->total_cost = remaining_shares * lot->cost_per_share;
                // Adjust wash sale proportionally
                if (lot->wash_sale_adjustment > 0) {
                    double ratio = remaining_shares / (remaining_shares + to_sell);
                    lot->wash_sale_adjustment *= ratio;
                }
            }
            
            remaining -= to_sell;
        }
        
        // Store realized gains
        for (const auto& gl : realized) {
            realized_gains_.push_back(gl);
        }
        
        return realized;
    }
    
    /**
     * @brief Get open lots
     */
    std::vector<TaxLotRecord> get_open_lots() const {
        std::vector<TaxLotRecord> open;
        for (const auto& lot : lots_) {
            if (lot.is_open && lot.shares > 0) {
                open.push_back(lot);
            }
        }
        return open;
    }
    
    /**
     * @brief Get closed lots
     */
    std::vector<TaxLotRecord> get_closed_lots() const {
        std::vector<TaxLotRecord> closed;
        for (const auto& lot : lots_) {
            if (!lot.is_open) {
                closed.push_back(lot);
            }
        }
        return closed;
    }
    
    /**
     * @brief Get all lots
     */
    const std::vector<TaxLotRecord>& all_lots() const { return lots_; }
    
    /**
     * @brief Get realized gains/losses
     */
    const std::vector<RealizedGainLoss>& realized_gains() const { return realized_gains_; }
    
    /**
     * @brief Calculate total position
     */
    double total_shares() const {
        double total = 0;
        for (const auto& lot : lots_) {
            if (lot.is_open) total += lot.shares;
        }
        return total;
    }
    
    /**
     * @brief Calculate total cost basis
     */
    double total_cost_basis() const {
        double total = 0;
        for (const auto& lot : lots_) {
            if (lot.is_open) total += lot.adjusted_cost();
        }
        return total;
    }
    
    /**
     * @brief Calculate average cost per share
     */
    double average_cost() const {
        double shares = total_shares();
        return shares > 0 ? total_cost_basis() / shares : 0;
    }
    
    /**
     * @brief Get specific lot by ID
     */
    TaxLotRecord* get_lot(const std::string& lot_id) {
        for (auto& lot : lots_) {
            if (lot.id == lot_id) return &lot;
        }
        return nullptr;
    }
    
    const std::string& symbol() const { return symbol_; }

private:
    std::string symbol_;
    std::vector<TaxLotRecord> lots_;
    std::vector<RealizedGainLoss> realized_gains_;
    int lot_counter_{0};
    int gl_counter_{0};
    
    std::string generate_lot_id() {
        std::ostringstream oss;
        oss << symbol_ << "-LOT-" << std::setfill('0') << std::setw(6) << ++lot_counter_;
        return oss.str();
    }
    
    std::string generate_gl_id() {
        std::ostringstream oss;
        oss << symbol_ << "-GL-" << std::setfill('0') << std::setw(6) << ++gl_counter_;
        return oss.str();
    }
    
    std::vector<TaxLotRecord*> get_sorted_lots(CostBasisMethod method) {
        std::vector<TaxLotRecord*> sorted;
        for (auto& lot : lots_) {
            if (lot.is_open && lot.shares > 0) {
                sorted.push_back(&lot);
            }
        }
        
        switch (method) {
            case CostBasisMethod::FIFO:
                std::sort(sorted.begin(), sorted.end(),
                    [](TaxLotRecord* a, TaxLotRecord* b) {
                        return a->acquisition_date < b->acquisition_date;
                    });
                break;
                
            case CostBasisMethod::LIFO:
                std::sort(sorted.begin(), sorted.end(),
                    [](TaxLotRecord* a, TaxLotRecord* b) {
                        return a->acquisition_date > b->acquisition_date;
                    });
                break;
                
            case CostBasisMethod::HighCost:
                std::sort(sorted.begin(), sorted.end(),
                    [](TaxLotRecord* a, TaxLotRecord* b) {
                        return a->adjusted_cost_per_share() > b->adjusted_cost_per_share();
                    });
                break;
                
            case CostBasisMethod::LowCost:
                std::sort(sorted.begin(), sorted.end(),
                    [](TaxLotRecord* a, TaxLotRecord* b) {
                        return a->adjusted_cost_per_share() < b->adjusted_cost_per_share();
                    });
                break;
                
            default:
                // FIFO default
                std::sort(sorted.begin(), sorted.end(),
                    [](TaxLotRecord* a, TaxLotRecord* b) {
                        return a->acquisition_date < b->acquisition_date;
                    });
                break;
        }
        
        return sorted;
    }
};

// ============================================================================
// Wash Sale Detector
// ============================================================================

/**
 * @brief Wash sale detection result
 */
struct WashSaleResult {
    bool is_wash_sale{false};
    std::string disallowing_lot_id;
    double disallowed_loss{0};
    std::string replacement_lot_id;
    std::string explanation;
};

/**
 * @brief Detects wash sales (30-day rule)
 */
class WashSaleDetector {
public:
    static constexpr int WASH_SALE_WINDOW = 30;  // Days before and after
    
    /**
     * @brief Check if a sale triggers wash sale rules
     */
    static WashSaleResult check_wash_sale(
        const RealizedGainLoss& sale,
        const std::vector<TaxLotRecord>& all_lots) {
        
        WashSaleResult result;
        
        // Only losses can be wash sales
        if (sale.gain_loss >= 0) {
            return result;
        }
        
        std::string window_start = add_days(sale.sale_date, -WASH_SALE_WINDOW);
        std::string window_end = add_days(sale.sale_date, WASH_SALE_WINDOW);
        
        // Look for substantially identical securities purchased in window
        for (const auto& lot : all_lots) {
            if (lot.id == sale.lot_id) continue;
            if (lot.symbol != sale.symbol) continue;
            
            // Check if lot was acquired within wash sale window
            if (lot.acquisition_date >= window_start && 
                lot.acquisition_date <= window_end) {
                
                result.is_wash_sale = true;
                result.disallowing_lot_id = lot.id;
                result.replacement_lot_id = lot.id;
                
                // Disallowed loss = min(loss, replacement shares * loss per share)
                double loss_per_share = std::abs(sale.gain_loss) / sale.shares;
                double max_disallowed = lot.shares * loss_per_share;
                result.disallowed_loss = std::min(std::abs(sale.gain_loss), max_disallowed);
                
                std::ostringstream oss;
                oss << "Wash sale: repurchased " << lot.shares << " shares on "
                    << lot.acquisition_date << " (within 30 days of sale on "
                    << sale.sale_date << "). $" << std::fixed << std::setprecision(2)
                    << result.disallowed_loss << " loss disallowed.";
                result.explanation = oss.str();
                
                break;  // Found a wash sale
            }
        }
        
        return result;
    }
    
    /**
     * @brief Process wash sale adjustment
     */
    static void apply_wash_sale(RealizedGainLoss& sale, 
                                TaxLotRecord& replacement_lot,
                                const WashSaleResult& result) {
        if (!result.is_wash_sale) return;
        
        sale.wash_sale_type = (result.disallowed_loss >= std::abs(sale.gain_loss)) ?
            WashSaleType::Full : WashSaleType::Partial;
        sale.wash_sale_disallowed = result.disallowed_loss;
        sale.wash_sale_allowed = std::abs(sale.gain_loss) - result.disallowed_loss;
        sale.wash_sale_lot_id = result.replacement_lot_id;
        sale.adjustment_code = "W";
        sale.adjustment_amount = result.disallowed_loss;
        
        // Adjust replacement lot's cost basis
        replacement_lot.wash_sale_adjustment += result.disallowed_loss;
        replacement_lot.has_wash_sale = true;
        replacement_lot.wash_sale_from_lot = sale.lot_id;
    }
};

// ============================================================================
// Tax Tracker
// ============================================================================

/**
 * @brief Year-to-date tax summary
 */
struct TaxSummary {
    int tax_year{0};
    
    // Short-term
    double short_term_gains{0};
    double short_term_losses{0};
    double short_term_wash_adjustments{0};
    int short_term_transactions{0};
    
    // Long-term
    double long_term_gains{0};
    double long_term_losses{0};
    double long_term_wash_adjustments{0};
    int long_term_transactions{0};
    
    // Totals
    double net_short_term() const {
        return short_term_gains + short_term_losses;
    }
    
    double net_long_term() const {
        return long_term_gains + long_term_losses;
    }
    
    double total_gains() const {
        return short_term_gains + long_term_gains;
    }
    
    double total_losses() const {
        return short_term_losses + long_term_losses;
    }
    
    double net_gain_loss() const {
        return total_gains() + total_losses();
    }
    
    int total_transactions() const {
        return short_term_transactions + long_term_transactions;
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Tax Summary for " << tax_year << ":\n";
        oss << "Short-Term:\n";
        oss << "  Gains: $" << short_term_gains << "\n";
        oss << "  Losses: $" << short_term_losses << "\n";
        oss << "  Net: $" << net_short_term() << "\n";
        oss << "  Transactions: " << short_term_transactions << "\n";
        oss << "Long-Term:\n";
        oss << "  Gains: $" << long_term_gains << "\n";
        oss << "  Losses: $" << long_term_losses << "\n";
        oss << "  Net: $" << net_long_term() << "\n";
        oss << "  Transactions: " << long_term_transactions << "\n";
        oss << "Total Net: $" << net_gain_loss() << "\n";
        return oss.str();
    }
};

/**
 * @brief Comprehensive tax tracker
 */
class TaxTracker {
public:
    TaxTracker(CostBasisMethod method = CostBasisMethod::FIFO,
               bool auto_wash_sale = true)
        : default_method_(method)
        , auto_wash_sale_(auto_wash_sale) {}
    
    /**
     * @brief Record a purchase
     */
    TaxLotRecord record_purchase(const std::string& symbol,
                           double shares,
                           double price_per_share,
                           const std::string& date) {
        
        auto& inventory = get_or_create_inventory(symbol);
        auto lot = inventory.add_lot(shares, price_per_share, date);
        
        // Check for wash sale from prior loss
        if (auto_wash_sale_ && !pending_losses_.empty()) {
            check_purchase_for_wash_sale(symbol, lot, date);
        }
        
        return lot;
    }
    
    /**
     * @brief Record a sale
     */
    std::vector<RealizedGainLoss> record_sale(
        const std::string& symbol,
        double shares,
        double price_per_share,
        const std::string& date,
        CostBasisMethod method = CostBasisMethod::FIFO) {
        
        auto it = inventories_.find(symbol);
        if (it == inventories_.end()) {
            return {};
        }
        
        CostBasisMethod use_method = (method == CostBasisMethod::FIFO) ? 
            default_method_ : method;
        
        auto gains = it->second.remove_shares(shares, price_per_share, date, use_method);
        
        // Process wash sales
        if (auto_wash_sale_) {
            for (auto& gl : gains) {
                process_wash_sale(gl);
            }
        }
        
        return gains;
    }
    
    /**
     * @brief Get inventory for symbol
     */
    TaxLotInventory* get_inventory(const std::string& symbol) {
        auto it = inventories_.find(symbol);
        return it != inventories_.end() ? &it->second : nullptr;
    }
    
    /**
     * @brief Get all inventories
     */
    std::vector<std::string> get_symbols() const {
        std::vector<std::string> symbols;
        for (const auto& [symbol, inv] : inventories_) {
            symbols.push_back(symbol);
        }
        return symbols;
    }
    
    /**
     * @brief Get all realized gains/losses
     */
    std::vector<RealizedGainLoss> get_all_realized(
        const std::string& from_date = "",
        const std::string& to_date = "") const {
        
        std::vector<RealizedGainLoss> all;
        
        for (const auto& [symbol, inv] : inventories_) {
            for (const auto& gl : inv.realized_gains()) {
                if (!from_date.empty() && gl.sale_date < from_date) continue;
                if (!to_date.empty() && gl.sale_date > to_date) continue;
                all.push_back(gl);
            }
        }
        
        // Sort by sale date
        std::sort(all.begin(), all.end(),
            [](const RealizedGainLoss& a, const RealizedGainLoss& b) {
                return a.sale_date < b.sale_date;
            });
        
        return all;
    }
    
    /**
     * @brief Get tax summary for year
     */
    TaxSummary get_summary(int year) const {
        TaxSummary summary;
        summary.tax_year = year;
        
        std::string year_start = std::to_string(year) + "-01-01";
        std::string year_end = std::to_string(year) + "-12-31";
        
        auto realized = get_all_realized(year_start, year_end);
        
        for (const auto& gl : realized) {
            double taxable = gl.taxable_gain();
            
            if (gl.holding_period == HoldingPeriod::ShortTerm) {
                summary.short_term_transactions++;
                if (taxable >= 0) {
                    summary.short_term_gains += taxable;
                } else {
                    summary.short_term_losses += taxable;
                }
                summary.short_term_wash_adjustments += gl.wash_sale_disallowed;
            } else {
                summary.long_term_transactions++;
                if (taxable >= 0) {
                    summary.long_term_gains += taxable;
                } else {
                    summary.long_term_losses += taxable;
                }
                summary.long_term_wash_adjustments += gl.wash_sale_disallowed;
            }
        }
        
        return summary;
    }
    
    /**
     * @brief Get unrealized gains/losses
     */
    struct UnrealizedPosition {
        std::string symbol;
        double shares{0};
        double cost_basis{0};
        double current_value{0};
        double unrealized_gain{0};
        HoldingPeriod holding_period;
    };
    
    std::vector<UnrealizedPosition> get_unrealized(
        const std::map<std::string, double>& current_prices) const {
        
        std::vector<UnrealizedPosition> positions;
        
        for (const auto& [symbol, inv] : inventories_) {
            double shares = inv.total_shares();
            if (shares <= 0) continue;
            
            auto price_it = current_prices.find(symbol);
            double price = price_it != current_prices.end() ? price_it->second : 0;
            
            UnrealizedPosition pos;
            pos.symbol = symbol;
            pos.shares = shares;
            pos.cost_basis = inv.total_cost_basis();
            pos.current_value = shares * price;
            pos.unrealized_gain = pos.current_value - pos.cost_basis;
            
            // Use oldest lot for holding period
            auto lots = inv.get_open_lots();
            if (!lots.empty()) {
                pos.holding_period = lots.front().holding_period();
            }
            
            positions.push_back(pos);
        }
        
        return positions;
    }
    
    void set_default_method(CostBasisMethod method) {
        default_method_ = method;
    }
    
    void set_auto_wash_sale(bool enabled) {
        auto_wash_sale_ = enabled;
    }

private:
    std::map<std::string, TaxLotInventory> inventories_;
    std::vector<RealizedGainLoss> pending_losses_;  // For wash sale detection
    CostBasisMethod default_method_;
    bool auto_wash_sale_;
    
    TaxLotInventory& get_or_create_inventory(const std::string& symbol) {
        auto it = inventories_.find(symbol);
        if (it == inventories_.end()) {
            inventories_.emplace(symbol, TaxLotInventory(symbol));
        }
        return inventories_.at(symbol);
    }
    
    void process_wash_sale(RealizedGainLoss& gl) {
        if (gl.gain_loss >= 0) return;  // Only losses
        
        auto& inventory = inventories_.at(gl.symbol);
        auto lots = inventory.all_lots();
        
        auto result = WashSaleDetector::check_wash_sale(gl, lots);
        
        if (result.is_wash_sale) {
            if (auto* lot = inventory.get_lot(result.replacement_lot_id)) {
                WashSaleDetector::apply_wash_sale(gl, *lot, result);
            }
        }
        
        // Track pending loss for future purchase detection
        pending_losses_.push_back(gl);
        
        // Clean old pending losses
        std::string cutoff = add_days(gl.sale_date, -60);
        pending_losses_.erase(
            std::remove_if(pending_losses_.begin(), pending_losses_.end(),
                [&cutoff](const RealizedGainLoss& l) {
                    return l.sale_date < cutoff;
                }),
            pending_losses_.end());
    }
    
    void check_purchase_for_wash_sale(const std::string& symbol,
                                      TaxLotRecord& new_lot,
                                      const std::string& date) {
        for (auto& loss : pending_losses_) {
            if (loss.symbol != symbol) continue;
            if (loss.wash_sale_type != WashSaleType::None) continue;
            
            int days = days_between(loss.sale_date, date);
            if (days >= 0 && days <= WashSaleDetector::WASH_SALE_WINDOW) {
                // This purchase triggers wash sale on prior loss
                double loss_per_share = std::abs(loss.gain_loss) / loss.shares;
                double disallowed = std::min(new_lot.shares * loss_per_share,
                                            std::abs(loss.gain_loss));
                
                loss.wash_sale_type = WashSaleType::Full;
                loss.wash_sale_disallowed = disallowed;
                loss.wash_sale_lot_id = new_lot.id;
                loss.adjustment_code = "W";
                loss.adjustment_amount = disallowed;
                
                new_lot.wash_sale_adjustment = disallowed;
                new_lot.has_wash_sale = true;
                new_lot.wash_sale_from_lot = loss.lot_id;
                
                break;
            }
        }
    }
};

// ============================================================================
// Form 8949 Data
// ============================================================================

/**
 * @brief Form 8949 line item
 */
struct Form8949Line {
    std::string description;          // (a) Description of property
    std::string date_acquired;        // (b) Date acquired
    std::string date_sold;            // (c) Date sold
    double proceeds{0};               // (d) Proceeds
    double cost_basis{0};             // (e) Cost or other basis
    std::string adjustment_code;      // (f) Code(s)
    double adjustment_amount{0};      // (g) Adjustment amount
    double gain_loss{0};              // (h) Gain or loss
    
    // Classification
    bool is_short_term{true};
    bool reported_to_irs{true};       // Broker reported cost basis
    char form_box{'A'};               // A, B, C for short-term; D, E, F for long-term
    
    static char determine_box(bool short_term, bool reported_to_irs, bool has_adjustment) {
        if (short_term) {
            if (reported_to_irs && !has_adjustment) return 'A';
            if (reported_to_irs && has_adjustment) return 'B';
            return 'C';  // Not reported
        } else {
            if (reported_to_irs && !has_adjustment) return 'D';
            if (reported_to_irs && has_adjustment) return 'E';
            return 'F';  // Not reported
        }
    }
};

/**
 * @brief Generate Form 8949 data
 */
inline std::vector<Form8949Line> generate_form_8949(
    const std::vector<RealizedGainLoss>& realized,
    bool broker_reported = true) {
    
    std::vector<Form8949Line> lines;
    
    for (const auto& gl : realized) {
        Form8949Line line;
        
        line.description = gl.symbol + " stock (" + std::to_string(static_cast<int>(gl.shares)) + " sh)";
        line.date_acquired = gl.acquisition_date;
        line.date_sold = gl.sale_date;
        line.proceeds = gl.proceeds;
        line.cost_basis = gl.cost_basis;
        line.adjustment_code = gl.adjustment_code;
        line.adjustment_amount = gl.adjustment_amount;
        line.gain_loss = gl.taxable_gain();
        line.is_short_term = (gl.holding_period == HoldingPeriod::ShortTerm);
        line.reported_to_irs = broker_reported;
        line.form_box = Form8949Line::determine_box(
            line.is_short_term,
            broker_reported,
            !gl.adjustment_code.empty());
        
        lines.push_back(line);
    }
    
    // Sort by box, then by date
    std::sort(lines.begin(), lines.end(),
        [](const Form8949Line& a, const Form8949Line& b) {
            if (a.form_box != b.form_box) return a.form_box < b.form_box;
            return a.date_sold < b.date_sold;
        });
    
    return lines;
}

} // namespace genie::tax

#endif // GENIE_TAX_TAX_TRACKING_HPP
