/**
 * @file market_impact.hpp
 * @brief Market Impact Model (Almgren-Chriss)
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements the Almgren-Chriss optimal execution model:
 * - Temporary and permanent market impact
 * - Optimal execution trajectory
 * - Trade-off between market impact and timing risk
 */

#pragma once
#ifndef GENIE_TRADING_MARKET_IMPACT_HPP
#define GENIE_TRADING_MARKET_IMPACT_HPP

#include <cmath>
#include <vector>

namespace genie::trading {

/**
 * @brief Market impact estimation result
 */
struct MarketImpact {
    double temporary_impact{0};      // Price impact during execution (reverts)
    double permanent_impact{0};      // Lasting price change
    double total_cost{0};            // Expected total cost
    double optimal_horizon{0};       // Suggested execution time (minutes)
    double optimal_participation{0}; // Suggested % of volume
    double urgency_penalty{0};       // Extra cost for faster execution
    double risk_premium{0};          // Cost of timing risk
    double execution_risk{0};        // Std dev of execution cost
};

/**
 * @brief Parameters for impact estimation
 */
struct ImpactParams {
    double avg_daily_volume{0};   // ADV in shares
    double volatility{0};         // Daily volatility (decimal)
    double spread{0};             // Bid-ask spread (decimal)
    double urgency{0.5};          // 0=patient, 1=urgent
    double risk_aversion{1e-6};   // Risk aversion parameter
};

/**
 * @brief Execution schedule point
 */
struct TrajectoryPoint {
    double time{0};               // Time as fraction of horizon
    double shares_remaining{0};   // Shares left to execute
    double execution_rate{0};     // Shares per unit time
    double expected_cost{0};      // Cumulative expected cost
};

/**
 * @brief Market Impact Model (Almgren-Chriss)
 * 
 * Based on: Almgren, R. & Chriss, N. (2000) "Optimal Execution of Portfolio Transactions"
 * 
 * The model balances:
 * - Market impact costs (trading faster = more impact)
 * - Timing risk (trading slower = more price uncertainty)
 */
class MarketImpactModel {
    // Default model parameters (can be calibrated)
    double eta_{0.01};      // Temporary impact coefficient
    double gamma_{0.1};     // Permanent impact coefficient
    double epsilon_{0.01};  // Fixed cost component

public:
    /**
     * @brief Set temporary impact coefficient
     */
    void set_temporary_impact(double eta) { eta_ = eta; }
    
    /**
     * @brief Set permanent impact coefficient
     */
    void set_permanent_impact(double gamma) { gamma_ = gamma; }

    /**
     * @brief Estimate market impact
     * 
     * @param quantity Number of shares to trade
     * @param params Market parameters
     * @return MarketImpact estimation
     */
    [[nodiscard]] MarketImpact estimate(
        double quantity,
        const ImpactParams& params
    ) const {
        MarketImpact result;
        
        if (params.avg_daily_volume < 1e-10 || quantity < 1e-10) {
            return result;
        }
        
        // Normalize quantity by ADV
        double adv_ratio = quantity / params.avg_daily_volume;
        
        // Participation rate (default based on square root law)
        result.optimal_participation = std::min(0.25, std::sqrt(adv_ratio) * 0.1);
        
        // Execution horizon (in trading minutes, 390 per day)
        double trading_minutes = 390.0;
        result.optimal_horizon = (1.0 - params.urgency) * (adv_ratio / result.optimal_participation) * trading_minutes;
        result.optimal_horizon = std::max(5.0, std::min(result.optimal_horizon, trading_minutes * 5));  // 5 min to 5 days
        
        // Temporary impact: eta * sigma * sqrt(Q/V)
        result.temporary_impact = eta_ * params.volatility * std::sqrt(adv_ratio);
        
        // Permanent impact: gamma * Q/V
        result.permanent_impact = gamma_ * adv_ratio;
        
        // Total expected cost
        double horizon_days = result.optimal_horizon / trading_minutes;
        
        // Fixed costs (spread)
        double fixed_cost = params.spread / 2.0;
        
        // Impact costs
        double impact_cost = result.temporary_impact + result.permanent_impact / 2.0;
        
        // Timing risk (price variance over execution horizon)
        result.execution_risk = params.volatility * std::sqrt(horizon_days);
        
        // Risk premium for patient trading
        result.risk_premium = params.risk_aversion * result.execution_risk * result.execution_risk * quantity;
        
        // Urgency penalty (trading faster than optimal)
        result.urgency_penalty = 0.0;
        if (params.urgency > 0.5) {
            double urgency_factor = (params.urgency - 0.5) * 2.0;
            result.urgency_penalty = result.temporary_impact * urgency_factor;
        }
        
        result.total_cost = fixed_cost + impact_cost + result.urgency_penalty;
        
        return result;
    }

