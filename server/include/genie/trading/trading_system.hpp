/**
 * @file trading_system.hpp
 * @brief Unified trading system orchestrating all prototype components
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides a complete trading system that integrates:
 * - Broker connectivity (Alpaca paper/live trading)
 * - Market data (Alpha Vantage, Yahoo Finance)
 * - Real-time price streaming
 * - Position management and synchronization
 * - Live portfolio valuation and P&L
 * - Order management with status tracking
 * - Risk monitoring and alerts
 * 
 * This is the main entry point for prototype functionality.
 */
#pragma once
#ifndef GENIE_TRADING_TRADING_SYSTEM_HPP
#define GENIE_TRADING_TRADING_SYSTEM_HPP

#include "broker_interface.hpp"
#include "alpaca_client.hpp"
#include "../market/data_manager.hpp"
#include "../market/live_feed.hpp"
#include "../market/price_cache.hpp"
#include "../portfolio/position_sync.hpp"
#include "../analytics/live_valuation.hpp"
#include "../core/logging.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>

namespace genie::trading {

// Forward declarations for analytics types used in this file
namespace analytics_adapter {

/**
 * @brief Alert configuration
 */
struct AlertConfig {
    double price_change_pct_threshold{5.0};   // Alert on 5% price change
    double pnl_threshold{1000.0};             // Alert on $1000 P&L change
    bool enable_price_alerts{true};
    bool enable_pnl_alerts{true};
};

/**
 * @brief Valuation alert event
 */
struct ValuationAlert {
    std::string symbol;
    double threshold{0};
    double actual{0};
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Portfolio valuation snapshot for trading system
 */
struct PortfolioValuation {
    double nav{0};                      // Net Asset Value (total value)
    double total_market_value{0};       // Total positions market value
    double cash{0};                     // Cash balance
    double total_unrealized_pl{0};      // Total unrealized P&L
    double total_unrealized_pl_pct{0};  // Total unrealized P&L %
    double day_pl{0};                   // Day P&L
    double day_pl_pct{0};               // Day P&L %
    int position_count{0};
    std::string best_performer;
    double best_performer_pct{0};
    std::string worst_performer;
    double worst_performer_pct{0};
    
    struct PositionVal {
        double quantity{0};
        double cost_basis{0};
        double current_price{0};
        double market_value{0};
        double unrealized_pl{0};
        double unrealized_pl_pct{0};
        double day_pl{0};
        double weight{0};
    };
    std::map<std::string, PositionVal> positions;
    
    std::chrono::system_clock::time_point timestamp;
};

using OnAlertCallback = std::function<void(const ValuationAlert&)>;

/**
 * @brief Live valuation wrapper for trading system
 */
class LiveValuation {
public:
    explicit LiveValuation(const AlertConfig& config = {})
        : config_(config)
        , price_cache_(60, 300)  // 60s staleness, 300s max age
        , engine_(price_cache_) {}
    
    /**
     * @brief Load positions from broker
     */
    void load_from_broker(const std::vector<BrokerPosition>& positions) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        positions_.clear();
        best_performer_.clear();
        worst_performer_.clear();
        double best_pct = -1e10;
        double worst_pct = 1e10;
        
        for (const auto& pos : positions) {
            PositionData pd;
            pd.symbol = pos.symbol;
            pd.quantity = static_cast<int64_t>(pos.qty);
            pd.cost_basis = pos.avg_entry_price * pos.qty;
            pd.current_price = pos.current_price;
            pd.previous_close = pos.current_price - (pos.unrealized_pl / pos.qty);
            
            positions_[pos.symbol] = pd;
            
            // Update price cache
            market::CachedPrice cp;
            cp.symbol = pos.symbol;
            cp.price = pos.current_price;
            cp.timestamp = std::chrono::system_clock::now();
            price_cache_.set(cp);
            
            // Track best/worst performers
            if (pos.unrealized_plpc > best_pct) {
                best_pct = pos.unrealized_plpc;
                best_performer_ = pos.symbol;
                best_performer_pct_ = pos.unrealized_plpc;
            }
            if (pos.unrealized_plpc < worst_pct) {
                worst_pct = pos.unrealized_plpc;
                worst_performer_ = pos.symbol;
                worst_performer_pct_ = pos.unrealized_plpc;
            }
        }
    }
    
    /**
     * @brief Set cash balance
     */
    void set_cash(double cash) {
        std::lock_guard<std::mutex> lock(mutex_);
        cash_ = cash;
    }
    
