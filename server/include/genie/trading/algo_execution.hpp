/**
 * @file algo_execution.hpp
 * @brief Algorithmic execution strategies - TWAP, VWAP, and advanced order routing
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 4: Advanced Trading
 * - TWAP (Time-Weighted Average Price) algorithm
 * - VWAP (Volume-Weighted Average Price) tracking
 * - Smart order routing with venue selection
 * - Transaction cost analysis
 * - Market impact estimation
 */
#pragma once
#ifndef GENIE_TRADING_ALGO_EXECUTION_HPP
#define GENIE_TRADING_ALGO_EXECUTION_HPP

#include "broker_abstraction.hpp"
#include "../market/market_data.hpp"
#include "../core/thread_pool.hpp"
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>
#include <algorithm>
#include <numeric>

namespace genie::trading {

// =============================================================================
// Execution Metrics
// =============================================================================

/**
 * @brief Execution quality metrics
 */
struct ExecutionMetrics {
    std::string order_id;
    std::string symbol;
    
    // Prices
    double arrival_price{0};        // Price when algo started
    double decision_price{0};       // Price when order was decided
    double vwap{0};                 // Volume-weighted average price
    double twap{0};                 // Time-weighted average price
    double avg_fill_price{0};       // Actual average fill price
    
    // Quantities
    double target_qty{0};
    double filled_qty{0};
    double remaining_qty{0};
    
    // Slippage analysis
    double slippage_bps{0};         // Basis points vs arrival
    double vwap_slippage_bps{0};    // Basis points vs VWAP
    double implementation_shortfall{0};  // Total cost of execution
    
    // Timing
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    int duration_seconds{0};
    int child_orders{0};
    int fills{0};
    
    // Market impact
    double estimated_impact_bps{0};
    double realized_impact_bps{0};
    
    double fill_rate() const {
        return target_qty > 0 ? (filled_qty / target_qty) * 100 : 0;
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Execution Metrics for " << symbol << " (Order: " << order_id << ")\n"
            << "  Target: " << target_qty << ", Filled: " << filled_qty 
            << " (" << fill_rate() << "%)\n"
            << "  Arrival: $" << arrival_price << ", Avg Fill: $" << avg_fill_price << "\n"
            << "  VWAP: $" << vwap << ", TWAP: $" << twap << "\n"
            << "  Slippage: " << slippage_bps << " bps, VWAP Slippage: " << vwap_slippage_bps << " bps\n"
            << "  Implementation Shortfall: $" << implementation_shortfall << "\n"
            << "  Duration: " << duration_seconds << "s, Child Orders: " << child_orders;
        return oss.str();
    }
};

/**
 * @brief Child order for algo execution
 */
struct AlgoChildOrder {
    std::string id;
    std::string parent_id;
    std::string symbol;
    OrderSide side;
    double quantity{0};
    double limit_price{0};
    OrderType type{OrderType::Limit};
    OrderStatus status{OrderStatus::New};
    
    double filled_qty{0};
    double avg_fill_price{0};
    
    std::chrono::system_clock::time_point scheduled_time;
    std::chrono::system_clock::time_point submit_time;
    std::chrono::system_clock::time_point fill_time;
    
    bool is_complete() const {
        return status == OrderStatus::Filled || 
               status == OrderStatus::Canceled ||
               status == OrderStatus::Rejected;
    }
};

// =============================================================================
// TWAP Algorithm
// =============================================================================

/**
 * @brief TWAP execution parameters
 */
struct TWAPParams {
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    double total_quantity{0};
    
    // Timing
    int duration_minutes{60};       // Total execution window
    int slice_interval_seconds{60}; // Time between slices
    
    // Limits
    double limit_price{0};          // Optional price limit
    double max_participation_rate{0.1};  // Max % of volume
    
    // Behavior
    bool aggressive_finish{false};  // Cross spread if behind
    bool cancel_on_limit_breach{true};
    double price_tolerance_pct{0.5}; // Max deviation from TWAP
    
    int num_slices() const {
        return std::max(1, (duration_minutes * 60) / slice_interval_seconds);
    }
    
    double slice_quantity() const {
        return total_quantity / num_slices();
    }
};

/**
 * @brief TWAP execution algorithm
 */
class TWAPExecutor {
public:
    using OrderCallback = std::function<std::string(const OrderRequest&)>;
    using QuoteCallback = std::function<Quote(const std::string&)>;
    using StatusCallback = std::function<void(const ExecutionMetrics&)>;
    
private:
    TWAPParams params_;
    OrderCallback submit_order_;
    QuoteCallback get_quote_;
    StatusCallback on_status_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    
    std::vector<AlgoChildOrder> child_orders_;
    ExecutionMetrics metrics_;
    
    // TWAP tracking
    std::vector<double> prices_;
    std::vector<std::chrono::system_clock::time_point> timestamps_;
    
    double calculate_twap() const {
        if (prices_.empty()) return 0;
        return std::accumulate(prices_.begin(), prices_.end(), 0.0) / prices_.size();
    }
    
    void update_metrics() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        double total_value = 0;
        double total_qty = 0;
        
        for (const auto& child : child_orders_) {
            if (child.filled_qty > 0) {
                total_value += child.filled_qty * child.avg_fill_price;
                total_qty += child.filled_qty;
            }
        }
        
        metrics_.filled_qty = total_qty;
        metrics_.remaining_qty = params_.total_quantity - total_qty;
        metrics_.avg_fill_price = total_qty > 0 ? total_value / total_qty : 0;
        metrics_.twap = calculate_twap();
        metrics_.child_orders = static_cast<int>(child_orders_.size());
        
