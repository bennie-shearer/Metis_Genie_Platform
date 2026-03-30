/**
 * @file enhanced_orders.hpp
 * @brief Enhanced order management for prototype trading
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides advanced order management features:
 * - Limit order with price validation
 * - Stop-loss order with trigger monitoring
 * - Order preview with estimated fees
 * - Order modification and cancellation
 * - Partial fill handling and tracking
 * - Order lifecycle management
 * - Fill simulation for backtesting
 */
#pragma once
#ifndef GENIE_TRADING_ENHANCED_ORDERS_HPP
#define GENIE_TRADING_ENHANCED_ORDERS_HPP

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

// ============================================================================
// Order Validation
// ============================================================================

/**
 * @brief Order validation result
 */
struct EnhancedOrderValidation {
    bool is_valid{true};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // Price validation
    bool price_valid{true};
    double market_price{0};
    double limit_deviation_pct{0};   // How far limit is from market
    
    // Position validation
    bool position_valid{true};
    double current_position{0};
    double resulting_position{0};
    
    // Buying power validation
    bool buying_power_valid{true};
    double required_buying_power{0};
    double available_buying_power{0};
    
    // Risk validation
    bool risk_valid{true};
    double position_concentration{0};  // % of portfolio
    double max_loss_estimate{0};
    
    void add_error(const std::string& msg) {
        errors.push_back(msg);
        is_valid = false;
    }
    
    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
    }
    
    std::string format() const {
        std::ostringstream oss;
        if (is_valid) {
            oss << "Order VALID";
            if (!warnings.empty()) {
                oss << " with " << warnings.size() << " warning(s):\n";
                for (const auto& w : warnings) {
                    oss << "  - " << w << "\n";
                }
            }
        } else {
            oss << "Order INVALID:\n";
            for (const auto& e : errors) {
                oss << "  [ERROR] " << e << "\n";
            }
            for (const auto& w : warnings) {
                oss << "  [WARN] " << w << "\n";
            }
        }
        return oss.str();
    }
};

/**
 * @brief Order validation configuration
 */
struct ValidationConfig {
    // Price limits
    double max_limit_deviation_pct{10.0};    // Max % away from market for limit orders
    double max_stop_deviation_pct{20.0};     // Max % away from market for stop orders
    bool require_price_for_limits{true};     // Require market price to validate limits
    
    // Position limits
    double max_position_concentration{0.25}; // Max 25% in one position
    double max_order_size{100000};           // Max shares per order
    double min_order_size{1};                // Min shares
    
    // Risk limits
    double max_loss_per_trade_pct{5.0};      // Max 5% loss per trade
    bool allow_shorting{true};
    bool allow_margin{true};
    
    // Buying power
    double buying_power_buffer{0.95};        // Use max 95% of buying power
};

/**
 * @brief Order validator
 */
class OrderValidator {
public:
    explicit OrderValidator(const ValidationConfig& config = {})
        : config_(config) {}
    
