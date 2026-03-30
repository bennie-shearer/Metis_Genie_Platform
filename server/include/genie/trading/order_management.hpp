/**
 * @file order_management.hpp
 * @brief Order Management System for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_TRADING_ORDER_MANAGEMENT_HPP
#define GENIE_TRADING_ORDER_MANAGEMENT_HPP
#include "../portfolio/portfolio.hpp"
#include "../core/events.hpp"

namespace genie::trading {
using namespace genie::portfolio;
using namespace genie::market;

struct Order {
    OrderId id{UuidGenerator::generate()}; PortfolioId portfolio_id; SecurityId security_id;
    OrderSide side{OrderSide::Buy}; OrderType type{OrderType::Market}; OrderStatus status{OrderStatus::Pending};
    TimeInForce tif{TimeInForce::Day}; Quantity quantity{0}, filled_quantity{0};
    Price limit_price{0}, stop_price{0}, avg_fill_price{0};
    TimePoint created_time{std::chrono::system_clock::now()}, updated_time{std::chrono::system_clock::now()};
    std::string rejection_reason;
    [[nodiscard]] bool is_active() const { return status == OrderStatus::Pending || status == OrderStatus::New || status == OrderStatus::PartiallyFilled; }
    [[nodiscard]] Quantity remaining() const { return quantity - filled_quantity; }
};

struct Trade {
    TradeId id{UuidGenerator::generate()}; OrderId order_id; PortfolioId portfolio_id; SecurityId security_id;
    OrderSide side{OrderSide::Buy}; Quantity quantity{0}; Price price{0}; Money commission, fees;
    TimePoint execution_time{std::chrono::system_clock::now()}; std::string venue{"INTERNAL"};
};

struct AuditEntry { TimePoint timestamp{std::chrono::system_clock::now()}; std::string event_type, description, user_id; OrderId order_id; TradeId trade_id; };

class AuditTrail {
    std::vector<AuditEntry> entries_; mutable std::mutex mutex_;
public:
    void log(const std::string& type, const std::string& desc, const OrderId& oid = "", const TradeId& tid = "") {
        std::lock_guard lk(mutex_); AuditEntry e; e.event_type = type; e.description = desc; e.order_id = oid; e.trade_id = tid; entries_.push_back(e);
    }
    [[nodiscard]] std::vector<AuditEntry> get_entries() const { std::lock_guard lk(mutex_); return entries_; }
    [[nodiscard]] size_t size() const { std::lock_guard lk(mutex_); return entries_.size(); }
};

class OrderManagementSystem {
    std::map<OrderId, Order> orders_; std::vector<Trade> trades_;
    MarketDataService* market_data_{nullptr}; PortfolioManager* portfolios_{nullptr}; EventBus* event_bus_{nullptr};
    AuditTrail audit_; mutable std::shared_mutex mutex_;
    bool auto_execute_{true}; double slippage_bps_{5.0}, commission_per_share_{0.01};
public:
    void set_market_data(MarketDataService* md) { market_data_ = md; }
    void set_portfolio_manager(PortfolioManager* pm) { portfolios_ = pm; }
    void set_event_bus(EventBus* eb) { event_bus_ = eb; }
    void set_auto_execute(bool ae) { auto_execute_ = ae; }
    void set_slippage(double bps) { slippage_bps_ = bps; }
    void set_commission(double cps) { commission_per_share_ = cps; }
    [[nodiscard]] Order submit_order(Order order) {
        std::unique_lock lk(mutex_);
        order.status = OrderStatus::New; order.updated_time = std::chrono::system_clock::now();
        orders_[order.id] = order; audit_.log("ORDER_SUBMITTED", "Order " + order.id + " submitted", order.id);
        if (event_bus_) { EventData e(EventType::OrderSubmitted); e.set("order_id", order.id); event_bus_->publish(e); }
        lk.unlock();
        if (auto_execute_) execute_order(order.id);
        std::shared_lock slk(mutex_); return orders_[order.id];
    }
    void cancel_order(const OrderId& id) {
        std::unique_lock lk(mutex_); auto it = orders_.find(id);
        if (it != orders_.end() && it->second.is_active()) {
            it->second.status = OrderStatus::Cancelled; it->second.updated_time = std::chrono::system_clock::now();
            audit_.log("ORDER_CANCELLED", "Order " + id + " cancelled", id);
        }
    }
    [[nodiscard]] std::optional<Order> get_order(const OrderId& id) const {
        std::shared_lock lk(mutex_); auto it = orders_.find(id);
        return it != orders_.end() ? std::optional(it->second) : std::nullopt;
    }
    [[nodiscard]] std::vector<Trade> get_trades(const PortfolioId& pid = "") const {
        std::shared_lock lk(mutex_); if (pid.empty()) return trades_;
        std::vector<Trade> r; for (const auto& t : trades_) if (t.portfolio_id == pid) r.push_back(t); return r;
    }
    [[nodiscard]] const AuditTrail& audit() const { return audit_; }
private:
    void execute_order(const OrderId& id) {
        std::unique_lock lk(mutex_); auto it = orders_.find(id);
        if (it == orders_.end() || !it->second.is_active()) return;
        Order& order = it->second;
        Price market_price = 0;
        if (market_data_) {
            auto quote = market_data_->store()->get_quote(order.security_id);
            if (quote) { market_price = (order.side == OrderSide::Buy || order.side == OrderSide::BuyToCover) ? quote->ask : quote->bid; if (market_price <= 0) market_price = quote->last; }
        }
        if (market_price <= 0) { order.status = OrderStatus::Rejected; order.rejection_reason = "No market price"; return; }
        if (order.type == OrderType::Limit) {
            if (order.side == OrderSide::Buy && market_price > order.limit_price) return;
            if (order.side == OrderSide::Sell && market_price < order.limit_price) return;
        }
        double slip = (order.side == OrderSide::Buy) ? 1.0 + slippage_bps_ / 10000.0 : 1.0 - slippage_bps_ / 10000.0;
        Price fill_price = market_price * slip;
        Trade trade; trade.order_id = order.id; trade.portfolio_id = order.portfolio_id;
        trade.security_id = order.security_id; trade.side = order.side;
        trade.quantity = order.remaining(); trade.price = fill_price;
        trade.commission = Money(trade.quantity * commission_per_share_, "USD"); trades_.push_back(trade);
        order.filled_quantity = order.quantity; order.avg_fill_price = fill_price;
        order.status = OrderStatus::Filled; order.updated_time = std::chrono::system_clock::now();
        audit_.log("ORDER_FILLED", "Order " + order.id + " filled at " + std::to_string(fill_price), order.id, trade.id);
        if (event_bus_) { EventData e(EventType::OrderFilled); e.set("order_id", order.id); e.set("trade_id", trade.id); event_bus_->publish(e); }
    }
};
} // namespace genie::trading
#endif
