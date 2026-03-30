/**
 * @file broker_interface.hpp
 * @brief Common broker interface for trading integrations
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides abstraction layer for broker APIs:
 * - Account information and balances
 * - Position management
 * - Order submission and tracking
 * - Transaction history
 * 
 * Implementations:
 * - AlpacaClient (paper and live trading)
 * - IBKRClient (Interactive Brokers)
 * - TDAClient (TD Ameritrade/Schwab)
 */
#pragma once
#ifndef GENIE_TRADING_BROKER_INTERFACE_HPP
#define GENIE_TRADING_BROKER_INTERFACE_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <functional>
#include <memory>

namespace genie::trading {

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @brief Order side (buy/sell)
 */
enum class OrderSide {
    Buy,
    Sell
};

inline std::string order_side_to_string(OrderSide side) {
    return side == OrderSide::Buy ? "buy" : "sell";
}

inline OrderSide string_to_order_side(const std::string& s) {
    return (s == "buy" || s == "BUY") ? OrderSide::Buy : OrderSide::Sell;
}

/**
 * @brief Order type
 */
enum class OrderType {
    Market,
    Limit,
    Stop,
    StopLimit,
    TrailingStop
};

inline std::string order_type_to_string(OrderType type) {
    switch (type) {
        case OrderType::Market: return "market";
        case OrderType::Limit: return "limit";
        case OrderType::Stop: return "stop";
        case OrderType::StopLimit: return "stop_limit";
        case OrderType::TrailingStop: return "trailing_stop";
    }
    return "market";
}

inline OrderType string_to_order_type(const std::string& s) {
    if (s == "limit" || s == "LIMIT") return OrderType::Limit;
    if (s == "stop" || s == "STOP") return OrderType::Stop;
    if (s == "stop_limit" || s == "STOP_LIMIT") return OrderType::StopLimit;
    if (s == "trailing_stop" || s == "TRAILING_STOP") return OrderType::TrailingStop;
    return OrderType::Market;
}

/**
 * @brief Time in force for orders
 */
enum class TimeInForce {
    Day,        // Day order
    GTC,        // Good til canceled
    IOC,        // Immediate or cancel
    FOK,        // Fill or kill
    OPG,        // Market on open
    CLS,        // Market on close
    GTD         // Good til date
};

inline std::string time_in_force_to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::Day: return "day";
        case TimeInForce::GTC: return "gtc";
        case TimeInForce::IOC: return "ioc";
        case TimeInForce::FOK: return "fok";
        case TimeInForce::OPG: return "opg";
        case TimeInForce::CLS: return "cls";
        case TimeInForce::GTD: return "gtd";
    }
    return "day";
}

inline TimeInForce string_to_time_in_force(const std::string& s) {
    if (s == "gtc" || s == "GTC") return TimeInForce::GTC;
    if (s == "ioc" || s == "IOC") return TimeInForce::IOC;
    if (s == "fok" || s == "FOK") return TimeInForce::FOK;
    if (s == "opg" || s == "OPG") return TimeInForce::OPG;
    if (s == "cls" || s == "CLS") return TimeInForce::CLS;
    if (s == "gtd" || s == "GTD") return TimeInForce::GTD;
    return TimeInForce::Day;
}

/**
 * @brief Order status
 */
enum class OrderStatus {
    New,
    Accepted,
    PendingNew,
    PartiallyFilled,
    Filled,
    DoneForDay,
    Canceled,
    Expired,
    Replaced,
    PendingCancel,
    PendingReplace,
    Rejected,
    Suspended,
    Stopped,
    Calculated,
    Unknown
};

inline std::string order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::New: return "new";
        case OrderStatus::Accepted: return "accepted";
        case OrderStatus::PendingNew: return "pending_new";
        case OrderStatus::PartiallyFilled: return "partially_filled";
        case OrderStatus::Filled: return "filled";
        case OrderStatus::DoneForDay: return "done_for_day";
        case OrderStatus::Canceled: return "canceled";
        case OrderStatus::Expired: return "expired";
        case OrderStatus::Replaced: return "replaced";
        case OrderStatus::PendingCancel: return "pending_cancel";
        case OrderStatus::PendingReplace: return "pending_replace";
        case OrderStatus::Rejected: return "rejected";
        case OrderStatus::Suspended: return "suspended";
        case OrderStatus::Stopped: return "stopped";
        case OrderStatus::Calculated: return "calculated";
        case OrderStatus::Unknown: return "unknown";
    }
    return "unknown";
}

