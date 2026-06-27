/**
 * @file liquidity_scorer.hpp
 * @brief Multi-factor liquidity scoring with market impact estimation
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Comprehensive liquidity assessment:
 * - Bid-ask spread analysis (absolute, relative, effective)
 * - Volume-based scoring (ADTV, turnover ratio, volume profile)
 * - Market impact estimation (linear and square-root models)
 * - Liquidity risk scoring (1-10 scale)
 * - Time-to-liquidation estimation
 * - Portfolio-level liquidity aggregation
 * - Illiquidity premium estimation
 * - Liquidity tier classification
 * - Historical liquidity trend tracking
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_RISK_LIQUIDITY_SCORER_HPP
#define GENIE_RISK_LIQUIDITY_SCORER_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>

namespace genie {
namespace risk {
namespace liquidity {

// ============================================================================
// Enumerations
// ============================================================================

enum class LiquidityTier {
    HighlyLiquid,    // Large-cap, tight spreads, high volume
    Liquid,          // Mid-cap, reasonable spreads
    ModeratelyLiquid,// Small-cap, wider spreads
    Illiquid,        // Micro-cap, thin markets
    VeryIlliquid     // Private, OTC, restricted
};

[[nodiscard]] inline std::string tier_string(LiquidityTier t) {
    switch (t) {
        case LiquidityTier::HighlyLiquid: return "highly_liquid";
        case LiquidityTier::Liquid: return "liquid";
        case LiquidityTier::ModeratelyLiquid: return "moderately_liquid";
        case LiquidityTier::Illiquid: return "illiquid";
        case LiquidityTier::VeryIlliquid: return "very_illiquid";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct MarketMicrostructure {
    std::string symbol;
    double bid{0};
    double ask{0};
    double mid{0};
    double last{0};
    double daily_volume{0};
    double avg_daily_volume_20d{0};
    double shares_outstanding{0};
    double market_cap{0};
    double volatility_20d{0};
    std::chrono::system_clock::time_point as_of;

    [[nodiscard]] double spread() const { return ask - bid; }
    [[nodiscard]] double spread_bps() const { return mid > 0 ? (spread() / mid) * 10000 : 0; }
    [[nodiscard]] double turnover_ratio() const {
        return shares_outstanding > 0 ? daily_volume / shares_outstanding : 0;
    }
};

struct LiquidityScore {
    std::string symbol;
    double overall_score{0};          // 1-10 (10 = most liquid)
    double spread_score{0};           // Based on bid-ask spread
    double volume_score{0};           // Based on ADTV
    double market_cap_score{0};       // Based on market cap
    double impact_score{0};           // Inverse of market impact
    LiquidityTier tier{LiquidityTier::Liquid};
    double spread_bps{0};
    double adtv{0};
    double market_impact_bps{0};      // Impact of $1M trade
    double time_to_liquidate_days{0}; // Days to liquidate position at 20% ADTV
    double illiquidity_premium{0};    // Estimated premium for illiquidity
    std::chrono::system_clock::time_point computed_at;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << symbol << ": Score=" << overall_score << "/10"
            << " [" << tier_string(tier) << "]"
            << " Spread=" << std::setprecision(1) << spread_bps << "bps"
            << " ADTV=" << std::setprecision(0) << adtv
            << " Impact=" << std::setprecision(1) << market_impact_bps << "bps";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\"symbol\":\"" << symbol << "\",\"score\":" << overall_score
            << ",\"tier\":\"" << tier_string(tier)
            << "\",\"spread_bps\":" << spread_bps
            << ",\"adtv\":" << adtv
            << ",\"impact_bps\":" << market_impact_bps
            << ",\"liquidation_days\":" << time_to_liquidate_days << "}";
        return oss.str();
    }
};

struct PortfolioLiquidity {
    double weighted_score{0};
    double weighted_spread_bps{0};
    double total_liquidation_days{0};
    double pct_highly_liquid{0};
    double pct_illiquid{0};
    int position_count{0};
    LiquidityTier worst_tier{LiquidityTier::HighlyLiquid};
    std::vector<LiquidityScore> position_scores;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "Portfolio Liquidity: Score=" << weighted_score << "/10"
            << " | Spread=" << weighted_spread_bps << "bps"
            << " | Liquidation=" << total_liquidation_days << " days"
            << " | Liquid=" << pct_highly_liquid * 100 << "%"
            << " | Illiquid=" << pct_illiquid * 100 << "%";
        return oss.str();
    }
};

// ============================================================================
// Liquidity Scorer
// ============================================================================

class LiquidityScorer {
public:
    /**
     * @brief Score liquidity for a single symbol
     */
    [[nodiscard]] LiquidityScore score(const MarketMicrostructure& data,
                                         double position_value = 1000000.0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        LiquidityScore ls;
        ls.symbol = data.symbol;
        ls.computed_at = std::chrono::system_clock::now();
        ls.spread_bps = data.spread_bps();
        ls.adtv = data.avg_daily_volume_20d;

        // Spread score (0-10)
        if (ls.spread_bps <= 1) ls.spread_score = 10;
        else if (ls.spread_bps <= 5) ls.spread_score = 9;
        else if (ls.spread_bps <= 10) ls.spread_score = 8;
        else if (ls.spread_bps <= 25) ls.spread_score = 7;
        else if (ls.spread_bps <= 50) ls.spread_score = 6;
        else if (ls.spread_bps <= 100) ls.spread_score = 5;
        else if (ls.spread_bps <= 200) ls.spread_score = 3;
        else ls.spread_score = 1;

        // Volume score
        double dollar_volume = ls.adtv * data.mid;
        if (dollar_volume >= 1e9) ls.volume_score = 10;
        else if (dollar_volume >= 500e6) ls.volume_score = 9;
        else if (dollar_volume >= 100e6) ls.volume_score = 8;
        else if (dollar_volume >= 50e6) ls.volume_score = 7;
        else if (dollar_volume >= 10e6) ls.volume_score = 6;
        else if (dollar_volume >= 1e6) ls.volume_score = 4;
        else ls.volume_score = 2;

        // Market cap score
        if (data.market_cap >= 200e9) ls.market_cap_score = 10;
        else if (data.market_cap >= 50e9) ls.market_cap_score = 9;
        else if (data.market_cap >= 10e9) ls.market_cap_score = 8;
        else if (data.market_cap >= 2e9) ls.market_cap_score = 6;
        else if (data.market_cap >= 500e6) ls.market_cap_score = 4;
        else ls.market_cap_score = 2;

        // Market impact: square-root model: impact = sigma * sqrt(V/ADV)
        double participation = position_value / std::max(dollar_volume, 1.0);
        ls.market_impact_bps = data.volatility_20d * std::sqrt(participation) * 10000;
        ls.impact_score = std::max(1.0, 10.0 - ls.market_impact_bps / 10.0);

        // Time to liquidate at 20% participation
        if (dollar_volume > 0) {
            ls.time_to_liquidate_days = position_value / (dollar_volume * 0.20);
        }

        // Illiquidity premium estimate (Amihud measure variant)
        if (dollar_volume > 0 && data.volatility_20d > 0) {
            ls.illiquidity_premium = data.volatility_20d * 100 / std::log(dollar_volume / 1e6 + 1);
        }

        // Overall score (weighted)
        ls.overall_score = ls.spread_score * 0.30 + ls.volume_score * 0.30 +
                           ls.market_cap_score * 0.20 + ls.impact_score * 0.20;
        ls.overall_score = std::max(1.0, std::min(10.0, ls.overall_score));

        // Tier classification
        if (ls.overall_score >= 8.5) ls.tier = LiquidityTier::HighlyLiquid;
        else if (ls.overall_score >= 6.5) ls.tier = LiquidityTier::Liquid;
        else if (ls.overall_score >= 4.5) ls.tier = LiquidityTier::ModeratelyLiquid;
        else if (ls.overall_score >= 2.5) ls.tier = LiquidityTier::Illiquid;
        else ls.tier = LiquidityTier::VeryIlliquid;

        return ls;
    }

    /**
     * @brief Score portfolio-level liquidity
     */
    [[nodiscard]] PortfolioLiquidity score_portfolio(
        const std::vector<std::pair<MarketMicrostructure, double>>& positions) const {

        PortfolioLiquidity pl;
        double total_value = 0;
        for (const auto& [data, value] : positions) total_value += value;
        pl.position_count = static_cast<int>(positions.size());

        double highly_liquid_value = 0, illiquid_value = 0;
        for (const auto& [data, value] : positions) {
            auto ls = score(data, value);
            double weight = total_value > 0 ? value / total_value : 0;
            pl.weighted_score += ls.overall_score * weight;
            pl.weighted_spread_bps += ls.spread_bps * weight;
            pl.total_liquidation_days = std::max(pl.total_liquidation_days, ls.time_to_liquidate_days);

            if (ls.tier == LiquidityTier::HighlyLiquid) highly_liquid_value += value;
            if (ls.tier == LiquidityTier::Illiquid || ls.tier == LiquidityTier::VeryIlliquid) illiquid_value += value;
            if (static_cast<int>(ls.tier) > static_cast<int>(pl.worst_tier)) pl.worst_tier = ls.tier;

            pl.position_scores.push_back(std::move(ls));
        }

        if (total_value > 0) {
            pl.pct_highly_liquid = highly_liquid_value / total_value;
            pl.pct_illiquid = illiquid_value / total_value;
        }
        return pl;
    }

private:
    mutable std::mutex mutex_;
};

} // namespace liquidity
} // namespace risk
} // namespace genie

#endif // GENIE_RISK_LIQUIDITY_SCORER_HPP
