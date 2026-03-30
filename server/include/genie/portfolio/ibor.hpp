/**
 * @file ibor.hpp
 * @brief Investment Book of Record for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Transaction-based position calculation, corporate action processing,
 * cash flow projection, and NAV calculation.
 */
#pragma once
#ifndef GENIE_PORTFOLIO_IBOR_HPP
#define GENIE_PORTFOLIO_IBOR_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>

namespace genie::portfolio {

/** Transaction types in the book */
enum class TxnType { Buy, Sell, Dividend, Interest, Fee, CashDeposit, CashWithdraw,
                     StockSplit, Merger, SpinOff, RightsIssue };

inline std::string txn_type_name(TxnType t) {
    switch (t) {
        case TxnType::Buy: return "Buy"; case TxnType::Sell: return "Sell";
        case TxnType::Dividend: return "Dividend"; case TxnType::Interest: return "Interest";
        case TxnType::Fee: return "Fee"; case TxnType::CashDeposit: return "CashDeposit";
        case TxnType::CashWithdraw: return "CashWithdraw"; case TxnType::StockSplit: return "StockSplit";
        case TxnType::Merger: return "Merger"; case TxnType::SpinOff: return "SpinOff";
        case TxnType::RightsIssue: return "RightsIssue";
        default: return "Unknown";
    }
}

/** A single transaction in the book */
struct Transaction {
    std::string id;
    std::string portfolio_id;
    std::string security_id;
    TxnType type{TxnType::Buy};
    double quantity{0};
    double price{0};
    double amount{0};         // total cash impact (positive = inflow)
    double commission{0};
    std::string currency{"USD"};
    std::string trade_date;   // ISO-8601
    std::string settle_date;  // ISO-8601
    std::string description;
    // Corporate action specific
    double split_ratio{0};    // for stock splits (e.g., 2.0 for 2:1)
    std::string new_security_id; // for mergers/spinoffs
};

/** Calculated position from transactions */
struct BookPosition {
    std::string security_id;
    double quantity{0};
    double avg_cost{0};
    double total_cost{0};
    double realized_pnl{0};
    double dividend_income{0};
    int trade_count{0};
    std::string first_trade_date;
    std::string last_trade_date;
};

/** Projected cash flow */
struct CashFlow {
    std::string date;
    std::string type;         // "Settlement", "Dividend", "Fee"
    std::string security_id;
    double amount{0};
    std::string currency{"USD"};
};

/** NAV breakdown */
struct NavBreakdown {
    std::string as_of_date;
    double cash{0};
    double securities_value{0};
    double accrued_income{0};
    double pending_settlements{0};
    double nav{0};
    int position_count{0};
};

/** Investment Book of Record */
class IBOR {
    std::string portfolio_id_;
    std::vector<Transaction> transactions_;
    double initial_cash_{0};

public:
    explicit IBOR(const std::string& portfolio_id, double initial_cash = 0)
        : portfolio_id_(portfolio_id), initial_cash_(initial_cash) {}

    const std::string& portfolio_id() const { return portfolio_id_; }
    const std::vector<Transaction>& transactions() const { return transactions_; }

    /** Record a transaction */
    void record(const Transaction& txn) {
        transactions_.push_back(txn);
        // Keep sorted by trade date
        std::stable_sort(transactions_.begin(), transactions_.end(),
            [](const auto& a, const auto& b) { return a.trade_date < b.trade_date; });
    }

    /** Calculate positions from all transactions up to a given date */
    [[nodiscard]] std::map<std::string, BookPosition> positions(
            const std::string& as_of = "") const {
        std::map<std::string, BookPosition> pos;

        for (const auto& txn : transactions_) {
            if (!as_of.empty() && txn.trade_date > as_of) break;
            if (txn.security_id.empty() && txn.type != TxnType::CashDeposit
                && txn.type != TxnType::CashWithdraw && txn.type != TxnType::Fee) continue;

            auto& p = pos[txn.security_id];
            p.security_id = txn.security_id;

            switch (txn.type) {
                case TxnType::Buy: {
                    double new_cost = txn.quantity * txn.price + txn.commission;
                    p.total_cost += new_cost;
                    p.quantity += txn.quantity;
                    p.avg_cost = (p.quantity > 0) ? p.total_cost / p.quantity : 0;
                    p.trade_count++;
                    if (p.first_trade_date.empty()) p.first_trade_date = txn.trade_date;
                    p.last_trade_date = txn.trade_date;
                    break;
                }
                case TxnType::Sell: {
                    double proceeds = txn.quantity * txn.price - txn.commission;
                    double cost_basis = txn.quantity * p.avg_cost;
                    p.realized_pnl += proceeds - cost_basis;
                    p.quantity -= txn.quantity;
                    p.total_cost = p.quantity * p.avg_cost;
                    p.trade_count++;
                    p.last_trade_date = txn.trade_date;
                    break;
                }
                case TxnType::Dividend:
                    p.dividend_income += txn.amount;
                    break;
                case TxnType::StockSplit:
                    if (txn.split_ratio > 0) {
                        p.quantity *= txn.split_ratio;
                        p.avg_cost /= txn.split_ratio;
                    }
                    break;
                default: break;
            }
        }

        // Remove zero-quantity positions
        for (auto it = pos.begin(); it != pos.end(); ) {
            if (std::abs(it->second.quantity) < 1e-10 && it->first != "")
                it = pos.erase(it);
            else
                ++it;
        }
        return pos;
    }

