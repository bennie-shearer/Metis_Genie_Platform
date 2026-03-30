/**
 * @file smart_router.hpp
 * @brief Smart order routing for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Venue selection, algorithmic execution (TWAP/VWAP/Iceberg),
 * dark pool awareness, and transaction cost analysis.
 */
#pragma once
#ifndef GENIE_TRADING_SMART_ROUTER_HPP
#define GENIE_TRADING_SMART_ROUTER_HPP

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <chrono>
#include <functional>

namespace genie::trading {

/** Execution venue */
struct Venue {
    std::string id;
    std::string name;
    std::string type;           // "Exchange", "DarkPool", "ATS", "Internal"
    double fee_per_share{0};    // in dollars
    double rebate_per_share{0}; // maker rebate
    double avg_fill_rate{0.85}; // % of orders filled
    double avg_latency_ms{5.0}; // typical round-trip
    bool dark_pool{false};
    double avg_spread_bps{10};  // typical spread in basis points
    double market_share{0};     // % of total volume
    bool active{true};
    
    // Extended venue analytics (used by algo execution engine)
    double avg_displayed_size{500};     // Average displayed liquidity
    double avg_hidden_liquidity{0.3};   // Hidden liquidity ratio
    bool supports_hidden{false};        // Supports hidden/iceberg orders
    bool supports_midpoint{false};      // Supports midpoint pegging
    double price_improvement_bps{0};    // Average price improvement
    
    /**
     * @brief Estimate effective cost for given passivity
     */
    double effective_cost(bool passive = false) const {
        if (passive) return fee_per_share - rebate_per_share;
        return fee_per_share;
    }
};

/** Execution algorithm types */
enum class AlgoType { Market, Limit, TWAP, VWAP, Iceberg, POV, BestExecution, IS, Arrival, Close };

inline std::string algo_name(AlgoType a) {
    switch (a) {
        case AlgoType::Market: return "Market"; case AlgoType::Limit: return "Limit";
        case AlgoType::TWAP: return "TWAP"; case AlgoType::VWAP: return "VWAP";
        case AlgoType::Iceberg: return "Iceberg"; case AlgoType::POV: return "POV";
        case AlgoType::BestExecution: return "BestExec";
        case AlgoType::IS: return "IS";
        case AlgoType::Arrival: return "Arrival";
        case AlgoType::Close: return "Close";
        default: return "Unknown";
    }
}

/** Algorithm parameters */
struct AlgoParams {
    AlgoType type{AlgoType::Market};
    double limit_price{0};
    int duration_seconds{300};   // for TWAP/VWAP
    int num_slices{10};          // for TWAP
    double display_qty{0};       // for Iceberg (visible portion)
    double pov_target{0.10};     // for POV (10% of volume)
    double urgency{0.5};         // 0=passive, 1=aggressive
    bool allow_dark_pools{true};
    double max_spread_bps{50};   // max acceptable spread
};

/** A child order (slice of parent) */
struct ChildOrder {
    std::string id;
    std::string parent_id;
    std::string venue_id;
    std::string side;
    double quantity{0};
    double limit_price{0};
    int slice_number{0};
    std::string status{"Pending"}; // Pending, Sent, Filled, Cancelled
    double fill_price{0};
    double fill_qty{0};
    std::chrono::system_clock::time_point scheduled;
    std::chrono::system_clock::time_point executed;
};

/** Route decision for a single order */
struct RouteDecision {
    std::string order_id;
    std::string security_id;
    std::string side;
    double total_quantity{0};
    AlgoType algorithm{AlgoType::Market};
    std::vector<ChildOrder> children;
    double estimated_cost_bps{0};
    std::string rationale;
};

/** Transaction cost analysis */
struct TcaResult {
    std::string order_id;
    double arrival_price{0};     // price at order entry
    double avg_fill_price{0};    // actual execution price
    double vwap{0};              // market VWAP during execution
    double implementation_shortfall_bps{0}; // (fill - arrival) / arrival * 10000
    double vs_vwap_bps{0};       // (fill - vwap) / vwap * 10000
    double market_impact_bps{0}; // estimated market impact
    double commission_bps{0};    // commission as bps of notional
    double total_cost_bps{0};    // all-in cost
    double slippage{0};          // absolute $ slippage
};

/** Smart order router */
class SmartRouter {
    std::map<std::string, Venue> venues_;
    int order_counter_{0};

public:
    SmartRouter() { load_default_venues(); }