    /**
     * @brief Validate order request
     */
    EnhancedOrderValidation validate(const OrderRequest& request,
                            double market_price,
                            double available_buying_power,
                            double current_position = 0,
                            double portfolio_value = 0) {
        EnhancedOrderValidation result;
        result.market_price = market_price;
        result.available_buying_power = available_buying_power;
        result.current_position = current_position;
        
        // Basic validation
        if (request.symbol.empty()) {
            result.add_error("Symbol is required");
        }
        
        if (request.qty <= 0) {
            result.add_error("Quantity must be positive");
        }
        
        if (request.qty < config_.min_order_size) {
            result.add_error("Quantity below minimum: " + 
                            std::to_string(config_.min_order_size));
        }
        
        if (request.qty > config_.max_order_size) {
            result.add_error("Quantity exceeds maximum: " + 
                            std::to_string(config_.max_order_size));
        }
        
        // Price validation for limit orders
        if (request.type == OrderType::Limit) {
            if (!request.limit_price || *request.limit_price <= 0) {
                result.add_error("Limit price required for limit orders");
                result.price_valid = false;
            } else if (market_price > 0) {
                double deviation = std::abs(*request.limit_price - market_price) / 
                                  market_price * 100;
                result.limit_deviation_pct = deviation;
                
                if (deviation > config_.max_limit_deviation_pct) {
                    result.add_warning("Limit price deviates " + 
                        format_pct(deviation) + " from market");
                }
                
                // Check if limit is on wrong side
                if (request.side == OrderSide::Buy && 
                    *request.limit_price > market_price * 1.05) {
                    result.add_warning("Buy limit above market - will fill immediately");
                }
                if (request.side == OrderSide::Sell && 
                    *request.limit_price < market_price * 0.95) {
                    result.add_warning("Sell limit below market - will fill immediately");
                }
            } else if (config_.require_price_for_limits) {
                result.add_error("Market price required to validate limit order");
                result.price_valid = false;
            }
        }
        
        // Price validation for stop orders
        if (request.type == OrderType::Stop || request.type == OrderType::StopLimit) {
            if (!request.stop_price || *request.stop_price <= 0) {
                result.add_error("Stop price required for stop orders");
                result.price_valid = false;
            } else if (market_price > 0) {
                double deviation = std::abs(*request.stop_price - market_price) / 
                                  market_price * 100;
                
                if (deviation > config_.max_stop_deviation_pct) {
                    result.add_warning("Stop price deviates " + 
                        format_pct(deviation) + " from market");
                }
                
                // Check if stop is on wrong side
                if (request.side == OrderSide::Sell && 
                    *request.stop_price > market_price) {
                    result.add_warning("Sell stop above market - unusual configuration");
                }
                if (request.side == OrderSide::Buy && 
                    *request.stop_price < market_price) {
                    result.add_warning("Buy stop below market - unusual configuration");
                }
            }
        }
        
        // Position validation
        double resulting_qty = current_position;
        if (request.side == OrderSide::Buy) {
            resulting_qty += request.qty;
        } else {
            resulting_qty -= request.qty;
        }
        result.resulting_position = resulting_qty;
        
        if (resulting_qty < 0 && !config_.allow_shorting) {
            result.add_error("Shorting not allowed - would result in short position");
            result.position_valid = false;
        }
        
        // Concentration check
        if (portfolio_value > 0 && market_price > 0) {
            double position_value = std::abs(resulting_qty) * market_price;
            result.position_concentration = position_value / portfolio_value;
            
            if (result.position_concentration > config_.max_position_concentration) {
                result.add_warning("Position concentration " + 
                    format_pct(result.position_concentration * 100) + 
                    " exceeds limit " + 
                    format_pct(config_.max_position_concentration * 100));
            }
        }
        
        // Buying power validation
        if (request.side == OrderSide::Buy && market_price > 0) {
            double price = request.limit_price ? *request.limit_price : market_price;
            result.required_buying_power = request.qty * price;
            
            double usable_bp = available_buying_power * config_.buying_power_buffer;
            if (result.required_buying_power > usable_bp) {
                result.add_error("Insufficient buying power: need $" + 
                    format_money(result.required_buying_power) + 
                    ", have $" + format_money(usable_bp));
                result.buying_power_valid = false;
            }
        }
        
        // Risk validation - estimate max loss
        if (market_price > 0) {
            double entry_price = request.limit_price ? *request.limit_price : market_price;
            
            if (request.stop_price) {
                // Have a stop - max loss is to stop
                result.max_loss_estimate = std::abs(entry_price - *request.stop_price) * 
                                          request.qty;
            } else {
                // No stop - estimate based on volatility or fixed %
                result.max_loss_estimate = entry_price * request.qty * 0.10;  // Assume 10%
            }
            
            if (portfolio_value > 0) {
                double loss_pct = result.max_loss_estimate / portfolio_value * 100;
                if (loss_pct > config_.max_loss_per_trade_pct) {
                    result.add_warning("Estimated max loss " + format_pct(loss_pct) + 
                        " exceeds limit " + format_pct(config_.max_loss_per_trade_pct));
                    result.risk_valid = false;
                }
            }
        }
        
        return result;
    }
    
    const ValidationConfig& config() const { return config_; }
    void set_config(const ValidationConfig& config) { config_ = config; }

private:
    ValidationConfig config_;
    
    std::string format_pct(double pct) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << pct << "%";
        return oss.str();
    }
    
    std::string format_money(double amount) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << amount;
        return oss.str();
    }
};

