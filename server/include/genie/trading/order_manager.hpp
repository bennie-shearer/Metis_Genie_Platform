/**
 * @file order_manager.hpp
 * @brief Enhanced order management with validation, preview, and monitoring
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Comprehensive order management system:
 * - Order validation against market data
 * - Price limit validation (% from market)
 * - Order preview with fee estimation
 * - Stop-loss monitoring and triggers
 * - Order modification and cancellation
 * - Partial fill tracking and handling
 * - Order book maintenance
 * - Execution quality analysis
 */
#pragma once
#ifndef GENIE_TRADING_ORDER_MANAGER_HPP
#define GENIE_TRADING_ORDER_MANAGER_HPP

#include "broker_interface.hpp"
#include "../market/price_cache.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace genie::trading {

/**
 * @brief Order validation result
 */
struct OrderValidation {
    bool valid{false};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // Validation details
    bool symbol_valid{false};
    bool quantity_valid{false};
    bool price_valid{false};
    bool funds_sufficient{false};
    bool market_open{false};
    
    // Price analysis
    double current_price{0};
    double limit_deviation_pct{0};  // % deviation from market
    double estimated_fill_price{0};
    
    void add_error(const std::string& msg) {
        valid = false;
        errors.push_back(msg);
    }
    
    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
    }
    
    std::string summary() const {
        std::ostringstream oss;
        if (valid) {
            oss << "VALID";
            if (!warnings.empty()) {
                oss << " with " << warnings.size() << " warning(s)";
            }
        } else {
            oss << "INVALID: " << errors.size() << " error(s)";
        }
        return oss.str();
    }
};

/**
 * @brief Fee structure
 */
struct FeeStructure {
    double commission_per_share{0};      // Per-share commission
    double commission_minimum{0};         // Minimum commission
    double commission_maximum{0};         // Maximum commission (0 = no max)
    double sec_fee_rate{0.0000278};       // SEC fee (sell only)
    double taf_fee_rate{0.000166};        // TAF fee per share
    double exchange_fee_rate{0};          // Exchange fees
    bool commission_free{true};           // Most brokers now commission-free
    
    static FeeStructure alpaca() {
        FeeStructure fees;
        fees.commission_free = true;
        fees.sec_fee_rate = 0.0000278;    // $27.80 per $1M
        fees.taf_fee_rate = 0.000166;     // $0.000166 per share
        return fees;
    }
    
    static FeeStructure interactive_brokers() {
        FeeStructure fees;
        fees.commission_free = false;
        fees.commission_per_share = 0.005;
        fees.commission_minimum = 1.00;
        fees.commission_maximum = 0;  // % of trade value
        fees.sec_fee_rate = 0.0000278;
        fees.taf_fee_rate = 0.000166;
        return fees;
    }
};

/**
 * @brief Order preview with cost breakdown
 */
struct OrderPreview {
    // Order details
    std::string symbol;
    OrderSide side;
    OrderType type;
    double quantity{0};
    double limit_price{0};
    
    // Price estimates
    double estimated_price{0};
    double estimated_value{0};
    
    // Fee breakdown
    double commission{0};
    double sec_fee{0};
    double taf_fee{0};
    double exchange_fee{0};
    double total_fees{0};
    
    // Total cost/proceeds
    double total_cost{0};           // For buys: value + fees
    double total_proceeds{0};       // For sells: value - fees
    
    // Impact analysis
    double market_impact_pct{0};    // Estimated market impact
    double slippage_estimate{0};    // Estimated slippage
    
    // Account impact
    double buying_power_before{0};
    double buying_power_after{0};
    double portfolio_weight{0};     // Position weight after trade
    
    // Validation
    OrderValidation validation;
    
    bool is_valid() const { return validation.valid; }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        oss << "=== Order Preview ===\n";
        oss << order_side_to_string(side) << " " << quantity << " " << symbol;
        if (type == OrderType::Limit) {
            oss << " @ $" << limit_price;
        }
        oss << "\n\n";
        
        oss << "Estimated Price: $" << estimated_price << "\n";
        oss << "Estimated Value: $" << estimated_value << "\n\n";
        
        oss << "Fees:\n";
        if (commission > 0) oss << "  Commission: $" << commission << "\n";
        if (sec_fee > 0) oss << "  SEC Fee: $" << sec_fee << "\n";
        if (taf_fee > 0) oss << "  TAF Fee: $" << taf_fee << "\n";
        if (exchange_fee > 0) oss << "  Exchange: $" << exchange_fee << "\n";
        oss << "  Total Fees: $" << total_fees << "\n\n";
        