        if (metrics_.arrival_price > 0 && metrics_.avg_fill_price > 0) {
            double diff = metrics_.avg_fill_price - metrics_.arrival_price;
            if (params_.side == OrderSide::Sell) diff = -diff;
            metrics_.slippage_bps = (diff / metrics_.arrival_price) * 10000;
        }
        
        if (metrics_.twap > 0 && metrics_.avg_fill_price > 0) {
            double diff = metrics_.avg_fill_price - metrics_.twap;
            if (params_.side == OrderSide::Sell) diff = -diff;
            metrics_.vwap_slippage_bps = (diff / metrics_.twap) * 10000;
        }
        
        metrics_.implementation_shortfall = 
            (metrics_.avg_fill_price - metrics_.arrival_price) * metrics_.filled_qty;
        if (params_.side == OrderSide::Sell) {
            metrics_.implementation_shortfall = -metrics_.implementation_shortfall;
        }
    }

public:
    TWAPExecutor(const TWAPParams& params,
                 OrderCallback submit_order,
                 QuoteCallback get_quote,
                 StatusCallback on_status = nullptr)
        : params_(params)
        , submit_order_(std::move(submit_order))
        , get_quote_(std::move(get_quote))
        , on_status_(std::move(on_status)) {
        
        metrics_.symbol = params_.symbol;
        metrics_.target_qty = params_.total_quantity;
    }
    
    /**
     * @brief Start TWAP execution
     */
    void start() {
        if (running_) return;
        
        running_ = true;
        metrics_.start_time = std::chrono::system_clock::now();
        
        // Get arrival price
        auto quote = get_quote_(params_.symbol);
        metrics_.arrival_price = (quote.bid + quote.ask) / 2;
        metrics_.decision_price = metrics_.arrival_price;
        
        // Execute in separate thread
        std::thread([this]() { execute_loop(); }).detach();
    }
    
    /**
     * @brief Stop execution
     */
    void stop() {
        running_ = false;
    }
    
    /**
     * @brief Pause execution
     */
    void pause() {
        paused_ = true;
    }
    
    /**
     * @brief Resume execution
     */
    void resume() {
        paused_ = false;
    }
    
    /**
     * @brief Get current metrics
     */
    ExecutionMetrics get_metrics() const {
        return metrics_;
    }
    
    /**
     * @brief Record a fill
     */
    void record_fill(const std::string& child_id, double qty, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& child : child_orders_) {
            if (child.id == child_id) {
                child.filled_qty += qty;
                // Update average
                double total = child.avg_fill_price * (child.filled_qty - qty) + price * qty;
                child.avg_fill_price = total / child.filled_qty;
                
                if (child.filled_qty >= child.quantity) {
                    child.status = OrderStatus::Filled;
                    child.fill_time = std::chrono::system_clock::now();
                }
                
                metrics_.fills++;
                break;
            }
        }
        
        // Track price for TWAP calculation
        prices_.push_back(price);
        timestamps_.push_back(std::chrono::system_clock::now());
        
        update_metrics();
        
        if (on_status_) {
            on_status_(metrics_);
        }
    }

private:
    void execute_loop() {
        int slices_sent = 0;
        int total_slices = params_.num_slices();
        double slice_qty = params_.slice_quantity();
        
        while (running_ && slices_sent < total_slices && 
               metrics_.filled_qty < params_.total_quantity) {
            
            if (paused_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // Get current quote
            auto quote = get_quote_(params_.symbol);
            double mid_price = (quote.bid + quote.ask) / 2;
            prices_.push_back(mid_price);
            timestamps_.push_back(std::chrono::system_clock::now());
            
            // Check limit price
            if (params_.limit_price > 0) {
                if (params_.side == OrderSide::Buy && mid_price > params_.limit_price) {
                    if (params_.cancel_on_limit_breach) {
                        break;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::seconds(params_.slice_interval_seconds));
                    continue;
                }
                if (params_.side == OrderSide::Sell && mid_price < params_.limit_price) {
                    if (params_.cancel_on_limit_breach) {
                        break;
                    }
                    std::this_thread::sleep_for(
                        std::chrono::seconds(params_.slice_interval_seconds));
                    continue;
                }
            }
            
            // Calculate quantity for this slice
            double remaining = params_.total_quantity - metrics_.filled_qty;
            double qty = std::min(slice_qty, remaining);
            
            // Adjust for aggressive finish
            if (params_.aggressive_finish && slices_sent >= total_slices - 2) {
                qty = remaining;
            }
            
            if (qty > 0) {
                // Create child order
                AlgoChildOrder child;
                child.id = "TWAP-" + std::to_string(slices_sent + 1);
                child.parent_id = metrics_.order_id;
                child.symbol = params_.symbol;
                child.side = params_.side;
                child.quantity = qty;
                child.scheduled_time = std::chrono::system_clock::now();
                
                // Set limit price slightly aggressive
                if (params_.side == OrderSide::Buy) {
                    child.limit_price = quote.ask;  // Cross the spread
                    child.type = OrderType::Limit;
                } else {
                    child.limit_price = quote.bid;
                    child.type = OrderType::Limit;
                }
                
                // Submit order
                OrderRequest req;
                req.symbol = child.symbol;
                req.side = child.side;
                req.type = child.type;
                req.qty = child.quantity;
                req.limit_price = child.limit_price;
                req.time_in_force = TimeInForce::IOC;  // Immediate or cancel
                
                child.submit_time = std::chrono::system_clock::now();
                std::string order_id = submit_order_(req);
                
                if (!order_id.empty()) {
                    child.id = order_id;
                    child.status = OrderStatus::New;
                    
                    std::lock_guard<std::mutex> lock(mutex_);
                    child_orders_.push_back(child);
                }
            }
            
            slices_sent++;
            update_metrics();
            
            if (on_status_) {
                on_status_(metrics_);
            }
            
            // Wait for next slice
            if (slices_sent < total_slices) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(params_.slice_interval_seconds));
            }
        }
        
        // Final update
        metrics_.end_time = std::chrono::system_clock::now();
        metrics_.duration_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                metrics_.end_time - metrics_.start_time).count());
        
        running_ = false;
        
        if (on_status_) {
            on_status_(metrics_);
        }
    }
};