// ============================================================================
// Order Preview
// ============================================================================

/**
 * @brief Fee structure
 */
struct EnhancedFeeStructure {
    double commission_per_share{0};      // Per-share commission
    double commission_minimum{0};         // Minimum commission
    double commission_maximum{0};         // Maximum commission (0 = no max)
    double sec_fee_rate{0.0000278};      // SEC fee per dollar sold
    double taf_fee_rate{0.000166};       // TAF fee per share sold
    double exchange_fee{0};               // Exchange/ECN fee
    double clearing_fee{0};               // Clearing fee
    bool commission_free{false};          // Zero commission (like modern brokers)
};

/**
 * @brief Order preview with estimated costs
 */
struct EnhancedOrderPreview {
    // Order details
    std::string symbol;
    OrderSide side;
    OrderType type;
    double qty{0};
    double estimated_price{0};       // Expected fill price
    
    // Cost breakdown
    double principal{0};             // qty * price
    double commission{0};
    double sec_fee{0};               // SEC fee (sells only)
    double taf_fee{0};               // TAF fee (sells only)
    double exchange_fee{0};
    double clearing_fee{0};
    double total_fees{0};
    double total_cost{0};            // Principal + fees (buys) or Principal - fees (sells)
    
    // Impact estimates
    double market_impact_bps{0};     // Estimated market impact
    double slippage_estimate{0};     // Expected slippage
    
    // Validation
    EnhancedOrderValidation validation;
    
    // Net proceeds (for sells)
    double net_proceeds{0};
    
    bool is_valid() const { return validation.price_valid; }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        oss << "=== Order Preview ===\n";
        oss << order_side_to_string(side) << " " << qty << " " << symbol;
        if (type == OrderType::Limit) oss << " @ LIMIT";
        else if (type == OrderType::Stop) oss << " @ STOP";
        oss << "\n\n";
        
        oss << "Estimated Price: $" << estimated_price << "\n";
        oss << "Principal: $" << principal << "\n\n";
        
        oss << "Fees:\n";
        if (commission > 0) oss << "  Commission: $" << commission << "\n";
        if (sec_fee > 0) oss << "  SEC Fee: $" << sec_fee << "\n";
        if (taf_fee > 0) oss << "  TAF Fee: $" << taf_fee << "\n";
        if (exchange_fee > 0) oss << "  Exchange: $" << exchange_fee << "\n";
        if (clearing_fee > 0) oss << "  Clearing: $" << clearing_fee << "\n";
        oss << "  Total Fees: $" << total_fees << "\n\n";
        
        if (side == OrderSide::Buy) {
            oss << "Total Cost: $" << total_cost << "\n";
        } else {
            oss << "Net Proceeds: $" << net_proceeds << "\n";
        }
        
        if (market_impact_bps > 0) {
            oss << "\nMarket Impact: " << market_impact_bps << " bps\n";
        }
        
        if (!validation.price_valid) {
            oss << "\n" << validation.format();
        }
        
        return oss.str();
    }
};

/**
 * @brief Order preview generator
 */
class OrderPreviewGenerator {
public:
    explicit OrderPreviewGenerator(const EnhancedFeeStructure& fees = {},
                                   const ValidationConfig& validation_config = {})
        : fees_(fees)
        , validator_(validation_config) {}
    