        if (side == OrderSide::Buy) {
            oss << "Total Cost: $" << total_cost << "\n";
        } else {
            oss << "Total Proceeds: $" << total_proceeds << "\n";
        }
        
        oss << "\nBuying Power: $" << buying_power_before 
            << " -> $" << buying_power_after << "\n";
        
        if (!validation.valid) {
            oss << "\n** VALIDATION FAILED **\n";
            for (const auto& err : validation.errors) {
                oss << "  - " << err << "\n";
            }
        }
        
        return oss.str();
    }
};

/**
 * @brief Partial fill information
 */
struct PartialFill {
    std::string order_id;
    std::string fill_id;
    double filled_qty{0};
    double fill_price{0};
    double cumulative_qty{0};
    double cumulative_value{0};
    double average_price{0};
    double remaining_qty{0};
    std::chrono::system_clock::time_point fill_time;
};

/**
 * @brief Order fill state tracking
 */
struct OrderFillState {
    std::string order_id;
    double total_qty{0};
    double filled_qty{0};
    double remaining_qty{0};
    double average_fill_price{0};
    double total_fill_value{0};
    int fill_count{0};
    std::vector<PartialFill> fills;
    bool is_complete() const { return remaining_qty <= 0; }
};

/**
 * @brief Stop-loss monitor entry
 */
struct StopLossMonitor {
    std::string id;
    std::string symbol;
    double trigger_price{0};
    double quantity{0};
    OrderType order_type{OrderType::Market};  // Market or Limit
    double limit_price{0};                     // If limit order
    bool triggered{false};
    std::string order_id;                      // Resulting order
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point triggered_at;
};

/**
 * @brief Order manager configuration
 */
struct OrderManagerConfig {
    // Validation limits
    double max_limit_deviation_pct{10.0};    // Max % from market for limits
    double max_order_value{100000};          // Max single order value
    double min_order_value{1.0};             // Min order value
    int max_shares_per_order{10000};         // Max shares per order
    
    // Stop-loss monitoring
    int stop_monitor_interval_ms{1000};      // Check interval
    bool auto_submit_stops{true};            // Auto-submit when triggered
    
    // Fee structure
    FeeStructure fees{FeeStructure::alpaca()};
    
    // Market impact model
    double market_impact_coefficient{0.1};   // Impact = coef * sqrt(qty/adv)
    double slippage_coefficient{0.5};        // Slippage estimate
};

// Callbacks
using OnFillCallback = std::function<void(const PartialFill&)>;
using OnStopTriggerCallback = std::function<void(const StopLossMonitor&)>;
using OnOrderUpdateCallback = std::function<void(const BrokerOrder&)>;

/**
 * @brief Enhanced order manager
 */
class OrderManager {
public:
    OrderManager() : stop_monitor_running_(false) {}
    
    OrderManager(std::shared_ptr<IBroker> broker,
                 std::shared_ptr<market::PriceCache> price_cache,
                 const OrderManagerConfig& config = {})
        : broker_(broker)
        , price_cache_(price_cache)
        , config_(config)
        , stop_monitor_running_(false) {}
    
    ~OrderManager() {
        stop_monitoring();
    }
    
    // ========================================================================
    // Order Validation
    // ========================================================================
    
