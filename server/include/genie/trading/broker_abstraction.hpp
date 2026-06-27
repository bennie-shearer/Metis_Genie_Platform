/**
 * @file broker_abstraction.hpp
 * @brief Unified broker abstraction layer with multiple broker support
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides a unified interface for multiple brokers:
 * - Alpaca Markets
 * - Interactive Brokers Client Portal
 * - TD Ameritrade / Schwab
 * - Tradier
 * - Webull
 * 
 * Features:
 * - Common order types across all brokers
 * - Unified position and balance representation
 * - Account status normalization
 * - Order routing abstraction
 * - Multi-account support
 */
#pragma once
#ifndef GENIE_TRADING_BROKER_ABSTRACTION_HPP
#define GENIE_TRADING_BROKER_ABSTRACTION_HPP

#include "broker_interface.hpp"
#include "../core/http_client.hpp"
#include "../market/market_data.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <chrono>

namespace genie::trading {

// Import Quote from market namespace so broker clients can use it directly
using genie::market::Quote;

// ============================================================================
// Unified Broker Types
// ============================================================================

/**
 * @brief Broker identifier
 */
enum class BrokerId {
    Alpaca,
    InteractiveBrokers,
    TDAmeritrade,
    Tradier,
    Webull,
    Simulation          // Paper trading simulation
};

inline std::string broker_id_to_string(BrokerId id) {
    switch (id) {
        case BrokerId::Alpaca: return "Alpaca";
        case BrokerId::InteractiveBrokers: return "Interactive Brokers";
        case BrokerId::TDAmeritrade: return "TD Ameritrade";
        case BrokerId::Tradier: return "Tradier";
        case BrokerId::Webull: return "Webull";
        case BrokerId::Simulation: return "Simulation";
        default: return "Unknown";
    }
}

/**
 * @brief Account type
 */
enum class AccountType {
    Cash,
    Margin,
    IRA,
    Roth,
    Portfolio_Margin,
    Paper
};

/**
 * @brief Unified account info
 */
struct UnifiedAccount {
    std::string id;
    std::string broker_account_id;
    BrokerId broker;
    AccountType type{AccountType::Margin};
    std::string name;
    std::string currency{"USD"};
    
    // Balances
    double cash{0};
    double buying_power{0};
    double portfolio_value{0};
    double equity{0};
    double day_trading_buying_power{0};
    double margin_used{0};
    double maintenance_margin{0};
    
    // Status
    bool active{true};
    bool trading_blocked{false};
    bool transfers_blocked{false};
    bool pattern_day_trader{false};
    
    // Timestamps
    std::string last_sync;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "Account: " << name << " (" << broker_id_to_string(broker) << ")\n";
        oss << "  Portfolio Value: $" << std::fixed << std::setprecision(2) << portfolio_value << "\n";
        oss << "  Buying Power: $" << buying_power << "\n";
        oss << "  Cash: $" << cash << "\n";
        return oss.str();
    }
};

/**
 * @brief Unified position
 */
struct UnifiedPosition {
    std::string symbol;
    std::string account_id;
    BrokerId broker;
    
    double qty{0};
    double avg_entry_price{0};
    double market_value{0};
    double cost_basis{0};
    double unrealized_pl{0};
    double unrealized_pl_pct{0};
    double current_price{0};
    double day_change{0};
    double day_change_pct{0};
    
    // Metadata
    std::string asset_class;          // us_equity, crypto, etc.
    std::string exchange;
    bool is_marginable{true};
    bool is_shortable{true};
    
    std::string last_update;
    
    bool is_long() const { return qty > 0; }
    bool is_short() const { return qty < 0; }
};

/**
 * @brief Unified order
 */
struct UnifiedOrder {
    std::string id;
    std::string broker_order_id;
    std::string client_order_id;
    std::string account_id;
    BrokerId broker;
    
    std::string symbol;
    OrderSide side;
    OrderType type;
    TimeInForce time_in_force{TimeInForce::Day};
    OrderStatus status{OrderStatus::New};
    
