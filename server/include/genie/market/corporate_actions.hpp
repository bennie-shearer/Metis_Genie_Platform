/**
 * @file corporate_actions.hpp
 * @brief Corporate Actions Processing Engine
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Processes corporate actions affecting portfolio positions:
 * - Stock splits (forward and reverse)
 * - Cash and stock dividends
 * - Mergers and acquisitions
 * - Spinoffs and demergers
 * - Rights issues and warrants
 * - Tender offers and buybacks
 * - Name/symbol changes
 * - Delisting events
 * - Position adjustment calculation
 * - Audit trail for all adjustments
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_MARKET_CORPORATE_ACTIONS_HPP
#define GENIE_MARKET_CORPORATE_ACTIONS_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <functional>

namespace genie::market {

enum class CorporateActionType {
    STOCK_SPLIT,
    REVERSE_SPLIT,
    CASH_DIVIDEND,
    STOCK_DIVIDEND,
    MERGER,
    ACQUISITION,
    SPINOFF,
    RIGHTS_ISSUE,
    TENDER_OFFER,
    BUYBACK,
    NAME_CHANGE,
    SYMBOL_CHANGE,
    DELISTING
};

inline std::string action_type_name(CorporateActionType t) {
    switch (t) {
        case CorporateActionType::STOCK_SPLIT: return "stock_split";
        case CorporateActionType::REVERSE_SPLIT: return "reverse_split";
        case CorporateActionType::CASH_DIVIDEND: return "cash_dividend";
        case CorporateActionType::STOCK_DIVIDEND: return "stock_dividend";
        case CorporateActionType::MERGER: return "merger";
        case CorporateActionType::ACQUISITION: return "acquisition";
        case CorporateActionType::SPINOFF: return "spinoff";
        case CorporateActionType::RIGHTS_ISSUE: return "rights_issue";
        case CorporateActionType::TENDER_OFFER: return "tender_offer";
        case CorporateActionType::BUYBACK: return "buyback";
        case CorporateActionType::NAME_CHANGE: return "name_change";
        case CorporateActionType::SYMBOL_CHANGE: return "symbol_change";
        case CorporateActionType::DELISTING: return "delisting";
    }
    return "unknown";
}

struct CorporateAction {
    std::string action_id;
    CorporateActionType type;
    std::string symbol;
    std::string ex_date; // YYYY-MM-DD
    std::string record_date;
    std::string pay_date;

    // Split: ratio (e.g., 4.0 for 4:1 split, 0.1 for 1:10 reverse)
    double split_ratio{1.0};

    // Dividend
    double cash_amount{0.0};
    std::string currency{"USD"};
    double stock_ratio{0.0}; // For stock dividends

    // Merger/Acquisition
    std::string target_symbol;
    std::string acquirer_symbol;
    double exchange_ratio{0.0};
    double cash_component{0.0};

    // Spinoff
    std::string spinoff_symbol;
    double spinoff_ratio{0.0};
    double cost_basis_allocation{0.0}; // % of original cost to spinoff

    // Name/Symbol change
    std::string new_symbol;
    std::string new_name;

    std::string status{"pending"}; // "pending","processed","failed"
    std::string notes;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"action_id\":\"" << action_id << "\""
           << ",\"type\":\"" << action_type_name(type) << "\""
           << ",\"symbol\":\"" << symbol << "\""
           << ",\"ex_date\":\"" << ex_date << "\""
           << ",\"status\":\"" << status << "\"";

        switch (type) {
            case CorporateActionType::STOCK_SPLIT:
            case CorporateActionType::REVERSE_SPLIT:
                os << ",\"split_ratio\":" << split_ratio;
                break;
            case CorporateActionType::CASH_DIVIDEND:
                os << ",\"cash_amount\":" << cash_amount
                   << ",\"currency\":\"" << currency << "\"";
                break;
            case CorporateActionType::STOCK_DIVIDEND:
                os << ",\"stock_ratio\":" << stock_ratio;
                break;
            case CorporateActionType::MERGER:
            case CorporateActionType::ACQUISITION:
                os << ",\"exchange_ratio\":" << exchange_ratio
                   << ",\"cash_component\":" << cash_component;
                break;
            case CorporateActionType::SPINOFF:
                os << ",\"spinoff_symbol\":\"" << spinoff_symbol << "\""
                   << ",\"spinoff_ratio\":" << spinoff_ratio;
                break;
            default:
                break;
        }

        os << "}";
        return os.str();
    }
};

struct PositionAdjustment {
    std::string action_id;
    std::string symbol;
    double old_quantity{0.0};
    double new_quantity{0.0};
    double old_cost_basis{0.0};
    double new_cost_basis{0.0};
    double cash_received{0.0};
    std::string new_symbol; // If symbol changed
    double new_position_quantity{0.0}; // For spinoffs
    std::string new_position_symbol;
    std::string description;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"action_id\":\"" << action_id << "\""
           << ",\"symbol\":\"" << symbol << "\""
           << ",\"old_qty\":" << old_quantity
           << ",\"new_qty\":" << new_quantity
           << ",\"old_cost_basis\":" << old_cost_basis
           << ",\"new_cost_basis\":" << new_cost_basis
           << ",\"cash_received\":" << cash_received
           << ",\"description\":\"" << description << "\""
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Corporate Actions Engine
// ---------------------------------------------------------------
class CorporateActionsEngine {
public:
    // Register a corporate action
    void register_action(const CorporateAction& action) {
        std::lock_guard<std::mutex> lock(mtx_);
        actions_[action.action_id] = action;
        pending_.push_back(action.action_id);
    }

    // Process a corporate action against a position
    PositionAdjustment process_action(const std::string& action_id,
                                       double current_quantity,
                                       double current_cost_basis) {
        std::lock_guard<std::mutex> lock(mtx_);
        PositionAdjustment adj;

        auto it = actions_.find(action_id);
        if (it == actions_.end()) return adj;

        auto& action = it->second;
        adj.action_id = action_id;
        adj.symbol = action.symbol;
        adj.old_quantity = current_quantity;
        adj.old_cost_basis = current_cost_basis;

        switch (action.type) {
            case CorporateActionType::STOCK_SPLIT:
            case CorporateActionType::REVERSE_SPLIT:
                adj.new_quantity = current_quantity * action.split_ratio;
                adj.new_cost_basis = current_cost_basis; // Total basis unchanged
                adj.description = std::to_string(action.split_ratio) + ":1 split";
                break;

            case CorporateActionType::CASH_DIVIDEND:
                adj.new_quantity = current_quantity; // Unchanged
                adj.new_cost_basis = current_cost_basis;
                adj.cash_received = current_quantity * action.cash_amount;
                adj.description = "Cash dividend $" + std::to_string(action.cash_amount) + "/share";
                break;

            case CorporateActionType::STOCK_DIVIDEND:
                adj.new_quantity = current_quantity * (1.0 + action.stock_ratio);
                adj.new_cost_basis = current_cost_basis; // Total basis unchanged
                adj.description = "Stock dividend " + std::to_string(action.stock_ratio * 100.0) + "%";
                break;

            case CorporateActionType::MERGER:
            case CorporateActionType::ACQUISITION:
                adj.new_quantity = current_quantity * action.exchange_ratio;
                adj.new_symbol = action.acquirer_symbol;
                adj.cash_received = current_quantity * action.cash_component;
                adj.new_cost_basis = current_cost_basis - adj.cash_received;
                adj.description = "Merger: " + action.symbol + " -> " + action.acquirer_symbol;
                break;

            case CorporateActionType::SPINOFF:
                adj.new_quantity = current_quantity;
                adj.new_cost_basis = current_cost_basis * (1.0 - action.cost_basis_allocation);
                adj.new_position_quantity = current_quantity * action.spinoff_ratio;
                adj.new_position_symbol = action.spinoff_symbol;
                adj.description = "Spinoff: " + action.spinoff_symbol + " at " +
                                   std::to_string(action.spinoff_ratio) + ":1";
                break;

            case CorporateActionType::NAME_CHANGE:
            case CorporateActionType::SYMBOL_CHANGE:
                adj.new_quantity = current_quantity;
                adj.new_cost_basis = current_cost_basis;
                adj.new_symbol = action.new_symbol;
                adj.description = "Symbol change: " + action.symbol + " -> " + action.new_symbol;
                break;

            default:
                adj.new_quantity = current_quantity;
                adj.new_cost_basis = current_cost_basis;
                adj.description = "Action: " + action_type_name(action.type);
                break;
        }

        action.status = "processed";
        adjustments_.push_back(adj);
        return adj;
    }

    // Get pending actions
    std::vector<CorporateAction> get_pending() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<CorporateAction> result;
        for (const auto& id : pending_) {
            auto it = actions_.find(id);
            if (it != actions_.end() && it->second.status == "pending") {
                result.push_back(it->second);
            }
        }
        return result;
    }

    // Get all actions
    std::vector<CorporateAction> get_all_actions() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<CorporateAction> result;
        for (const auto& [id, action] : actions_) {
            result.push_back(action);
        }
        return result;
    }

    // Get adjustment history
    std::vector<PositionAdjustment> get_adjustments() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return adjustments_;
    }

    // Get actions for a symbol
    std::vector<CorporateAction> get_actions_for_symbol(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<CorporateAction> result;
        for (const auto& [id, action] : actions_) {
            if (action.symbol == symbol) {
                result.push_back(action);
            }
        }
        return result;
    }

    size_t action_count() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return actions_.size();
    }

private:
    mutable std::mutex mtx_;
    std::map<std::string, CorporateAction> actions_;
    std::vector<std::string> pending_;
    std::vector<PositionAdjustment> adjustments_;
};

} // namespace genie::market

#endif // GENIE_MARKET_CORPORATE_ACTIONS_HPP
