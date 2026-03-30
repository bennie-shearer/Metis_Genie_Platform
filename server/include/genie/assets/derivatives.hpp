/**
 * @file derivatives.hpp
 * @brief Derivative securities for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_ASSETS_DERIVATIVES_HPP
#define GENIE_ASSETS_DERIVATIVES_HPP
#include "../market/security.hpp"
#include "../core/math_utils.hpp"
#include "../core/date_utils.hpp"

namespace genie::assets {
using namespace genie::market;

class Option : public Security {
    SecurityId underlying_id_;
    double strike_{0}, underlying_price_{0}, volatility_{0.25}, risk_free_rate_{0.05};
    TimePoint expiry_;
    bool is_call_{true};
    double delta_{0}, gamma_{0}, theta_{0}, vega_{0}, theoretical_price_{0};
public:
    Option() : Security("", SecurityType::Option, AssetClass::Derivative) {}
    Option(SecurityId id, const SecurityId& underlying, double strike, TimePoint expiry, bool is_call)
        : Security(std::move(id), SecurityType::Option, AssetClass::Derivative),
          underlying_id_(underlying), strike_(strike), expiry_(expiry), is_call_(is_call) {}
    [[nodiscard]] const SecurityId& underlying_id() const { return underlying_id_; }
    [[nodiscard]] double strike() const { return strike_; }
    [[nodiscard]] bool is_call() const { return is_call_; }
    [[nodiscard]] TimePoint expiry() const { return expiry_; }
    [[nodiscard]] double delta() const { return delta_; }
    [[nodiscard]] double gamma() const { return gamma_; }
    [[nodiscard]] double vega() const { return vega_; }
    [[nodiscard]] double theoretical_price() const { return theoretical_price_; }
    
    void update_market_data(double S, double sigma, double T, double r) {
        underlying_price_ = S; volatility_ = sigma; risk_free_rate_ = r; calculate_greeks(T);
    }
    void calculate_greeks(double T) {
        if (T <= 0) T = date_utils::years_between(date_utils::today(), expiry_);
        if (T <= 0) { theoretical_price_ = is_call_ ? std::max(0.0, underlying_price_ - strike_) : std::max(0.0, strike_ - underlying_price_); return; }
        double S = underlying_price_, K = strike_, r = risk_free_rate_, sig = volatility_;
        theoretical_price_ = is_call_ ? math::black_scholes_call(S, K, r, sig, T) : math::black_scholes_put(S, K, r, sig, T);
        delta_ = is_call_ ? math::bs_delta_call(S, K, r, sig, T) : math::bs_delta_put(S, K, r, sig, T);
        gamma_ = math::bs_gamma(S, K, r, sig, T); vega_ = math::bs_vega(S, K, r, sig, T);
    }
    void calculate_price() { calculate_greeks(date_utils::years_between(date_utils::today(), expiry_)); }
};

class Future : public Security {
    SecurityId underlying_id_;
    double contract_size_{100};
    TimePoint expiry_;
public:
    Future() : Security("", SecurityType::Future, AssetClass::Derivative) {}
    Future(SecurityId id, const SecurityId& underlying, double size, TimePoint expiry)
        : Security(std::move(id), SecurityType::Future, AssetClass::Derivative), underlying_id_(underlying), contract_size_(size), expiry_(expiry) {}
    [[nodiscard]] const SecurityId& underlying_id() const { return underlying_id_; }
    [[nodiscard]] double contract_size() const { return contract_size_; }
    [[nodiscard]] double notional_value() const { return current_price_ * contract_size_; }
};

inline std::shared_ptr<Option> create_call_option(const SecurityId& id, const SecurityId& underlying, double strike, TimePoint expiry) {
    return std::make_shared<Option>(id, underlying, strike, expiry, true);
}
inline std::shared_ptr<Option> create_put_option(const SecurityId& id, const SecurityId& underlying, double strike, TimePoint expiry) {
    return std::make_shared<Option>(id, underlying, strike, expiry, false);
}
} // namespace genie::assets
#endif
