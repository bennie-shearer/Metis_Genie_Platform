/**
 * @file liquidity_risk.hpp
 * @brief Liquidity Risk Analysis Engine
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides comprehensive liquidity risk measurement and monitoring:
 * - Bid-ask spread analysis and cost estimation
 * - Time-to-liquidation modeling per position
 * - Liquidity-adjusted VaR (LVaR)
 * - Portfolio liquidity scoring (1-10 scale)
 * - Concentration risk metrics
 * - Market depth estimation
 * - Liquidation cost curves
 * - Stress liquidity scenarios (fire sale, market freeze)
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_RISK_LIQUIDITY_RISK_HPP
#define GENIE_RISK_LIQUIDITY_RISK_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <chrono>
#include <sstream>
#include <functional>

namespace genie::risk {

// ---------------------------------------------------------------
// Liquidity profile for a single security
// ---------------------------------------------------------------
struct LiquidityProfile {
    std::string symbol;
    double avg_daily_volume{0.0};
    double avg_bid_ask_spread_bps{0.0};
    double market_cap{0.0};
    double free_float_pct{100.0};
    double avg_trade_size{0.0};
    double volatility_30d{0.0};
    int days_to_full_liquidation{1};
    double liquidity_score{5.0}; // 1-10 scale
    std::string liquidity_tier; // "highly_liquid","liquid","semi_liquid","illiquid"

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"symbol\":\"" << symbol << "\""
           << ",\"avg_daily_volume\":" << avg_daily_volume
           << ",\"avg_bid_ask_spread_bps\":" << avg_bid_ask_spread_bps
           << ",\"market_cap\":" << market_cap
           << ",\"free_float_pct\":" << free_float_pct
           << ",\"days_to_full_liquidation\":" << days_to_full_liquidation
           << ",\"liquidity_score\":" << liquidity_score
           << ",\"liquidity_tier\":\"" << liquidity_tier << "\""
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Liquidation cost estimate
// ---------------------------------------------------------------
struct LiquidationCost {
    std::string symbol;
    double position_value{0.0};
    double spread_cost{0.0};
    double market_impact_cost{0.0};
    double timing_cost{0.0};
    double total_cost{0.0};
    double cost_pct{0.0};
    int estimated_days{1};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"symbol\":\"" << symbol << "\""
           << ",\"position_value\":" << position_value
           << ",\"spread_cost\":" << spread_cost
           << ",\"market_impact_cost\":" << market_impact_cost
           << ",\"timing_cost\":" << timing_cost
           << ",\"total_cost\":" << total_cost
           << ",\"cost_pct\":" << cost_pct
           << ",\"estimated_days\":" << estimated_days
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Portfolio liquidity summary
// ---------------------------------------------------------------
struct PortfolioLiquiditySummary {
    double weighted_liquidity_score{0.0};
    double total_liquidation_cost{0.0};
    double liquidation_cost_pct{0.0};
    double lvar_95{0.0}; // Liquidity-adjusted VaR
    double lvar_99{0.0};
    double pct_highly_liquid{0.0};
    double pct_liquid{0.0};
    double pct_semi_liquid{0.0};
    double pct_illiquid{0.0};
    int max_days_to_liquidate{0};
    double concentration_hhi{0.0}; // Herfindahl-Hirschman Index
    std::vector<LiquidationCost> position_costs;
    std::vector<LiquidityProfile> profiles;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"weighted_liquidity_score\":" << weighted_liquidity_score
           << ",\"total_liquidation_cost\":" << total_liquidation_cost
           << ",\"liquidation_cost_pct\":" << liquidation_cost_pct
           << ",\"lvar_95\":" << lvar_95
           << ",\"lvar_99\":" << lvar_99
           << ",\"pct_highly_liquid\":" << pct_highly_liquid
           << ",\"pct_liquid\":" << pct_liquid
           << ",\"pct_semi_liquid\":" << pct_semi_liquid
           << ",\"pct_illiquid\":" << pct_illiquid
           << ",\"max_days_to_liquidate\":" << max_days_to_liquidate
           << ",\"concentration_hhi\":" << concentration_hhi
           << ",\"position_count\":" << position_costs.size()
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Position input for liquidity analysis
// ---------------------------------------------------------------
struct LiquidityPosition {
    std::string symbol;
    double quantity{0.0};
    double market_price{0.0};
    double avg_daily_volume{0.0};
    double bid_ask_spread_bps{5.0};
    double market_cap{0.0};
    double volatility_30d{0.20};

    double market_value() const { return quantity * market_price; }
};

// ---------------------------------------------------------------
// Liquidity stress scenario
// ---------------------------------------------------------------
struct LiquidityStressResult {
    std::string scenario_name;
    double normal_liquidation_cost{0.0};
    double stressed_liquidation_cost{0.0};
    double cost_multiplier{1.0};
    int normal_days{0};
    int stressed_days{0};
    double lvar_stressed{0.0};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"scenario\":\"" << scenario_name << "\""
           << ",\"normal_cost\":" << normal_liquidation_cost
           << ",\"stressed_cost\":" << stressed_liquidation_cost
           << ",\"cost_multiplier\":" << cost_multiplier
           << ",\"normal_days\":" << normal_days
           << ",\"stressed_days\":" << stressed_days
           << ",\"lvar_stressed\":" << lvar_stressed
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Liquidity Risk Engine
// ---------------------------------------------------------------
class LiquidityRiskEngine {
public:
    // Maximum participation rate (fraction of ADV per day)
    void set_max_participation_rate(double rate) {
        std::lock_guard<std::mutex> lock(mtx_);
        max_participation_rate_ = std::clamp(rate, 0.01, 0.50);
    }

    // Calculate liquidity profile for a single position
    LiquidityProfile calculate_profile(const LiquidityPosition& pos) const {
        LiquidityProfile profile;
        profile.symbol = pos.symbol;
        profile.avg_daily_volume = pos.avg_daily_volume;
        profile.avg_bid_ask_spread_bps = pos.bid_ask_spread_bps;
        profile.market_cap = pos.market_cap;
        profile.volatility_30d = pos.volatility_30d;

        // Days to liquidate based on participation rate
        double daily_capacity = pos.avg_daily_volume * max_participation_rate_ * pos.market_price;
        double value = pos.market_value();
        profile.days_to_full_liquidation = (daily_capacity > 0.0)
            ? std::max(1, static_cast<int>(std::ceil(value / daily_capacity)))
            : 999;

        // Liquidity score (1-10)
        profile.liquidity_score = compute_liquidity_score(pos);

        // Tier assignment
        if (profile.liquidity_score >= 8.0) {
            profile.liquidity_tier = "highly_liquid";
        } else if (profile.liquidity_score >= 5.0) {
            profile.liquidity_tier = "liquid";
        } else if (profile.liquidity_score >= 3.0) {
            profile.liquidity_tier = "semi_liquid";
        } else {
            profile.liquidity_tier = "illiquid";
        }

        return profile;
    }

    // Calculate liquidation cost for a position
    LiquidationCost calculate_liquidation_cost(const LiquidityPosition& pos) const {
        LiquidationCost cost;
        cost.symbol = pos.symbol;
        cost.position_value = pos.market_value();

        // Spread cost: half the bid-ask spread
        cost.spread_cost = cost.position_value * (pos.bid_ask_spread_bps / 10000.0) * 0.5;

        // Market impact (Almgren-Chriss square root model)
        double participation = (pos.avg_daily_volume > 0.0)
            ? pos.quantity / pos.avg_daily_volume
            : 1.0;
        double impact_bps = 10.0 * pos.volatility_30d * std::sqrt(participation) * 10000.0;
        cost.market_impact_cost = cost.position_value * (impact_bps / 10000.0);

        // Timing cost (volatility during liquidation period)
        double daily_capacity = pos.avg_daily_volume * max_participation_rate_ * pos.market_price;
        cost.estimated_days = (daily_capacity > 0.0)
            ? std::max(1, static_cast<int>(std::ceil(cost.position_value / daily_capacity)))
            : 30;
        double daily_vol = pos.volatility_30d / std::sqrt(252.0);
        cost.timing_cost = cost.position_value * daily_vol * std::sqrt(static_cast<double>(cost.estimated_days));

        cost.total_cost = cost.spread_cost + cost.market_impact_cost + cost.timing_cost;
        cost.cost_pct = (cost.position_value > 0.0)
            ? (cost.total_cost / cost.position_value) * 100.0
            : 0.0;

        return cost;
    }

    // Full portfolio liquidity analysis
    PortfolioLiquiditySummary analyze_portfolio(const std::vector<LiquidityPosition>& positions) const {
        PortfolioLiquiditySummary summary;
        if (positions.empty()) return summary;

        double total_value = 0.0;
        for (const auto& pos : positions) {
            total_value += pos.market_value();
        }

        double weighted_score_sum = 0.0;
        double hl_value = 0.0, l_value = 0.0, sl_value = 0.0, il_value = 0.0;
        double hhi_sum = 0.0;

        for (const auto& pos : positions) {
            auto profile = calculate_profile(pos);
            auto cost = calculate_liquidation_cost(pos);

            summary.profiles.push_back(profile);
            summary.position_costs.push_back(cost);
            summary.total_liquidation_cost += cost.total_cost;

            double weight = (total_value > 0.0) ? pos.market_value() / total_value : 0.0;
            weighted_score_sum += profile.liquidity_score * weight;

            // Weight squared for HHI
            hhi_sum += weight * weight;

            // Tier buckets
            if (profile.liquidity_tier == "highly_liquid") hl_value += pos.market_value();
            else if (profile.liquidity_tier == "liquid") l_value += pos.market_value();
            else if (profile.liquidity_tier == "semi_liquid") sl_value += pos.market_value();
            else il_value += pos.market_value();

            if (cost.estimated_days > summary.max_days_to_liquidate) {
                summary.max_days_to_liquidate = cost.estimated_days;
            }
        }

        summary.weighted_liquidity_score = weighted_score_sum;
        summary.concentration_hhi = hhi_sum * 10000.0; // Normalized to 10000 scale
        summary.liquidation_cost_pct = (total_value > 0.0)
            ? (summary.total_liquidation_cost / total_value) * 100.0
            : 0.0;

        if (total_value > 0.0) {
            summary.pct_highly_liquid = (hl_value / total_value) * 100.0;
            summary.pct_liquid = (l_value / total_value) * 100.0;
            summary.pct_semi_liquid = (sl_value / total_value) * 100.0;
            summary.pct_illiquid = (il_value / total_value) * 100.0;
        }

        // Liquidity-adjusted VaR: VaR + liquidation cost
        // Simplified parametric VaR
        double portfolio_vol = estimate_portfolio_volatility(positions);
        double var_95 = total_value * portfolio_vol * 1.645;
        double var_99 = total_value * portfolio_vol * 2.326;
        summary.lvar_95 = var_95 + summary.total_liquidation_cost;
        summary.lvar_99 = var_99 + summary.total_liquidation_cost;

        return summary;
    }

    // Stress scenarios
    std::vector<LiquidityStressResult> run_stress_scenarios(
        const std::vector<LiquidityPosition>& positions) const
    {
        std::vector<LiquidityStressResult> results;

        auto normal = analyze_portfolio(positions);

        // Scenario 1: Spread widening (3x)
        {
            auto stressed = positions;
            for (auto& p : stressed) p.bid_ask_spread_bps *= 3.0;
            auto s = analyze_portfolio(stressed);
            LiquidityStressResult r;
            r.scenario_name = "spread_widening_3x";
            r.normal_liquidation_cost = normal.total_liquidation_cost;
            r.stressed_liquidation_cost = s.total_liquidation_cost;
            r.cost_multiplier = (normal.total_liquidation_cost > 0.0)
                ? s.total_liquidation_cost / normal.total_liquidation_cost : 1.0;
            r.normal_days = normal.max_days_to_liquidate;
            r.stressed_days = s.max_days_to_liquidate;
            r.lvar_stressed = s.lvar_99;
            results.push_back(r);
        }

        // Scenario 2: Volume drought (50% reduction)
        {
            auto stressed = positions;
            for (auto& p : stressed) p.avg_daily_volume *= 0.5;
            auto s = analyze_portfolio(stressed);
            LiquidityStressResult r;
            r.scenario_name = "volume_drought_50pct";
            r.normal_liquidation_cost = normal.total_liquidation_cost;
            r.stressed_liquidation_cost = s.total_liquidation_cost;
            r.cost_multiplier = (normal.total_liquidation_cost > 0.0)
                ? s.total_liquidation_cost / normal.total_liquidation_cost : 1.0;
            r.normal_days = normal.max_days_to_liquidate;
            r.stressed_days = s.max_days_to_liquidate;
            r.lvar_stressed = s.lvar_99;
            results.push_back(r);
        }

        // Scenario 3: Fire sale (5x spread + 75% volume reduction)
        {
            auto stressed = positions;
            for (auto& p : stressed) {
                p.bid_ask_spread_bps *= 5.0;
                p.avg_daily_volume *= 0.25;
                p.volatility_30d *= 2.0;
            }
            auto s = analyze_portfolio(stressed);
            LiquidityStressResult r;
            r.scenario_name = "fire_sale";
            r.normal_liquidation_cost = normal.total_liquidation_cost;
            r.stressed_liquidation_cost = s.total_liquidation_cost;
            r.cost_multiplier = (normal.total_liquidation_cost > 0.0)
                ? s.total_liquidation_cost / normal.total_liquidation_cost : 1.0;
            r.normal_days = normal.max_days_to_liquidate;
            r.stressed_days = s.max_days_to_liquidate;
            r.lvar_stressed = s.lvar_99;
            results.push_back(r);
        }

        return results;
    }

private:
    mutable std::mutex mtx_;
    double max_participation_rate_{0.10}; // 10% of ADV

    double compute_liquidity_score(const LiquidityPosition& pos) const {
        double score = 5.0;

        // Volume factor (higher volume = more liquid)
        if (pos.avg_daily_volume > 10'000'000) score += 2.0;
        else if (pos.avg_daily_volume > 1'000'000) score += 1.0;
        else if (pos.avg_daily_volume < 100'000) score -= 2.0;
        else if (pos.avg_daily_volume < 500'000) score -= 1.0;

        // Spread factor (tighter = more liquid)
        if (pos.bid_ask_spread_bps < 5.0) score += 1.5;
        else if (pos.bid_ask_spread_bps < 15.0) score += 0.5;
        else if (pos.bid_ask_spread_bps > 100.0) score -= 2.0;
        else if (pos.bid_ask_spread_bps > 50.0) score -= 1.0;

        // Market cap factor
        if (pos.market_cap > 50e9) score += 1.0;
        else if (pos.market_cap > 10e9) score += 0.5;
        else if (pos.market_cap < 500e6) score -= 1.5;
        else if (pos.market_cap < 2e9) score -= 0.5;

        return std::clamp(score, 1.0, 10.0);
    }

    double estimate_portfolio_volatility(const std::vector<LiquidityPosition>& positions) const {
        if (positions.empty()) return 0.0;
        double total_value = 0.0;
        for (const auto& p : positions) total_value += p.market_value();
        if (total_value <= 0.0) return 0.0;

        // Weighted average volatility (simplified, assumes moderate correlation)
        double weighted_vol = 0.0;
        for (const auto& p : positions) {
            double w = p.market_value() / total_value;
            weighted_vol += w * p.volatility_30d;
        }
        // Apply diversification benefit (assume avg correlation ~0.4)
        double n = static_cast<double>(positions.size());
        double diversification = std::sqrt((1.0 / n) + ((n - 1.0) / n) * 0.4);
        return weighted_vol * diversification / std::sqrt(252.0);
    }
};

} // namespace genie::risk

#endif // GENIE_RISK_LIQUIDITY_RISK_HPP