    double qty{0};
    double filled_qty{0};
    double limit_price{0};
    double stop_price{0};
    double filled_avg_price{0};
    
    // Extended hours
    bool extended_hours{false};
    
    // Timestamps
    std::string created_at;
    std::string updated_at;
    std::string submitted_at;
    std::string filled_at;
    std::string canceled_at;
    std::string expired_at;
    std::string failed_at;
    
    // Error info
    std::string error_message;
    
    bool is_open() const {
        return status == OrderStatus::New ||
               status == OrderStatus::Accepted ||
               status == OrderStatus::PendingNew ||
               status == OrderStatus::PartiallyFilled;
    }
    
    double remaining_qty() const {
        return qty - filled_qty;
    }
};

/**
 * @brief Order result from broker
 */
template<typename T>
struct BrokerResponse {
    bool success{false};
    T data;
    std::string error;
    int error_code{0};
    
    static BrokerResponse ok(const T& data) {
        BrokerResponse r;
        r.success = true;
        r.data = data;
        return r;
    }
    
    static BrokerResponse fail(const std::string& error, int code = 0) {
        BrokerResponse r;
        r.success = false;
        r.error = error;
        r.error_code = code;
        return r;
    }
};

// ============================================================================
// Abstract Broker Interface
// ============================================================================

/**
 * @brief Abstract broker interface
 */
class IUnifiedBroker {
public:
    virtual ~IUnifiedBroker() = default;
    
    // Identity
    virtual BrokerId broker_id() const = 0;
    virtual std::string broker_name() const = 0;
    virtual bool is_connected() const = 0;
    
    // Connection
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    
    // Account
    virtual BrokerResponse<UnifiedAccount> get_account() = 0;
    virtual BrokerResponse<std::vector<UnifiedAccount>> get_accounts() = 0;
    
    // Positions
    virtual BrokerResponse<std::vector<UnifiedPosition>> get_positions() = 0;
    virtual BrokerResponse<UnifiedPosition> get_position(const std::string& symbol) = 0;
    
    // Orders
    virtual BrokerResponse<UnifiedOrder> submit_order(const OrderRequest& request) = 0;
    virtual BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) = 0;
    virtual BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status = "") = 0;
    virtual BrokerResponse<bool> cancel_order(const std::string& order_id) = 0;
    virtual BrokerResponse<UnifiedOrder> replace_order(const std::string& order_id,
                                                        const OrderRequest& request) = 0;
    
    // Market Data (if supported)
    virtual bool supports_market_data() const { return false; }
    virtual BrokerResponse<Quote> get_quote([[maybe_unused]] const std::string& symbol) { 
        return BrokerResponse<Quote>::fail("Not supported");
    }
    
    // Error handling
    virtual std::string last_error() const = 0;
};

// ============================================================================
// Alpaca Broker Implementation
// ============================================================================

/**
 * @brief Alpaca Markets broker implementation
 */
class AlpacaBroker : public IUnifiedBroker {
public:
    struct Config {
        std::string api_key;
        std::string api_secret;
        bool paper{true};
        int timeout_ms{10000};
        
        std::string base_url() const {
            return paper ? "https://paper-api.alpaca.markets"
                        : "https://api.alpaca.markets";
        }
        
        std::string data_url() const {
            return "https://data.alpaca.markets";
        }
    };
    
    explicit AlpacaBroker(const Config& config)
        : config_(config)
        , http_(config.base_url())
        , connected_(false) {
        
        http_.set_timeout(config.timeout_ms);
        http_.set_header("APCA-API-KEY-ID", config.api_key);
        http_.set_header("APCA-API-SECRET-KEY", config.api_secret);
    }
    
    BrokerId broker_id() const override { return BrokerId::Alpaca; }
    std::string broker_name() const override { return "Alpaca Markets"; }
    bool is_connected() const override { return connected_; }
    
    bool connect() override {
        auto result = get_account();
        connected_ = result.success;
        return connected_;
    }
    