// =============================================================================
// VWAP Algorithm
// =============================================================================

/**
 * @brief Historical volume profile for VWAP
 */
struct VolumeProfile {
    std::string symbol;
    std::vector<double> intraday_volume_pct;  // % of daily volume by interval
    int interval_minutes{5};
    double avg_daily_volume{0};
    
    /**
     * @brief Get expected volume percentage for time of day
     */
    double get_volume_pct(int minutes_from_open) const {
        if (intraday_volume_pct.empty()) return 1.0 / 78;  // Default: even distribution
        
        int idx = minutes_from_open / interval_minutes;
        if (idx >= static_cast<int>(intraday_volume_pct.size())) {
            idx = static_cast<int>(intraday_volume_pct.size()) - 1;
        }
        return intraday_volume_pct[idx];
    }
    
    /**
     * @brief Create typical U-shaped volume profile
     */
    static VolumeProfile create_typical() {
        VolumeProfile profile;
        profile.interval_minutes = 5;
        
        // U-shaped: high at open, low mid-day, high at close
        // 78 5-minute intervals in 6.5 hour trading day
        for (int i = 0; i < 78; ++i) {
            double t = static_cast<double>(i) / 77;  // 0 to 1
            // U-shape: higher at ends
            double pct = 0.8 + 0.4 * (std::pow(t - 0.5, 2) * 4);
            profile.intraday_volume_pct.push_back(pct);
        }
        
        // Normalize to sum to 1
        double sum = std::accumulate(profile.intraday_volume_pct.begin(),
                                     profile.intraday_volume_pct.end(), 0.0);
        for (auto& v : profile.intraday_volume_pct) {
            v /= sum;
        }
        
        return profile;
    }
};

/**
 * @brief VWAP execution parameters
 */
struct VWAPParams {
    std::string symbol;
    OrderSide side{OrderSide::Buy};
    double total_quantity{0};
    
    // Timing
    int start_minutes_from_open{0};
    int end_minutes_from_open{390};  // Full day
    
    // Volume participation
    double target_participation_rate{0.1};  // % of volume
    double max_participation_rate{0.25};
    
    // Limits
    double limit_price{0};
    double max_spread_bps{50};  // Max bid-ask spread
    
    // Behavior
    bool track_benchmark_vwap{true};
    double vwap_tolerance_bps{10};  // Alert if deviating
    
    VolumeProfile volume_profile;
};

/**
 * @brief Real-time VWAP tracker
 */
class VWAPTracker {
private:
    std::string symbol_;
    double cumulative_pv_{0};  // Price * Volume sum
    double cumulative_volume_{0};
    std::vector<std::pair<double, double>> trades_;  // price, volume
    mutable std::mutex mutex_;

public:
    explicit VWAPTracker(const std::string& symbol) : symbol_(symbol) {}
    
    void add_trade(double price, double volume) {
        std::lock_guard<std::mutex> lock(mutex_);
        cumulative_pv_ += price * volume;
        cumulative_volume_ += volume;
        trades_.emplace_back(price, volume);
    }
    
    double get_vwap() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cumulative_volume_ > 0 ? cumulative_pv_ / cumulative_volume_ : 0;
    }
    
    double get_volume() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cumulative_volume_;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        cumulative_pv_ = 0;
        cumulative_volume_ = 0;
        trades_.clear();
    }
    
    std::string symbol() const { return symbol_; }
};

/**
 * @brief VWAP execution algorithm
 */
class VWAPExecutor {
public:
    using OrderCallback = std::function<std::string(const OrderRequest&)>;
    using QuoteCallback = std::function<Quote(const std::string&)>;
    using VolumeCallback = std::function<double(const std::string&)>;  // Get current volume
    using StatusCallback = std::function<void(const ExecutionMetrics&)>;
    
private:
    VWAPParams params_;
    OrderCallback submit_order_;
    QuoteCallback get_quote_;
    VolumeCallback get_volume_;
    StatusCallback on_status_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    
    std::vector<AlgoChildOrder> child_orders_;
    ExecutionMetrics metrics_;
    VWAPTracker market_vwap_;
    VWAPTracker execution_vwap_;
    
    double last_market_volume_{0};

public:
    VWAPExecutor(const VWAPParams& params,
                 OrderCallback submit_order,
                 QuoteCallback get_quote,
                 VolumeCallback get_volume,
                 StatusCallback on_status = nullptr)
        : params_(params)
        , submit_order_(std::move(submit_order))
        , get_quote_(std::move(get_quote))
        , get_volume_(std::move(get_volume))
        , on_status_(std::move(on_status))
        , market_vwap_(params.symbol)
        , execution_vwap_(params.symbol) {
        
        metrics_.symbol = params_.symbol;
        metrics_.target_qty = params_.total_quantity;
        
        // Use typical volume profile if not provided
        if (params_.volume_profile.intraday_volume_pct.empty()) {
            params_.volume_profile = VolumeProfile::create_typical();
        }
    }
    