    /**
     * @brief Generate order preview
     */
    EnhancedOrderPreview preview(const OrderRequest& request,
                         double market_price,
                         double available_buying_power = 0,
                         double current_position = 0,
                         double portfolio_value = 0,
                         double avg_daily_volume = 0) {
        EnhancedOrderPreview preview;
        preview.symbol = request.symbol;
        preview.side = request.side;
        preview.type = request.type;
        preview.qty = request.qty;
        
        // Determine estimated fill price
        if (request.type == OrderType::Market) {
            preview.estimated_price = market_price;
        } else if (request.type == OrderType::Limit && request.limit_price) {
            preview.estimated_price = *request.limit_price;
        } else if (request.type == OrderType::Stop && request.stop_price) {
            preview.estimated_price = *request.stop_price;
        } else if (request.type == OrderType::StopLimit) {
            preview.estimated_price = request.limit_price ? 
                *request.limit_price : (request.stop_price ? *request.stop_price : market_price);
        } else {
            preview.estimated_price = market_price;
        }
        
        // Calculate principal
        preview.principal = request.qty * preview.estimated_price;
        
        // Calculate fees
        calculate_fees(preview, request);
        
        // Estimate market impact
        if (avg_daily_volume > 0) {
            double participation = request.qty / avg_daily_volume;
            // Simple square-root market impact model
            preview.market_impact_bps = 10.0 * std::sqrt(participation) * 10000;
        }
        
        // Slippage estimate (for market orders)
        if (request.type == OrderType::Market) {
            preview.slippage_estimate = preview.principal * 0.0005;  // 5 bps
        }
        
        // Calculate totals
        preview.total_fees = preview.commission + preview.sec_fee + preview.taf_fee +
                            preview.exchange_fee + preview.clearing_fee;
        
        if (request.side == OrderSide::Buy) {
            preview.total_cost = preview.principal + preview.total_fees + 
                                preview.slippage_estimate;
            preview.net_proceeds = 0;
        } else {
            preview.net_proceeds = preview.principal - preview.total_fees - 
                                  preview.slippage_estimate;
            preview.total_cost = preview.total_fees;
        }
        
        // Validate
        preview.validation = validator_.validate(request, market_price,
                                                  available_buying_power,
                                                  current_position, portfolio_value);
        
        return preview;
    }
    
    const EnhancedFeeStructure& fees() const { return fees_; }
    void set_fees(const EnhancedFeeStructure& fees) { fees_ = fees; }

private:
    EnhancedFeeStructure fees_;
    OrderValidator validator_;
    
    void calculate_fees(EnhancedOrderPreview& preview, const OrderRequest& request) {
        // Commission
        if (!fees_.commission_free) {
            preview.commission = request.qty * fees_.commission_per_share;
            
            if (fees_.commission_minimum > 0) {
                preview.commission = std::max(preview.commission, fees_.commission_minimum);
            }
            if (fees_.commission_maximum > 0) {
                preview.commission = std::min(preview.commission, fees_.commission_maximum);
            }
        }
        
        // SEC and TAF fees (sells only)
        if (request.side == OrderSide::Sell) {
            preview.sec_fee = preview.principal * fees_.sec_fee_rate;
            preview.taf_fee = request.qty * fees_.taf_fee_rate;
        }
        
        // Other fees
        preview.exchange_fee = fees_.exchange_fee;
        preview.clearing_fee = fees_.clearing_fee;
    }
};

// ============================================================================
// Stop Loss Monitoring
// ============================================================================

/**
 * @brief Stop order state
 */
enum class StopState {
    Pending,      // Waiting to trigger
    Triggered,    // Price hit, order submitted
    Filled,       // Order filled
    Cancelled,    // Manually cancelled
    Expired       // Time expired
};

/**
 * @brief Monitored stop order
 */
struct MonitoredStop {
    std::string id;
    std::string symbol;
    OrderSide side{OrderSide::Sell};  // Usually sell for stop-loss
    double qty{0};
    double stop_price{0};
    double limit_price{0};            // 0 = market order when triggered
    StopState state{StopState::Pending};
    
    // Tracking
    double highest_price{0};          // For trailing stops
    double lowest_price{0};
    double trigger_price{0};          // Price that triggered
    std::string triggered_order_id;   // Resulting order ID
    
    // Configuration
    bool is_trailing{false};
    double trail_amount{0};           // Dollar amount
    double trail_percent{0};          // Percentage (0-100)
    
    // Timestamps
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point triggered_at;
    std::chrono::system_clock::time_point expires_at;  // 0 = GTC
    
    bool is_active() const {
        return state == StopState::Pending;
    }
    
    bool is_expired() const {
        if (expires_at.time_since_epoch().count() == 0) return false;
        return std::chrono::system_clock::now() >= expires_at;
    }
    