    /** Add or update a venue */
    void add_venue(const Venue& v) { venues_[v.id] = v; }

    /** Remove a venue */
    void remove_venue(const std::string& id) { venues_.erase(id); }

    /** Route an order using specified algorithm */
    [[nodiscard]] RouteDecision route(
            const std::string& security_id, const std::string& side,
            double quantity, double current_price,
            const AlgoParams& params = AlgoParams()) {

        RouteDecision decision;
        decision.order_id = "R-" + std::to_string(++order_counter_);
        decision.security_id = security_id;
        decision.side = side;
        decision.total_quantity = quantity;
        decision.algorithm = params.type;

        // Select venues
        auto ranked = rank_venues(security_id, side, quantity, current_price, params);
        if (ranked.empty()) {
            decision.rationale = "No suitable venues";
            return decision;
        }

        switch (params.type) {
            case AlgoType::Market:
            case AlgoType::Limit:
                // Route entire order to best venue
                decision.children.push_back(create_child(
                    decision.order_id, ranked[0].id, side, quantity,
                    (params.type == AlgoType::Limit) ? params.limit_price : 0, 1));
                decision.rationale = "Direct route to " + ranked[0].name;
                break;

            case AlgoType::TWAP: {
                // Slice evenly across time
                double slice_qty = quantity / params.num_slices;
                auto interval = std::chrono::seconds(params.duration_seconds / params.num_slices);
                auto start = std::chrono::system_clock::now();
                for (int i = 0; i < params.num_slices; ++i) {
                    const auto& venue = ranked[i % ranked.size()];
                    auto child = create_child(decision.order_id, venue.id, side,
                                              (i == params.num_slices - 1) ? (quantity - slice_qty * i) : slice_qty,
                                              0, i + 1);
                    child.scheduled = start + interval * i;
                    decision.children.push_back(child);
                }
                decision.rationale = "TWAP: " + std::to_string(params.num_slices) + " slices over "
                                   + std::to_string(params.duration_seconds) + "s";
                break;
            }

            case AlgoType::Iceberg: {
                double display = (params.display_qty > 0) ? params.display_qty : quantity * 0.1;
                int slices = static_cast<int>(std::ceil(quantity / display));
                for (int i = 0; i < slices; ++i) {
                    double sq = std::min(display, quantity - i * display);
                    const auto& venue = ranked[0]; // Iceberg stays on one venue
                    decision.children.push_back(create_child(
                        decision.order_id, venue.id, side, sq, params.limit_price, i + 1));
                }
                decision.rationale = "Iceberg: " + std::to_string(slices) + " slices, display "
                                   + std::to_string(static_cast<int>(display));
                break;
            }

            case AlgoType::BestExecution: {
                // Split across venues proportional to fill rate * (1 - fee)
                double total_score = 0;
                for (const auto& v : ranked)
                    total_score += v.avg_fill_rate * (1.0 - v.fee_per_share / current_price * 10000);
                for (const auto& v : ranked) {
                    double score = v.avg_fill_rate * (1.0 - v.fee_per_share / current_price * 10000);
                    double alloc = quantity * (score / total_score);
                    if (alloc >= 1.0) {
                        decision.children.push_back(create_child(
                            decision.order_id, v.id, side, std::round(alloc), 0,
                            static_cast<int>(decision.children.size() + 1)));
                    }
                }
                decision.rationale = "BestExec: split across " + std::to_string(decision.children.size()) + " venues";
                break;
            }

            default:
                decision.children.push_back(create_child(
                    decision.order_id, ranked[0].id, side, quantity, 0, 1));
                decision.rationale = "Default: " + ranked[0].name;
                break;
        }

        // Estimate cost
        decision.estimated_cost_bps = estimate_cost(decision, current_price);
        return decision;
    }