    void start() {
        if (running_) return;
        
        running_ = true;
        metrics_.start_time = std::chrono::system_clock::now();
        
        auto quote = get_quote_(params_.symbol);
        metrics_.arrival_price = (quote.bid + quote.ask) / 2;
        
        std::thread([this]() { execute_loop(); }).detach();
    }
    
    void stop() { running_ = false; }
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    
    ExecutionMetrics get_metrics() const { return metrics_; }
    
    double get_market_vwap() const { return market_vwap_.get_vwap(); }
    double get_execution_vwap() const { return execution_vwap_.get_vwap(); }
    
    void record_fill(const std::string& child_id, double qty, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& child : child_orders_) {
            if (child.id == child_id) {
                child.filled_qty += qty;
                double total = child.avg_fill_price * (child.filled_qty - qty) + price * qty;
                child.avg_fill_price = total / child.filled_qty;
                
                if (child.filled_qty >= child.quantity) {
                    child.status = OrderStatus::Filled;
                }
                break;
            }
        }
        
        execution_vwap_.add_trade(price, qty);
        update_metrics();
        
        if (on_status_) on_status_(metrics_);
    }
    
    void update_market_trade(double price, double volume) {
        market_vwap_.add_trade(price, volume);
    }

private:
    void update_metrics() {
        metrics_.filled_qty = execution_vwap_.get_volume();
        metrics_.remaining_qty = params_.total_quantity - metrics_.filled_qty;
        metrics_.avg_fill_price = execution_vwap_.get_vwap();
        metrics_.vwap = market_vwap_.get_vwap();
        metrics_.child_orders = static_cast<int>(child_orders_.size());
        
        if (metrics_.arrival_price > 0 && metrics_.avg_fill_price > 0) {
            double diff = metrics_.avg_fill_price - metrics_.arrival_price;
            if (params_.side == OrderSide::Sell) diff = -diff;
            metrics_.slippage_bps = (diff / metrics_.arrival_price) * 10000;
        }
        
        if (metrics_.vwap > 0 && metrics_.avg_fill_price > 0) {
            double diff = metrics_.avg_fill_price - metrics_.vwap;
            if (params_.side == OrderSide::Sell) diff = -diff;
            metrics_.vwap_slippage_bps = (diff / metrics_.vwap) * 10000;
        }
    }
    
    void execute_loop() {
        const int check_interval_seconds = 30;
        
        while (running_ && metrics_.filled_qty < params_.total_quantity) {
            if (paused_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            auto quote = get_quote_(params_.symbol);
            double mid_price = (quote.bid + quote.ask) / 2;
            
            // Check spread
            double spread_bps = ((quote.ask - quote.bid) / mid_price) * 10000;
            if (spread_bps > params_.max_spread_bps) {
                std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds));
                continue;
            }
            
            // Check limit
            if (params_.limit_price > 0) {
                if (params_.side == OrderSide::Buy && mid_price > params_.limit_price) {
                    std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds));
                    continue;
                }
                if (params_.side == OrderSide::Sell && mid_price < params_.limit_price) {
                    std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds));
                    continue;
                }
            }
            
            // Get current market volume
            double current_volume = get_volume_(params_.symbol);
            double volume_delta = current_volume - last_market_volume_;
            last_market_volume_ = current_volume;
            
            if (volume_delta > 0) {
                market_vwap_.add_trade(mid_price, volume_delta);
            }
            
            // Calculate target quantity based on volume participation
            double target_participation = current_volume * params_.target_participation_rate;
            double behind = target_participation - metrics_.filled_qty;
            
            if (behind > 0) {
                // Calculate slice size
                double remaining = params_.total_quantity - metrics_.filled_qty;
                double qty = std::min({
                    behind,
                    remaining,
                    volume_delta * params_.max_participation_rate
                });
                
                if (qty >= 1) {  // Minimum order size
                    AlgoChildOrder child;
                    child.id = "VWAP-" + std::to_string(child_orders_.size() + 1);
                    child.symbol = params_.symbol;
                    child.side = params_.side;
                    child.quantity = std::floor(qty);
                    child.limit_price = (params_.side == OrderSide::Buy) ? quote.ask : quote.bid;
                    child.type = OrderType::Limit;
                    
                    OrderRequest req;
                    req.symbol = child.symbol;
                    req.side = child.side;
                    req.type = child.type;
                    req.qty = child.quantity;
                    req.limit_price = child.limit_price;
                    req.time_in_force = TimeInForce::IOC;
                    
                    std::string order_id = submit_order_(req);
                    if (!order_id.empty()) {
                        child.id = order_id;
                        std::lock_guard<std::mutex> lock(mutex_);
                        child_orders_.push_back(child);
                    }
                }
            }
            
            update_metrics();
            if (on_status_) on_status_(metrics_);
            
            std::this_thread::sleep_for(std::chrono::seconds(check_interval_seconds));
        }
        
        metrics_.end_time = std::chrono::system_clock::now();
        metrics_.duration_seconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                metrics_.end_time - metrics_.start_time).count());
        
        running_ = false;
        if (on_status_) on_status_(metrics_);
    }
};