    double effective_stop_price() const {
        if (!is_trailing) return stop_price;
        
        // Calculate trailing stop price
        if (side == OrderSide::Sell) {
            // Trailing sell stop - follows price up
            if (trail_percent > 0) {
                return highest_price * (1.0 - trail_percent / 100.0);
            } else if (trail_amount > 0) {
                return highest_price - trail_amount;
            }
        } else {
            // Trailing buy stop - follows price down
            if (trail_percent > 0) {
                return lowest_price * (1.0 + trail_percent / 100.0);
            } else if (trail_amount > 0) {
                return lowest_price + trail_amount;
            }
        }
        
        return stop_price;
    }
};

/**
 * @brief Stop trigger event
 */
struct StopTriggerEvent {
    std::string stop_id;
    std::string symbol;
    double stop_price;
    double trigger_price;
    std::string order_id;
    std::chrono::system_clock::time_point timestamp;
};

using EnhancedOnStopTriggerCallback = std::function<void(const StopTriggerEvent&)>;

/**
 * @brief Stop loss monitor
 */
class EnhancedStopLossMonitor {
public:
    using SubmitOrderCallback = std::function<std::optional<BrokerOrder>(const OrderRequest&)>;
    
    EnhancedStopLossMonitor() = default;
    
    /**
     * @brief Set order submission callback
     */
    void set_submit_callback(SubmitOrderCallback callback) {
        submit_callback_ = callback;
    }
    
    /**
     * @brief Add stop loss order
     */
    std::string add_stop_loss(const std::string& symbol,
                               double qty,
                               double stop_price,
                               double limit_price = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        MonitoredStop stop;
        stop.id = generate_id();
        stop.symbol = symbol;
        stop.side = OrderSide::Sell;
        stop.qty = qty;
        stop.stop_price = stop_price;
        stop.limit_price = limit_price;
        stop.created_at = std::chrono::system_clock::now();
        
        stops_[stop.id] = stop;
        symbol_index_[symbol].insert(stop.id);
        
        return stop.id;
    }
    
    /**
     * @brief Add trailing stop
     */
    std::string add_trailing_stop(const std::string& symbol,
                                   double qty,
                                   double current_price,
                                   double trail_amount = 0,
                                   double trail_percent = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        MonitoredStop stop;
        stop.id = generate_id();
        stop.symbol = symbol;
        stop.side = OrderSide::Sell;
        stop.qty = qty;
        stop.is_trailing = true;
        stop.trail_amount = trail_amount;
        stop.trail_percent = trail_percent;
        stop.highest_price = current_price;
        stop.lowest_price = current_price;
        stop.stop_price = stop.effective_stop_price();
        stop.created_at = std::chrono::system_clock::now();
        
        stops_[stop.id] = stop;
        symbol_index_[symbol].insert(stop.id);
        
        return stop.id;
    }
    
    /**
     * @brief Process price update
     */
    std::vector<StopTriggerEvent> check_price(const std::string& symbol, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StopTriggerEvent> triggers;
        
        auto it = symbol_index_.find(symbol);
        if (it == symbol_index_.end()) return triggers;
        
        std::vector<std::string> to_remove;
        
        for (const auto& id : it->second) {
            auto stop_it = stops_.find(id);
            if (stop_it == stops_.end()) continue;
            
            auto& stop = stop_it->second;
            if (!stop.is_active()) continue;
            
            // Check expiration
            if (stop.is_expired()) {
                stop.state = StopState::Expired;
                to_remove.push_back(id);
                continue;
            }
            
            // Update trailing stop tracking
            if (stop.is_trailing) {
                if (price > stop.highest_price) {
                    stop.highest_price = price;
                    stop.stop_price = stop.effective_stop_price();
                }
                if (price < stop.lowest_price) {
                    stop.lowest_price = price;
                    stop.stop_price = stop.effective_stop_price();
                }
            }
            
            // Check trigger condition
            bool triggered = false;
            double effective_stop = stop.effective_stop_price();
            
            if (stop.side == OrderSide::Sell) {
                triggered = (price <= effective_stop);
            } else {
                triggered = (price >= effective_stop);
            }
            
            if (triggered) {
                stop.state = StopState::Triggered;
                stop.trigger_price = price;
                stop.triggered_at = std::chrono::system_clock::now();
                
                // Submit order
                OrderRequest request;
                request.symbol = symbol;
                request.qty = stop.qty;
                request.side = stop.side;
                request.type = (stop.limit_price > 0) ? OrderType::Limit : OrderType::Market;
                if (stop.limit_price > 0) {
                    request.limit_price = stop.limit_price;
                }
                request.time_in_force = TimeInForce::Day;
                
                if (submit_callback_) {
                    auto order = submit_callback_(request);
                    if (order) {
                        stop.triggered_order_id = order->id;
                        stop.state = StopState::Filled;  // Assume market order fills
                    }
                }
                
                // Create trigger event
                StopTriggerEvent event;
                event.stop_id = id;
                event.symbol = symbol;
                event.stop_price = effective_stop;
                event.trigger_price = price;
                event.order_id = stop.triggered_order_id;
                event.timestamp = stop.triggered_at;
                
                triggers.push_back(event);
                
                // Fire callback
                if (on_trigger_) {
                    on_trigger_(event);
                }
            }
        }
        
        // Clean up triggered/expired stops
        for (const auto& id : to_remove) {
            symbol_index_[symbol].erase(id);
        }
        
        return triggers;
    }
    