    /**
     * @brief Validate order before submission
     */
    OrderValidation validate_order(const OrderRequest& request) {
        OrderValidation result;
        result.valid = true;
        
        // 1. Symbol validation
        if (request.symbol.empty()) {
            result.add_error("Symbol is required");
        } else {
            result.symbol_valid = true;
        }
        
        // 2. Quantity validation
        if (request.qty <= 0) {
            result.add_error("Quantity must be positive");
        } else if (request.qty > config_.max_shares_per_order) {
            result.add_error("Quantity exceeds maximum of " + 
                           std::to_string(config_.max_shares_per_order));
        } else {
            result.quantity_valid = true;
        }
        
        // 3. Get current market price
        auto cached = price_cache_->get(request.symbol);
        if (cached && cached->price > 0) {
            result.current_price = cached->price;
            result.estimated_fill_price = cached->price;
        } else {
            result.add_warning("Current price not available for " + request.symbol);
        }
        
        // 4. Price validation for limit orders
        if (request.type == OrderType::Limit) {
            if (!request.limit_price || *request.limit_price <= 0) {
                result.add_error("Limit price required for limit orders");
            } else if (result.current_price > 0) {
                double deviation = std::abs(*request.limit_price - result.current_price) / 
                                  result.current_price * 100;
                result.limit_deviation_pct = deviation;
                
                if (deviation > config_.max_limit_deviation_pct) {
                    result.add_error("Limit price deviates " + 
                                    std::to_string(deviation) + 
                                    "% from market (max " +
                                    std::to_string(config_.max_limit_deviation_pct) + "%)");
                } else if (deviation > 5.0) {
                    result.add_warning("Limit price deviates " + 
                                      std::to_string(deviation) + "% from market");
                }
                
                result.estimated_fill_price = *request.limit_price;
                result.price_valid = true;
            }
        } else if (request.type == OrderType::Stop || 
                   request.type == OrderType::StopLimit) {
            if (!request.stop_price || *request.stop_price <= 0) {
                result.add_error("Stop price required for stop orders");
            } else {
                result.price_valid = true;
            }
        } else {
            result.price_valid = true;
        }
        
        // 5. Order value check
        double estimated_value = request.qty * result.estimated_fill_price;
        if (estimated_value > config_.max_order_value) {
            result.add_error("Order value $" + std::to_string(estimated_value) +
                           " exceeds maximum $" + std::to_string(config_.max_order_value));
        } else if (estimated_value < config_.min_order_value) {
            result.add_error("Order value below minimum $" + 
                           std::to_string(config_.min_order_value));
        }
        
        // 6. Buying power check (for buys)
        if (request.side == OrderSide::Buy && broker_) {
            auto account_result = broker_->get_account();
            if (account_result.success) {
                double buying_power = account_result.data.buying_power;
                if (estimated_value > buying_power) {
                    result.add_error("Insufficient buying power: need $" +
                                    std::to_string(estimated_value) + ", have $" +
                                    std::to_string(buying_power));
                } else {
                    result.funds_sufficient = true;
                }
            }
        } else {
            result.funds_sufficient = true;  // Sells don't need buying power
        }
        
        // 7. Market hours check
        if (broker_) {
            auto clock_result = broker_->get_clock();
            if (clock_result.success) {
                result.market_open = clock_result.data.is_open;
                if (!result.market_open && !request.extended_hours) {
                    result.add_warning("Market is currently closed");
                }
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // Order Preview
    // ========================================================================
    
    /**
     * @brief Generate order preview with cost breakdown
     */
    OrderPreview preview_order(const OrderRequest& request) {
        OrderPreview preview;
        preview.symbol = request.symbol;
        preview.side = request.side;
        preview.type = request.type;
        preview.quantity = request.qty;
        preview.limit_price = request.limit_price.value_or(0);
        
        // Validate first
        preview.validation = validate_order(request);
        
        // Get current price
        auto cached = price_cache_->get(request.symbol);
        if (cached) {
            preview.estimated_price = cached->price;
        }
        
        // For limit orders, use limit price
        if (request.type == OrderType::Limit && request.limit_price) {
            // For buys, estimate fill at worse of market/limit
            // For sells, estimate at worse of market/limit
            if (request.side == OrderSide::Buy) {
                preview.estimated_price = std::max(preview.estimated_price, 
                                                   *request.limit_price);
            } else {
                preview.estimated_price = std::min(preview.estimated_price,
                                                   *request.limit_price);
            }
        }
        
        // Calculate estimated value
        preview.estimated_value = request.qty * preview.estimated_price;
        
        // Calculate fees
        calculate_fees(preview, request);
        
        // Calculate total cost/proceeds
        if (request.side == OrderSide::Buy) {
            preview.total_cost = preview.estimated_value + preview.total_fees;
            preview.total_proceeds = 0;
        } else {
            preview.total_cost = 0;
            preview.total_proceeds = preview.estimated_value - preview.total_fees;
        }
        
        // Account impact
        if (broker_) {
            auto account_result = broker_->get_account();
            if (account_result.success) {
                preview.buying_power_before = account_result.data.buying_power;
                
                if (request.side == OrderSide::Buy) {
                    preview.buying_power_after = preview.buying_power_before - 
                                                 preview.total_cost;
                } else {
                    preview.buying_power_after = preview.buying_power_before + 
                                                 preview.total_proceeds;
                }
                
                // Calculate portfolio weight
                double portfolio_value = account_result.data.portfolio_value;
                if (portfolio_value > 0) {
                    preview.portfolio_weight = preview.estimated_value / portfolio_value * 100;
                }
            }
        }
        
        // Market impact estimate
        preview.market_impact_pct = estimate_market_impact(request);
        preview.slippage_estimate = preview.estimated_value * preview.market_impact_pct / 100;
        
        return preview;
    }
    
    // ========================================================================
    // Order Submission
    // ========================================================================
    
    /**
     * @brief Submit order with validation
     */
    BrokerResult<BrokerOrder> submit_order(const OrderRequest& request,
                                           bool validate_first = true) {
        BrokerResult<BrokerOrder> result;
        
        // Validate if requested
        if (validate_first) {
            auto validation = validate_order(request);
            if (!validation.valid) {
                result.success = false;
                result.error = "Validation failed: ";
                for (const auto& err : validation.errors) {
                    result.error += err + "; ";
                }
                return result;
            }
        }
        
        // Submit to broker
        result = broker_->submit_order(request);
        
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Track the order
            active_orders_[result.data.id] = result.data;
            
            // Initialize fill tracking
            OrderFillState fill_state;
            fill_state.order_id = result.data.id;
            fill_state.total_qty = request.qty;
            fill_state.filled_qty = 0;
            fill_state.remaining_qty = request.qty;
            fill_tracking_[result.data.id] = fill_state;
        }
        
        return result;
    }
    
    /**
     * @brief Submit limit buy order
     */
    BrokerResult<BrokerOrder> submit_limit_buy(const std::string& symbol,
                                                double qty, double price) {
        return submit_order(limit_order(symbol, qty, OrderSide::Buy, price));
    }
    
    /**
     * @brief Submit limit sell order
     */
    BrokerResult<BrokerOrder> submit_limit_sell(const std::string& symbol,
                                                 double qty, double price) {
        return submit_order(limit_order(symbol, qty, OrderSide::Sell, price));
    }
    
    /**
     * @brief Submit stop-loss order
     */
    BrokerResult<BrokerOrder> submit_stop_loss(const std::string& symbol,
                                                double qty, double stop_price) {
        return submit_order(stop_order(symbol, qty, OrderSide::Sell, stop_price));
    }
    
    // ========================================================================
    // Order Modification
    // ========================================================================
    
    /**
     * @brief Modify existing order
     */
    BrokerResult<BrokerOrder> modify_order(const std::string& order_id,
                                           std::optional<double> new_qty = std::nullopt,
                                           std::optional<double> new_limit = std::nullopt,
                                           std::optional<double> new_stop = std::nullopt) {
        BrokerResult<BrokerOrder> result;
        
        // Get current order
        auto order_result = broker_->get_order(order_id);
        if (!order_result.success) {
            result.success = false;
            result.error = "Order not found: " + order_id;
            return result;
        }
        
        auto& order = order_result.data;
        
        // Build replacement request
        OrderRequest replacement;
        replacement.symbol = order.symbol;
        replacement.qty = new_qty.value_or(order.qty);
        replacement.side = order.side;
        replacement.type = order.type;
        replacement.time_in_force = order.time_in_force;
        
        if (new_limit) {
            replacement.limit_price = *new_limit;
        } else if (order.limit_price) {
            replacement.limit_price = *order.limit_price;
        }
        
        if (new_stop) {
            replacement.stop_price = *new_stop;
        } else if (order.stop_price) {
            replacement.stop_price = *order.stop_price;
        }
        
        // Use broker's replace functionality
        return broker_->replace_order(order_id, replacement);
    }
    
    /**
     * @brief Cancel order
     */
    BrokerResult<void> cancel_order(const std::string& order_id) {
        auto result = broker_->cancel_order(order_id);
        
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            active_orders_.erase(order_id);
            fill_tracking_.erase(order_id);
        }
        
        BrokerResult<void> ret;
        ret.success = result.success;
        ret.error = result.error;
        ret.http_status = result.http_status;
        return ret;
    }
    
    /**
     * @brief Cancel all open orders
     */
    BrokerResult<int> cancel_all_orders() {
        BrokerResult<int> result;
        
        auto cancel_result = broker_->cancel_all_orders();
        if (cancel_result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            result.success = true;
            result.data = static_cast<int>(active_orders_.size());
            active_orders_.clear();
            fill_tracking_.clear();
        } else {
            result.success = false;
            result.error = cancel_result.error;
        }
        
        return result;
    }
    
    // ========================================================================
    // Partial Fill Handling
    // ========================================================================
    
    /**
     * @brief Process fill update
     */
    void process_fill(const std::string& order_id, double filled_qty, double fill_price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = fill_tracking_.find(order_id);
        if (it == fill_tracking_.end()) return;
        
        auto& state = it->second;
        
        // Calculate partial fill
        double new_fill_qty = filled_qty - state.filled_qty;
        if (new_fill_qty <= 0) return;  // Not a new fill
        
        PartialFill fill;
        fill.order_id = order_id;
        fill.fill_id = generate_fill_id();
        fill.filled_qty = new_fill_qty;
        fill.fill_price = fill_price;
        fill.fill_time = std::chrono::system_clock::now();
        
        // Update state
        state.fills.push_back(fill);
        state.filled_qty = filled_qty;
        state.remaining_qty = state.total_qty - filled_qty;
        
        // Calculate weighted average price
        double total_value = 0;
        for (const auto& f : state.fills) {
            total_value += f.filled_qty * f.fill_price;
        }
        state.average_fill_price = total_value / state.filled_qty;
        
        fill.cumulative_qty = state.filled_qty;
        fill.cumulative_value = total_value;
        fill.average_price = state.average_fill_price;
        fill.remaining_qty = state.remaining_qty;
        
        // Fire callback
        if (on_fill_) {
            on_fill_(fill);
        }
    }
    
    /**
     * @brief Get fill state for order
     */
    std::optional<OrderFillState> get_fill_state(const std::string& order_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fill_tracking_.find(order_id);
        if (it == fill_tracking_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get all partial fills for order
     */
    std::vector<PartialFill> get_fills(const std::string& order_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fill_tracking_.find(order_id);
        if (it == fill_tracking_.end()) return {};
        return it->second.fills;
    }
    
    // ========================================================================
    // Stop-Loss Monitoring
    // ========================================================================
    
    /**
     * @brief Add stop-loss monitor
     */
    std::string add_stop_monitor(const std::string& symbol, double trigger_price,
                                  double quantity, OrderType order_type = OrderType::Market,
                                  double limit_price = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        StopLossMonitor monitor;
        monitor.id = generate_stop_id();
        monitor.symbol = symbol;
        monitor.trigger_price = trigger_price;
        monitor.quantity = quantity;
        monitor.order_type = order_type;
        monitor.limit_price = limit_price;
        monitor.triggered = false;
        monitor.created_at = std::chrono::system_clock::now();
        
        stop_monitors_[monitor.id] = monitor;
        symbol_stops_[symbol].insert(monitor.id);
        
        return monitor.id;
    }
    
    /**
     * @brief Remove stop-loss monitor
     */
    bool remove_stop_monitor(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stop_monitors_.find(id);
        if (it == stop_monitors_.end()) return false;
        
        symbol_stops_[it->second.symbol].erase(id);
        stop_monitors_.erase(it);
        
        return true;
    }
    
    /**
     * @brief Get all stop monitors
     */
    std::vector<StopLossMonitor> get_stop_monitors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StopLossMonitor> result;
        for (const auto& [id, monitor] : stop_monitors_) {
            result.push_back(monitor);
        }
        return result;
    }
    
    /**
     * @brief Start stop-loss monitoring
     */
    void start_monitoring() {
        if (stop_monitor_running_) return;
        
        stop_monitor_running_ = true;
        monitor_thread_ = std::thread([this]() {
            while (stop_monitor_running_) {
                check_stop_triggers();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.stop_monitor_interval_ms));
            }
        });
    }
    