// =============================================================================
// Smart Order Router
// =============================================================================

/**
 * @brief Venue characteristics
 */
struct AlgoVenue {
    std::string id;
    std::string name;
    
    // Fees (in dollars per share)
    double maker_rebate{0.002};     // Rebate for adding liquidity
    double taker_fee{0.003};        // Fee for removing liquidity
    double routing_fee{0.0001};
    
    // Execution quality
    double fill_rate{0.85};         // Historical fill rate
    double avg_latency_ms{5};       // Average latency
    double price_improvement_bps{0}; // Average price improvement
    
    // Liquidity
    double avg_displayed_size{500};
    double avg_hidden_liquidity{0.3}; // % of displayed that's hidden
    
    bool supports_hidden{true};
    bool supports_midpoint{true};
    bool supports_ioc{true};
    
    double effective_cost(bool is_maker) const {
        return is_maker ? -maker_rebate : taker_fee;
    }
};

/**
 * @brief Smart order routing decision
 */
struct RoutingDecision {
    std::string venue_id;
    double quantity{0};
    double limit_price{0};
    OrderType order_type{OrderType::Limit};
    bool hidden{false};
    bool midpoint_peg{false};
    
    double expected_fill_rate{0};
    double expected_cost{0};
    std::string rationale;
};

/**
 * @brief Smart Order Router
 */
class SmartOrderRouter {
private:
    std::vector<AlgoVenue> venues_;
    std::map<std::string, double> venue_scores_;  // Real-time scoring
    mutable std::mutex mutex_;

public:
    SmartOrderRouter() {
        // Initialize with common venues
        venues_.push_back({"NYSE", "New York Stock Exchange", 0.0013, 0.003, 0.0001, 0.9, 3, 0.5, 1000, 0.2, true, true, true});
        venues_.push_back({"NASDAQ", "NASDAQ", 0.002, 0.003, 0.0001, 0.88, 2, 0.3, 800, 0.25, true, true, true});
        venues_.push_back({"ARCA", "NYSE Arca", 0.002, 0.003, 0.0001, 0.85, 4, 0.2, 600, 0.3, true, true, true});
        venues_.push_back({"BATS", "BATS Exchange", 0.0025, 0.0028, 0.0001, 0.82, 2, 0.4, 500, 0.35, true, true, true});
        venues_.push_back({"IEX", "IEX Exchange", 0.0009, 0.0009, 0.0, 0.75, 1, 1.0, 300, 0.4, true, true, true});
        venues_.push_back({"EDGX", "CBOE EDGX", 0.002, 0.003, 0.0001, 0.8, 3, 0.3, 400, 0.3, true, true, true});
    }
    
    void add_venue(const AlgoVenue& venue) {
        std::lock_guard<std::mutex> lock(mutex_);
        venues_.push_back(venue);
    }
    
    const std::vector<AlgoVenue>& get_venues() const { return venues_; }
    
    /**
     * @brief Route an order optimally
     */
    std::vector<RoutingDecision> route(const std::string& /*symbol*/,
                                        OrderSide /*side*/,
                                        double quantity,
                                        double limit_price,
                                        bool urgent = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<RoutingDecision> decisions;
        
        // Score venues
        std::vector<std::pair<double, const AlgoVenue*>> scored_venues;
        
        for (const auto& venue : venues_) {
            double score = calculate_venue_score(venue, quantity, urgent);
            scored_venues.emplace_back(score, &venue);
        }
        
        // Sort by score (higher is better)
        std::sort(scored_venues.begin(), scored_venues.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        
        // Allocate quantity across venues
        double remaining = quantity;
        
        for (const auto& [score, venue] : scored_venues) {
            if (remaining <= 0) break;
            
            // Estimate available liquidity
            double est_liquidity = venue->avg_displayed_size * (1 + venue->avg_hidden_liquidity);
            double venue_qty = std::min(remaining, est_liquidity);
            
            if (venue_qty >= 1) {
                RoutingDecision decision;
                decision.venue_id = venue->id;
                decision.quantity = venue_qty;
                decision.limit_price = limit_price;
                decision.expected_fill_rate = venue->fill_rate;
                decision.expected_cost = venue_qty * venue->effective_cost(!urgent);
                
                // Use hidden orders for large quantities
                if (venue_qty > 500 && venue->supports_hidden) {
                    decision.hidden = true;
                    decision.rationale = "Hidden order to minimize market impact";
                }
                
                // Use midpoint peg for passive fills
                if (!urgent && venue->supports_midpoint && venue->price_improvement_bps > 0) {
                    decision.midpoint_peg = true;
                    decision.rationale = "Midpoint peg for price improvement";
                }
                
                if (decision.rationale.empty()) {
                    decision.rationale = "Best execution venue based on fill rate and cost";
                }
                
                decisions.push_back(decision);
                remaining -= venue_qty;
            }
        }
        
        return decisions;
    }
    
    /**
     * @brief Update venue performance metrics
     */
    void update_venue_metrics(const std::string& venue_id,
                              double fill_rate,
                              double latency_ms,
                              double price_improvement_bps) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& venue : venues_) {
            if (venue.id == venue_id) {
                // Exponential moving average update
                const double alpha = 0.1;
                venue.fill_rate = alpha * fill_rate + (1 - alpha) * venue.fill_rate;
                venue.avg_latency_ms = alpha * latency_ms + (1 - alpha) * venue.avg_latency_ms;
                venue.price_improvement_bps = alpha * price_improvement_bps + 
                                              (1 - alpha) * venue.price_improvement_bps;
                break;
            }
        }
    }

