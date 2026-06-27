/**
 * @file tca.hpp
 * @brief Transaction Cost Analysis (TCA)
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements institutional TCA:
 * - Implementation shortfall decomposition
 * - Arrival cost analysis
 * - Market impact estimation
 * - VWAP/TWAP benchmarking
 * - Execution quality metrics
 */

#pragma once
#ifndef GENIE_TRADING_TCA_HPP
#define GENIE_TRADING_TCA_HPP

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace genie::trading {

/**
 * @brief Market tick data for TCA
 */
struct MarketTick {
    int64_t timestamp{0};  // Unix timestamp (ms)
    double bid{0};
    double ask{0};
    double last{0};
    double volume{0};
    
    [[nodiscard]] double mid() const { return (bid + ask) / 2.0; }
    [[nodiscard]] double spread() const { return ask - bid; }
};

/**
 * @brief Order fill information
 */
struct Fill {
    int64_t timestamp{0};
    double quantity{0};
    double price{0};
    std::string venue;
    double commission{0};
};

/**
 * @brief Order information for TCA
 */
struct TCAOrder {
    std::string order_id;
    std::string symbol;
    bool is_buy{true};
    double total_quantity{0};
    double filled_quantity{0};
    int64_t decision_time{0};    // When decision to trade was made
    int64_t arrival_time{0};     // When order hit the market
    int64_t completion_time{0};  // When fully executed or cancelled
    double decision_price{0};    // Price at decision
    double limit_price{0};       // Limit price (if any)
    std::vector<Fill> fills;
};

/**
 * @brief TCA analysis result
 */
struct TCAResult {
    std::string order_id;
    std::string symbol;
    bool is_buy{true};
    
    // Quantities
    double ordered_quantity{0};
    double filled_quantity{0};
    double fill_rate{0};         // % filled
    
    // Benchmark Prices
    double decision_price{0};    // Price at decision time
    double arrival_price{0};     // Price when order arrived
    double vwap_price{0};        // Volume-weighted average price
    double twap_price{0};        // Time-weighted average price
    double close_price{0};       // Closing price
    double average_fill_price{0}; // Actual execution price
    
    // Implementation Shortfall Components (basis points)
    double implementation_shortfall{0};  // Total cost vs decision
    double delay_cost{0};                // Cost of delay (decision to arrival)
    double trading_cost{0};              // Cost during trading
    double timing_cost{0};               // Market movement during execution
    double market_impact{0};             // Price impact of trading
    double spread_cost{0};               // Bid-ask spread cost
    double opportunity_cost{0};          // Cost of unfilled portion
    
    // Execution Quality Metrics
    double price_improvement{0};   // vs quoted spread
    double effective_spread{0};    // Realized spread
    double reversion{0};           // Price reversion after execution
    
    // Costs in currency
    double total_cost_bps{0};
    double total_cost_currency{0};
    
    /**
     * @brief Convert costs to basis points
     */
    [[nodiscard]] double to_bps(double cost) const {
        if (decision_price < 1e-10) return 0.0;
        return (cost / decision_price) * 10000.0;
    }
};

/**
 * @brief TCA analysis engine
 */
class TCAEngine {
public:
    /**
     * @brief Analyze a single order
     */
    [[nodiscard]] TCAResult analyze(
        const TCAOrder& order,
        const std::vector<MarketTick>& market_data
    ) const {
        TCAResult result;
        result.order_id = order.order_id;
        result.symbol = order.symbol;
        result.is_buy = order.is_buy;
        result.ordered_quantity = order.total_quantity;
        result.filled_quantity = order.filled_quantity;
        
        if (order.total_quantity > 0) {
            result.fill_rate = order.filled_quantity / order.total_quantity * 100.0;
        }
        
        result.decision_price = order.decision_price;
        
        // Calculate average fill price
        double total_value = 0.0;
        for (const auto& fill : order.fills) {
            total_value += fill.quantity * fill.price;
        }
        if (order.filled_quantity > 0) {
            result.average_fill_price = total_value / order.filled_quantity;
        }
        
        // Find arrival price
        result.arrival_price = find_price_at_time(market_data, order.arrival_time);
        if (result.arrival_price == 0 && !market_data.empty()) {
            result.arrival_price = market_data.front().mid();
        }
        
        // Calculate VWAP
        result.vwap_price = calculate_vwap(market_data, order.arrival_time, order.completion_time);
        if (result.vwap_price == 0) result.vwap_price = result.arrival_price;
        
        // Calculate TWAP
        result.twap_price = calculate_twap(market_data, order.arrival_time, order.completion_time);
        if (result.twap_price == 0) result.twap_price = result.arrival_price;
        
        // Get closing price
        if (!market_data.empty()) {
            result.close_price = market_data.back().mid();
        }
        
        // Calculate implementation shortfall components
        int side = order.is_buy ? 1 : -1;
        
        // Delay cost: (arrival - decision) * side
        result.delay_cost = (result.arrival_price - result.decision_price) * side;
        
        // Trading cost: (fill - arrival) * side
        result.trading_cost = (result.average_fill_price - result.arrival_price) * side;
        
        // Total implementation shortfall
        result.implementation_shortfall = (result.average_fill_price - result.decision_price) * side;
        
        // Market impact: estimated using price movement
        result.market_impact = estimate_market_impact(order, market_data);
        
        // Timing cost: market movement independent of our trading
        result.timing_cost = result.trading_cost - result.market_impact;
        
        // Spread cost
        double avg_spread = calculate_avg_spread(market_data, order.arrival_time, order.completion_time);
        result.spread_cost = avg_spread / 2.0;  // Half spread for one-way
        
        // Opportunity cost (unfilled portion)
        if (order.filled_quantity < order.total_quantity) {
            double unfilled = order.total_quantity - order.filled_quantity;
            double unfilled_pct = unfilled / order.total_quantity;
            result.opportunity_cost = (result.close_price - result.decision_price) * side * unfilled_pct;
        }
        
        // Convert to basis points
        result.total_cost_bps = result.to_bps(result.implementation_shortfall);
        result.total_cost_currency = result.implementation_shortfall * order.filled_quantity;
        
        // Execution quality
        result.effective_spread = 2.0 * std::abs(result.average_fill_price - result.vwap_price);
        result.price_improvement = avg_spread / 2.0 - std::abs(result.average_fill_price - result.arrival_price);
        
        // Reversion: price movement after execution
        if (!market_data.empty() && order.completion_time > 0) {
            double post_price = find_price_at_time(market_data, order.completion_time + 300000);  // 5 min later
            if (post_price > 0) {
                result.reversion = (post_price - result.average_fill_price) * side;
            }
        }
        
        return result;
    }

    /**
     * @brief Analyze multiple orders
     */
    [[nodiscard]] std::vector<TCAResult> analyze_batch(
        const std::vector<TCAOrder>& orders,
        const std::map<std::string, std::vector<MarketTick>>& market_data
    ) const {
        std::vector<TCAResult> results;
        results.reserve(orders.size());
        
        for (const auto& order : orders) {
            auto it = market_data.find(order.symbol);
            if (it != market_data.end()) {
                results.push_back(analyze(order, it->second));
            }
        }
        
        return results;
    }

    /**
     * @brief Calculate aggregate TCA statistics
     */
    struct AggregateStats {
        int order_count{0};
        double total_value{0};
        double avg_fill_rate{0};
        double avg_implementation_shortfall_bps{0};
        double avg_market_impact_bps{0};
        double total_cost_currency{0};
    };

    [[nodiscard]] AggregateStats aggregate(const std::vector<TCAResult>& results) const {
        AggregateStats stats;
        
        if (results.empty()) return stats;
        
        stats.order_count = static_cast<int>(results.size());
        
        double sum_fill_rate = 0.0;
        double sum_is_bps = 0.0;
        double sum_mi_bps = 0.0;
        
        for (const auto& r : results) {
            stats.total_value += r.filled_quantity * r.average_fill_price;
            sum_fill_rate += r.fill_rate;
            sum_is_bps += r.total_cost_bps;
            sum_mi_bps += r.to_bps(r.market_impact);
            stats.total_cost_currency += r.total_cost_currency;
        }
        
        stats.avg_fill_rate = sum_fill_rate / stats.order_count;
        stats.avg_implementation_shortfall_bps = sum_is_bps / stats.order_count;
        stats.avg_market_impact_bps = sum_mi_bps / stats.order_count;
        
        return stats;
    }

private:
    [[nodiscard]] double find_price_at_time(
        const std::vector<MarketTick>& ticks,
        int64_t timestamp
    ) const {
        for (const auto& tick : ticks) {
            if (tick.timestamp >= timestamp) {
                return tick.mid();
            }
        }
        return 0.0;
    }

    [[nodiscard]] double calculate_vwap(
        const std::vector<MarketTick>& ticks,
        int64_t start,
        int64_t end
    ) const {
        double sum_pv = 0.0;
        double sum_v = 0.0;
        
        for (const auto& tick : ticks) {
            if (tick.timestamp >= start && tick.timestamp <= end && tick.volume > 0) {
                sum_pv += tick.mid() * tick.volume;
                sum_v += tick.volume;
            }
        }
        
        return sum_v > 0 ? sum_pv / sum_v : 0.0;
    }

    [[nodiscard]] double calculate_twap(
        const std::vector<MarketTick>& ticks,
        int64_t start,
        int64_t end
    ) const {
        double sum = 0.0;
        int count = 0;
        
        for (const auto& tick : ticks) {
            if (tick.timestamp >= start && tick.timestamp <= end) {
                sum += tick.mid();
                ++count;
            }
        }
        
        return count > 0 ? sum / count : 0.0;
    }

    [[nodiscard]] double calculate_avg_spread(
        const std::vector<MarketTick>& ticks,
        int64_t start,
        int64_t end
    ) const {
        double sum = 0.0;
        int count = 0;
        
        for (const auto& tick : ticks) {
            if (tick.timestamp >= start && tick.timestamp <= end) {
                sum += tick.spread();
                ++count;
            }
        }
        
        return count > 0 ? sum / count : 0.0;
    }

    [[nodiscard]] double estimate_market_impact(
        const TCAOrder& order,
        const std::vector<MarketTick>& market_data
    ) const {
        // Simple market impact estimation
        // More sophisticated models (Almgren-Chriss) are in market_impact.hpp
        
        if (market_data.empty() || order.fills.empty()) return 0.0;
        
        // Calculate average daily volume
        double total_volume = 0.0;
        for (const auto& tick : market_data) {
            total_volume += tick.volume;
        }
        
        if (total_volume < 1e-10) return 0.0;
        
        // Participation rate
        double participation = order.filled_quantity / total_volume;
        
        // Temporary impact estimate (square root law)
        double volatility = estimate_volatility(market_data);
        double temp_impact = volatility * std::sqrt(participation);
        
        return temp_impact * (order.is_buy ? 1 : -1);
    }

    [[nodiscard]] double estimate_volatility(const std::vector<MarketTick>& ticks) const {
        if (ticks.size() < 2) return 0.0;
        
        std::vector<double> returns;
        returns.reserve(ticks.size() - 1);
        
        for (size_t i = 1; i < ticks.size(); ++i) {
            if (ticks[i-1].mid() > 1e-10) {
                returns.push_back(std::log(ticks[i].mid() / ticks[i-1].mid()));
            }
        }
        
        if (returns.empty()) return 0.0;
        
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double sum_sq = 0.0;
        for (double r : returns) {
            sum_sq += (r - mean) * (r - mean);
        }
        
        return std::sqrt(sum_sq / returns.size());
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_TCA_HPP