    /**
     * @brief Set previous NAV for day P&L calculation
     */
    void set_previous_nav(double nav) {
        std::lock_guard<std::mutex> lock(mutex_);
        previous_nav_ = nav;
    }
    
    /**
     * @brief Get current valuation
     */
    PortfolioValuation get_valuation() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PortfolioValuation val;
        val.timestamp = std::chrono::system_clock::now();
        val.cash = cash_;
        
        double total_market_value = 0;
        double total_cost = 0;
        double day_pl = 0;
        
        for (const auto& [symbol, pd] : positions_) {
            auto cached = price_cache_.get(symbol);
            double price = cached ? cached->price : pd.current_price;
            
            double market_value = pd.quantity * price;
            double unrealized_pl = market_value - pd.cost_basis;
            double unrealized_pl_pct = pd.cost_basis != 0 ? 
                (unrealized_pl / pd.cost_basis) : 0;
            double pos_day_pl = pd.quantity * (price - pd.previous_close);
            
            PortfolioValuation::PositionVal pv;
            pv.quantity = static_cast<double>(pd.quantity);
            pv.cost_basis = pd.cost_basis;
            pv.current_price = price;
            pv.market_value = market_value;
            pv.unrealized_pl = unrealized_pl;
            pv.unrealized_pl_pct = unrealized_pl_pct;
            pv.day_pl = pos_day_pl;
            
            val.positions[symbol] = pv;
            
            total_market_value += market_value;
            total_cost += pd.cost_basis;
            day_pl += pos_day_pl;
        }
        
        // Calculate weights
        double total_value = total_market_value + cash_;
        for (auto& [symbol, pv] : val.positions) {
            pv.weight = total_value != 0 ? (pv.market_value / total_value) : 0;
        }
        
        val.nav = total_value;
        val.total_market_value = total_market_value;
        val.total_unrealized_pl = total_market_value - total_cost;
        val.total_unrealized_pl_pct = total_cost != 0 ?
            (val.total_unrealized_pl / total_cost) : 0;
        val.day_pl = day_pl;
        val.day_pl_pct = previous_nav_ != 0 ? (day_pl / previous_nav_) : 0;
        val.position_count = static_cast<int>(positions_.size());
        val.best_performer = best_performer_;
        val.best_performer_pct = best_performer_pct_;
        val.worst_performer = worst_performer_;
        val.worst_performer_pct = worst_performer_pct_;
        
        return val;
    }
    
    /**
     * @brief Get total unrealized P&L
     */
    double get_unrealized_pl() {
        auto val = get_valuation();
        return val.total_unrealized_pl;
    }
    
    /**
     * @brief Get day P&L
     */
    double get_day_pl() {
        auto val = get_valuation();
        return val.day_pl;
    }
    
    /**
     * @brief Add price alert
     */
    void add_price_target(const std::string& symbol, double target, bool above = true) {
        analytics::AlertCondition cond = above ? 
            analytics::AlertCondition::PriceAbove : 
            analytics::AlertCondition::PriceBelow;
        
        engine_.add_alert(symbol, cond, target);
    }
    
    /**
     * @brief Update price from live feed
     */
    void update_price(const std::string& symbol, double price, 
                      double bid = 0, double ask = 0) {
        market::CachedPrice cp;
        cp.symbol = symbol;
        cp.price = price;
        cp.bid = bid;
        cp.ask = ask;
        cp.timestamp = std::chrono::system_clock::now();
        price_cache_.set(cp);
        
        // Check alerts
        engine_.on_price_update(symbol, price);
    }
    
    /**
     * @brief Set alert callback
     */
    void on_alert(OnAlertCallback callback) {
        on_alert_ = callback;
        
        engine_.on_alert([this](const analytics::AlertEvent& event) {
            if (on_alert_) {
                ValuationAlert alert;
                alert.symbol = event.symbol;
                alert.threshold = event.threshold;
                alert.actual = event.actual_value;
                alert.message = event.message;
                alert.timestamp = event.timestamp;
                on_alert_(alert);
            }
        });
    }

private:
    struct PositionData {
        std::string symbol;
        int64_t quantity{0};
        double cost_basis{0};
        double current_price{0};
        double previous_close{0};
    };
    
    AlertConfig config_;
    market::PriceCache price_cache_;
    analytics::LiveValuationEngine engine_;
    
    std::map<std::string, PositionData> positions_;
    double cash_{0};
    double previous_nav_{0};
    std::string best_performer_;
    double best_performer_pct_{0};
    std::string worst_performer_;
    double worst_performer_pct_{0};
    
