/**
 * @file position.hpp
 * @brief Position management for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_PORTFOLIO_POSITION_HPP
#define GENIE_PORTFOLIO_POSITION_HPP
#include "../market/security.hpp"

namespace genie::portfolio {
using namespace genie::market;

struct TaxLot { TimePoint acquisition_date; Quantity quantity{0}; Price cost_per_share{0}; };

class Position {
    SecurityId security_id_;
    SecurityPtr security_;
    Quantity quantity_{0};
    Money market_value_, cost_basis_, unrealized_pnl_, realized_pnl_;
    Price avg_cost_{0}, current_price_{0};
    double weight_{0};
    std::deque<TaxLot> tax_lots_;
    Currency currency_{"USD"};
public:
    Position() = default;
    explicit Position(SecurityId id) : security_id_(std::move(id)) {}
    [[nodiscard]] const SecurityId& security_id() const { return security_id_; }
    [[nodiscard]] SecurityPtr security() const { return security_; }
    void set_security(SecurityPtr s) { security_ = std::move(s); }
    [[nodiscard]] Quantity quantity() const { return quantity_; }
    [[nodiscard]] bool is_long() const { return quantity_ > 0; }
    [[nodiscard]] bool is_short() const { return quantity_ < 0; }
    [[nodiscard]] bool is_flat() const { return std::abs(quantity_) < 1e-10; }
    [[nodiscard]] Money market_value() const { return market_value_; }
    [[nodiscard]] Money cost_basis() const { return cost_basis_; }
    [[nodiscard]] Money unrealized_pnl() const { return unrealized_pnl_; }
    [[nodiscard]] double weight() const { return weight_; }
    void set_weight(double w) { weight_ = w; }
    
    void add_shares(Quantity qty, Price price, const Currency& ccy) {
        TaxLot lot; lot.acquisition_date = std::chrono::system_clock::now(); lot.quantity = qty; lot.cost_per_share = price;
        tax_lots_.push_back(lot); quantity_ += qty;
        cost_basis_ = cost_basis_ + Money(qty * price, ccy);
        avg_cost_ = quantity_ > 0 ? cost_basis_.amount / quantity_ : 0; currency_ = ccy;
    }
    void short_sell(Quantity qty, Price price, const Currency& ccy) {
        quantity_ -= qty; cost_basis_ = cost_basis_ + Money(qty * price, ccy); currency_ = ccy;
    }
    Money remove_shares(Quantity qty, Price price, const Currency& ccy) {
        double realized = 0; Quantity remaining = qty;
        while (remaining > 0 && !tax_lots_.empty()) {
            auto& lot = tax_lots_.front();
            Quantity sold = std::min(remaining, lot.quantity);
            realized += sold * (price - lot.cost_per_share);
            lot.quantity -= sold; remaining -= sold;
            if (lot.quantity <= 0) tax_lots_.pop_front();
        }
        quantity_ -= qty; cost_basis_ = Money(quantity_ * avg_cost_, ccy);
        realized_pnl_ = realized_pnl_ + Money(realized, ccy);
        return Money(realized, ccy);
    }
    void update_market_value(Price price) {
        current_price_ = price;
        market_value_ = Money(std::abs(quantity_) * price, currency_);
        unrealized_pnl_ = Money(quantity_ * (price - avg_cost_), currency_);
    }
};

class CashPosition {
    std::map<Currency, double> balances_;
public:
    void deposit(const Money& m) { balances_[m.currency] += m.amount; }
    void withdraw(const Money& m) { balances_[m.currency] -= m.amount; }
    [[nodiscard]] Money balance(const Currency& ccy) const { auto it = balances_.find(ccy); return Money(it != balances_.end() ? it->second : 0.0, ccy); }
    [[nodiscard]] const std::map<Currency, double>& all_balances() const { return balances_; }
};

struct HoldingsSummary {
    size_t position_count{0}, long_positions{0}, short_positions{0};
    Money total_long_value, total_short_value, net_market_value, total_cost_basis, total_unrealized_pnl;
    std::map<AssetClass, Money> by_asset_class;
    std::map<AssetClass, double> weight_by_asset_class;
    std::map<std::string, Money> by_sector;
    std::map<std::string, double> weight_by_sector;
};
} // namespace genie::portfolio
#endif
