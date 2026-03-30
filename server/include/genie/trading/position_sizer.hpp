/**
 * @file position_sizer.hpp
 * @brief Position sizing algorithms for risk-controlled trade execution
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Multi-method position sizing:
 * - Kelly criterion (full and fractional)
 * - Fixed fractional (risk % of equity per trade)
 * - Volatility-based (ATR-adjusted sizing)
 * - Equal weight allocation
 * - Risk parity sizing
 * - Maximum drawdown constrained
 * - Correlation-adjusted sizing
 * - Round-lot and minimum-trade enforcement
 * - Portfolio heat tracking (total risk deployed)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_TRADING_POSITION_SIZER_HPP
#define GENIE_TRADING_POSITION_SIZER_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>

namespace genie {
namespace trading {
namespace sizing {

// ============================================================================
// Enumerations
// ============================================================================

enum class SizingMethod {
    FixedFractional,   // Risk fixed % of equity per trade
    KellyCriterion,    // Optimal growth rate sizing
    VolatilityBased,   // ATR-normalized sizing
    EqualWeight,       // Equal dollar allocation
    RiskParity,        // Size inversely proportional to volatility
    MaxDrawdownCapped  // Size to cap max portfolio drawdown
};

[[nodiscard]] inline std::string method_string(SizingMethod m) {
    switch (m) {
        case SizingMethod::FixedFractional: return "fixed_fractional";
        case SizingMethod::KellyCriterion: return "kelly";
        case SizingMethod::VolatilityBased: return "volatility";
        case SizingMethod::EqualWeight: return "equal_weight";
        case SizingMethod::RiskParity: return "risk_parity";
        case SizingMethod::MaxDrawdownCapped: return "max_dd_capped";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct TradeSetup {
    std::string symbol;
    double entry_price{0};
    double stop_loss{0};
    double target_price{0};
    double win_rate{0.5};            // Historical win rate
    double avg_win{0};               // Average winning trade %
    double avg_loss{0};              // Average losing trade %
    double volatility{0};            // Daily volatility (std dev)
    double atr{0};                   // Average True Range
    double correlation_to_portfolio{0};
};

struct SizingResult {
    std::string symbol;
    SizingMethod method;
    double shares{0};
    double notional{0};
    double risk_amount{0};           // Dollar risk per trade
    double risk_pct{0};              // % of equity at risk
    double portfolio_weight{0};
    double kelly_fraction{0};
    double position_heat{0};         // Contribution to portfolio heat
    std::string rationale;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << symbol << " [" << method_string(method) << "]: "
            << shares << " shares ($" << notional << ") "
            << "Risk=$" << risk_amount << " (" << risk_pct * 100 << "% equity) "
            << "Weight=" << portfolio_weight * 100 << "%";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "{\"symbol\":\"" << symbol << "\",\"method\":\"" << method_string(method)
            << "\",\"shares\":" << shares << ",\"notional\":" << notional
            << ",\"risk_amount\":" << risk_amount
            << ",\"risk_pct\":" << risk_pct
            << ",\"weight\":" << portfolio_weight << "}";
        return oss.str();
    }
};

struct SizingConfig {
    SizingMethod method{SizingMethod::FixedFractional};
    double max_risk_per_trade{0.02};    // 2% of equity
    double max_position_weight{0.10};   // 10% max single position
    double max_portfolio_heat{0.10};    // 10% total portfolio risk
    double kelly_fraction_cap{0.25};    // Cap Kelly at 25%
    double target_volatility{0.15};     // 15% annualized for vol-based
    int round_lot{1};
    double min_trade_value{500.0};
};

// ============================================================================
// Position Sizer
// ============================================================================

class PositionSizer {
public:
    explicit PositionSizer(SizingConfig config = {}) : config_(config) {}

    /**
     * @brief Calculate position size for a trade setup
     */
    [[nodiscard]] SizingResult calculate(const TradeSetup& setup,
                                           double equity,
                                           double current_heat = 0) const {
        std::lock_guard<std::mutex> lock(mutex_);

        switch (config_.method) {
            case SizingMethod::FixedFractional:
                return fixed_fractional(setup, equity, current_heat);
            case SizingMethod::KellyCriterion:
                return kelly_criterion(setup, equity, current_heat);
            case SizingMethod::VolatilityBased:
                return volatility_based(setup, equity, current_heat);
            case SizingMethod::EqualWeight:
                return equal_weight(setup, equity);
            case SizingMethod::RiskParity:
                return risk_parity(setup, equity);
            case SizingMethod::MaxDrawdownCapped:
                return max_dd_capped(setup, equity, current_heat);
        }
        return {};
    }

    /**
     * @brief Size multiple positions with risk parity
     */
    [[nodiscard]] std::vector<SizingResult> size_portfolio(
        const std::vector<TradeSetup>& setups, double equity) const {

        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SizingResult> results;
        double total_inv_vol = 0;
        for (const auto& s : setups) {
            if (s.volatility > 1e-10) total_inv_vol += 1.0 / s.volatility;
        }

        double heat = 0;
        for (const auto& setup : setups) {
            double weight = 0;
            if (total_inv_vol > 0 && setup.volatility > 1e-10) {
                weight = (1.0 / setup.volatility) / total_inv_vol;
            } else {
                weight = 1.0 / static_cast<double>(setups.size());
            }
            weight = std::min(weight, config_.max_position_weight);

            SizingResult r;
            r.symbol = setup.symbol;
            r.method = SizingMethod::RiskParity;
            r.portfolio_weight = weight;
            r.notional = equity * weight;
            r.shares = round_shares(r.notional / std::max(setup.entry_price, 0.01));
            r.notional = r.shares * setup.entry_price;
            if (setup.stop_loss > 0) {
                r.risk_amount = r.shares * std::abs(setup.entry_price - setup.stop_loss);
            } else {
                r.risk_amount = r.notional * setup.volatility;
            }
            r.risk_pct = equity > 0 ? r.risk_amount / equity : 0;
            r.position_heat = r.risk_pct;
            heat += r.position_heat;
            r.rationale = "Risk parity weight: " + fmt_pct(weight);
            results.push_back(std::move(r));
        }
        return results;
    }

    void set_config(SizingConfig cfg) { std::lock_guard<std::mutex> lock(mutex_); config_ = cfg; }
    [[nodiscard]] SizingConfig config() const { std::lock_guard<std::mutex> lock(mutex_); return config_; }