    void disconnect() override {
        connected_ = false;
    }
    
    BrokerResponse<UnifiedAccount> get_account() override {
        auto response = http_.get("/v2/account");
        if (!response.success) {
            return BrokerResponse<UnifiedAccount>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        UnifiedAccount account;
        account.id = json.get_string("id", "");
        account.broker_account_id = json.get_string("account_number", "");
        account.broker = BrokerId::Alpaca;
        account.name = "Alpaca " + std::string(config_.paper ? "Paper" : "Live");
        account.currency = json.get_string("currency", "USD");
        
        account.cash = std::stod(json.get_string("cash", "0"));
        account.buying_power = std::stod(json.get_string("buying_power", "0"));
        account.portfolio_value = std::stod(json.get_string("portfolio_value", "0"));
        account.equity = std::stod(json.get_string("equity", "0"));
        account.day_trading_buying_power = std::stod(json.get_string("daytrading_buying_power", "0"));
        
        account.trading_blocked = json.get_bool("trading_blocked", false);
        account.transfers_blocked = json.get_bool("transfers_blocked", false);
        account.pattern_day_trader = json.get_bool("pattern_day_trader", false);
        
        account.type = config_.paper ? AccountType::Paper : AccountType::Margin;
        
        return BrokerResponse<UnifiedAccount>::ok(account);
    }
    
    BrokerResponse<std::vector<UnifiedAccount>> get_accounts() override {
        auto result = get_account();
        if (!result.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(result.error);
        }
        return BrokerResponse<std::vector<UnifiedAccount>>::ok({result.data});
    }
    
    BrokerResponse<std::vector<UnifiedPosition>> get_positions() override {
        auto response = http_.get("/v2/positions");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array()) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail("Invalid response");
        }
        
        std::vector<UnifiedPosition> positions;
        for (const auto& item : json.array()) {
            UnifiedPosition pos;
            pos.symbol = item.get_string("symbol", "");
            pos.broker = BrokerId::Alpaca;
            pos.qty = std::stod(item.get_string("qty", "0"));
            pos.avg_entry_price = std::stod(item.get_string("avg_entry_price", "0"));
            pos.market_value = std::stod(item.get_string("market_value", "0"));
            pos.cost_basis = std::stod(item.get_string("cost_basis", "0"));
            pos.unrealized_pl = std::stod(item.get_string("unrealized_pl", "0"));
            pos.unrealized_pl_pct = std::stod(item.get_string("unrealized_plpc", "0"));
            pos.current_price = std::stod(item.get_string("current_price", "0"));
            pos.asset_class = item.get_string("asset_class", "");
            pos.exchange = item.get_string("exchange", "");
            
            positions.push_back(pos);
        }
        
