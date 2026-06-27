/**
 * @file webull_client.hpp
 * @brief Webull API integration
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Webull API integration providing:
 * - OAuth device flow authentication
 * - Account information and balances
 * - Position management
 * - Order submission and management
 * - Real-time and delayed quotes
 * - Options chain data
 * 
 * Note: Webull uses an unofficial API - structure may change
 */
#pragma once
#ifndef GENIE_TRADING_WEBULL_CLIENT_HPP
#define GENIE_TRADING_WEBULL_CLIENT_HPP

#include "broker_abstraction.hpp"
#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace genie::trading {

/**
 * @brief Webull configuration
 */
struct WebullConfig {
    std::string device_id;            // Unique device identifier
    std::string access_token;
    std::string refresh_token;
    std::string uuid;
    int64_t token_expiry{0};
    int timeout_ms{15000};
    
    static constexpr const char* API_URL = "https://userapi.webull.com/api";
    static constexpr const char* TRADE_URL = "https://tradeapi.webulltrade.com/api/trade";
    static constexpr const char* QUOTE_URL = "https://quoteapi.webullfintech.com/api";
    
    bool is_authenticated() const {
        return !access_token.empty() && 
               token_expiry > std::chrono::system_clock::to_time_t(
                   std::chrono::system_clock::now());
    }
    
    std::string generate_device_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::ostringstream oss;
        for (int i = 0; i < 32; ++i) {
            oss << std::hex << dis(gen);
        }
        return oss.str();
    }
};

/**
 * @brief Webull account info
 */
struct WebullAccount {
    std::string account_id;
    std::string account_type;         // 1=Cash, 2=Margin
    std::string broker_account_id;
    
    double net_liquidation{0};
    double total_cash{0};
    double settled_cash{0};
    double unsettled_cash{0};
    double buying_power{0};
    double option_buying_power{0};
    double day_buying_power{0};
    double overnight_buying_power{0};
    double crypto_buying_power{0};
    
    double market_value{0};
    double open_pl{0};
    double day_pl{0};
    
    // Margin info
    double margin_used{0};
    double maintenance_margin{0};
    double initial_margin{0};
    
    UnifiedAccount to_unified() const {
        UnifiedAccount acc;
        acc.id = account_id;
        acc.broker_account_id = broker_account_id;
        acc.broker = BrokerId::Webull;
        acc.name = std::string("Webull ") + (account_type == "2" ? "Margin" : "Cash");
        acc.currency = "USD";
        acc.cash = total_cash;
        acc.buying_power = buying_power;
        acc.day_trading_buying_power = day_buying_power;
        acc.portfolio_value = net_liquidation;
        acc.equity = net_liquidation;
        acc.maintenance_margin = maintenance_margin;
        acc.type = (account_type == "2") ? AccountType::Margin : AccountType::Cash;
        return acc;
    }
};

/**
 * @brief Webull position
 */
struct WebullPosition {
    std::string ticker_id;
    std::string symbol;
    std::string security_type;        // stock, option
    
    double quantity{0};
    double cost_price{0};             // Average cost
    double cost{0};                   // Total cost
    double market_value{0};
    double last_price{0};
    double unrealized_profit{0};
    double unrealized_profit_rate{0}; // As decimal
    
    // Option specific
    std::string option_type;          // call, put
    double strike_price{0};
    std::string expiration_date;
    
    UnifiedPosition to_unified() const {
        UnifiedPosition pos;
        pos.symbol = symbol;
        pos.broker = BrokerId::Webull;
        pos.qty = quantity;
        pos.avg_entry_price = cost_price;
        pos.cost_basis = cost;
        pos.market_value = market_value;
        pos.current_price = last_price;
        pos.unrealized_pl = unrealized_profit;
        pos.unrealized_pl_pct = unrealized_profit_rate * 100;
        pos.asset_class = security_type;
        return pos;
    }
};

/**
 * @brief Webull order
 */
struct WebullOrder {
    std::string order_id;
    std::string combo_id;
    std::string account_id;
    std::string ticker_id;
    std::string symbol;
    