inline OrderStatus string_to_order_status(const std::string& s) {
    if (s == "new" || s == "NEW") return OrderStatus::New;
    if (s == "accepted" || s == "ACCEPTED") return OrderStatus::Accepted;
    if (s == "pending_new" || s == "PENDING_NEW") return OrderStatus::PendingNew;
    if (s == "partially_filled" || s == "PARTIALLY_FILLED") return OrderStatus::PartiallyFilled;
    if (s == "filled" || s == "FILLED") return OrderStatus::Filled;
    if (s == "done_for_day" || s == "DONE_FOR_DAY") return OrderStatus::DoneForDay;
    if (s == "canceled" || s == "cancelled" || s == "CANCELED") return OrderStatus::Canceled;
    if (s == "expired" || s == "EXPIRED") return OrderStatus::Expired;
    if (s == "replaced" || s == "REPLACED") return OrderStatus::Replaced;
    if (s == "pending_cancel" || s == "PENDING_CANCEL") return OrderStatus::PendingCancel;
    if (s == "pending_replace" || s == "PENDING_REPLACE") return OrderStatus::PendingReplace;
    if (s == "rejected" || s == "REJECTED") return OrderStatus::Rejected;
    if (s == "suspended" || s == "SUSPENDED") return OrderStatus::Suspended;
    if (s == "stopped" || s == "STOPPED") return OrderStatus::Stopped;
    if (s == "calculated" || s == "CALCULATED") return OrderStatus::Calculated;
    return OrderStatus::Unknown;
}

/**
 * @brief Asset class
 */
enum class AssetClass {
    USEquity,
    Crypto,
    Option,
    Future,
    Forex,
    Unknown
};