        return BrokerResponse<std::vector<UnifiedPosition>>::ok(positions);
    }
    
    BrokerResponse<UnifiedPosition> get_position(const std::string& symbol) override {
        auto response = http_.get("/v2/positions/" + symbol);
        if (!response.success) {
            return BrokerResponse<UnifiedPosition>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        UnifiedPosition pos;
        pos.symbol = json.get_string("symbol", symbol);
        pos.broker = BrokerId::Alpaca;
        pos.qty = std::stod(json.get_string("qty", "0"));
        pos.avg_entry_price = std::stod(json.get_string("avg_entry_price", "0"));
        pos.market_value = std::stod(json.get_string("market_value", "0"));
        pos.cost_basis = std::stod(json.get_string("cost_basis", "0"));
        pos.unrealized_pl = std::stod(json.get_string("unrealized_pl", "0"));
        pos.current_price = std::stod(json.get_string("current_price", "0"));
        
        return BrokerResponse<UnifiedPosition>::ok(pos);
    }
    
    BrokerResponse<UnifiedOrder> submit_order(const OrderRequest& request) override {
        core::JsonObject body;
        body["symbol"] = request.symbol;
        body["qty"] = std::to_string(static_cast<int>(request.qty));
        body["side"] = order_side_to_string(request.side);
        body["type"] = order_type_to_string(request.type);
        body["time_in_force"] = time_in_force_to_string(request.time_in_force);
        
        if (request.limit_price && *request.limit_price > 0) {
            body["limit_price"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            body["stop_price"] = std::to_string(*request.stop_price);
        }
        if (request.extended_hours) {
            body["extended_hours"] = "true";
        }
        
        auto response = http_.post("/v2/orders", core::JsonParser::stringify(body));
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        return parse_order_response(response.body);
    }
    
    BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) override {
        auto response = http_.get("/v2/orders/" + order_id);
        if (!response.success) {
            return BrokerResponse<UnifiedOrder>::fail(response.error);
        }
        return parse_order_response(response.body);
    }
    
    BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status) override {
        std::string url = "/v2/orders";
        if (!status.empty()) {
            url += "?status=" + status;
        }
        
        auto response = http_.get(url);
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array()) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail("Invalid response");
        }
        
        std::vector<UnifiedOrder> orders;
        for (const auto& item : json.array()) {
            auto order = parse_order_from_json(item);
            orders.push_back(order);
        }
        
        return BrokerResponse<std::vector<UnifiedOrder>>::ok(orders);
    }
    
    BrokerResponse<bool> cancel_order(const std::string& order_id) override {
        auto response = http_.del("/v2/orders/" + order_id);
        if (!response.success && response.status_code != 204) {
            return BrokerResponse<bool>::fail(response.error);
        }
        return BrokerResponse<bool>::ok(true);
    }
    
    BrokerResponse<UnifiedOrder> replace_order(const std::string& order_id,
                                                const OrderRequest& request) override {
        core::JsonObject body;
        body["qty"] = std::to_string(static_cast<int>(request.qty));
        body["time_in_force"] = time_in_force_to_string(request.time_in_force);
        
        if (request.limit_price && *request.limit_price > 0) {
            body["limit_price"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            body["stop_price"] = std::to_string(*request.stop_price);
        }
        
        auto response = http_.patch("/v2/orders/" + order_id, core::JsonParser::stringify(body));
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        return parse_order_response(response.body);
    }
    
    bool supports_market_data() const override { return true; }
    
    BrokerResponse<Quote> get_quote(const std::string& symbol) override {
        core::HttpClient data_http(config_.data_url());
        data_http.set_header("APCA-API-KEY-ID", config_.api_key);
        data_http.set_header("APCA-API-SECRET-KEY", config_.api_secret);
        
        auto response = data_http.get("/v2/stocks/" + symbol + "/quotes/latest");
        if (!response.success) {
            return BrokerResponse<Quote>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains("quote")) {
            return BrokerResponse<Quote>::fail("No quote data");
        }
        
        const auto& q = json["quote"];
        Quote quote;
        quote.symbol = symbol;
        quote.bid = q.get_double("bp", 0);
        quote.ask = q.get_double("ap", 0);
        quote.bid_size = q.get_int("bs", 0);
        quote.ask_size = q.get_int("as", 0);
        quote.price = (quote.bid + quote.ask) / 2;
        
        return BrokerResponse<Quote>::ok(quote);
    }
    
    std::string last_error() const override { return last_error_; }

private:
    Config config_;
    core::HttpClient http_;
    bool connected_;
    std::string last_error_;
    
    BrokerResponse<UnifiedOrder> parse_order_response(const std::string& body) {
        auto json = core::JsonParser::parse(body);
        return BrokerResponse<UnifiedOrder>::ok(parse_order_from_json(json));
    }
    
    UnifiedOrder parse_order_from_json(const core::JsonValue& json) {
        UnifiedOrder order;
        order.id = json.get_string("id", "");
        order.broker_order_id = order.id;
        order.client_order_id = json.get_string("client_order_id", "");
        order.broker = BrokerId::Alpaca;
        order.symbol = json.get_string("symbol", "");
        order.side = string_to_order_side(json.get_string("side", "buy"));
        order.type = string_to_order_type(json.get_string("type", "market"));
        order.time_in_force = string_to_time_in_force(json.get_string("time_in_force", "day"));
        order.status = string_to_order_status(json.get_string("status", "new"));
        order.qty = std::stod(json.get_string("qty", "0"));
        order.filled_qty = std::stod(json.get_string("filled_qty", "0"));
        
        auto limit = json.get_string("limit_price", "");
        if (!limit.empty()) order.limit_price = std::stod(limit);
        
        auto stop = json.get_string("stop_price", "");
        if (!stop.empty()) order.stop_price = std::stod(stop);
        
        auto filled = json.get_string("filled_avg_price", "");
        if (!filled.empty()) order.filled_avg_price = std::stod(filled);
        
        order.extended_hours = json.get_bool("extended_hours", false);
        order.created_at = json.get_string("created_at", "");
        order.updated_at = json.get_string("updated_at", "");
        order.submitted_at = json.get_string("submitted_at", "");
        order.filled_at = json.get_string("filled_at", "");
        order.canceled_at = json.get_string("canceled_at", "");
        order.expired_at = json.get_string("expired_at", "");
        order.failed_at = json.get_string("failed_at", "");
        
        return order;
    }
};