    /**
     * @brief Stop monitoring
     */
    void stop_monitoring() {
        stop_monitor_running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    // ========================================================================
    // Order Book
    // ========================================================================
    
    /**
     * @brief Get active orders
     */
    std::vector<BrokerOrder> get_active_orders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BrokerOrder> result;
        for (const auto& [id, order] : active_orders_) {
            result.push_back(order);
        }
        return result;
    }
    
    /**
     * @brief Refresh order status from broker
     */
    void refresh_orders() {
        auto orders_result = broker_->get_orders("open");
        if (!orders_result.success) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        active_orders_.clear();
        
        for (const auto& order : orders_result.data) {
            active_orders_[order.id] = order;
            
            // Update fill tracking
            if (fill_tracking_.find(order.id) == fill_tracking_.end()) {
                OrderFillState state;
                state.order_id = order.id;
                state.total_qty = order.qty;
                state.filled_qty = order.filled_qty;
                state.remaining_qty = order.qty - order.filled_qty;
                if (order.filled_avg_price > 0) {
                    state.average_fill_price = order.filled_avg_price;
                }
                fill_tracking_[order.id] = state;
            }
        }
    }
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    void on_fill(OnFillCallback callback) { on_fill_ = callback; }
    void on_stop_trigger(OnStopTriggerCallback callback) { on_stop_trigger_ = callback; }
    void on_order_update(OnOrderUpdateCallback callback) { on_order_update_ = callback; }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    const OrderManagerConfig& config() const { return config_; }
    void set_config(const OrderManagerConfig& config) { config_ = config; }

private:
    std::shared_ptr<IBroker> broker_;
    std::shared_ptr<market::PriceCache> price_cache_;
    OrderManagerConfig config_;
    