    /**
     * @brief Cancel stop
     */
    bool cancel_stop(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stops_.find(id);
        if (it == stops_.end()) return false;
        
        if (it->second.is_active()) {
            it->second.state = StopState::Cancelled;
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Modify stop price
     */
    bool modify_stop_price(const std::string& id, double new_stop_price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = stops_.find(id);
        if (it == stops_.end() || !it->second.is_active()) return false;
        
        it->second.stop_price = new_stop_price;
        return true;
    }
    
    /**
     * @brief Get stop by ID
     */
    std::optional<MonitoredStop> get_stop(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = stops_.find(id);
        if (it == stops_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get all stops for symbol
     */
    std::vector<MonitoredStop> get_stops_for_symbol(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MonitoredStop> result;
        
        auto it = symbol_index_.find(symbol);
        if (it != symbol_index_.end()) {
            for (const auto& id : it->second) {
                auto stop_it = stops_.find(id);
                if (stop_it != stops_.end()) {
                    result.push_back(stop_it->second);
                }
            }
        }
        
        return result;
    }
    
    /**
     * @brief Get all active stops
     */
    std::vector<MonitoredStop> get_active_stops() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MonitoredStop> result;
        
        for (const auto& [id, stop] : stops_) {
            if (stop.is_active()) {
                result.push_back(stop);
            }
        }
        
        return result;
    }
    
    /**
     * @brief Set trigger callback
     */
    void on_trigger(EnhancedOnStopTriggerCallback callback) {
        on_trigger_ = callback;
    }
    
    /**
     * @brief Get stop count
     */
    size_t stop_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stops_.size();
    }
    
    size_t active_stop_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& [id, stop] : stops_) {
            if (stop.is_active()) count++;
        }
        return count;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, MonitoredStop> stops_;
    std::map<std::string, std::set<std::string>> symbol_index_;
    
    SubmitOrderCallback submit_callback_;
    EnhancedOnStopTriggerCallback on_trigger_;
    
    int id_counter_{0};
    
    std::string generate_id() {
        return "STOP-" + std::to_string(++id_counter_) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
};

// ============================================================================
// Partial Fill Tracking
// ============================================================================

/**
 * @brief Fill details
 */
struct EnhancedFill {
    std::string id;
    std::string order_id;
    double qty{0};
    double price{0};
    double commission{0};
    std::string exchange;
    std::string liquidity;           // "A" = added, "R" = removed
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Order with fill tracking
 */
struct TrackedOrder {
    std::string id;
    std::string symbol;
    OrderSide side;
    OrderType type;
    double total_qty{0};
    double filled_qty{0};
    double remaining_qty{0};
    double avg_fill_price{0};
    double total_commission{0};
    OrderStatus status{OrderStatus::New};
    
    std::vector<EnhancedFill> fills;
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    
    bool is_complete() const {
        return status == OrderStatus::Filled ||
               status == OrderStatus::Canceled ||
               status == OrderStatus::Rejected ||
               status == OrderStatus::Expired;
    }
    
    double fill_rate() const {
        return total_qty > 0 ? filled_qty / total_qty : 0;
    }
    
    void add_fill(const EnhancedFill& fill) {
        fills.push_back(fill);
        
        // Update aggregates
        double prev_value = avg_fill_price * filled_qty;
        filled_qty += fill.qty;
        remaining_qty = total_qty - filled_qty;
        avg_fill_price = filled_qty > 0 ? 
            (prev_value + fill.qty * fill.price) / filled_qty : 0;
        total_commission += fill.commission;
        
        // Update status
        if (filled_qty >= total_qty) {
            status = OrderStatus::Filled;
        } else if (filled_qty > 0) {
            status = OrderStatus::PartiallyFilled;
        }
        
        updated_at = std::chrono::system_clock::now();
    }
};

/**
 * @brief Fill event
 */
struct FillEvent {
    std::string order_id;
    EnhancedFill fill;
    TrackedOrder order_state;
    bool is_complete;
};

using EnhancedOnFillCallback = std::function<void(const FillEvent&)>;

/**
 * @brief Partial fill tracker
 */
class PartialFillTracker {
public:
    /**
     * @brief Track new order
     */
    void track_order(const BrokerOrder& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        TrackedOrder tracked;
        tracked.id = order.id;
        tracked.symbol = order.symbol;
        tracked.side = order.side;
        tracked.type = order.type;
        tracked.total_qty = order.qty;
        tracked.filled_qty = order.filled_qty;
        tracked.remaining_qty = order.qty - order.filled_qty;
        tracked.avg_fill_price = order.filled_avg_price;
        tracked.status = order.status;
        tracked.created_at = std::chrono::system_clock::now();
        tracked.updated_at = tracked.created_at;
        
        orders_[order.id] = tracked;
    }
    
    /**
     * @brief Record fill
     */
    void record_fill(const std::string& order_id, const EnhancedFill& fill) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;
        
        it->second.add_fill(fill);
        
        // Fire callback
        if (on_fill_) {
            FillEvent event;
            event.order_id = order_id;
            event.fill = fill;
            event.order_state = it->second;
            event.is_complete = it->second.is_complete();
            on_fill_(event);
        }
    }
    
    /**
     * @brief Update from broker order
     */
    void update_from_broker(const BrokerOrder& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = orders_.find(order.id);
        if (it == orders_.end()) {
            // New order
            track_order(order);
            return;
        }
        
        auto& tracked = it->second;
        
        // Check for new fill
        if (order.filled_qty > tracked.filled_qty) {
            EnhancedFill fill;
            fill.id = generate_fill_id();
            fill.order_id = order.id;
            fill.qty = order.filled_qty - tracked.filled_qty;
            fill.price = order.filled_avg_price;  // Approximate
            fill.timestamp = std::chrono::system_clock::now();
            
            tracked.add_fill(fill);
            
            // Fire callback
            if (on_fill_) {
                FillEvent event;
                event.order_id = order.id;
                event.fill = fill;
                event.order_state = tracked;
                event.is_complete = tracked.is_complete();
                on_fill_(event);
            }
        }
        
        // Update status
        tracked.status = order.status;
        tracked.updated_at = std::chrono::system_clock::now();
    }
    
    /**
     * @brief Get tracked order
     */
    std::optional<TrackedOrder> get_order(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(id);
        if (it == orders_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get fills for order
     */
    std::vector<EnhancedFill> get_fills(const std::string& order_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return {};
        return it->second.fills;
    }
    
    /**
     * @brief Get all partially filled orders
     */
    std::vector<TrackedOrder> get_partial_fills() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TrackedOrder> result;
        
        for (const auto& [id, order] : orders_) {
            if (order.status == OrderStatus::PartiallyFilled) {
                result.push_back(order);
            }
        }
        
        return result;
    }
    
    /**
     * @brief Set fill callback
     */
    void on_fill(EnhancedOnFillCallback callback) {
        on_fill_ = callback;
    }
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        int total_orders{0};
        int complete_orders{0};
        int partial_orders{0};
        int pending_orders{0};
        double total_filled_value{0};
        double total_commission{0};
    };
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats;
        stats.total_orders = static_cast<int>(orders_.size());
        
        for (const auto& [id, order] : orders_) {
            if (order.is_complete()) {
                stats.complete_orders++;
            } else if (order.status == OrderStatus::PartiallyFilled) {
                stats.partial_orders++;
            } else {
                stats.pending_orders++;
            }
            
            stats.total_filled_value += order.filled_qty * order.avg_fill_price;
            stats.total_commission += order.total_commission;
        }
        
        return stats;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, TrackedOrder> orders_;
    EnhancedOnFillCallback on_fill_;
    
    int fill_counter_{0};
    
    std::string generate_fill_id() {
        return "FILL-" + std::to_string(++fill_counter_);
    }
};

// ============================================================================
// Order Modification
// ============================================================================

/**
 * @brief Order modification request
 */
struct OrderModification {
    std::string order_id;
    std::optional<double> new_qty;
    std::optional<double> new_limit_price;
    std::optional<double> new_stop_price;
    std::optional<TimeInForce> new_tif;
};

/**
 * @brief Order modification result
 */
struct ModificationResult {
    bool success{false};
    std::string original_order_id;
    std::string new_order_id;      // Some brokers create new order
    std::string error;
    BrokerOrder updated_order;
};

/**
 * @brief Order modifier (works with broker)
 */
class OrderModifier {
public:
    using ReplaceOrderCallback = std::function<BrokerResult<BrokerOrder>(
        const std::string&, const OrderRequest&)>;
    using CancelOrderCallback = std::function<BrokerResult<bool>(const std::string&)>;
    using GetOrderCallback = std::function<BrokerResult<BrokerOrder>(const std::string&)>;
    
    void set_replace_callback(ReplaceOrderCallback cb) { replace_cb_ = cb; }
    void set_cancel_callback(CancelOrderCallback cb) { cancel_cb_ = cb; }
    void set_get_order_callback(GetOrderCallback cb) { get_order_cb_ = cb; }
    
    /**
     * @brief Modify order
     */
    ModificationResult modify(const OrderModification& mod) {
        ModificationResult result;
        result.original_order_id = mod.order_id;
        
        if (!get_order_cb_) {
            result.error = "Get order callback not set";
            return result;
        }
        
        // Get current order
        auto order_result = get_order_cb_(mod.order_id);
        if (!order_result.success) {
            result.error = "Failed to get order: " + order_result.error;
            return result;
        }
        
        auto& order = order_result.data;
        
        // Check if modifiable
        if (order.status != OrderStatus::New &&
            order.status != OrderStatus::Accepted &&
            order.status != OrderStatus::PendingNew) {
            result.error = "Order not modifiable in current state: " +
                          order_status_to_string(order.status);
            return result;
        }
        
        // Build replacement request
        OrderRequest new_request;
        new_request.symbol = order.symbol;
        new_request.side = order.side;
        new_request.type = order.type;
        new_request.qty = mod.new_qty.value_or(order.qty);
        
        if (mod.new_limit_price) {
            new_request.limit_price = *mod.new_limit_price;
        } else if (order.limit_price) {
            new_request.limit_price = *order.limit_price;
        }
        
        if (mod.new_stop_price) {
            new_request.stop_price = *mod.new_stop_price;
        } else if (order.stop_price) {
            new_request.stop_price = *order.stop_price;
        }
        
        new_request.time_in_force = mod.new_tif.value_or(order.time_in_force);
        
        // Submit replacement
        if (replace_cb_) {
            auto replace_result = replace_cb_(mod.order_id, new_request);
            if (replace_result.success) {
                result.success = true;
                result.new_order_id = replace_result.data.id;
                result.updated_order = replace_result.data;
            } else {
                result.error = replace_result.error;
            }
        } else {
            result.error = "Replace callback not set";
        }
        
        return result;
    }
    
    /**
     * @brief Cancel order
     */
    bool cancel(const std::string& order_id, std::string* error = nullptr) {
        if (!cancel_cb_) {
            if (error) *error = "Cancel callback not set";
            return false;
        }
        
        auto result = cancel_cb_(order_id);
        if (!result.success && error) {
            *error = result.error;
        }
        return result.success;
    }

private:
    ReplaceOrderCallback replace_cb_;
    CancelOrderCallback cancel_cb_;
    GetOrderCallback get_order_cb_;
};

} // namespace genie::trading

#endif // GENIE_TRADING_ENHANCED_ORDERS_HPP