private:
    double calculate_venue_score(const AlgoVenue& venue, double quantity, bool urgent) const {
        double score = 0;
        
        // Fill rate (most important for urgent)
        score += venue.fill_rate * (urgent ? 50 : 30);
        
        // Cost (more important for non-urgent)
        double cost_score = 10 - venue.taker_fee * 1000;
        score += cost_score * (urgent ? 10 : 30);
        
        // Latency
        score += (10 - venue.avg_latency_ms) * (urgent ? 20 : 10);
        
        // Price improvement
        score += venue.price_improvement_bps * 2;
        
        // Liquidity match
        double liquidity_match = 1.0 - std::abs(quantity - venue.avg_displayed_size) / 
                                       std::max(quantity, venue.avg_displayed_size);
        score += liquidity_match * 10;
        
        return score;
    }
};

// =============================================================================
// Transaction Cost Analysis
// =============================================================================

/**
 * @brief TCA result for a single execution
 */
struct AlgoTCAResult {
    std::string order_id;
    std::string symbol;
    OrderSide side;
    
    // Prices
    double decision_price{0};
    double arrival_price{0};
    double execution_price{0};
    double close_price{0};
    double vwap{0};
    
    // Quantities
    double quantity{0};
    double notional{0};
    
    // Cost breakdown (in basis points)
    double spread_cost_bps{0};
    double market_impact_bps{0};
    double timing_cost_bps{0};
    double opportunity_cost_bps{0};
    double total_cost_bps{0};
    
    // Benchmarks
    double vs_arrival_bps{0};
    double vs_vwap_bps{0};
    double vs_close_bps{0};
    
    // Dollar amounts
    double implementation_shortfall{0};
    double total_fees{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "TCA for " << symbol << " (" << order_id << ")\n"
            << "  Notional: $" << notional << "\n"
            << "  Cost Breakdown:\n"
            << "    Spread:        " << spread_cost_bps << " bps\n"
            << "    Market Impact: " << market_impact_bps << " bps\n"
            << "    Timing:        " << timing_cost_bps << " bps\n"
            << "    Opportunity:   " << opportunity_cost_bps << " bps\n"
            << "    Total:         " << total_cost_bps << " bps\n"
            << "  Benchmarks:\n"
            << "    vs Arrival:    " << vs_arrival_bps << " bps\n"
            << "    vs VWAP:       " << vs_vwap_bps << " bps\n"
            << "    vs Close:      " << vs_close_bps << " bps\n"
            << "  Implementation Shortfall: $" << implementation_shortfall;
        return oss.str();
    }
};

/**
 * @brief Transaction Cost Analyzer
 */
class TransactionCostAnalyzer {
public:
    /**
     * @brief Analyze a single execution
     */
    AlgoTCAResult analyze(const ExecutionMetrics& execution,
                      double close_price,
                      double bid_ask_spread) {
        AlgoTCAResult result;
        
        result.order_id = execution.order_id;
        result.symbol = execution.symbol;
        result.arrival_price = execution.arrival_price;
        result.decision_price = execution.decision_price;
        result.execution_price = execution.avg_fill_price;
        result.close_price = close_price;
        result.vwap = execution.vwap;
        result.quantity = execution.filled_qty;
        result.notional = result.quantity * result.execution_price;
        
        // Spread cost (half spread)
        result.spread_cost_bps = (bid_ask_spread / 2 / result.arrival_price) * 10000;
        
        // Market impact (execution vs arrival minus spread)
        double raw_impact = result.execution_price - result.arrival_price;
        // if (execution.side == OrderSide::Sell) raw_impact = -raw_impact;
        result.market_impact_bps = (raw_impact / result.arrival_price) * 10000 - result.spread_cost_bps;
        result.market_impact_bps = std::max(0.0, result.market_impact_bps);
        
        // Timing cost (arrival vs decision)
        double timing = result.arrival_price - result.decision_price;
        result.timing_cost_bps = (timing / result.decision_price) * 10000;
        
        // Opportunity cost (unfilled portion)
        if (execution.target_qty > execution.filled_qty) {
            double unfilled_pct = (execution.target_qty - execution.filled_qty) / execution.target_qty;
            double missed_gain = close_price - result.arrival_price;
            result.opportunity_cost_bps = unfilled_pct * (missed_gain / result.arrival_price) * 10000;
        }
        
        // Total
        result.total_cost_bps = result.spread_cost_bps + result.market_impact_bps + 
                                result.timing_cost_bps + std::max(0.0, result.opportunity_cost_bps);
        
        // Benchmark comparisons
        result.vs_arrival_bps = ((result.execution_price - result.arrival_price) / 
                                 result.arrival_price) * 10000;
        result.vs_vwap_bps = result.vwap > 0 ? 
            ((result.execution_price - result.vwap) / result.vwap) * 10000 : 0;
        result.vs_close_bps = ((result.execution_price - close_price) / close_price) * 10000;
        
        // Implementation shortfall
        result.implementation_shortfall = 
            (result.execution_price - result.decision_price) * result.quantity;
        
        return result;
    }
    
