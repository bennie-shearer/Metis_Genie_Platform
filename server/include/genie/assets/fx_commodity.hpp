/**
 * @file fx_commodity.hpp
 * @brief Foreign Exchange and Commodity instruments for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_FX_COMMODITY_HPP
#define GENIE_FX_COMMODITY_HPP

#include "equity.hpp"
#include <string>
#include <cmath>

namespace genie {
namespace assets {

// Foreign Exchange Pair
class FXPair : public Security {
    std::string base_currency_;
    std::string quote_currency_;
    double spot_rate_{1.0};
    double bid_{1.0};
    double ask_{1.0};
    int pip_decimal_{4};  // 4 for most pairs, 2 for JPY pairs
    
public:
    FXPair(const std::string& id, const std::string& base, const std::string& quote)
        : Security(id, SecurityType::Unknown, AssetClass::FX), base_currency_(base), quote_currency_(quote) {
        reference_.name = base + "/" + quote;
        reference_.ticker = id;
        if (quote == "JPY" || base == "JPY") pip_decimal_ = 2;
    }
    
    bool is_fx() const override { return true; }
    
    const std::string& base_currency() const { return base_currency_; }
    const std::string& quote_currency() const { return quote_currency_; }
    
    double spot() const { return spot_rate_; }
    double bid() const { return bid_; }
    double ask() const { return ask_; }
    double spread() const { return ask_ - bid_; }
    double spread_pips() const { return spread() * std::pow(10, pip_decimal_); }
    double mid() const { return (bid_ + ask_) / 2.0; }
    
    void update_quote(double bid, double ask) {
        bid_ = bid;
        ask_ = ask;
        spot_rate_ = mid();
    }
    
    void update_spot(double spot, double spread_pips = 1.0) {
        double half_spread = spread_pips / 2.0 / std::pow(10, pip_decimal_);
        bid_ = spot - half_spread;
        ask_ = spot + half_spread;
        spot_rate_ = spot;
    }
    
    // Convert amount from base to quote currency
    double to_quote(double base_amount) const { return base_amount * spot_rate_; }
    
    // Convert amount from quote to base currency
    double to_base(double quote_amount) const { return quote_amount / spot_rate_; }
    
    // Calculate P&L in quote currency for a position
    double calculate_pnl(double position_size, double entry_rate, double exit_rate) const {
        return position_size * (exit_rate - entry_rate);
    }
    
    // Pip value for a standard lot (100,000 units)
    double pip_value(double lot_size = 100000.0) const {
        return lot_size * (1.0 / std::pow(10, pip_decimal_));
    }
};

// Commodity Types
enum class CommodityType { 
    PreciousMetal,  // Gold, Silver, Platinum
    Energy,         // Oil, Natural Gas
    Agriculture,    // Wheat, Corn, Soybeans
    Industrial      // Copper, Aluminum
};

inline std::string commodity_type_string(CommodityType type) {
    switch (type) {
        case CommodityType::PreciousMetal: return "Precious Metal";
        case CommodityType::Energy: return "Energy";
        case CommodityType::Agriculture: return "Agriculture";
        case CommodityType::Industrial: return "Industrial";
        default: return "Unknown";
    }
}

// Commodity
class Commodity : public Security {
    CommodityType type_;
    std::string unit_;          // oz, bbl, bushel, lb
    double contract_size_;      // Units per contract
    double spot_price_{0.0};
    double storage_cost_{0.0};  // Annual storage cost per unit
    
public:
    Commodity(const std::string& id, const std::string& name, CommodityType type,
              const std::string& unit, double contract_size)
        : Security(id, SecurityType::Unknown, AssetClass::Commodity), type_(type), unit_(unit), contract_size_(contract_size) {
        reference_.name = name;
        reference_.ticker = id;
    }
    
    bool is_commodity() const override { return true; }
    
    CommodityType commodity_type() const { return type_; }
    const std::string& unit() const { return unit_; }
    double contract_size() const { return contract_size_; }
    double spot_price() const { return spot_price_; }
    double storage_cost() const { return storage_cost_; }
    
    void set_spot_price(double price) { spot_price_ = price; }
    void set_storage_cost(double cost) { storage_cost_ = cost; }
    
    // Contract value
    double contract_value() const { return spot_price_ * contract_size_; }
    
    // Forward price (cost-of-carry model)
    double forward_price(double risk_free_rate, double time_years) const {
        double carry_cost = storage_cost_ * time_years;
        return spot_price_ * std::exp((risk_free_rate + carry_cost / spot_price_) * time_years);
    }
    
    // Convenience factor for backwardation/contango
    double convenience_yield(double forward_price, double risk_free_rate, double time_years) const {
        if (time_years <= 0 || spot_price_ <= 0) return 0;
        double carry = storage_cost_ / spot_price_;
        return risk_free_rate + carry - std::log(forward_price / spot_price_) / time_years;
    }
};

// Money Market Instrument
class MoneyMarket : public Security {
    double face_value_;
    double discount_rate_;
    int days_to_maturity_;
    
public:
    MoneyMarket(const std::string& id, const std::string& name, 
                double face_value, double discount_rate, int days)
        : Security(id, SecurityType::Unknown, AssetClass::Cash), face_value_(face_value), 
          discount_rate_(discount_rate), days_to_maturity_(days) {
        reference_.name = name;
        reference_.ticker = id;
    }
    
    bool is_cash() const override { return true; }
    
    double face_value() const { return face_value_; }
    double discount_rate() const { return discount_rate_; }
    int days_to_maturity() const { return days_to_maturity_; }
    
    // Price using discount rate
    double price() const {
        return face_value_ * (1.0 - discount_rate_ * days_to_maturity_ / 360.0);
    }
    
    // Yield equivalent
    double bond_equivalent_yield() const {
        double p = price();
        return (face_value_ - p) / p * (365.0 / days_to_maturity_);
    }
    
    // Money market yield
    double money_market_yield() const {
        double p = price();
        return (face_value_ - p) / p * (360.0 / days_to_maturity_);
    }
};

// Factory functions
inline std::shared_ptr<FXPair> create_fx_pair(const std::string& base, const std::string& quote) {
    return std::make_shared<FXPair>(base + quote, base, quote);
}

inline std::shared_ptr<Commodity> create_commodity(const std::string& id, const std::string& name,
                                                    CommodityType type, const std::string& unit,
                                                    double contract_size) {
    return std::make_shared<Commodity>(id, name, type, unit, contract_size);
}

inline std::shared_ptr<MoneyMarket> create_tbill(const std::string& id, double face_value,
                                                  double discount_rate, int days) {
    return std::make_shared<MoneyMarket>(id, "T-Bill " + id, face_value, discount_rate, days);
}

// Common instruments
inline std::shared_ptr<Commodity> gold() {
    auto g = create_commodity("GC", "Gold", CommodityType::PreciousMetal, "oz", 100.0);
    g->set_spot_price(2000.0);
    return g;
}

inline std::shared_ptr<Commodity> silver() {
    auto s = create_commodity("SI", "Silver", CommodityType::PreciousMetal, "oz", 5000.0);
    s->set_spot_price(25.0);
    return s;
}

inline std::shared_ptr<Commodity> crude_oil() {
    auto c = create_commodity("CL", "Crude Oil WTI", CommodityType::Energy, "bbl", 1000.0);
    c->set_spot_price(75.0);
    c->set_storage_cost(2.0);
    return c;
}

inline std::shared_ptr<Commodity> natural_gas() {
    auto n = create_commodity("NG", "Natural Gas", CommodityType::Energy, "MMBtu", 10000.0);
    n->set_spot_price(3.0);
    return n;
}

inline std::shared_ptr<FXPair> eurusd() {
    auto fx = create_fx_pair("EUR", "USD");
    fx->update_spot(1.0850, 1.0);
    return fx;
}

inline std::shared_ptr<FXPair> usdjpy() {
    auto fx = create_fx_pair("USD", "JPY");
    fx->update_spot(150.50, 1.0);
    return fx;
}

inline std::shared_ptr<FXPair> gbpusd() {
    auto fx = create_fx_pair("GBP", "USD");
    fx->update_spot(1.2650, 1.0);
    return fx;
}

} // namespace assets
} // namespace genie
#endif // GENIE_FX_COMMODITY_HPP