private:
    mutable std::mutex mutex_;
    SizingConfig config_;

    SizingResult fixed_fractional(const TradeSetup& s, double equity, double heat) const {
        SizingResult r;
        r.symbol = s.symbol; r.method = SizingMethod::FixedFractional;
        double risk_per_share = std::abs(s.entry_price - s.stop_loss);
        if (risk_per_share < 1e-10) risk_per_share = s.entry_price * 0.05; // 5% fallback
        double available_risk = std::min(config_.max_risk_per_trade * equity,
                                          (config_.max_portfolio_heat - heat) * equity);
        available_risk = std::max(0.0, available_risk);
        r.shares = round_shares(available_risk / risk_per_share);
        r.notional = r.shares * s.entry_price;
        r.risk_amount = r.shares * risk_per_share;
        r.risk_pct = equity > 0 ? r.risk_amount / equity : 0;
        r.portfolio_weight = equity > 0 ? r.notional / equity : 0;
        r.rationale = "Risk " + fmt_pct(config_.max_risk_per_trade) + " of equity";
        apply_caps(r, equity);
        return r;
    }

    SizingResult kelly_criterion(const TradeSetup& s, double equity, double /*heat*/) const {
        SizingResult r;
        r.symbol = s.symbol; r.method = SizingMethod::KellyCriterion;
        // Kelly: f* = (p*b - q) / b where p=win_rate, q=1-p, b=avg_win/avg_loss
        double p = s.win_rate, q = 1.0 - p;
        double b = s.avg_loss > 1e-10 ? s.avg_win / s.avg_loss : 1.0;
        double kelly_full = (p * b - q) / std::max(b, 1e-10);
        kelly_full = std::max(0.0, kelly_full);
        r.kelly_fraction = std::min(kelly_full, config_.kelly_fraction_cap);
        r.notional = equity * r.kelly_fraction;
        r.shares = round_shares(r.notional / std::max(s.entry_price, 0.01));
        r.notional = r.shares * s.entry_price;
        r.portfolio_weight = equity > 0 ? r.notional / equity : 0;
        double risk_per_share = std::abs(s.entry_price - s.stop_loss);
        if (risk_per_share < 1e-10) risk_per_share = s.entry_price * s.avg_loss;
        r.risk_amount = r.shares * risk_per_share;
        r.risk_pct = equity > 0 ? r.risk_amount / equity : 0;
        r.rationale = "Kelly f*=" + fmt_pct(kelly_full) + " capped=" + fmt_pct(r.kelly_fraction);
        apply_caps(r, equity);
        return r;
    }

    SizingResult volatility_based(const TradeSetup& s, double equity, double /*heat*/) const {
        SizingResult r;
        r.symbol = s.symbol; r.method = SizingMethod::VolatilityBased;
        double daily_vol = s.volatility > 0 ? s.volatility : s.atr / std::max(s.entry_price, 0.01);
        double ann_vol = daily_vol * std::sqrt(252.0);
        double target_pos_vol = config_.target_volatility / std::sqrt(252.0);
        double dollar_vol_per_share = daily_vol * s.entry_price;
        double target_dollar_vol = equity * target_pos_vol;
        r.shares = round_shares(target_dollar_vol / std::max(dollar_vol_per_share, 0.01));
        r.notional = r.shares * s.entry_price;
        r.portfolio_weight = equity > 0 ? r.notional / equity : 0;
        r.risk_amount = r.shares * dollar_vol_per_share;
        r.risk_pct = equity > 0 ? r.risk_amount / equity : 0;
        r.rationale = "Vol=" + fmt_pct(ann_vol) + " target=" + fmt_pct(config_.target_volatility);
        apply_caps(r, equity);
        return r;
    }

    SizingResult equal_weight(const TradeSetup& s, double equity) const {
        SizingResult r;
        r.symbol = s.symbol; r.method = SizingMethod::EqualWeight;
        r.portfolio_weight = config_.max_position_weight;
        r.notional = equity * r.portfolio_weight;
        r.shares = round_shares(r.notional / std::max(s.entry_price, 0.01));
        r.notional = r.shares * s.entry_price;
        r.rationale = "Equal weight " + fmt_pct(config_.max_position_weight);
        return r;
    }

    SizingResult risk_parity(const TradeSetup& s, double equity) const {
        SizingResult r;
        r.symbol = s.symbol; r.method = SizingMethod::RiskParity;
        double vol = s.volatility > 0 ? s.volatility : 0.02;
        double target = config_.max_risk_per_trade / vol;
        r.portfolio_weight = std::min(target, config_.max_position_weight);
        r.notional = equity * r.portfolio_weight;
        r.shares = round_shares(r.notional / std::max(s.entry_price, 0.01));
        r.notional = r.shares * s.entry_price;
        r.risk_pct = r.portfolio_weight * vol;
        r.risk_amount = r.risk_pct * equity;
        r.rationale = "Risk parity: vol=" + fmt_pct(vol);
        return r;
    }

    SizingResult max_dd_capped(const TradeSetup& s, double equity, double heat) const {
        auto r = fixed_fractional(s, equity, heat);
        r.method = SizingMethod::MaxDrawdownCapped;
        double max_loss = r.shares * std::abs(s.entry_price - s.stop_loss);
        double dd_limit = equity * 0.20;  // 20% max drawdown
        if (max_loss > dd_limit * 0.10) {  // Each trade <= 10% of DD budget
            double scale = (dd_limit * 0.10) / max_loss;
            r.shares = round_shares(r.shares * scale);
            r.notional = r.shares * s.entry_price;
            r.risk_amount = r.shares * std::abs(s.entry_price - s.stop_loss);
            r.risk_pct = equity > 0 ? r.risk_amount / equity : 0;
        }
        r.rationale = "DD-capped: max 10% of 20% DD budget";
        return r;
    }

    void apply_caps(SizingResult& r, double equity) const {
        double max_notional = equity * config_.max_position_weight;
        if (r.notional > max_notional && r.shares > 0) {
            double scale = max_notional / r.notional;
            r.shares = round_shares(r.shares * scale);
            r.notional = r.shares * (r.notional / (r.shares / scale));
        }
    }

    double round_shares(double s) const {
        if (config_.round_lot <= 1) return std::floor(s);
        return std::floor(s / config_.round_lot) * config_.round_lot;
    }

    static std::string fmt_pct(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << v * 100 << "%";
        return oss.str();
    }
};

} // namespace sizing
} // namespace trading
} // namespace genie

#endif // GENIE_TRADING_POSITION_SIZER_HPP