inline std::string asset_class_to_string(AssetClass cls) {
    switch (cls) {
        case AssetClass::USEquity: return "us_equity";
        case AssetClass::Crypto: return "crypto";
        case AssetClass::Option: return "option";
        case AssetClass::Future: return "future";
        case AssetClass::Forex: return "forex";
        case AssetClass::Unknown: return "unknown";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Account information
 */
struct BrokerAccount {
    std::string account_id;
    std::string account_number;
    std::string status;
    std::string currency{"USD"};
    bool is_paper{false};
    
    // Cash balances
    double cash{0};
    double cash_available{0};
    double cash_withdrawable{0};
    
    // Buying power
    double buying_power{0};
    double daytrading_buying_power{0};
    double regt_buying_power{0};
    
    // Portfolio values
    double portfolio_value{0};
    double equity{0};
    double long_market_value{0};
    double short_market_value{0};
    
    // Margin
    double initial_margin{0};
    double maintenance_margin{0};
    double sma{0};  // Special Memorandum Account
    
    // Day trading
    int daytrade_count{0};
    bool pattern_day_trader{false};
    bool trading_blocked{false};
    bool transfers_blocked{false};
    bool account_blocked{false};
    
    // Timestamps
    std::string created_at;
    std::string updated_at;
    
    bool is_valid() const {
        return !account_id.empty() && cash >= 0 && portfolio_value >= 0;
    }
};

/**
 * @brief Position from broker
 */
struct BrokerPosition {
    std::string asset_id;
    std::string symbol;
    std::string exchange;
    AssetClass asset_class{AssetClass::USEquity};
    
    // Quantity
    double qty{0};
    double qty_available{0};  // Available to trade (not in open orders)
    OrderSide side{OrderSide::Buy};  // Long or short
    
    // Prices
    double avg_entry_price{0};
    double current_price{0};
    double lastday_price{0};
    
    // Market values
    double market_value{0};
    double cost_basis{0};
    
    // P&L
    double unrealized_pl{0};
    double unrealized_plpc{0};  // Percentage
    double unrealized_intraday_pl{0};
    double unrealized_intraday_plpc{0};
    
    // Change
    double change_today{0};
    
    bool is_valid() const {
        return !symbol.empty() && qty != 0;
    }
    
    bool is_long() const { return qty > 0; }
    bool is_short() const { return qty < 0; }
};

/**
 * @brief Order request
 */
struct OrderRequest {
    std::string symbol;
    double qty{0};
    OrderSide side{OrderSide::Buy};
    OrderType type{OrderType::Market};
    TimeInForce time_in_force{TimeInForce::Day};
    
    // Price fields (for limit/stop orders)
    std::optional<double> limit_price;
    std::optional<double> stop_price;
    std::optional<double> trail_price;
    std::optional<double> trail_percent;
    
    // Extended hours trading
    bool extended_hours{false};
    
    // Client order ID for tracking
    std::string client_order_id;
    
    // Order class for bracket/OCO orders
    std::string order_class;  // simple, bracket, oco, oto
    
    // Take profit leg
    std::optional<double> take_profit_limit_price;
    
    // Stop loss leg
    std::optional<double> stop_loss_stop_price;
    std::optional<double> stop_loss_limit_price;
    
    // Notional (alternative to qty - dollar amount)
    std::optional<double> notional;
    
    bool is_valid() const {
        if (symbol.empty()) return false;
        if (qty <= 0 && !notional) return false;
        if (type == OrderType::Limit && !limit_price) return false;
        if (type == OrderType::Stop && !stop_price) return false;
        if (type == OrderType::StopLimit && (!limit_price || !stop_price)) return false;
        return true;
    }
};

/**
 * @brief Order from broker
 */
struct BrokerOrder {
    std::string id;
    std::string client_order_id;
    std::string created_at;
    std::string updated_at;
    std::string submitted_at;
    std::string filled_at;
    std::string expired_at;
    std::string canceled_at;
    std::string failed_at;
    std::string replaced_at;
    std::string replaced_by;
    std::string replaces;
    
    std::string asset_id;
    std::string symbol;
    AssetClass asset_class{AssetClass::USEquity};
    
    double qty{0};
    double filled_qty{0};
    double notional{0};
    double filled_avg_price{0};
    
    OrderSide side{OrderSide::Buy};
    OrderType type{OrderType::Market};
    TimeInForce time_in_force{TimeInForce::Day};
    
    std::optional<double> limit_price;
    std::optional<double> stop_price;
    std::optional<double> trail_price;
    std::optional<double> trail_percent;
    std::optional<double> hwm;  // High water mark for trailing stop
    
    OrderStatus status{OrderStatus::Unknown};
    bool extended_hours{false};
    
    // Legs for bracket/OCO orders
    std::vector<BrokerOrder> legs;
    
    bool is_valid() const { return !id.empty() && !symbol.empty(); }
    bool is_open() const {
        return status == OrderStatus::New || 
               status == OrderStatus::Accepted ||
               status == OrderStatus::PendingNew ||
               status == OrderStatus::PartiallyFilled;
    }
    bool is_filled() const { return status == OrderStatus::Filled; }
    bool is_canceled() const { return status == OrderStatus::Canceled; }
    bool is_rejected() const { return status == OrderStatus::Rejected; }
};

/**
 * @brief Trade execution / fill
 */
struct BrokerFill {
    std::string id;
    std::string order_id;
    std::string symbol;
    std::string activity_type;  // FILL, PARTIAL_FILL
    
    double qty{0};
    double price{0};
    double commission{0};
    
    OrderSide side{OrderSide::Buy};
    std::string timestamp;
    
    bool is_valid() const { return !id.empty() && qty > 0 && price > 0; }
};

/**
 * @brief Account activity / transaction
 */
struct BrokerActivity {
    std::string id;
    std::string activity_type;  // FILL, DIVIDEND, ACATC, etc.
    std::string date;
    
    std::string symbol;
    double qty{0};
    double price{0};
    double net_amount{0};
    
    std::string description;
    std::string per_share_amount;
    
    OrderSide side{OrderSide::Buy};
    std::string status;
};

/**
 * @brief Asset information
 */
struct BrokerAsset {
    std::string id;
    std::string symbol;
    std::string name;
    std::string exchange;
    AssetClass asset_class{AssetClass::USEquity};
    
    bool tradable{false};
    bool marginable{false};
    bool shortable{false};
    bool easy_to_borrow{false};
    bool fractionable{false};
    
    double min_order_size{0};
    double min_trade_increment{0};
    double price_increment{0};
    
    std::string status;
};

/**
 * @brief Market clock
 */
struct MarketClock {
    std::string timestamp;
    bool is_open{false};
    std::string next_open;
    std::string next_close;
};

/**
 * @brief Broker API result wrapper
 */
template<typename T>
struct BrokerResult {
    bool success{false};
    T data;
    std::string error;
    int http_status{0};
    
    explicit operator bool() const { return success; }
};

/**
 * @brief Specialization for void result (no data payload)
 */
template<>
struct BrokerResult<void> {
    bool success{false};
    std::string error;
    int http_status{0};
    
    explicit operator bool() const { return success; }
};

// ============================================================================
// Callback Types
// ============================================================================

using OnOrderUpdateCallback = std::function<void(const BrokerOrder&)>;
using OnTradeUpdateCallback = std::function<void(const BrokerFill&)>;
using OnPositionUpdateCallback = std::function<void(const BrokerPosition&)>;
using OnAccountUpdateCallback = std::function<void(const BrokerAccount&)>;

// ============================================================================
// Broker Interface
// ============================================================================

/**
 * @brief Abstract broker interface
 */
class IBroker {
public:
    virtual ~IBroker() = default;
    
    // === Connection ===
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // === Account ===
    virtual BrokerResult<BrokerAccount> get_account() = 0;
    
    // === Positions ===
    virtual BrokerResult<std::vector<BrokerPosition>> get_positions() = 0;
    virtual BrokerResult<BrokerPosition> get_position(const std::string& symbol) = 0;
    virtual BrokerResult<bool> close_position(const std::string& symbol) = 0;
    virtual BrokerResult<bool> close_all_positions() = 0;
    
    // === Orders ===
    virtual BrokerResult<BrokerOrder> submit_order(const OrderRequest& request) = 0;
    virtual BrokerResult<BrokerOrder> get_order(const std::string& order_id) = 0;
    virtual BrokerResult<std::vector<BrokerOrder>> get_orders(
        const std::string& status = "open",
        int limit = 50,
        const std::string& after = "",
        const std::string& until = "") = 0;
    virtual BrokerResult<bool> cancel_order(const std::string& order_id) = 0;
    virtual BrokerResult<bool> cancel_all_orders() = 0;
    virtual BrokerResult<BrokerOrder> replace_order(
        const std::string& order_id,
        const OrderRequest& new_request) = 0;
    
    // === Assets ===
    virtual BrokerResult<BrokerAsset> get_asset(const std::string& symbol) = 0;
    virtual BrokerResult<std::vector<BrokerAsset>> get_assets(
        const std::string& status = "active",
        const std::string& asset_class = "") = 0;
    
    // === Market Info ===
    virtual BrokerResult<MarketClock> get_clock() = 0;
    
    // === Activities ===
    virtual BrokerResult<std::vector<BrokerActivity>> get_activities(
        const std::string& activity_type = "",
        const std::string& date = "",
        int limit = 100) = 0;
    
    // === Streaming (optional) ===
    virtual void on_order_update(OnOrderUpdateCallback callback) {
        on_order_update_ = callback;
    }
    virtual void on_trade_update(OnTradeUpdateCallback callback) {
        on_trade_update_ = callback;
    }
    virtual void on_position_update(OnPositionUpdateCallback callback) {
        on_position_update_ = callback;
    }
    virtual void on_account_update(OnAccountUpdateCallback callback) {
        on_account_update_ = callback;
    }
    
    // === Utility ===
    virtual std::string broker_name() const = 0;
    virtual bool is_paper_trading() const = 0;
    virtual const std::string& last_error() const = 0;

protected:
    OnOrderUpdateCallback on_order_update_;
    OnTradeUpdateCallback on_trade_update_;
    OnPositionUpdateCallback on_position_update_;
    OnAccountUpdateCallback on_account_update_;
};

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * @brief Create a market order request
 */
inline OrderRequest market_order(const std::string& symbol, double qty, OrderSide side) {
    OrderRequest req;
    req.symbol = symbol;
    req.qty = qty;
    req.side = side;
    req.type = OrderType::Market;
    req.time_in_force = TimeInForce::Day;
    return req;
}

/**
 * @brief Create a limit order request
 */
inline OrderRequest limit_order(const std::string& symbol, double qty, 
                                OrderSide side, double limit_price) {
    OrderRequest req;
    req.symbol = symbol;
    req.qty = qty;
    req.side = side;
    req.type = OrderType::Limit;
    req.limit_price = limit_price;
    req.time_in_force = TimeInForce::Day;
    return req;
}

/**
 * @brief Create a stop order request
 */
inline OrderRequest stop_order(const std::string& symbol, double qty,
                               OrderSide side, double stop_price) {
    OrderRequest req;
    req.symbol = symbol;
    req.qty = qty;
    req.side = side;
    req.type = OrderType::Stop;
    req.stop_price = stop_price;
    req.time_in_force = TimeInForce::Day;
    return req;
}

/**
 * @brief Create a stop-limit order request
 */
inline OrderRequest stop_limit_order(const std::string& symbol, double qty,
                                     OrderSide side, double stop_price, double limit_price) {
    OrderRequest req;
    req.symbol = symbol;
    req.qty = qty;
    req.side = side;
    req.type = OrderType::StopLimit;
    req.stop_price = stop_price;
    req.limit_price = limit_price;
    req.time_in_force = TimeInForce::Day;
    return req;
}

} // namespace genie::trading

#endif // GENIE_TRADING_BROKER_INTERFACE_HPP