    mutable std::mutex mutex_;
    OnAlertCallback on_alert_;
};

/**
 * @brief Format position detail for display
 */
inline std::string format_position_detail(const PortfolioValuation& val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << std::left << std::setw(8) << "Symbol"
        << std::right << std::setw(10) << "Qty"
        << std::setw(12) << "Price"
        << std::setw(14) << "Value"
        << std::setw(12) << "P&L"
        << std::setw(10) << "P&L%"
        << std::setw(8) << "Weight"
        << "\n";
    oss << std::string(74, '-') << "\n";
    
    for (const auto& [symbol, pos] : val.positions) {
        oss << std::left << std::setw(8) << symbol
            << std::right << std::setw(10) << pos.quantity
            << std::setw(12) << pos.current_price
            << std::setw(14) << pos.market_value
            << std::setw(12) << pos.unrealized_pl
            << std::setw(9) << (pos.unrealized_pl_pct * 100) << "%"
            << std::setw(7) << (pos.weight * 100) << "%"
            << "\n";
    }
    
    return oss.str();
}

} // namespace analytics_adapter

// Use adapter types in trading namespace
namespace analytics = analytics_adapter;

/**
 * @brief Trading system configuration
 */
struct TradingSystemConfig {
    // Alpaca credentials
    std::string alpaca_api_key;
    std::string alpaca_api_secret;
    bool paper_trading{true};
    
    // Market data credentials
    std::string alpha_vantage_key;
    
    // Database paths
    std::string price_db_path{"prices.db"};
    std::string symbol_db_path{"symbols.db"};
    
    // Real-time settings
    bool enable_live_feed{true};
    bool auto_sync_positions{true};
    int sync_interval_seconds{300};
    
    // Valuation settings
    analytics::AlertConfig alert_config;
    
    // Data source preference
    market::DataSourcePriority data_priority{market::DataSourcePriority::YahooFirst};
    
    bool is_valid() const {
        return !alpaca_api_key.empty() && !alpaca_api_secret.empty();
    }
};

/**
 * @brief Trading system status
 */
struct SystemStatus {
    bool broker_connected{false};
    bool market_data_ready{false};
    bool live_feed_connected{false};
    bool position_sync_running{false};
    
    std::string broker_name;
    int positions_count{0};
    int open_orders_count{0};
    double portfolio_value{0};
    double cash_balance{0};
    double day_pnl{0};
    
    std::string last_error;
    std::chrono::system_clock::time_point timestamp;
    
    bool all_systems_ready() const {
        return broker_connected && market_data_ready;
    }
};

/**
 * @brief Trade event for callbacks
 */
struct TradeEvent {
    enum class Type {
        OrderSubmitted,
        OrderAccepted,
        OrderFilled,
        OrderPartialFill,
        OrderCanceled,
        OrderRejected,
        PositionOpened,
        PositionClosed,
        PositionUpdated,
        PriceAlert,
        RiskAlert,
        SystemError
    };
    
    Type type;
    std::string symbol;
    std::string order_id;
    double quantity{0};
    double price{0};
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

using OnTradeEventCallback = std::function<void(const TradeEvent&)>;
using OnStatusChangeCallback = std::function<void(const SystemStatus&)>;

/**
 * @brief Unified trading system
 */
class TradingSystem {
public:
    explicit TradingSystem(const TradingSystemConfig& config = {})
        : config_(config)
        , running_(false) {
        
        // Initialize logging
        genie::Logger::instance().set_level(genie::LogLevel::DEBUG);
        
        LOG_INFO("TradingSystem", "Initializing trading system");
    }
    
    ~TradingSystem() {
        shutdown();
    }
    
    // ========================================================================
    // System Lifecycle
    // ========================================================================
    
    /**
     * @brief Initialize and connect all systems
     */
    bool initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        LOG_INFO("TradingSystem", "Starting initialization");
        
        // 1. Initialize broker
        if (!init_broker()) {
            return false;
        }
        
        // 2. Initialize market data
        if (!init_market_data()) {
            return false;
        }
        
        // 3. Initialize position sync
        if (!init_position_sync()) {
            return false;
        }
        
        // 4. Initialize live valuation
        init_valuation();
        
        // 5. Initialize live feed (optional)
        if (config_.enable_live_feed) {
            init_live_feed();
        }
        
        // 6. Perform initial sync
        sync_all();
        
        running_ = true;
        LOG_INFO("TradingSystem", "Initialization complete");
        