    /**
     * @brief Aggregate TCA for multiple executions
     */
    AlgoTCAResult aggregate(const std::vector<AlgoTCAResult>& results) const {
        AlgoTCAResult agg;
        agg.order_id = "AGGREGATE";
        agg.symbol = "MULTIPLE";
        
        double total_notional = 0;
        double weighted_spread = 0;
        double weighted_impact = 0;
        double weighted_timing = 0;
        double weighted_opportunity = 0;
        double weighted_arrival = 0;
        double weighted_vwap = 0;
        double weighted_close = 0;
        
        for (const auto& r : results) {
            total_notional += r.notional;
            weighted_spread += r.spread_cost_bps * r.notional;
            weighted_impact += r.market_impact_bps * r.notional;
            weighted_timing += r.timing_cost_bps * r.notional;
            weighted_opportunity += r.opportunity_cost_bps * r.notional;
            weighted_arrival += r.vs_arrival_bps * r.notional;
            weighted_vwap += r.vs_vwap_bps * r.notional;
            weighted_close += r.vs_close_bps * r.notional;
            agg.implementation_shortfall += r.implementation_shortfall;
        }
        
        agg.notional = total_notional;
        if (total_notional > 0) {
            agg.spread_cost_bps = weighted_spread / total_notional;
            agg.market_impact_bps = weighted_impact / total_notional;
            agg.timing_cost_bps = weighted_timing / total_notional;
            agg.opportunity_cost_bps = weighted_opportunity / total_notional;
            agg.vs_arrival_bps = weighted_arrival / total_notional;
            agg.vs_vwap_bps = weighted_vwap / total_notional;
            agg.vs_close_bps = weighted_close / total_notional;
        }
        
        agg.total_cost_bps = agg.spread_cost_bps + agg.market_impact_bps + 
                            agg.timing_cost_bps + std::max(0.0, agg.opportunity_cost_bps);
        
        return agg;
    }
};

// =============================================================================
// Market Impact Model
// =============================================================================

/**
 * @brief Market impact estimation parameters
 */
struct ImpactParams {
    double volatility{0.02};        // Daily volatility
    double avg_daily_volume{1000000};
    double bid_ask_spread{0.01};
    double permanent_impact_coef{0.1};   // Permanent impact coefficient
    double temporary_impact_coef{0.1};   // Temporary impact coefficient
};

/**
 * @brief Market impact estimation result
 */
struct ImpactEstimate {
    double temporary_impact_bps{0};
    double permanent_impact_bps{0};
    double total_impact_bps{0};
    double impact_cost_dollars{0};
    double optimal_execution_time_minutes{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Market Impact Estimate:\n"
            << "  Temporary: " << temporary_impact_bps << " bps\n"
            << "  Permanent: " << permanent_impact_bps << " bps\n"
            << "  Total:     " << total_impact_bps << " bps\n"
            << "  Cost:      $" << impact_cost_dollars << "\n"
            << "  Optimal Execution: " << optimal_execution_time_minutes << " minutes";
        return oss.str();
    }
};

/**
 * @brief Market Impact Model (Almgren-Chriss style)
 */
class MarketImpactModel {
public:
    /**
     * @brief Estimate market impact
     * 
     * Uses square-root model: Impact = sigma * sqrt(Q/V) * coefficient
     */
    ImpactEstimate estimate(double quantity,
                           double price,
                           const ImpactParams& params,
                           double execution_time_minutes = 60) {
        ImpactEstimate result;
        
        double notional = quantity * price;
        
        // Participation rate
        double participation = quantity / params.avg_daily_volume;
        
        // Square-root temporary impact
        // I_temp = sigma * eta * sqrt(Q/V)
        result.temporary_impact_bps = params.volatility * 
                                      params.temporary_impact_coef * 
                                      std::sqrt(participation) * 10000;
        
        // Linear permanent impact
        // I_perm = gamma * Q/V
        result.permanent_impact_bps = params.permanent_impact_coef * 
                                      participation * 10000;
        
        // Adjust for execution time (faster = more impact)
        double time_factor = std::sqrt(60.0 / std::max(1.0, execution_time_minutes));
        result.temporary_impact_bps *= time_factor;
        
        result.total_impact_bps = result.temporary_impact_bps + result.permanent_impact_bps;
        
        // Cost in dollars
        result.impact_cost_dollars = notional * result.total_impact_bps / 10000;
        
        // Optimal execution time (Almgren-Chriss)
        // T* minimizes risk-adjusted cost
        // Simplified: T* proportional to sqrt(Q/V)
        result.optimal_execution_time_minutes = 
            60 * std::sqrt(participation) * 10;  // Base is 60 min for 1% ADV
        result.optimal_execution_time_minutes = 
            std::clamp(result.optimal_execution_time_minutes, 5.0, 390.0);
        
        return result;
    }
    
    /**
     * @brief Estimate with real-time market data
     */
    ImpactEstimate estimate_realtime(double quantity,
                                     double price,
                                     double current_volume,
                                     double volatility,
                                     double spread) {
        ImpactParams params;
        params.volatility = volatility;
        params.avg_daily_volume = current_volume * (390.0 / 60.0);  // Extrapolate to full day
        params.bid_ask_spread = spread;
        
        return estimate(quantity, price, params);
    }
};

// =============================================================================
// Algo Execution Manager
// =============================================================================

// AlgoType enum defined in smart_router.hpp

inline std::string algo_type_to_string(AlgoType type) {
    return algo_name(type);
}

/**
 * @brief Manages algorithmic execution strategies
 */
class AlgoExecutionManager {
private:
    SmartOrderRouter router_;
    TransactionCostAnalyzer tca_;
    MarketImpactModel impact_model_;
    
