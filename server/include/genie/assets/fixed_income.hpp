/**
 * @file fixed_income.hpp
 * @brief Fixed income securities for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_ASSETS_FIXED_INCOME_HPP
#define GENIE_ASSETS_FIXED_INCOME_HPP
#include "../market/security.hpp"
#include "../core/math_utils.hpp"

namespace genie::assets {
using namespace genie::market;

struct BondData {
    double face_value{1000}, coupon_rate{0}, yield_to_maturity{0};
    int coupon_frequency{2}, years_to_maturity{10};
    CreditRating rating{CreditRating::A};
    TimePoint maturity_date, issue_date;
    [[nodiscard]] double duration() const { return math::modified_duration(face_value, coupon_rate, yield_to_maturity, years_to_maturity, coupon_frequency); }
    [[nodiscard]] double dv01() const { return math::dv01(face_value, coupon_rate, yield_to_maturity, years_to_maturity, coupon_frequency); }
    [[nodiscard]] double theoretical_price() const { return math::bond_price(face_value, coupon_rate, yield_to_maturity, years_to_maturity, coupon_frequency); }
};

class Bond : public Security {
protected:
    BondData data_;
public:
    Bond() : Security("", SecurityType::CorporateBond, AssetClass::FixedIncome) {}
    Bond(SecurityId id, SecurityType t, const std::string& name) : Security(std::move(id), t, AssetClass::FixedIncome) { reference_.name = name; }
    [[nodiscard]] BondData& bond_data() { return data_; }
    [[nodiscard]] const BondData& bond_data() const { return data_; }
    [[nodiscard]] double duration() const { return data_.duration(); }
    [[nodiscard]] double ytm() const { return data_.yield_to_maturity; }
};

class GovernmentBond : public Bond {
public:
    GovernmentBond() { type_ = SecurityType::GovernmentBond; data_.rating = CreditRating::AAA; }
    GovernmentBond(SecurityId id, const std::string& name) : Bond(std::move(id), SecurityType::GovernmentBond, name) { data_.rating = CreditRating::AAA; }
};

class CorporateBond : public Bond {
    std::string issuer_;
public:
    CorporateBond() { type_ = SecurityType::CorporateBond; }
    CorporateBond(SecurityId id, const std::string& name, const std::string& issuer) : Bond(std::move(id), SecurityType::CorporateBond, name), issuer_(issuer) {}
    [[nodiscard]] const std::string& issuer() const { return issuer_; }
};

inline std::shared_ptr<GovernmentBond> create_treasury(const SecurityId& id, const std::string& name, double coupon, double yield, int years, TimePoint maturity) {
    auto bond = std::make_shared<GovernmentBond>(id, name);
    bond->bond_data().coupon_rate = coupon; bond->bond_data().yield_to_maturity = yield;
    bond->bond_data().years_to_maturity = years; bond->bond_data().maturity_date = maturity;
    bond->set_price(bond->bond_data().theoretical_price());
    return bond;
}
} // namespace genie::assets
#endif