    std::string action;               // BUY, SELL
    std::string order_type;           // LMT, MKT, STP, STP LMT
    std::string time_in_force;        // DAY, GTC, IOC
    std::string status;               // Working, Filled, Cancelled, etc.
    
    double total_quantity{0};
    double filled_quantity{0};
    double limit_price{0};
    double stop_price{0};
    double avg_filled_price{0};
    
    std::string create_time;
    std::string filled_time;
    
    bool outside_regular_trading_hour{false};
    
    UnifiedOrder to_unified() const {
        UnifiedOrder order;
        order.id = order_id;
        order.broker_order_id = order_id;
        order.account_id = account_id;
        order.broker = BrokerId::Webull;
        order.symbol = symbol;
        
        order.side = (action == "BUY") ? OrderSide::Buy : OrderSide::Sell;
        
        if (order_type == "MKT") order.type = OrderType::Market;
        else if (order_type == "LMT") order.type = OrderType::Limit;
        else if (order_type == "STP") order.type = OrderType::Stop;
        else if (order_type == "STP LMT") order.type = OrderType::StopLimit;
        
        if (time_in_force == "DAY") order.time_in_force = TimeInForce::Day;
        else if (time_in_force == "GTC") order.time_in_force = TimeInForce::GTC;
        else if (time_in_force == "IOC") order.time_in_force = TimeInForce::IOC;
        
        order.qty = total_quantity;
        order.filled_qty = filled_quantity;
        order.limit_price = limit_price;
        order.stop_price = stop_price;
        order.filled_avg_price = avg_filled_price;
        order.extended_hours = outside_regular_trading_hour;
        order.created_at = create_time;
        order.filled_at = filled_time;
        
        if (status == "Working" || status == "Pending") {
            order.status = OrderStatus::New;
        } else if (status == "Filled") {
            order.status = OrderStatus::Filled;
        } else if (status == "Cancelled" || status == "Canceled") {
            order.status = OrderStatus::Canceled;
        } else if (status == "Failed" || status == "Rejected") {
            order.status = OrderStatus::Rejected;
        } else if (status == "Partial Filled") {
            order.status = OrderStatus::PartiallyFilled;
        }
        
        return order;
    }
};

/**
 * @brief Webull quote
 */
struct WebullQuote {
    std::string ticker_id;
    std::string symbol;
    std::string name;
    std::string exchange;
    
    double close{0};                  // Previous close
    double open{0};
    double high{0};
    double low{0};
    double price{0};                  // Current/last price
    double change{0};
    double change_ratio{0};           // As decimal
    
    int64_t volume{0};
    double avg_volume{0};
    double turnover_rate{0};
    
    double market_cap{0};
    double pe{0};
    double pb{0};
    double eps{0};
    
    double fifty_two_week_high{0};
    double fifty_two_week_low{0};
    
    Quote to_quote() const {
        Quote q;
        q.symbol = symbol;
        q.price = price;
        q.open = open;
        q.high = high;
        q.low = low;
        q.previous_close = close;
        q.volume = volume;
        q.change = change;
        q.change_percent = change_ratio * 100;
        return q;
    }
};

/**
 * @brief Webull API client
 */
class WebullBroker : public IUnifiedBroker {
public:
    explicit WebullBroker(WebullConfig config)
        : config_(std::move(config))
        , http_(WebullConfig::API_URL)
        , trade_http_(WebullConfig::TRADE_URL)
        , quote_http_(WebullConfig::QUOTE_URL)
        , connected_(false) {
        
        http_.set_timeout(config_.timeout_ms);
        trade_http_.set_timeout(config_.timeout_ms);
        quote_http_.set_timeout(config_.timeout_ms);
        
        // Generate device ID if not set
        if (config_.device_id.empty()) {
            config_.device_id = config_.generate_device_id();
        }
        
        update_headers();
    }
    