    /** Calculate transaction cost analysis */
    [[nodiscard]] TcaResult analyze(const RouteDecision& decision,
                                     double arrival_price, double market_vwap) const {
        TcaResult tca;
        tca.order_id = decision.order_id;
        tca.arrival_price = arrival_price;
        tca.vwap = market_vwap;

        double total_qty = 0, total_value = 0, total_commission = 0;
        for (const auto& child : decision.children) {
            double fp = (child.fill_price > 0) ? child.fill_price : arrival_price;
            double fq = (child.fill_qty > 0) ? child.fill_qty : child.quantity;
            total_qty += fq;
            total_value += fq * fp;
            auto it = venues_.find(child.venue_id);
            if (it != venues_.end()) total_commission += fq * it->second.fee_per_share;
        }

        tca.avg_fill_price = (total_qty > 0) ? total_value / total_qty : arrival_price;
        double notional = total_qty * arrival_price;

        tca.implementation_shortfall_bps = (tca.avg_fill_price - arrival_price) / arrival_price * 10000;
        if (decision.side == "Sell") tca.implementation_shortfall_bps = -tca.implementation_shortfall_bps;

        tca.vs_vwap_bps = (tca.avg_fill_price - market_vwap) / market_vwap * 10000;
        if (decision.side == "Sell") tca.vs_vwap_bps = -tca.vs_vwap_bps;

        tca.commission_bps = (notional > 0) ? total_commission / notional * 10000 : 0;
        tca.market_impact_bps = std::abs(tca.implementation_shortfall_bps) * 0.6; // simplified
        tca.total_cost_bps = std::abs(tca.implementation_shortfall_bps) + tca.commission_bps;
        tca.slippage = (tca.avg_fill_price - arrival_price) * total_qty;

        return tca;
    }

    [[nodiscard]] size_t venue_count() const { return venues_.size(); }
    [[nodiscard]] const std::map<std::string, Venue>& venues() const { return venues_; }

private:
    ChildOrder create_child(const std::string& parent, const std::string& venue,
                            const std::string& side, double qty, double px, int slice) {
        ChildOrder c;
        c.id = parent + "-" + std::to_string(slice);
        c.parent_id = parent;
        c.venue_id = venue;
        c.side = side;
        c.quantity = qty;
        c.limit_price = px;
        c.slice_number = slice;
        return c;
    }

    [[nodiscard]] std::vector<Venue> rank_venues(
            const std::string& /*security*/, const std::string& /*side*/,
            double /*qty*/, double /*price*/, const AlgoParams& params) const {
        std::vector<Venue> ranked;
        for (const auto& [id, v] : venues_) {
            if (!v.active) continue;
            if (!params.allow_dark_pools && v.dark_pool) continue;
            if (v.avg_spread_bps > params.max_spread_bps) continue;
            ranked.push_back(v);
        }
        // Sort by: fill_rate desc, then fee asc
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if (std::abs(a.avg_fill_rate - b.avg_fill_rate) > 0.01)
                return a.avg_fill_rate > b.avg_fill_rate;
            return a.fee_per_share < b.fee_per_share;
        });
        return ranked;
    }

    [[nodiscard]] double estimate_cost(const RouteDecision& decision, double price) const {
        double total_fee = 0, total_qty = 0;
        for (const auto& child : decision.children) {
            auto it = venues_.find(child.venue_id);
            if (it != venues_.end())
                total_fee += child.quantity * it->second.fee_per_share;
            total_qty += child.quantity;
        }
        double notional = total_qty * price;
        return (notional > 0) ? total_fee / notional * 10000 : 0;
    }

    void load_default_venues() {
        venues_["NYSE"] = {"NYSE", "New York Stock Exchange", "Exchange",
                           0.003, 0.002, 0.92, 2.0, false, 8, 0.22, true};
        venues_["NASDAQ"] = {"NASDAQ", "NASDAQ", "Exchange",
                             0.003, 0.002, 0.90, 1.5, false, 10, 0.20, true};
        venues_["ARCA"] = {"ARCA", "NYSE Arca", "Exchange",
                           0.003, 0.002, 0.85, 2.5, false, 9, 0.15, true};
        venues_["BATS"] = {"BATS", "CBOE BATS", "Exchange",
                           0.002, 0.003, 0.82, 1.0, false, 11, 0.12, true};
        venues_["IEX"] = {"IEX", "IEX Exchange", "Exchange",
                          0.0009, 0.0, 0.70, 3.5, false, 12, 0.04, true};
        venues_["SIGMA"] = {"SIGMA", "Sigma X Dark Pool", "DarkPool",
                            0.001, 0.0, 0.30, 5.0, true, 5, 0.05, true};
        venues_["XDARK"] = {"XDARK", "CrossFinder Dark", "DarkPool",
                            0.001, 0.0, 0.25, 4.0, true, 4, 0.03, true};
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_SMART_ROUTER_HPP