    mutable std::mutex mutex_;
    std::map<std::string, BrokerOrder> active_orders_;
    std::map<std::string, OrderFillState> fill_tracking_;
    
    // Stop-loss monitoring
    std::map<std::string, StopLossMonitor> stop_monitors_;
    std::map<std::string, std::set<std::string>> symbol_stops_;
    std::atomic<bool> stop_monitor_running_;
    std::thread monitor_thread_;
    
    OnFillCallback on_fill_;
    OnStopTriggerCallback on_stop_trigger_;
    OnOrderUpdateCallback on_order_update_;
    
    int fill_counter_{0};
    int stop_counter_{0};
    
    std::string generate_fill_id() {
        return "FILL-" + std::to_string(++fill_counter_);
    }
    
    std::string generate_stop_id() {
        return "STOP-" + std::to_string(++stop_counter_);
    }
    
    void calculate_fees(OrderPreview& preview, const OrderRequest& request) {
        // Commission
        if (!config_.fees.commission_free) {
            preview.commission = request.qty * config_.fees.commission_per_share;
            if (preview.commission < config_.fees.commission_minimum) {
                preview.commission = config_.fees.commission_minimum;
            }
            if (config_.fees.commission_maximum > 0 &&
                preview.commission > config_.fees.commission_maximum) {
                preview.commission = config_.fees.commission_maximum;
            }
        }
        
        // SEC fee (sells only)
        if (request.side == OrderSide::Sell) {
            preview.sec_fee = preview.estimated_value * config_.fees.sec_fee_rate;
        }
        
        // TAF fee
        preview.taf_fee = request.qty * config_.fees.taf_fee_rate;
        
        // Exchange fees
        preview.exchange_fee = preview.estimated_value * config_.fees.exchange_fee_rate;
        
        // Total fees
        preview.total_fees = preview.commission + preview.sec_fee + 
                            preview.taf_fee + preview.exchange_fee;
    }
    