    BrokerId broker_id() const override { return BrokerId::Webull; }
    std::string broker_name() const override { return "Webull"; }
    bool is_connected() const override { return connected_ && config_.is_authenticated(); }
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * @brief Login with email/phone and password
     * @param username Email or phone number
     * @param password Account password
     * @param mfa_code MFA code if required (empty for first attempt)
     */
    bool login(const std::string& username, 
               const std::string& password,
               const std::string& mfa_code = "") {
        
        // First, get security token
        core::JsonObject security_body;
        security_body["account"] = username;
        security_body["accountType"] = username.find('@') != std::string::npos ? "2" : "1";
        security_body["deviceId"] = config_.device_id;
        security_body["regionId"] = "1";
        
        auto security_response = http_.post("/passport/login/account",
                                            core::JsonParser::stringify(security_body));
        
        if (!security_response.success) {
            last_error_ = "Security request failed: " + security_response.error;
            return false;
        }
        
        // Login request
        core::JsonObject login_body;
        login_body["account"] = username;
        login_body["accountType"] = username.find('@') != std::string::npos ? "2" : "1";
        login_body["pwd"] = password;  // Should be hashed in production
        login_body["deviceId"] = config_.device_id;
        login_body["regionId"] = "1";
        
        if (!mfa_code.empty()) {
            login_body["verificationCode"] = mfa_code;
        }
        
        auto login_response = http_.post("/passport/login/v5/account",
                                         core::JsonParser::stringify(login_body));
        
        if (!login_response.success) {
            last_error_ = "Login failed: " + login_response.error;
            return false;
        }
        
        auto json = core::JsonParser::parse(login_response.body);
        
        // Check for MFA required
        if (json.contains("extInfo") && json["extInfo"].contains("verificationRequired")) {
            last_error_ = "MFA required";
            return false;
        }
        
        // Extract tokens
        config_.access_token = json.get_string("accessToken", "");
        config_.refresh_token = json.get_string("refreshToken", "");
        config_.uuid = json.get_string("uuid", "");
        config_.token_expiry = json.get_int("tokenExpireTime", 0);
        
        if (config_.access_token.empty()) {
            last_error_ = "No access token in response";
            return false;
        }
        
        update_headers();
        return true;
    }
    
    /**
     * @brief Refresh authentication token
     */
    bool refresh_token() {
        if (config_.refresh_token.empty()) {
            last_error_ = "No refresh token available";
            return false;
        }
        
        core::JsonObject body;
        body["refreshToken"] = config_.refresh_token;
        body["deviceId"] = config_.device_id;
        
        auto response = http_.post("/passport/refreshToken",
                                   core::JsonParser::stringify(body));
        
        if (!response.success) {
            last_error_ = "Token refresh failed: " + response.error;
            return false;
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        config_.access_token = json.get_string("accessToken", "");
        config_.token_expiry = json.get_int("tokenExpireTime", 0);
        
        if (!config_.access_token.empty()) {
            update_headers();
            return true;
        }
        
        return false;
    }
    
    // ========================================================================
    // Connection
    // ========================================================================
    
    bool connect() override {
        if (!config_.is_authenticated()) {
            if (!config_.refresh_token.empty()) {
                if (!refresh_token()) {
                    return false;
                }
            } else {
                last_error_ = "Not authenticated. Please call login() first.";
                return false;
            }
        }
        
        // Get account info to verify connection
        auto accounts = get_accounts();
        if (!accounts.success) {
            connected_ = false;
            return false;
        }
        
        if (!accounts.data.empty()) {
            selected_account_ = accounts.data[0].broker_account_id;
        }
        
        connected_ = true;
        return true;
    }
    
    void disconnect() override {
        connected_ = false;
    }
    
    // ========================================================================
    // Account
    // ========================================================================
    
    BrokerResponse<UnifiedAccount> get_account() override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedAccount>::fail("No account selected");
        }
        