// ============================================================================
// Tradier Broker Implementation
// ============================================================================

/**
 * @brief Tradier broker implementation
 */
class TradierBroker : public IUnifiedBroker {
public:
    struct Config {
        std::string access_token;
        std::string account_id;
        bool sandbox{false};
        int timeout_ms{10000};
        
        std::string base_url() const {
            return sandbox ? "https://sandbox.tradier.com"
                          : "https://api.tradier.com";
        }
    };
    
    explicit TradierBroker(const Config& config)
        : config_(config)
        , http_(config.base_url())
        , connected_(false) {
        
        http_.set_timeout(config.timeout_ms);
        http_.set_header("Authorization", "Bearer " + config.access_token);
        http_.set_header("Accept", "application/json");
    }
    
    BrokerId broker_id() const override { return BrokerId::Tradier; }
    std::string broker_name() const override { return "Tradier"; }
    bool is_connected() const override { return connected_; }
    
    bool connect() override {
        auto result = get_account();
        connected_ = result.success;
        return connected_;
    }
    
    void disconnect() override {
        connected_ = false;
    }
    
    BrokerResponse<UnifiedAccount> get_account() override {
        auto response = http_.get("/v1/accounts/" + config_.account_id);
        if (!response.success) {
            return BrokerResponse<UnifiedAccount>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains("account")) {
            return BrokerResponse<UnifiedAccount>::fail("Invalid response");
        }
        
        const auto& acc = json["account"];
        
        UnifiedAccount account;
        account.id = acc.get_string("account_number", "");
        account.broker_account_id = config_.account_id;
        account.broker = BrokerId::Tradier;
        account.name = "Tradier " + acc.get_string("type", "");
        account.currency = "USD";
        account.type = config_.sandbox ? AccountType::Paper : AccountType::Margin;
        
        if (acc.contains("balances")) {
            const auto& bal = acc["balances"];
            account.cash = bal.get_double("cash", 0);
            account.buying_power = bal.get_double("buying_power", 0);
            account.portfolio_value = bal.get_double("market_value", 0);
            account.equity = bal.get_double("equity", 0);
        }
        
        return BrokerResponse<UnifiedAccount>::ok(account);
    }
    
    BrokerResponse<std::vector<UnifiedAccount>> get_accounts() override {
        auto response = http_.get("/v1/user/profile");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(response.error);
        }
        