    double estimate_market_impact(const OrderRequest& request) {
        // Simple market impact model based on order size
        // Impact = coefficient * sqrt(quantity / average_daily_volume)
        // For now, use a simplified estimate
        double base_impact = 0.01;  // 1 basis point base
        double size_factor = std::sqrt(request.qty / 1000.0);  // Assume 1000 as reference
        return base_impact * size_factor * config_.market_impact_coefficient * 100;
    }
    
    void check_stop_triggers() {
        std::vector<StopLossMonitor> triggered;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            for (auto& [id, monitor] : stop_monitors_) {
                if (monitor.triggered) continue;
                
                auto cached = price_cache_->get(monitor.symbol);
                if (!cached) continue;
                
                // Check if price crossed below trigger
                if (cached->price <= monitor.trigger_price) {
                    monitor.triggered = true;
                    monitor.triggered_at = std::chrono::system_clock::now();
                    triggered.push_back(monitor);
                }
            }
        }
        
        // Process triggers outside lock
        for (auto& monitor : triggered) {
            // Fire callback
            if (on_stop_trigger_) {
                on_stop_trigger_(monitor);
            }
            
            // Auto-submit if configured
            if (config_.auto_submit_stops) {
                OrderRequest request;
                request.symbol = monitor.symbol;
                request.qty = monitor.quantity;
                request.side = OrderSide::Sell;
                request.type = monitor.order_type;
                
                if (monitor.order_type == OrderType::Limit && monitor.limit_price > 0) {
                    request.limit_price = monitor.limit_price;
                }
                
                auto result = broker_->submit_order(request);
                
                if (result.success) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stop_monitors_[monitor.id].order_id = result.data.id;
                }
            }
        }
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_ORDER_MANAGER_HPP