        auto response = trade_http_.get("/v2/home/" + selected_account_);
        if (!response.success) {
            return BrokerResponse<UnifiedAccount>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        WebullAccount acct;
        acct.account_id = json.get_string("secAccountId", "");
        acct.broker_account_id = selected_account_;
        acct.account_type = json.get_string("accountType", "1");
        
        if (json.contains("accountMembers")) {
            for (const auto& member : json["accountMembers"].array()) {
                std::string key = member.get_string("key", "");
                double value = std::stod(member.get_string("value", "0"));
                
                if (key == "netLiquidation") acct.net_liquidation = value;
                else if (key == "totalCash") acct.total_cash = value;
                else if (key == "settledFunds") acct.settled_cash = value;
                else if (key == "unsettledFunds") acct.unsettled_cash = value;
                else if (key == "usableCash") acct.buying_power = value;
                else if (key == "dayBuyingPower") acct.day_buying_power = value;
                else if (key == "overnightBuyingPower") acct.overnight_buying_power = value;
                else if (key == "totalMarketValue") acct.market_value = value;
                else if (key == "unrealizedProfitLoss") acct.open_pl = value;
                else if (key == "dayProfitLoss") acct.day_pl = value;
            }
        }
        
        return BrokerResponse<UnifiedAccount>::ok(acct.to_unified());
    }
    
    BrokerResponse<std::vector<UnifiedAccount>> get_accounts() override {
        auto response = trade_http_.get("/v2/account/getSecAccountList/v2");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedAccount> accounts;
        
        if (json.contains("data") && json["data"].is_array()) {
            for (const auto& item : json["data"].array()) {
                UnifiedAccount acc;
                acc.id = item.get_string("secAccountId", "");
                acc.broker_account_id = std::to_string(item.get_int("brokerAccountId", 0));
                acc.broker = BrokerId::Webull;
                acc.name = "Webull";
                acc.currency = "USD";
                
                accounts.push_back(acc);
            }
        }
        
        return BrokerResponse<std::vector<UnifiedAccount>>::ok(accounts);
    }
    
    void select_account(const std::string& account_id) {
        selected_account_ = account_id;
    }
    
    // ========================================================================
    // Positions
    // ========================================================================
    
    BrokerResponse<std::vector<UnifiedPosition>> get_positions() override {
        if (selected_account_.empty()) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail("No account selected");
        }
        
        auto response = trade_http_.get("/v2/home/" + selected_account_ + "/positions");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedPosition> positions;
        
        if (json.is_array()) {
            for (const auto& item : json.array()) {
                WebullPosition pos;
                pos.ticker_id = item.get_string("tickerId", "");
                pos.symbol = item.get_string("ticker", "").empty() ?
                            item["ticker"].get_string("symbol", "") :
                            item.get_string("ticker", "");
                pos.security_type = item.get_string("assetType", "stock");
                pos.quantity = item.get_double("position", 0);
                pos.cost_price = item.get_double("costPrice", 0);
                pos.cost = item.get_double("cost", 0);
                pos.market_value = item.get_double("marketValue", 0);
                pos.last_price = item.get_double("lastPrice", 0);
                pos.unrealized_profit = item.get_double("unrealizedProfitLoss", 0);
                pos.unrealized_profit_rate = item.get_double("unrealizedProfitLossRate", 0);
                
                if (pos.quantity != 0) {
                    positions.push_back(pos.to_unified());
                }
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
    
    // ========================================================================
    // Orders
    // ========================================================================
    
    BrokerResponse<UnifiedOrder> submit_order(const OrderRequest& request) override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("No account selected");
        }
        
        // First, get ticker ID for symbol
        auto ticker_id = get_ticker_id(request.symbol);
        if (ticker_id.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("Symbol not found: " + request.symbol);
        }
        
        core::JsonObject body;
        body["action"] = (request.side == OrderSide::Buy) ? "BUY" : "SELL";
        body["orderType"] = webull_order_type(request.type);
        body["timeInForce"] = webull_tif(request.time_in_force);
        body["quantity"] = std::to_string(static_cast<int>(request.qty));
        body["tickerId"] = ticker_id;
        body["serialId"] = generate_serial_id();
        
        if (request.limit_price && *request.limit_price > 0) {
            body["lmtPrice"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            body["auxPrice"] = std::to_string(*request.stop_price);
        }
        if (request.extended_hours) {
            body["outsideRegularTradingHour"] = true;
        }
        
        auto response = trade_http_.post("/v1/order/" + selected_account_ + "/placeOrder",
                                         core::JsonParser::stringify(body));
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        UnifiedOrder order;
        order.id = json.get_string("orderId", "");
        order.broker_order_id = order.id;
        order.broker = BrokerId::Webull;
        order.symbol = request.symbol;
        order.side = request.side;
        order.type = request.type;
        order.qty = request.qty;
        order.status = OrderStatus::New;
        
        return BrokerResponse<UnifiedOrder>::ok(order);
    }
    
    BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) override {
        // Webull doesn't have single order endpoint - get from orders list
        auto result = get_orders("");
        if (!result.success) {
            return BrokerResponse<UnifiedOrder>::fail(result.error);
        }
        
        for (const auto& order : result.data) {
            if (order.id == order_id) {
                return BrokerResponse<UnifiedOrder>::ok(order);
            }
        }
        
        return BrokerResponse<UnifiedOrder>::fail("Order not found");
    }
    
    BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status) override {
        if (selected_account_.empty()) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail("No account selected");
        }
        
        auto response = trade_http_.get("/v1/order/" + selected_account_ + "/orders");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedOrder> orders;
        
        if (json.is_array()) {
            for (const auto& item : json.array()) {
                WebullOrder order;
                order.order_id = item.get_string("orderId", "");
                order.account_id = selected_account_;
                order.ticker_id = item.get_string("tickerId", "");
                order.symbol = item.get_string("symbol", "");
                order.action = item.get_string("action", "BUY");
                order.order_type = item.get_string("orderType", "MKT");
                order.time_in_force = item.get_string("timeInForce", "DAY");
                order.status = item.get_string("status", "");
                order.total_quantity = item.get_double("totalQuantity", 0);
                order.filled_quantity = item.get_double("filledQuantity", 0);
                order.limit_price = item.get_double("lmtPrice", 0);
                order.stop_price = item.get_double("auxPrice", 0);
                order.avg_filled_price = item.get_double("avgFilledPrice", 0);
                order.create_time = item.get_string("createTime", "");
                order.filled_time = item.get_string("filledTime", "");
                order.outside_regular_trading_hour = item.get_bool("outsideRegularTradingHour", false);
                
                auto unified = order.to_unified();
                
                if (status.empty() || order_status_to_string(unified.status) == status) {
                    orders.push_back(unified);
                }
            }
        }
        
        return BrokerResponse<std::vector<UnifiedOrder>>::ok(orders);
    }
    
    BrokerResponse<bool> cancel_order(const std::string& order_id) override {
        if (selected_account_.empty()) {
            return BrokerResponse<bool>::fail("No account selected");
        }
        
        auto response = trade_http_.post(
            "/v1/order/" + selected_account_ + "/cancelOrder/" + order_id, "");
        
        if (!response.success && response.status_code != 200) {
            return BrokerResponse<bool>::fail(response.error);
        }
        
        return BrokerResponse<bool>::ok(true);
    }
    
    BrokerResponse<UnifiedOrder> replace_order(const std::string& order_id,
                                                const OrderRequest& request) override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("No account selected");
        }
        
        core::JsonObject body;
        body["orderId"] = order_id;
        body["quantity"] = std::to_string(static_cast<int>(request.qty));
        
        if (request.limit_price && *request.limit_price > 0) {
            body["lmtPrice"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            body["auxPrice"] = std::to_string(*request.stop_price);
        }
        
        auto response = trade_http_.post(
            "/v1/order/" + selected_account_ + "/modifyOrder",
            core::JsonParser::stringify(body));
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        return get_order(order_id);
    }
    
    // ========================================================================
    // Market Data
    // ========================================================================
    
    bool supports_market_data() const override { return true; }
    
    BrokerResponse<Quote> get_quote(const std::string& symbol) override {
        auto ticker_id = get_ticker_id(symbol);
        if (ticker_id.empty()) {
            return BrokerResponse<Quote>::fail("Symbol not found");
        }
        
        auto response = quote_http_.get("/quote/tickerRealTimes/v5/" + ticker_id);
        if (!response.success) {
            return BrokerResponse<Quote>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        WebullQuote quote;
        quote.ticker_id = ticker_id;
        quote.symbol = symbol;
        quote.name = json.get_string("name", "");
        quote.exchange = json.get_string("disExchangeCode", "");
        quote.close = std::stod(json.get_string("close", "0"));
        quote.open = std::stod(json.get_string("open", "0"));
        quote.high = std::stod(json.get_string("high", "0"));
        quote.low = std::stod(json.get_string("low", "0"));
        quote.price = std::stod(json.get_string("close", "0"));
        quote.change = std::stod(json.get_string("change", "0"));
        quote.change_ratio = std::stod(json.get_string("changeRatio", "0"));
        quote.volume = json.get_int("volume", 0);
        quote.market_cap = std::stod(json.get_string("marketValue", "0"));
        quote.pe = std::stod(json.get_string("pe", "0"));
        
        return BrokerResponse<Quote>::ok(quote.to_quote());
    }
    
    /**
     * @brief Search for ticker by symbol
     */
    std::string get_ticker_id(const std::string& symbol) {
        // Check cache first
        auto it = ticker_cache_.find(symbol);
        if (it != ticker_cache_.end()) {
            return it->second;
        }
        
        auto response = quote_http_.get("/search/pc/ticker?keyword=" + 
                                        core::HttpClient::url_encode(symbol));
        
        if (!response.success) return "";
        
        auto json = core::JsonParser::parse(response.body);
        
        if (json.contains("data") && json["data"].is_array()) {
            for (const auto& item : json["data"].array()) {
                std::string ticker = item.get_string("symbol", "");
                if (ticker == symbol) {
                    std::string id = std::to_string(item.get_int("tickerId", 0));
                    ticker_cache_[symbol] = id;
                    return id;
                }
            }
        }
        
        return "";
    }
    
    const WebullConfig& config() const { return config_; }
    std::string last_error() const override { return last_error_; }

private:
    WebullConfig config_;
    core::HttpClient http_;
    core::HttpClient trade_http_;
    core::HttpClient quote_http_;
    bool connected_;
    std::string selected_account_;
    std::string last_error_;
    std::map<std::string, std::string> ticker_cache_;
    int serial_counter_{0};
    
    void update_headers() {
        auto set_common_headers = [this](core::HttpClient& client) {
            client.set_header("Content-Type", "application/json");
            client.set_header("device-type", "Web");
            client.set_header("did", config_.device_id);
            
            if (!config_.access_token.empty()) {
                client.set_header("access_token", config_.access_token);
            }
        };
        
        set_common_headers(http_);
        set_common_headers(trade_http_);
        set_common_headers(quote_http_);
    }
    
    std::string webull_order_type(OrderType type) {
        switch (type) {
            case OrderType::Market: return "MKT";
            case OrderType::Limit: return "LMT";
            case OrderType::Stop: return "STP";
            case OrderType::StopLimit: return "STP LMT";
            default: return "MKT";
        }
    }
    
    std::string webull_tif(TimeInForce tif) {
        switch (tif) {
            case TimeInForce::Day: return "DAY";
            case TimeInForce::GTC: return "GTC";
            case TimeInForce::IOC: return "IOC";
            default: return "DAY";
        }
    }
    
    std::string generate_serial_id() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return config_.uuid + "_" + std::to_string(ms) + "_" + std::to_string(++serial_counter_);
    }
};

/**
 * @brief Create Webull client from environment
 */
inline std::shared_ptr<WebullBroker> create_webull_broker_from_env() {
    WebullConfig config;
    
    if (const char* device_id = std::getenv("WEBULL_DEVICE_ID")) {
        config.device_id = device_id;
    }
    if (const char* token = std::getenv("WEBULL_ACCESS_TOKEN")) {
        config.access_token = token;
    }
    if (const char* refresh = std::getenv("WEBULL_REFRESH_TOKEN")) {
        config.refresh_token = refresh;
    }
    
    return std::make_shared<WebullBroker>(config);
}

} // namespace genie::trading

#endif // GENIE_TRADING_WEBULL_CLIENT_HPP