        fire_status_change();
        
        return true;
    }
    
    /**
     * @brief Shutdown all systems
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!running_) return;
        
        LOG_INFO("TradingSystem", "Shutting down");
        
        running_ = false;
        
        // Stop position sync
        if (position_sync_) {
            position_sync_->stop_auto_sync();
        }
        
        // Stop live feed
        if (live_feed_) {
            live_feed_->stop();
        }
        
        // Disconnect broker
        if (broker_) {
            broker_->disconnect();
        }
        
        LOG_INFO("TradingSystem", "Shutdown complete");
    }
    
    /**
     * @brief Check if system is running
     */
    bool is_running() const { return running_; }
    
    // ========================================================================
    // Account Information
    // ========================================================================
    
    /**
     * @brief Get account information
     */
    std::optional<BrokerAccount> get_account() {
        if (!broker_) return std::nullopt;
        
        auto result = broker_->get_account();
        if (result.success) {
            return result.data;
        }
        
        last_error_ = result.error;
        return std::nullopt;
    }
    
    /**
     * @brief Get cash balance
     */
    double get_cash() {
        auto account = get_account();
        return account ? account->cash : 0;
    }
    
    /**
     * @brief Get buying power
     */
    double get_buying_power() {
        auto account = get_account();
        return account ? account->buying_power : 0;
    }
    
    /**
     * @brief Get portfolio value
     */
    double get_portfolio_value() {
        auto account = get_account();
        return account ? account->portfolio_value : 0;
    }
    
    // ========================================================================
    // Position Management
    // ========================================================================
    
    /**
     * @brief Get all positions from broker
     */
    std::vector<BrokerPosition> get_positions() {
        if (!broker_) return {};
        
        auto result = broker_->get_positions();
        if (result.success) {
            return result.data;
        }
        
        last_error_ = result.error;
        return {};
    }
    
