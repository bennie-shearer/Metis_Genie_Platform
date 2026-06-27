/**
 * @file security.hpp
 * @brief Security definitions for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_MARKET_SECURITY_HPP
#define GENIE_MARKET_SECURITY_HPP
#include "../core/types.hpp"

namespace genie::market {
struct ReferenceData {
    std::string name, ticker, exchange, country{"US"}, sector, industry;
    Currency currency{"USD"};
    double lot_size{1.0};
    bool is_active{true};
};

class Security {
protected:
    SecurityId id_;
    SecurityType type_{SecurityType::Unknown};
    AssetClass asset_class_{AssetClass::Unknown};
    ReferenceData reference_;
    Price current_price_{0.0};
public:
    Security() = default;
    Security(SecurityId id, SecurityType t, AssetClass ac) : id_(std::move(id)), type_(t), asset_class_(ac) {}
    virtual ~Security() = default;
    [[nodiscard]] const SecurityId& id() const { return id_; }
    [[nodiscard]] SecurityType type() const { return type_; }
    [[nodiscard]] AssetClass asset_class() const { return asset_class_; }
    [[nodiscard]] ReferenceData& reference() { return reference_; }
    [[nodiscard]] const ReferenceData& reference() const { return reference_; }
    [[nodiscard]] Price current_price() const { return current_price_; }
    void set_price(Price p) { current_price_ = p; }
    [[nodiscard]] virtual bool is_equity() const { return asset_class_ == AssetClass::Equity; }
    [[nodiscard]] virtual bool is_fixed_income() const { return asset_class_ == AssetClass::FixedIncome; }
    [[nodiscard]] virtual bool is_derivative() const { return asset_class_ == AssetClass::Derivative; }
    [[nodiscard]] virtual bool is_fx() const { return asset_class_ == AssetClass::FX; }
    [[nodiscard]] virtual bool is_commodity() const { return asset_class_ == AssetClass::Commodity; }
    [[nodiscard]] virtual bool is_cash() const { return asset_class_ == AssetClass::Cash; }
};

using SecurityPtr = std::shared_ptr<Security>;
} // namespace genie::market
#endif