        // For simplicity, return current account
        auto result = get_account();
        if (!result.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(result.error);
        }
        return BrokerResponse<std::vector<UnifiedAccount>>::ok({result.data});
    }
    
    BrokerResponse<std::vector<UnifiedPosition>> get_positions() override {
        auto response = http_.get("/v1/accounts/" + config_.account_id + "/positions");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedPosition> positions;
        
        if (json.contains("positions") && json["positions"].contains("position")) {
            auto pos_data = json["positions"]["position"];
            std::vector<core::JsonValue> items;
            
            if (pos_data.is_array()) {
                items = pos_data.array();
            } else if (pos_data.is_object()) {
                items.push_back(pos_data);
            }
            
            for (const auto& item : items) {
                UnifiedPosition pos;
                pos.symbol = item.get_string("symbol", "");
                pos.broker = BrokerId::Tradier;
                pos.qty = item.get_double("quantity", 0);
                pos.avg_entry_price = item.get_double("cost_basis", 0) / pos.qty;
                pos.cost_basis = item.get_double("cost_basis", 0);
                pos.current_price = item.get_double("last_price", 0);
                pos.market_value = pos.qty * pos.current_price;
                pos.unrealized_pl = pos.market_value - pos.cost_basis;
                
                positions.push_back(pos);
            }
        }
        
        return BrokerResponse<std::vector<UnifiedPosition>>::ok(positions);
    }
    
    BrokerResponse<UnifiedPosition> get_position(const std::string& symbol) override {
        auto result = get_positions();
        if (!result.success) {
            return BrokerResponse<UnifiedPosition>::fail(result.error);
        }
        
        for (const auto& pos : result.data) {
            if (pos.symbol == symbol) {
                return BrokerResponse<UnifiedPosition>::ok(pos);
            }
        }
        
        return BrokerResponse<UnifiedPosition>::fail("Position not found");
    }
    
    BrokerResponse<UnifiedOrder> submit_order(const OrderRequest& request) override {
        std::map<std::string, std::string> params = {
            {"class", "equity"},
            {"symbol", request.symbol},
            {"side", order_side_to_string(request.side)},
            {"quantity", std::to_string(static_cast<int>(request.qty))},
            {"type", tradier_order_type(request.type)},
            {"duration", tradier_duration(request.time_in_force)}
        };
        
        if (request.limit_price && *request.limit_price > 0) {
            params["price"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            params["stop"] = std::to_string(*request.stop_price);
        }
        
        auto response = http_.post("/v1/accounts/" + config_.account_id + "/orders",
                                   build_form_data(params),
                                   "application/x-www-form-urlencoded");
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains("order")) {
            return BrokerResponse<UnifiedOrder>::fail("Invalid response");
        }
        
        UnifiedOrder order;
        order.id = std::to_string(json["order"].get_int("id", 0));
        order.broker_order_id = order.id;
        order.broker = BrokerId::Tradier;
        order.symbol = request.symbol;
        order.side = request.side;
        order.type = request.type;
        order.qty = request.qty;
        order.status = OrderStatus::New;
        
        return BrokerResponse<UnifiedOrder>::ok(order);
    }
    
    BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) override {
        auto response = http_.get("/v1/accounts/" + config_.account_id + "/orders/" + order_id);
        if (!response.success) {
            return BrokerResponse<UnifiedOrder>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains("order")) {
            return BrokerResponse<UnifiedOrder>::fail("Order not found");
        }
        
        return BrokerResponse<UnifiedOrder>::ok(parse_tradier_order(json["order"]));
    }
    
    BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status) override {
        auto response = http_.get("/v1/accounts/" + config_.account_id + "/orders");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedOrder> orders;
        
        if (json.contains("orders") && json["orders"].contains("order")) {
            auto order_data = json["orders"]["order"];
            std::vector<core::JsonValue> items;
            
            if (order_data.is_array()) {
                items = order_data.array();
            } else if (order_data.is_object()) {
                items.push_back(order_data);
            }
            
            for (const auto& item : items) {
                auto order = parse_tradier_order(item);
                
                if (status.empty() || order_status_to_string(order.status) == status) {
                    orders.push_back(order);
                }
            }
        }
        
        return BrokerResponse<std::vector<UnifiedOrder>>::ok(orders);
    }
    
    BrokerResponse<bool> cancel_order(const std::string& order_id) override {
        auto response = http_.del("/v1/accounts/" + config_.account_id + "/orders/" + order_id);
        if (!response.success && response.status_code != 200) {
            return BrokerResponse<bool>::fail(response.error);
        }
        return BrokerResponse<bool>::ok(true);
    }
    
    BrokerResponse<UnifiedOrder> replace_order(const std::string& order_id,
                                                const OrderRequest& request) override {
        // Tradier requires cancel and re-submit
        auto cancel_result = cancel_order(order_id);
        if (!cancel_result.success) {
            return BrokerResponse<UnifiedOrder>::fail(cancel_result.error);
        }
        
        return submit_order(request);
    }
    
    std::string last_error() const override { return last_error_; }

