/**
 * @file order_book_simulator.hpp
 * @brief Order Book Simulator for Market Microstructure Analysis
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Simulated limit order book for algorithm testing and education:
 * - Limit/market/stop order placement and matching
 * - Price-time priority matching engine
 * - Simulated market depth (Level 2 data)
 * - Slippage and fill simulation
 * - Order book snapshot and event replay
 * - Configurable liquidity profiles
 * - Trade and quote generation
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_TRADING_ORDER_BOOK_SIMULATOR_HPP
#define GENIE_TRADING_ORDER_BOOK_SIMULATOR_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <sstream>
#include <random>
#include <functional>

namespace genie::trading {

enum class BookSide { BID, ASK };
enum class BookOrderType { LIMIT, MARKET, STOP };

struct BookOrder {
    std::string order_id;
    std::string symbol;
    BookSide side{BookSide::BID};
    BookOrderType type{BookOrderType::LIMIT};
    double price{0.0};
    double quantity{0.0};
    double filled_quantity{0.0};
    double stop_price{0.0};
    uint64_t timestamp_us{0};
    bool is_active{true};

    double remaining() const { return quantity - filled_quantity; }
    bool is_filled() const { return filled_quantity >= quantity - 1e-9; }
};

struct BookFill {
    std::string order_id;
    std::string symbol;
    BookSide side{BookSide::BID};
    double price{0.0};
    double quantity{0.0};
    uint64_t timestamp_us{0};
    std::string match_order_id;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"order_id\":\"" << order_id << "\""
           << ",\"symbol\":\"" << symbol << "\""
           << ",\"side\":\"" << (side == BookSide::BID ? "buy" : "sell") << "\""
           << ",\"price\":" << price
           << ",\"quantity\":" << quantity
           << ",\"match_order_id\":\"" << match_order_id << "\""
           << "}";
        return os.str();
    }
};

struct PriceLevel {
    double price{0.0};
    double total_quantity{0.0};
    int order_count{0};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"price\":" << price
           << ",\"quantity\":" << total_quantity
           << ",\"orders\":" << order_count << "}";
        return os.str();
    }
};

struct BookSnapshot {
    std::string symbol;
    double last_trade_price{0.0};
    double last_trade_quantity{0.0};
    double best_bid{0.0};
    double best_ask{0.0};
    double spread{0.0};
    double spread_bps{0.0};
    double mid_price{0.0};
    double total_bid_quantity{0.0};
    double total_ask_quantity{0.0};
    double bid_ask_imbalance{0.0};
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"symbol\":\"" << symbol << "\""
           << ",\"last_trade_price\":" << last_trade_price
           << ",\"best_bid\":" << best_bid
           << ",\"best_ask\":" << best_ask
           << ",\"spread\":" << spread
           << ",\"spread_bps\":" << spread_bps
           << ",\"mid_price\":" << mid_price
           << ",\"total_bid_qty\":" << total_bid_quantity
           << ",\"total_ask_qty\":" << total_ask_quantity
           << ",\"bid_ask_imbalance\":" << bid_ask_imbalance
           << ",\"bid_levels\":" << bids.size()
           << ",\"ask_levels\":" << asks.size()
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Order Book Simulator
// ---------------------------------------------------------------
class OrderBookSimulator {
public:
    explicit OrderBookSimulator(const std::string& symbol, double initial_price = 100.0)
        : symbol_(symbol), last_trade_price_(initial_price),
          rng_(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        generate_initial_depth(initial_price);
    }

    // Place a limit order, returns fills
    std::vector<BookFill> place_limit_order(const std::string& order_id,
                                             BookSide side, double price, double quantity) {
        std::lock_guard<std::mutex> lock(mtx_);
        BookOrder order;
        order.order_id = order_id;
        order.symbol = symbol_;
        order.side = side;
        order.type = BookOrderType::LIMIT;
        order.price = price;
        order.quantity = quantity;
        order.timestamp_us = now_us();
        return process_order(order);
    }

    // Place a market order, returns fills
    std::vector<BookFill> place_market_order(const std::string& order_id,
                                              BookSide side, double quantity) {
        std::lock_guard<std::mutex> lock(mtx_);
        BookOrder order;
        order.order_id = order_id;
        order.symbol = symbol_;
        order.side = side;
        order.type = BookOrderType::MARKET;
        order.price = (side == BookSide::BID) ? 1e9 : 0.0; // Aggressive price
        order.quantity = quantity;
        order.timestamp_us = now_us();
        return process_order(order);
    }

    // Cancel an order
    bool cancel_order(const std::string& order_id) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& [price, orders] : bid_book_) {
            for (auto& o : orders) {
                if (o.order_id == order_id && o.is_active) {
                    o.is_active = false;
                    return true;
                }
            }
        }
        for (auto& [price, orders] : ask_book_) {
            for (auto& o : orders) {
                if (o.order_id == order_id && o.is_active) {
                    o.is_active = false;
                    return true;
                }
            }
        }
        return false;
    }

    // Simulate a fill for a given quantity (for backtesting)
    BookFill simulate_market_fill(BookSide side, double quantity) {
        std::lock_guard<std::mutex> lock(mtx_);
        BookFill fill;
        fill.symbol = symbol_;
        fill.side = side;
        fill.quantity = quantity;
        fill.timestamp_us = now_us();

        if (side == BookSide::BID) {
            // Walk the ask book
            double cost = 0.0;
            double filled = 0.0;
            for (auto& [price, orders] : ask_book_) {
                for (auto& o : orders) {
                    if (!o.is_active) continue;
                    double available = o.remaining();
                    double take = std::min(available, quantity - filled);
                    cost += take * price;
                    filled += take;
                    if (filled >= quantity - 1e-9) break;
                }
                if (filled >= quantity - 1e-9) break;
            }
            fill.price = (filled > 0.0) ? cost / filled : last_trade_price_;
        } else {
            double proceeds = 0.0;
            double filled = 0.0;
            for (auto it = bid_book_.rbegin(); it != bid_book_.rend(); ++it) {
                for (auto& o : it->second) {
                    if (!o.is_active) continue;
                    double available = o.remaining();
                    double take = std::min(available, quantity - filled);
                    proceeds += take * it->first;
                    filled += take;
                    if (filled >= quantity - 1e-9) break;
                }
                if (filled >= quantity - 1e-9) break;
            }
            fill.price = (filled > 0.0) ? proceeds / filled : last_trade_price_;
        }

        return fill;
    }

    // Get current book snapshot
    BookSnapshot get_snapshot(int depth = 10) const {
        std::lock_guard<std::mutex> lock(mtx_);
        BookSnapshot snap;
        snap.symbol = symbol_;
        snap.last_trade_price = last_trade_price_;
        snap.last_trade_quantity = last_trade_qty_;

        // Bids (highest first)
        int count = 0;
        for (auto it = bid_book_.rbegin(); it != bid_book_.rend() && count < depth; ++it, ++count) {
            PriceLevel lvl;
            lvl.price = it->first;
            for (const auto& o : it->second) {
                if (o.is_active) {
                    lvl.total_quantity += o.remaining();
                    lvl.order_count++;
                }
            }
            if (lvl.order_count > 0) {
                snap.bids.push_back(lvl);
                snap.total_bid_quantity += lvl.total_quantity;
            }
        }

        // Asks (lowest first)
        count = 0;
        for (auto it = ask_book_.begin(); it != ask_book_.end() && count < depth; ++it, ++count) {
            PriceLevel lvl;
            lvl.price = it->first;
            for (const auto& o : it->second) {
                if (o.is_active) {
                    lvl.total_quantity += o.remaining();
                    lvl.order_count++;
                }
            }
            if (lvl.order_count > 0) {
                snap.asks.push_back(lvl);
                snap.total_ask_quantity += lvl.total_quantity;
            }
        }

        if (!snap.bids.empty()) snap.best_bid = snap.bids.front().price;
        if (!snap.asks.empty()) snap.best_ask = snap.asks.front().price;
        snap.spread = snap.best_ask - snap.best_bid;
        snap.mid_price = (snap.best_bid + snap.best_ask) / 2.0;
        snap.spread_bps = (snap.mid_price > 0.0)
            ? (snap.spread / snap.mid_price) * 10000.0 : 0.0;
        double total = snap.total_bid_quantity + snap.total_ask_quantity;
        snap.bid_ask_imbalance = (total > 0.0)
            ? (snap.total_bid_quantity - snap.total_ask_quantity) / total : 0.0;

        return snap;
    }

    // Simulate random market activity (for testing)
    std::vector<BookFill> simulate_tick() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<BookFill> fills;

        std::uniform_real_distribution<double> action_dist(0.0, 1.0);
        double action = action_dist(rng_);

        if (action < 0.3) {
            // New limit order
            BookSide side = (action < 0.15) ? BookSide::BID : BookSide::ASK;
            std::normal_distribution<double> price_dist(last_trade_price_, last_trade_price_ * 0.002);
            std::uniform_real_distribution<double> qty_dist(10.0, 500.0);
            BookOrder order;
            order.order_id = "SIM-" + std::to_string(sim_counter_++);
            order.symbol = symbol_;
            order.side = side;
            order.type = BookOrderType::LIMIT;
            order.price = std::round(price_dist(rng_) * 100.0) / 100.0;
            order.quantity = std::round(qty_dist(rng_));
            order.timestamp_us = now_us();
            fills = process_order(order);
        } else if (action < 0.4) {
            // Small market order
            BookSide side = (action < 0.35) ? BookSide::BID : BookSide::ASK;
            std::uniform_real_distribution<double> qty_dist(1.0, 100.0);
            BookOrder order;
            order.order_id = "SIM-" + std::to_string(sim_counter_++);
            order.symbol = symbol_;
            order.side = side;
            order.type = BookOrderType::MARKET;
            order.price = (side == BookSide::BID) ? 1e9 : 0.0;
            order.quantity = std::round(qty_dist(rng_));
            order.timestamp_us = now_us();
            fills = process_order(order);
        }
        // else: no activity this tick

        return fills;
    }

    std::vector<BookFill> get_trade_history() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return trade_history_;
    }

    std::string symbol() const { return symbol_; }

private:
    std::string symbol_;
    mutable std::mutex mtx_;
    std::map<double, std::deque<BookOrder>> bid_book_; // price -> orders
    std::map<double, std::deque<BookOrder>> ask_book_;
    double last_trade_price_{100.0};
    double last_trade_qty_{0.0};
    std::vector<BookFill> trade_history_;
    std::mt19937 rng_;
    uint64_t sim_counter_{1};

    uint64_t now_us() const {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
    }

    void generate_initial_depth(double mid_price) {
        std::uniform_real_distribution<double> qty_dist(50.0, 2000.0);
        double tick = 0.01;
        for (int i = 1; i <= 20; ++i) {
            double bid_price = mid_price - i * tick;
            double ask_price = mid_price + i * tick;
            double bid_qty = qty_dist(rng_) * (1.0 + 0.1 * i);
            double ask_qty = qty_dist(rng_) * (1.0 + 0.1 * i);

            BookOrder bid_order;
            bid_order.order_id = "INIT-B-" + std::to_string(i);
            bid_order.symbol = symbol_;
            bid_order.side = BookSide::BID;
            bid_order.price = std::round(bid_price * 100.0) / 100.0;
            bid_order.quantity = std::round(bid_qty);
            bid_order.timestamp_us = now_us();
            bid_book_[bid_order.price].push_back(bid_order);

            BookOrder ask_order;
            ask_order.order_id = "INIT-A-" + std::to_string(i);
            ask_order.symbol = symbol_;
            ask_order.side = BookSide::ASK;
            ask_order.price = std::round(ask_price * 100.0) / 100.0;
            ask_order.quantity = std::round(ask_qty);
            ask_order.timestamp_us = now_us();
            ask_book_[ask_order.price].push_back(ask_order);
        }
    }

    std::vector<BookFill> process_order(BookOrder& incoming) {
        std::vector<BookFill> fills;

        if (incoming.side == BookSide::BID) {
            // Match against asks
            while (incoming.remaining() > 1e-9 && !ask_book_.empty()) {
                auto it = ask_book_.begin();
                if (incoming.type == BookOrderType::LIMIT && it->first > incoming.price) break;

                auto& orders = it->second;
                while (!orders.empty() && incoming.remaining() > 1e-9) {
                    auto& resting = orders.front();
                    if (!resting.is_active) { orders.pop_front(); continue; }

                    double fill_qty = std::min(incoming.remaining(), resting.remaining());
                    double fill_price = resting.price;

                    incoming.filled_quantity += fill_qty;
                    resting.filled_quantity += fill_qty;

                    BookFill fill;
                    fill.order_id = incoming.order_id;
                    fill.symbol = symbol_;
                    fill.side = BookSide::BID;
                    fill.price = fill_price;
                    fill.quantity = fill_qty;
                    fill.timestamp_us = now_us();
                    fill.match_order_id = resting.order_id;
                    fills.push_back(fill);
                    trade_history_.push_back(fill);

                    last_trade_price_ = fill_price;
                    last_trade_qty_ = fill_qty;

                    if (resting.is_filled()) {
                        resting.is_active = false;
                        orders.pop_front();
                    }
                }
                if (orders.empty()) ask_book_.erase(it);
            }
            // Add remainder to book
            if (incoming.remaining() > 1e-9 && incoming.type == BookOrderType::LIMIT) {
                incoming.is_active = true;
                bid_book_[incoming.price].push_back(incoming);
            }
        } else {
            // Match against bids
            while (incoming.remaining() > 1e-9 && !bid_book_.empty()) {
                auto it = std::prev(bid_book_.end());
                if (incoming.type == BookOrderType::LIMIT && it->first < incoming.price) break;

                auto& orders = it->second;
                while (!orders.empty() && incoming.remaining() > 1e-9) {
                    auto& resting = orders.front();
                    if (!resting.is_active) { orders.pop_front(); continue; }

                    double fill_qty = std::min(incoming.remaining(), resting.remaining());
                    double fill_price = resting.price;

                    incoming.filled_quantity += fill_qty;
                    resting.filled_quantity += fill_qty;

                    BookFill fill;
                    fill.order_id = incoming.order_id;
                    fill.symbol = symbol_;
                    fill.side = BookSide::ASK;
                    fill.price = fill_price;
                    fill.quantity = fill_qty;
                    fill.timestamp_us = now_us();
                    fill.match_order_id = resting.order_id;
                    fills.push_back(fill);
                    trade_history_.push_back(fill);

                    last_trade_price_ = fill_price;
                    last_trade_qty_ = fill_qty;

                    if (resting.is_filled()) {
                        resting.is_active = false;
                        orders.pop_front();
                    }
                }
                if (orders.empty()) bid_book_.erase(it);
            }
            if (incoming.remaining() > 1e-9 && incoming.type == BookOrderType::LIMIT) {
                incoming.is_active = true;
                ask_book_[incoming.price].push_back(incoming);
            }
        }

        return fills;
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_ORDER_BOOK_SIMULATOR_HPP