    /**
     * @brief Generate optimal execution trajectory
     * 
     * Uses the Almgren-Chriss closed-form solution for optimal trading trajectory
     * 
     * @param total_shares Total shares to execute
     * @param num_intervals Number of trading intervals
     * @param params Market parameters
     * @return Vector of trajectory points
     */
    [[nodiscard]] std::vector<TrajectoryPoint> optimal_trajectory(
        double total_shares,
        int num_intervals,
        const ImpactParams& params
    ) const {
        std::vector<TrajectoryPoint> trajectory;
        
        if (total_shares < 1e-10 || num_intervals < 1) {
            return trajectory;
        }
        
        trajectory.reserve(num_intervals + 1);
        
        // Calculate kappa (urgency parameter)
        double kappa = calculate_kappa(params);
        
        // Time step
        double dt = 1.0 / num_intervals;
        
        // Remaining shares at each time step (exponential decay for urgent, linear for patient)
        double shares_remaining = total_shares;
        double cumulative_cost = 0.0;
        
        for (int i = 0; i <= num_intervals; ++i) {
            TrajectoryPoint point;
            point.time = static_cast<double>(i) / num_intervals;
            
            if (i == num_intervals) {
                point.shares_remaining = 0;
                point.execution_rate = 0;
            } else {
                // Almgren-Chriss optimal trajectory
                // x(t) = X * sinh(kappa * (T-t)) / sinh(kappa * T)
                double tau = 1.0 - point.time;  // Time remaining
                
                if (kappa > 1e-10) {
                    point.shares_remaining = total_shares * std::sinh(kappa * tau) / std::sinh(kappa);
                } else {
                    // Linear trajectory for kappa -> 0
                    point.shares_remaining = total_shares * tau;
                }
                
                // Execution rate
                double next_remaining = (i + 1 == num_intervals) ? 0 : 
                    total_shares * std::sinh(kappa * (1.0 - (i + 1.0) / num_intervals)) / std::sinh(kappa);
                point.execution_rate = (point.shares_remaining - next_remaining) / dt;
                
                // Expected cost at this point
                double participation = point.execution_rate / (params.avg_daily_volume / 390.0);
                double instant_impact = eta_ * params.volatility * std::sqrt(std::abs(participation));
                cumulative_cost += instant_impact * point.execution_rate * dt;
            }
            
            point.expected_cost = cumulative_cost;
            trajectory.push_back(point);
            
            shares_remaining = point.shares_remaining;
        }
        
        return trajectory;
    }

    /**
     * @brief Calculate optimal execution horizon
     * 
     * @param quantity Shares to trade
     * @param params Market parameters
     * @return Optimal horizon in minutes
     */
    [[nodiscard]] double optimal_horizon(
        double quantity,
        const ImpactParams& params
    ) const {
        auto impact = estimate(quantity, params);
        return impact.optimal_horizon;
    }

    /**
     * @brief Estimate cost of a specific execution plan
     * 
     * @param quantity Shares to trade
     * @param horizon Execution horizon (minutes)
     * @param params Market parameters
     * @return Expected cost (as decimal of notional)
     */
    [[nodiscard]] double estimate_cost(
        double quantity,
        double horizon,
        const ImpactParams& params
    ) const {
        if (params.avg_daily_volume < 1e-10 || quantity < 1e-10 || horizon < 1e-10) {
            return 0.0;
        }
        
        double trading_minutes = 390.0;
        double horizon_days = horizon / trading_minutes;
        
        // Participation rate
        double participation = quantity / (params.avg_daily_volume * horizon_days);
        
        // Fixed cost
        double fixed = params.spread / 2.0;
        
        // Temporary impact
        double temp = eta_ * params.volatility * std::sqrt(participation);
        
        // Permanent impact
        double perm = gamma_ * (quantity / params.avg_daily_volume) / 2.0;
        
        // Timing risk
        double risk = params.risk_aversion * params.volatility * params.volatility * horizon_days * quantity;
        
        return fixed + temp + perm + risk;
    }

private:
    /**
     * @brief Calculate kappa (urgency parameter) from risk aversion
     */
    [[nodiscard]] double calculate_kappa(const ImpactParams& params) const {
        // kappa = sqrt(lambda * sigma^2 / eta)
        // Higher kappa = more urgent trading
        
        double lambda = params.risk_aversion;
        double sigma = params.volatility;
        
        if (eta_ < 1e-10) return 0.0;
        
        double kappa_sq = lambda * sigma * sigma / eta_;
        
        // Adjust for explicit urgency parameter
        kappa_sq *= (1.0 + params.urgency * 2.0);
        
        return std::sqrt(std::max(0.0, kappa_sq));
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_MARKET_IMPACT_HPP