private:
    Config config_;
    core::HttpClient http_;
    bool connected_;
    std::string last_error_;
    
    std::string tradier_order_type(OrderType type) {
        switch (type) {
            case OrderType::Market: return "market";
            case OrderType::Limit: return "limit";
            case OrderType::Stop: return "stop";
            case OrderType::StopLimit: return "stop_limit";
            default: return "market";
        }
    }
    
    std::string tradier_duration(TimeInForce tif) {
        switch (tif) {
            case TimeInForce::Day: return "day";
            case TimeInForce::GTC: return "gtc";
            case TimeInForce::IOC: return "pre";  // Tradier doesn't have IOC
            default: return "day";
        }
    }
    
    std::string build_form_data(const std::map<std::string, std::string>& params) {
        std::ostringstream oss;
        bool first = true;
        for (const auto& [key, value] : params) {
            if (!first) oss << "&";
            oss << key << "=" << core::HttpClient::url_encode(value);
            first = false;
        }
        return oss.str();
    }
    
    UnifiedOrder parse_tradier_order(const core::JsonValue& json) {
        UnifiedOrder order;
        order.id = std::to_string(json.get_int("id", 0));
        order.broker_order_id = order.id;
        order.broker = BrokerId::Tradier;
        order.symbol = json.get_string("symbol", "");
        order.side = string_to_order_side(json.get_string("side", "buy"));
        order.type = string_to_order_type(json.get_string("type", "market"));
        order.qty = json.get_double("quantity", 0);
        order.filled_qty = json.get_double("exec_quantity", 0);
        order.limit_price = json.get_double("price", 0);
        order.stop_price = json.get_double("stop_price", 0);
        order.filled_avg_price = json.get_double("avg_fill_price", 0);
        order.created_at = json.get_string("create_date", "");
        
        std::string status = json.get_string("status", "");
        if (status == "pending") order.status = OrderStatus::PendingNew;
        else if (status == "open") order.status = OrderStatus::New;
        else if (status == "partially_filled") order.status = OrderStatus::PartiallyFilled;
        else if (status == "filled") order.status = OrderStatus::Filled;
        else if (status == "canceled") order.status = OrderStatus::Canceled;
        else if (status == "rejected") order.status = OrderStatus::Rejected;
        else if (status == "expired") order.status = OrderStatus::Expired;
        
        return order;
    }
};

// ============================================================================
// Broker Manager
// ============================================================================

/**
 * @brief Manages multiple broker connections
 */
class BrokerManager {
public:
    /**
     * @brief Add broker
     */
    void add_broker(std::shared_ptr<IUnifiedBroker> broker) {
        std::lock_guard<std::mutex> lock(mutex_);
        brokers_[broker->broker_id()] = broker;
    }
    
    /**
     * @brief Get broker by ID
     */
    std::shared_ptr<IUnifiedBroker> get_broker(BrokerId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = brokers_.find(id);
        return it != brokers_.end() ? it->second : nullptr;
    }
    
    /**
     * @brief Get primary broker
     */
    std::shared_ptr<IUnifiedBroker> primary() {
        std::lock_guard<std::mutex> lock(mutex_);
        return primary_broker_;
    }
    
    /**
     * @brief Set primary broker
     */
    void set_primary(BrokerId id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = brokers_.find(id);
        if (it != brokers_.end()) {
            primary_broker_ = it->second;
        }
    }
    