    /** Calculate cash balance from transactions */
    [[nodiscard]] double cash_balance(const std::string& as_of = "") const {
        double cash = initial_cash_;
        for (const auto& txn : transactions_) {
            if (!as_of.empty() && txn.trade_date > as_of) break;
            switch (txn.type) {
                case TxnType::Buy:
                    cash -= txn.quantity * txn.price + txn.commission; break;
                case TxnType::Sell:
                    cash += txn.quantity * txn.price - txn.commission; break;
                case TxnType::Dividend:
                case TxnType::Interest:
                case TxnType::CashDeposit:
                    cash += txn.amount; break;
                case TxnType::Fee:
                case TxnType::CashWithdraw:
                    cash -= std::abs(txn.amount); break;
                default: break;
            }
        }
        return cash;
    }

    /** Calculate NAV given current prices */
    [[nodiscard]] NavBreakdown nav(const std::map<std::string, double>& prices,
                                    const std::string& as_of = "") const {
        NavBreakdown nb;
        nb.as_of_date = as_of.empty() ? "current" : as_of;
        nb.cash = cash_balance(as_of);

        auto pos = positions(as_of);
        for (const auto& [id, p] : pos) {
            auto it = prices.find(id);
            double px = (it != prices.end()) ? it->second : p.avg_cost;
            nb.securities_value += p.quantity * px;
            nb.position_count++;
        }

        // Pending settlements: trades within last 2 business days
        // Simplified: check settle_date > as_of
        for (const auto& txn : transactions_) {
            if (txn.type == TxnType::Buy || txn.type == TxnType::Sell) {
                if (!txn.settle_date.empty() && (as_of.empty() || txn.settle_date > as_of)) {
                    if (!as_of.empty() && txn.trade_date <= as_of) {
                        nb.pending_settlements += (txn.type == TxnType::Buy)
                            ? -(txn.quantity * txn.price)
                            : (txn.quantity * txn.price);
                    }
                }
            }
        }

        nb.nav = nb.cash + nb.securities_value + nb.accrued_income;
        return nb;
    }

    /** Project future cash flows */
    [[nodiscard]] std::vector<CashFlow> project_cash_flows(
            const std::string& from_date, const std::string& to_date) const {
        std::vector<CashFlow> flows;

        for (const auto& txn : transactions_) {
            // Pending settlements
            if (!txn.settle_date.empty()
                && txn.settle_date >= from_date && txn.settle_date <= to_date) {
                CashFlow cf;
                cf.date = txn.settle_date;
                cf.type = "Settlement";
                cf.security_id = txn.security_id;
                cf.currency = txn.currency;
                if (txn.type == TxnType::Buy)
                    cf.amount = -(txn.quantity * txn.price + txn.commission);
                else if (txn.type == TxnType::Sell)
                    cf.amount = txn.quantity * txn.price - txn.commission;
                flows.push_back(cf);
            }
        }

        std::sort(flows.begin(), flows.end(),
            [](const auto& a, const auto& b) { return a.date < b.date; });
        return flows;
    }

    /** Total realized P&L across all positions */
    [[nodiscard]] double total_realized_pnl(const std::string& as_of = "") const {
        double total = 0;
        for (const auto& [id, p] : positions(as_of))
            total += p.realized_pnl;
        return total;
    }

    /** Total dividend income */
    [[nodiscard]] double total_dividend_income(const std::string& as_of = "") const {
        double total = 0;
        for (const auto& [id, p] : positions(as_of))
            total += p.dividend_income;
        return total;
    }

    [[nodiscard]] size_t transaction_count() const { return transactions_.size(); }
};

} // namespace genie::portfolio

#endif // GENIE_PORTFOLIO_IBOR_HPP