    std::map<std::string, std::unique_ptr<TWAPExecutor>> twap_algos_;
    std::map<std::string, std::unique_ptr<VWAPExecutor>> vwap_algos_;
    std::map<std::string, ExecutionMetrics> completed_;
    std::vector<AlgoTCAResult> tca_history_;
    
    mutable std::mutex mutex_;
    int algo_counter_{0};
    
    std::string generate_algo_id(AlgoType type) {
        return algo_type_to_string(type) + "-" + std::to_string(++algo_counter_);
    }

public:
    /**
     * @brief Start TWAP execution
     */
    std::string start_twap(const TWAPParams& params,
                           TWAPExecutor::OrderCallback submit_order,
                           TWAPExecutor::QuoteCallback get_quote) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string algo_id = generate_algo_id(AlgoType::TWAP);
        
        auto executor = std::make_unique<TWAPExecutor>(
            params, submit_order, get_quote,
            [this, algo_id](const ExecutionMetrics& metrics) {
                on_algo_update(algo_id, metrics);
            });
        
        executor->start();
        twap_algos_[algo_id] = std::move(executor);
        
        return algo_id;
    }
    
    /**
     * @brief Start VWAP execution
     */
    std::string start_vwap(const VWAPParams& params,
                           VWAPExecutor::OrderCallback submit_order,
                           VWAPExecutor::QuoteCallback get_quote,
                           VWAPExecutor::VolumeCallback get_volume) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string algo_id = generate_algo_id(AlgoType::VWAP);
        
        auto executor = std::make_unique<VWAPExecutor>(
            params, submit_order, get_quote, get_volume,
            [this, algo_id](const ExecutionMetrics& metrics) {
                on_algo_update(algo_id, metrics);
            });
        
        executor->start();
        vwap_algos_[algo_id] = std::move(executor);
        
        return algo_id;
    }
    
    /**
     * @brief Stop an algorithm
     */
    bool stop_algo(const std::string& algo_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (auto it = twap_algos_.find(algo_id); it != twap_algos_.end()) {
            it->second->stop();
            return true;
        }
        if (auto it = vwap_algos_.find(algo_id); it != vwap_algos_.end()) {
            it->second->stop();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Pause an algorithm
     */
    bool pause_algo(const std::string& algo_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (auto it = twap_algos_.find(algo_id); it != twap_algos_.end()) {
            it->second->pause();
            return true;
        }
        if (auto it = vwap_algos_.find(algo_id); it != vwap_algos_.end()) {
            it->second->pause();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Resume an algorithm
     */
    bool resume_algo(const std::string& algo_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (auto it = twap_algos_.find(algo_id); it != twap_algos_.end()) {
            it->second->resume();
            return true;
        }
        if (auto it = vwap_algos_.find(algo_id); it != vwap_algos_.end()) {
            it->second->resume();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Get algorithm metrics
     */
    ExecutionMetrics get_metrics(const std::string& algo_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (auto it = twap_algos_.find(algo_id); it != twap_algos_.end()) {
            return it->second->get_metrics();
        }
        if (auto it = vwap_algos_.find(algo_id); it != vwap_algos_.end()) {
            return it->second->get_metrics();
        }
        if (auto it = completed_.find(algo_id); it != completed_.end()) {
            return it->second;
        }
        return {};
    }
    
    /**
     * @brief Record a fill for an algorithm
     */
    void record_fill(const std::string& algo_id, 
                     const std::string& child_id,
                     double qty, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (auto it = twap_algos_.find(algo_id); it != twap_algos_.end()) {
            it->second->record_fill(child_id, qty, price);
        }
        if (auto it = vwap_algos_.find(algo_id); it != vwap_algos_.end()) {
            it->second->record_fill(child_id, qty, price);
        }
    }
    
    /**
     * @brief Get smart order routing
     */
    std::vector<RoutingDecision> route_order(const std::string& symbol,
                                              OrderSide side,
                                              double quantity,
                                              double limit_price,
                                              bool urgent = false) {
        return router_.route(symbol, side, quantity, limit_price, urgent);
    }
    
    /**
     * @brief Estimate market impact
     */
    ImpactEstimate estimate_impact(double quantity,
                                   double price,
                                   const ImpactParams& params) {
        return impact_model_.estimate(quantity, price, params);
    }
    
    /**
     * @brief Analyze execution quality
     */
    AlgoTCAResult analyze_execution(const ExecutionMetrics& execution,
                                double close_price,
                                double bid_ask_spread) {
        auto result = tca_.analyze(execution, close_price, bid_ask_spread);
        tca_history_.push_back(result);
        return result;
    }
    
    /**
     * @brief Get aggregate TCA
     */
    AlgoTCAResult get_aggregate_tca() const {
        return tca_.aggregate(tca_history_);
    }
    
    /**
     * @brief List active algorithms
     */
    std::vector<std::string> list_active_algos() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        
        for (const auto& [id, _] : twap_algos_) {
            result.push_back(id);
        }
        for (const auto& [id, _] : vwap_algos_) {
            result.push_back(id);
        }
        
        return result;
    }

private:
    void on_algo_update(const std::string& algo_id, const ExecutionMetrics& metrics) {
        // Store completed algos
        if (metrics.filled_qty >= metrics.target_qty || 
            metrics.remaining_qty <= 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_[algo_id] = metrics;
            twap_algos_.erase(algo_id);
            vwap_algos_.erase(algo_id);
        }
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_ALGO_EXECUTION_HPP