    /**
     * @brief Get position for symbol
     */
    std::optional<BrokerPosition> get_position(const std::string& symbol) {
        if (!broker_) return std::nullopt;
        
        auto result = broker_->get_position(symbol);
        if (result.success) {
            return result.data;
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Check if we have position in symbol
     */
    bool has_position(const std::string& symbol) {
        return get_position(symbol).has_value();
    }
    
    /**
     * @brief Get position quantity
     */
    double get_position_qty(const std::string& symbol) {
        auto pos = get_position(symbol);
        return pos ? pos->qty : 0;
    }
    
    // ========================================================================
    // Order Submission
    // ========================================================================
    
    /**
     * @brief Submit market buy order
     */
    std::optional<BrokerOrder> buy_market(const std::string& symbol, double qty) {
        return submit_order(market_order(symbol, qty, OrderSide::Buy));
    }
    
    /**
     * @brief Submit market sell order
     */
    std::optional<BrokerOrder> sell_market(const std::string& symbol, double qty) {
        return submit_order(market_order(symbol, qty, OrderSide::Sell));
    }
    
    /**
     * @brief Submit limit buy order
     */
    std::optional<BrokerOrder> buy_limit(const std::string& symbol, double qty, double price) {
        return submit_order(limit_order(symbol, qty, OrderSide::Buy, price));
    }
    
    /**
     * @brief Submit limit sell order
     */
    std::optional<BrokerOrder> sell_limit(const std::string& symbol, double qty, double price) {
        return submit_order(limit_order(symbol, qty, OrderSide::Sell, price));
    }
    
    /**
     * @brief Submit stop loss order
     */
    std::optional<BrokerOrder> stop_loss(const std::string& symbol, double qty, double stop_price) {
        return submit_order(stop_order(symbol, qty, OrderSide::Sell, stop_price));
    }
    
    /**
     * @brief Submit generic order
     */
    std::optional<BrokerOrder> submit_order(const OrderRequest& request) {
        if (!broker_) {
            last_error_ = "Broker not connected";
            return std::nullopt;
        }
        
        LOG_INFO("TradingSystem", "Submitting order: " + request.symbol + " " + 
                 order_side_to_string(request.side) + " " + std::to_string(request.qty));
        
        auto result = broker_->submit_order(request);
        
        if (result.success) {
            LOG_INFO("TradingSystem", "Order submitted: " + result.data.id);
            
            fire_event(TradeEvent::Type::OrderSubmitted, request.symbol,
                       result.data.id, request.qty, 0,
                       "Order submitted: " + result.data.id);
            
            return result.data;
        }
        
        last_error_ = result.error;
        LOG_ERROR("TradingSystem", "Order failed: " + result.error);
        
        fire_event(TradeEvent::Type::SystemError, request.symbol,
                   "", request.qty, 0, "Order failed: " + result.error);
        
        return std::nullopt;
    }
    
    /**
     * @brief Close position in symbol
     */
    bool close_position(const std::string& symbol) {
        if (!broker_) return false;
        
        LOG_INFO("TradingSystem", "Closing position: " + symbol);
        
        auto result = broker_->close_position(symbol);
        
        if (result.success) {
            fire_event(TradeEvent::Type::PositionClosed, symbol,
                       "", 0, 0, "Position closed");
            return true;
        }
        
        last_error_ = result.error;
        return false;
    }
    
    /**
     * @brief Close all positions (liquidate)
     */
    bool close_all_positions() {
        if (!broker_) return false;
        
        LOG_WARN("TradingSystem", "Closing ALL positions");
        
        auto result = broker_->close_all_positions();
        
        if (result.success) {
            fire_event(TradeEvent::Type::PositionClosed, "",
                       "", 0, 0, "All positions closed");
            return true;
        }
        
        last_error_ = result.error;
        return false;
    }
    
    // ========================================================================
    // Order Management
    // ========================================================================
    
    /**
     * @brief Get order by ID
     */
    std::optional<BrokerOrder> get_order(const std::string& order_id) {
        if (!broker_) return std::nullopt;
        
        auto result = broker_->get_order(order_id);
        if (result.success) {
            return result.data;
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Get open orders
     */
    std::vector<BrokerOrder> get_open_orders() {
        if (!broker_) return {};
        
        auto result = broker_->get_orders("open");
        if (result.success) {
            return result.data;
        }
        
        return {};
    }
    
    /**
     * @brief Get all orders
     */
    std::vector<BrokerOrder> get_orders(const std::string& status = "all", int limit = 50) {
        if (!broker_) return {};
        
        auto result = broker_->get_orders(status, limit);
        if (result.success) {
            return result.data;
        }
        
        return {};
    }
    
    /**
     * @brief Cancel order
     */
    bool cancel_order(const std::string& order_id) {
        if (!broker_) return false;
        
        LOG_INFO("TradingSystem", "Canceling order: " + order_id);
        
        auto result = broker_->cancel_order(order_id);
        
        if (result.success) {
            fire_event(TradeEvent::Type::OrderCanceled, "",
                       order_id, 0, 0, "Order canceled");
            return true;
        }
        
        last_error_ = result.error;
        return false;
    }
    
    /**
     * @brief Cancel all open orders
     */
    bool cancel_all_orders() {
        if (!broker_) return false;
        
        LOG_WARN("TradingSystem", "Canceling ALL orders");
        
        auto result = broker_->cancel_all_orders();
        
        if (result.success) {
            fire_event(TradeEvent::Type::OrderCanceled, "",
                       "", 0, 0, "All orders canceled");
            return true;
        }
        
        last_error_ = result.error;
        return false;
    }
    
    // ========================================================================
    // Market Data
    // ========================================================================
    
    /**
     * @brief Get real-time quote
     */
    std::optional<market::Quote> get_quote(const std::string& symbol) {
        if (!data_manager_) return std::nullopt;
        
        auto result = data_manager_->get_quote(symbol);
        if (result.success) {
            return result.data;
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Get historical prices
     */
    std::vector<market::PriceBar> get_historical_prices(
        const std::string& symbol,
        const std::string& start_date = "",
        const std::string& end_date = "") {
        
        if (!data_manager_) return {};
        
        auto result = data_manager_->get_historical_prices(symbol, start_date, end_date);
        if (result.success) {
            return result.data;
        }
        
        return {};
    }
    
    /**
     * @brief Get last price for symbol
     */
    double get_last_price(const std::string& symbol) {
        // Try live feed first
        if (live_feed_) {
            auto price = live_feed_->get_last_price(symbol);
            if (price) return *price;
        }
        
        // Fallback to quote
        auto quote = get_quote(symbol);
        return quote ? quote->price : 0;
    }
    
    /**
     * @brief Subscribe to real-time prices
     */
    bool subscribe_prices(const std::vector<std::string>& symbols) {
        if (!live_feed_) return false;
        return live_feed_->subscribe(symbols);
    }
    
    // ========================================================================
    // Valuation
    // ========================================================================
    
    /**
     * @brief Get live portfolio valuation
     */
    analytics::PortfolioValuation get_valuation() {
        if (!valuation_) return {};
        return valuation_->get_valuation();
    }
    
    /**
     * @brief Get unrealized P&L
     */
    double get_unrealized_pnl() {
        if (!valuation_) return 0;
        return valuation_->get_unrealized_pl();
    }
    
    /**
     * @brief Get day P&L
     */
    double get_day_pnl() {
        if (!valuation_) return 0;
        return valuation_->get_day_pl();
    }
    
    /**
     * @brief Set price alert
     */
    void set_price_alert(const std::string& symbol, double target, bool above = true) {
        if (valuation_) {
            valuation_->add_price_target(symbol, target, above);
        }
    }
    
    // ========================================================================
    // Synchronization
    // ========================================================================
    
    /**
     * @brief Sync positions from broker
     */
    bool sync_positions() {
        if (!position_sync_) return false;
        return position_sync_->import_positions();
    }
    
    /**
     * @brief Sync cash balance
     */
    bool sync_cash() {
        if (!position_sync_) return false;
        return position_sync_->sync_cash();
    }
    
    /**
     * @brief Sync everything
     */
    void sync_all() {
        sync_positions();
        sync_cash();
        update_valuation();
    }
    
    /**
     * @brief Reconcile positions
     */
    portfolio::ReconciliationResult reconcile() {
        if (!position_sync_) return {};
        
        // Get internal positions (from valuation)
        std::map<std::string, portfolio::Position> internal;
        
        if (valuation_) {
            auto val = valuation_->get_valuation();
            for (const auto& [sym, pos] : val.positions) {
                portfolio::Position p(sym);  // Use symbol as security_id
                // Add shares to set quantity and cost basis
                // Calculate cost_per_share from cost_basis / quantity
                double cost_per_share = (pos.quantity > 0) ? pos.cost_basis / pos.quantity : 0;
                if (pos.quantity > 0 && cost_per_share > 0) {
                    p.add_shares(static_cast<double>(pos.quantity), cost_per_share, "USD");
                }
                // Update market value with current price
                if (pos.current_price > 0) {
                    p.update_market_value(pos.current_price);
                }
                internal[sym] = p;
            }
        }
        
        double internal_cash = position_sync_->get_cash();
        
        return position_sync_->reconcile(internal, internal_cash);
    }
    
    // ========================================================================
    // Status and Callbacks
    // ========================================================================
    
    /**
     * @brief Get system status
     */
    SystemStatus get_status() {
        SystemStatus status;
        status.timestamp = std::chrono::system_clock::now();
        
        status.broker_connected = broker_ && broker_->is_connected();
        status.market_data_ready = data_manager_ != nullptr;
        status.live_feed_connected = live_feed_ && live_feed_->is_connected();
        status.position_sync_running = position_sync_ && position_sync_->is_auto_sync_running();
        
        if (broker_) {
            status.broker_name = broker_->broker_name();
            
            auto account = get_account();
            if (account) {
                status.portfolio_value = account->portfolio_value;
                status.cash_balance = account->cash;
            }
            
            status.positions_count = static_cast<int>(get_positions().size());
            status.open_orders_count = static_cast<int>(get_open_orders().size());
        }
        
        if (valuation_) {
            status.day_pnl = valuation_->get_day_pl();
        }
        
        status.last_error = last_error_;
        
        return status;
    }
    
    /**
     * @brief Set trade event callback
     */
    void on_trade_event(OnTradeEventCallback callback) {
        on_trade_event_ = callback;
    }
    
    /**
     * @brief Set status change callback
     */
    void on_status_change(OnStatusChangeCallback callback) {
        on_status_change_ = callback;
    }
    
    /**
     * @brief Get last error
     */
    const std::string& last_error() const { return last_error_; }
    
    // ========================================================================
    // Direct Access (for advanced usage)
    // ========================================================================
    
    IBroker* broker() { return broker_.get(); }
    AlpacaClient* alpaca() { return dynamic_cast<AlpacaClient*>(broker_.get()); }
    market::DataManager* data_manager() { return data_manager_.get(); }
    market::LiveFeed* live_feed() { return live_feed_.get(); }
    portfolio::PositionSync* position_sync() { return position_sync_.get(); }
    analytics::LiveValuation* valuation() { return valuation_.get(); }
    
    const TradingSystemConfig& config() const { return config_; }

private:
    TradingSystemConfig config_;
    
    std::unique_ptr<IBroker> broker_;
    std::unique_ptr<market::DataManager> data_manager_;
    std::unique_ptr<market::LiveFeed> live_feed_;
    std::unique_ptr<portfolio::PositionSync> position_sync_;
    std::unique_ptr<analytics::LiveValuation> valuation_;
    
    std::atomic<bool> running_;
    std::string last_error_;
    mutable std::mutex mutex_;
    
    OnTradeEventCallback on_trade_event_;
    OnStatusChangeCallback on_status_change_;
    
    bool init_broker() {
        LOG_INFO("TradingSystem", "Initializing broker connection");
        
        AlpacaConfig alpaca_config;
        alpaca_config.api_key = config_.alpaca_api_key;
        alpaca_config.api_secret = config_.alpaca_api_secret;
        alpaca_config.paper = config_.paper_trading;
        
        broker_ = std::make_unique<AlpacaClient>(alpaca_config);
        
        if (!broker_->connect()) {
            last_error_ = "Failed to connect to broker: " + broker_->last_error();
            LOG_ERROR("TradingSystem", last_error_);
            return false;
        }
        
        LOG_INFO("TradingSystem", "Broker connected: " + broker_->broker_name());
        
        // Set up order update callback
        broker_->on_order_update([this](const BrokerOrder& order) {
            handle_order_update(order);
        });
        
        return true;
    }
    
    bool init_market_data() {
        LOG_INFO("TradingSystem", "Initializing market data");
        
        market::DataManagerConfig dm_config;
        dm_config.alpha_vantage_key = config_.alpha_vantage_key;
        dm_config.price_db_path = config_.price_db_path;
        dm_config.symbol_db_path = config_.symbol_db_path;
        dm_config.priority = config_.data_priority;
        
        data_manager_ = std::make_unique<market::DataManager>(dm_config);
        
        LOG_INFO("TradingSystem", "Market data initialized");
        return true;
    }
    
    bool init_position_sync() {
        LOG_INFO("TradingSystem", "Initializing position sync");
        
        portfolio::SyncConfig sync_config;
        sync_config.sync_interval_seconds = config_.sync_interval_seconds;
        sync_config.auto_correct_missing = true;
        
        position_sync_ = std::make_unique<portfolio::PositionSync>(
            std::shared_ptr<IBroker>(broker_.get(), [](IBroker*){}),  // Non-owning
            sync_config);
        
        // Set up sync callbacks
        position_sync_->on_sync_event([this](const portfolio::SyncEvent& event) {
            handle_sync_event(event);
        });
        
        // Start auto sync if configured
        if (config_.auto_sync_positions) {
            position_sync_->start_auto_sync();
        }
        
        LOG_INFO("TradingSystem", "Position sync initialized");
        return true;
    }
    
    void init_valuation() {
        LOG_INFO("TradingSystem", "Initializing live valuation");
        
        valuation_ = std::make_unique<analytics::LiveValuation>(config_.alert_config);
        
        // Set up alert callback
        valuation_->on_alert([this](const analytics::ValuationAlert& alert) {
            handle_valuation_alert(alert);
        });
        
        LOG_INFO("TradingSystem", "Live valuation initialized");
    }
    
    void init_live_feed() {
        LOG_INFO("TradingSystem", "Initializing live feed");
        
        market::FeedConfig feed_config;
        feed_config.provider = market::FeedProvider::Alpaca;
        feed_config.api_key = config_.alpaca_api_key;
        feed_config.api_secret = config_.alpaca_api_secret;
        feed_config.paper = config_.paper_trading;
        
        live_feed_ = std::make_unique<market::LiveFeed>(feed_config);
        
        // Set up tick callback
        live_feed_->on_tick([this](const market::PriceTick& tick) {
            handle_price_tick(tick);
        });
        
        if (live_feed_->connect()) {
            LOG_INFO("TradingSystem", "Live feed connected");
        } else {
            LOG_WARN("TradingSystem", "Live feed connection failed (continuing without)");
        }
    }
    
    void update_valuation() {
        if (!valuation_ || !broker_) return;
        
        // Load positions from broker
        auto positions = get_positions();
        valuation_->load_from_broker(positions);
        
        // Set cash
        auto account = get_account();
        if (account) {
            valuation_->set_cash(account->cash);
            valuation_->set_previous_nav(account->portfolio_value);
        }
    }
    
    void handle_order_update(const BrokerOrder& order) {
        TradeEvent::Type type;
        
        switch (order.status) {
            case OrderStatus::New:
            case OrderStatus::Accepted:
                type = TradeEvent::Type::OrderAccepted;
                break;
            case OrderStatus::Filled:
                type = TradeEvent::Type::OrderFilled;
                break;
            case OrderStatus::PartiallyFilled:
                type = TradeEvent::Type::OrderPartialFill;
                break;
            case OrderStatus::Canceled:
                type = TradeEvent::Type::OrderCanceled;
                break;
            case OrderStatus::Rejected:
                type = TradeEvent::Type::OrderRejected;
                break;
            default:
                return;
        }
        
        fire_event(type, order.symbol, order.id, order.filled_qty,
                   order.filled_avg_price,
                   "Order " + order_status_to_string(order.status));
        
        // Update valuation after fills
        if (order.status == OrderStatus::Filled || 
            order.status == OrderStatus::PartiallyFilled) {
            update_valuation();
        }
    }
    
    void handle_sync_event(const portfolio::SyncEvent& event) {
        LOG_DEBUG("Sync event: " + event.message);
        
        if (event.type == portfolio::SyncEvent::Type::SyncCompleted) {
            update_valuation();
            fire_status_change();
        }
    }
    
    void handle_price_tick(const market::PriceTick& tick) {
        // Update valuation with new price
        if (valuation_) {
            valuation_->update_price(tick.symbol, tick.price, tick.bid, tick.ask);
        }
    }
    
    void handle_valuation_alert(const analytics::ValuationAlert& alert) {
        fire_event(TradeEvent::Type::PriceAlert, alert.symbol, "",
                   0, alert.actual, alert.message);
    }
    
    void fire_event(TradeEvent::Type type, const std::string& symbol,
                    const std::string& order_id, double qty, double price,
                    const std::string& message) {
        if (on_trade_event_) {
            TradeEvent event;
            event.type = type;
            event.symbol = symbol;
            event.order_id = order_id;
            event.quantity = qty;
            event.price = price;
            event.message = message;
            event.timestamp = std::chrono::system_clock::now();
            on_trade_event_(event);
        }
    }
    
    void fire_status_change() {
        if (on_status_change_) {
            on_status_change_(get_status());
        }
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Create trading system from environment variables
 * 
 * Reads:
 * - APCA_API_KEY_ID
 * - APCA_API_SECRET_KEY
 * - APCA_API_BASE_URL (optional, determines paper vs live)
 * - ALPHAVANTAGE_API_KEY (optional)
 */
inline std::unique_ptr<TradingSystem> create_trading_system_from_env() {
    TradingSystemConfig config;
    
    if (const char* key = std::getenv("APCA_API_KEY_ID")) {
        config.alpaca_api_key = key;
    }
    if (const char* secret = std::getenv("APCA_API_SECRET_KEY")) {
        config.alpaca_api_secret = secret;
    }
    if (const char* url = std::getenv("APCA_API_BASE_URL")) {
        std::string base_url = url;
        config.paper_trading = (base_url.find("paper") != std::string::npos);
    }
    if (const char* av_key = std::getenv("ALPHAVANTAGE_API_KEY")) {
        config.alpha_vantage_key = av_key;
    }
    
    if (!config.is_valid()) {
        return nullptr;
    }
    
    return std::make_unique<TradingSystem>(config);
}

/**
 * @brief Format system status for display
 */
inline std::string format_system_status(const SystemStatus& status) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "=== Trading System Status ===\n";
    oss << "Broker: " << (status.broker_connected ? "Connected" : "Disconnected");
    if (!status.broker_name.empty()) {
        oss << " (" << status.broker_name << ")";
    }
    oss << "\n";
    
    oss << "Market Data: " << (status.market_data_ready ? "Ready" : "Not Ready") << "\n";
    oss << "Live Feed: " << (status.live_feed_connected ? "Connected" : "Disconnected") << "\n";
    oss << "Position Sync: " << (status.position_sync_running ? "Running" : "Stopped") << "\n";
    oss << "\n";
    
    oss << "Portfolio Value: $" << status.portfolio_value << "\n";
    oss << "Cash Balance: $" << status.cash_balance << "\n";
    oss << "Day P&L: $" << status.day_pnl << "\n";
    oss << "\n";
    
    oss << "Open Positions: " << status.positions_count << "\n";
    oss << "Open Orders: " << status.open_orders_count << "\n";
    
    if (!status.last_error.empty()) {
        oss << "\nLast Error: " << status.last_error << "\n";
    }
    
    return oss.str();
}

} // namespace genie::trading

#endif // GENIE_TRADING_TRADING_SYSTEM_HPP