    /**
     * @brief Connect all brokers
     */
    std::map<BrokerId, bool> connect_all() {
        std::map<BrokerId, bool> results;
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [id, broker] : brokers_) {
            results[id] = broker->connect();
        }
        
        return results;
    }
    
    /**
     * @brief Get all accounts
     */
    std::vector<UnifiedAccount> get_all_accounts() {
        std::vector<UnifiedAccount> all_accounts;
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [id, broker] : brokers_) {
            auto result = broker->get_accounts();
            if (result.success) {
                for (auto& acc : result.data) {
                    all_accounts.push_back(acc);
                }
            }
        }
        
        return all_accounts;
    }
    
    /**
     * @brief Get all positions
     */
    std::vector<UnifiedPosition> get_all_positions() {
        std::vector<UnifiedPosition> all_positions;
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [id, broker] : brokers_) {
            auto result = broker->get_positions();
            if (result.success) {
                for (auto& pos : result.data) {
                    all_positions.push_back(pos);
                }
            }
        }
        
        return all_positions;
    }
    
    /**
     * @brief List connected brokers
     */
    std::vector<BrokerId> list_brokers() {
        std::vector<BrokerId> ids;
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& [id, broker] : brokers_) {
            ids.push_back(id);
        }
        
        return ids;
    }

private:
    std::mutex mutex_;
    std::map<BrokerId, std::shared_ptr<IUnifiedBroker>> brokers_;
    std::shared_ptr<IUnifiedBroker> primary_broker_;
};

/**
 * @brief Create broker from environment
 */
inline std::shared_ptr<IUnifiedBroker> create_broker_from_env(BrokerId id) {
    switch (id) {
        case BrokerId::Alpaca: {
            AlpacaBroker::Config config;
            if (const char* key = std::getenv("APCA_API_KEY_ID")) {
                config.api_key = key;
            }
            if (const char* secret = std::getenv("APCA_API_SECRET_KEY")) {
                config.api_secret = secret;
            }
            if (const char* url = std::getenv("APCA_API_BASE_URL")) {
                config.paper = std::string(url).find("paper") != std::string::npos;
            }
            
            if (!config.api_key.empty() && !config.api_secret.empty()) {
                return std::make_shared<AlpacaBroker>(config);
            }
            break;
        }
        
        case BrokerId::Tradier: {
            TradierBroker::Config config;
            if (const char* token = std::getenv("TRADIER_ACCESS_TOKEN")) {
                config.access_token = token;
            }
            if (const char* acc = std::getenv("TRADIER_ACCOUNT_ID")) {
                config.account_id = acc;
            }
            if (const char* sandbox = std::getenv("TRADIER_SANDBOX")) {
                config.sandbox = std::string(sandbox) == "true";
            }
            
            if (!config.access_token.empty() && !config.account_id.empty()) {
                return std::make_shared<TradierBroker>(config);
            }
            break;
        }
        
        case BrokerId::InteractiveBrokers:
            // IBKR requires separate client - see ibkr_client.hpp
            // Use create_ibkr_broker_from_env() from ibkr_client.hpp
            break;
            
        case BrokerId::TDAmeritrade:
            // TDA requires OAuth setup - see tda_client.hpp
            // Use create_tda_broker_from_env() from tda_client.hpp
            break;
            
        case BrokerId::Webull:
            // Webull requires login - see webull_client.hpp
            // Use create_webull_broker_from_env() from webull_client.hpp
            break;
        
        default:
            break;
    }
    
    return nullptr;
}

/**
 * @brief Get all available broker IDs
 */
inline std::vector<BrokerId> get_available_brokers() {
    return {
        BrokerId::Alpaca,
        BrokerId::InteractiveBrokers,
        BrokerId::TDAmeritrade,
        BrokerId::Tradier,
        BrokerId::Webull,
        BrokerId::Simulation
    };
}

} // namespace genie::trading

#endif // GENIE_TRADING_BROKER_ABSTRACTION_HPP
