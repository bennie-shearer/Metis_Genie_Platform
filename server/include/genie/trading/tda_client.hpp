/**
 * @file tda_client.hpp
 * @brief TD Ameritrade / Charles Schwab API integration with OAuth
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TD Ameritrade API integration (now Schwab) providing:
 * - OAuth 2.0 authentication flow
 * - Account information and balances
 * - Position management
 * - Order submission and management
 * - Real-time and delayed quotes
 * - Option chains
 * - Market hours
 * 
 * API Documentation: https://developer.tdameritrade.com/
 * Note: TD Ameritrade APIs transitioning to Schwab
 */
#pragma once
#ifndef GENIE_TRADING_TDA_CLIENT_HPP
#define GENIE_TRADING_TDA_CLIENT_HPP

#include "broker_abstraction.hpp"
#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <fstream>

namespace genie::trading {

/**
 * @brief TDA OAuth tokens
 */
struct TDATokens {
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
    int expires_in{0};                // Seconds
    int refresh_expires_in{0};
    std::string scope;
    
    std::chrono::system_clock::time_point access_expiry;
    std::chrono::system_clock::time_point refresh_expiry;
    
    bool is_access_expired() const {
        return std::chrono::system_clock::now() >= access_expiry;
    }
    
    bool is_refresh_expired() const {
        return std::chrono::system_clock::now() >= refresh_expiry;
    }
    
    bool is_valid() const {
        return !access_token.empty() && !is_access_expired();
    }
};

/**
 * @brief TDA configuration
 */
struct TDAConfig {
    std::string client_id;            // OAuth client ID (API key)
    std::string redirect_uri;         // OAuth redirect URI
    std::string token_file;           // File to persist tokens
    int timeout_ms{15000};
    
    static constexpr const char* AUTH_URL = "https://auth.tdameritrade.com/auth";
    static constexpr const char* TOKEN_URL = "https://api.tdameritrade.com/v1/oauth2/token";
    static constexpr const char* API_URL = "https://api.tdameritrade.com/v1";
    
    bool is_valid() const {
        return !client_id.empty();
    }
    
    std::string get_auth_url(const std::string& state = "") const {
        std::ostringstream url;
        url << AUTH_URL << "?"
            << "response_type=code"
            << "&redirect_uri=" << core::HttpClient::url_encode(redirect_uri)
            << "&client_id=" << core::HttpClient::url_encode(client_id + "@AMER.OAUTHAP");
        
        if (!state.empty()) {
            url << "&state=" << core::HttpClient::url_encode(state);
        }
        
        return url.str();
    }
};

/**
 * @brief TDA account balance
 */
struct TDABalance {
    double cash_available{0};
    double cash_balance{0};
    double buying_power{0};
    double day_trading_buying_power{0};
    double equity{0};
    double long_market_value{0};
    double short_market_value{0};
    double maintenance_requirement{0};
    double margin_balance{0};
    double money_market_fund{0};
    double pending_deposits{0};
    double liquidation_value{0};
};

/**
 * @brief TDA position
 */
struct TDAPosition {
    std::string symbol;
    std::string asset_type;           // EQUITY, OPTION, etc.
    std::string cusip;
    
    double long_quantity{0};
    double short_quantity{0};
    double average_price{0};
    double current_day_cost{0};
    double current_day_profit_loss{0};
    double current_day_profit_loss_pct{0};
    double market_value{0};
    double maintenance_requirement{0};
    double settled_long_quantity{0};
    double settled_short_quantity{0};
    
    // Option specific
    std::string underlying_symbol;
    std::string put_call;             // PUT, CALL
    double strike_price{0};
    std::string expiration_date;
    
    double quantity() const {
        return long_quantity - short_quantity;
    }
    
    UnifiedPosition to_unified() const {
        UnifiedPosition pos;
        pos.symbol = symbol;
        pos.broker = BrokerId::TDAmeritrade;
        pos.qty = quantity();
        pos.avg_entry_price = average_price;
        pos.market_value = market_value;
        pos.cost_basis = quantity() * average_price;
        pos.unrealized_pl = current_day_profit_loss;
        pos.unrealized_pl_pct = current_day_profit_loss_pct;
        pos.asset_class = asset_type;
        return pos;
    }
};

/**
 * @brief TDA order
 */
struct TDAOrder {
    std::string order_id;
    std::string account_id;
    std::string status;               // AWAITING_PARENT_ORDER, WORKING, FILLED, etc.
    std::string entered_time;
    std::string close_time;
    
    // Order details
    std::string order_type;           // MARKET, LIMIT, STOP, STOP_LIMIT
    std::string session;              // NORMAL, AM, PM, SEAMLESS
    std::string duration;             // DAY, GTC, FILL_OR_KILL
    std::string complex_order_strategy;
    
    double quantity{0};
    double filled_quantity{0};
    double remaining_quantity{0};
    double price{0};                  // Limit price
    double stop_price{0};
    
    // Leg details (for single orders)
    std::string symbol;
    std::string instruction;          // BUY, SELL, BUY_TO_COVER, SELL_SHORT
    std::string asset_type;
    
    // Fill info
    double average_fill_price{0};
    
    UnifiedOrder to_unified() const {
        UnifiedOrder order;
        order.id = order_id;
        order.broker_order_id = order_id;
        order.account_id = account_id;
        order.broker = BrokerId::TDAmeritrade;
        order.symbol = symbol;
        
        if (instruction == "BUY" || instruction == "BUY_TO_COVER") {
            order.side = OrderSide::Buy;
        } else {
            order.side = OrderSide::Sell;
        }
        
        if (order_type == "MARKET") order.type = OrderType::Market;
        else if (order_type == "LIMIT") order.type = OrderType::Limit;
        else if (order_type == "STOP") order.type = OrderType::Stop;
        else if (order_type == "STOP_LIMIT") order.type = OrderType::StopLimit;
        
        if (duration == "DAY") order.time_in_force = TimeInForce::Day;
        else if (duration == "GTC") order.time_in_force = TimeInForce::GTC;
        else if (duration == "FILL_OR_KILL") order.time_in_force = TimeInForce::FOK;
        
        order.qty = quantity;
        order.filled_qty = filled_quantity;
        order.limit_price = price;
        order.stop_price = stop_price;
        order.filled_avg_price = average_fill_price;
        order.created_at = entered_time;
        order.extended_hours = (session == "AM" || session == "PM");
        
        if (status == "AWAITING_PARENT_ORDER" || status == "AWAITING_CONDITION" ||
            status == "AWAITING_MANUAL_REVIEW" || status == "PENDING_ACTIVATION") {
            order.status = OrderStatus::PendingNew;
        } else if (status == "ACCEPTED" || status == "QUEUED" || status == "WORKING") {
            order.status = OrderStatus::New;
        } else if (status == "FILLED") {
            order.status = OrderStatus::Filled;
        } else if (status == "CANCELED" || status == "EXPIRED") {
            order.status = OrderStatus::Canceled;
        } else if (status == "REJECTED") {
            order.status = OrderStatus::Rejected;
        } else if (status == "PENDING_CANCEL" || status == "PENDING_REPLACE") {
            order.status = OrderStatus::PendingCancel;
        }
        
        return order;
    }
};

/**
 * @brief TDA quote
 */
struct TDAQuote {
    std::string symbol;
    std::string description;
    std::string exchange;
    std::string asset_type;
    
    double bid_price{0};
    double ask_price{0};
    double last_price{0};
    int64_t bid_size{0};
    int64_t ask_size{0};
    int64_t total_volume{0};
    
    double open_price{0};
    double high_price{0};
    double low_price{0};
    double close_price{0};
    double net_change{0};
    double net_change_pct{0};
    
    double week_52_high{0};
    double week_52_low{0};
    double pe_ratio{0};
    double div_amount{0};
    double div_yield{0};
    
    int64_t quote_time{0};
    int64_t trade_time{0};
    bool realtime{true};
    
    Quote to_quote() const {
        Quote q;
        q.symbol = symbol;
        q.price = last_price;
        q.bid = bid_price;
        q.ask = ask_price;
        q.bid_size = static_cast<int>(bid_size);
        q.ask_size = static_cast<int>(ask_size);
        q.volume = total_volume;
        q.open = open_price;
        q.high = high_price;
        q.low = low_price;
        q.previous_close = close_price;
        q.change = net_change;
        q.change_percent = net_change_pct;
        return q;
    }
};

/**
 * @brief TD Ameritrade API client with OAuth
 */
class TDABroker : public IUnifiedBroker {
public:
    explicit TDABroker(const TDAConfig& config)
        : config_(config)
        , http_(TDAConfig::API_URL)
        , connected_(false) {
        
        http_.set_timeout(config.timeout_ms);
        
        // Try to load saved tokens
        load_tokens();
    }
    
    BrokerId broker_id() const override { return BrokerId::TDAmeritrade; }
    std::string broker_name() const override { return "TD Ameritrade"; }
    bool is_connected() const override { return connected_ && tokens_.is_valid(); }
    
    // ========================================================================
    // OAuth Flow
    // ========================================================================
    
    /**
     * @brief Get authorization URL for user to visit
     */
    std::string get_authorization_url(const std::string& state = "") const {
        return config_.get_auth_url(state);
    }
    
    /**
     * @brief Exchange authorization code for tokens
     */
    bool exchange_code(const std::string& code) {
        core::HttpClient token_http(TDAConfig::TOKEN_URL);
        
        std::ostringstream body;
        body << "grant_type=authorization_code"
             << "&code=" << core::HttpClient::url_encode(code)
             << "&access_type=offline"
             << "&client_id=" << core::HttpClient::url_encode(config_.client_id + "@AMER.OAUTHAP")
             << "&redirect_uri=" << core::HttpClient::url_encode(config_.redirect_uri);
        
        auto response = token_http.post("", body.str(),
                                        "application/x-www-form-urlencoded");
        
        if (!response.success || response.status_code != 200) {
            last_error_ = "Token exchange failed: " + response.error;
            return false;
        }
        
        return parse_token_response(response.body);
    }
    
    /**
     * @brief Refresh access token
     */
    bool refresh_tokens() {
        if (tokens_.refresh_token.empty()) {
            last_error_ = "No refresh token available";
            return false;
        }
        
        core::HttpClient token_http(TDAConfig::TOKEN_URL);
        
        std::ostringstream body;
        body << "grant_type=refresh_token"
             << "&refresh_token=" << core::HttpClient::url_encode(tokens_.refresh_token)
             << "&client_id=" << core::HttpClient::url_encode(config_.client_id + "@AMER.OAUTHAP");
        
        auto response = token_http.post("", body.str(),
                                        "application/x-www-form-urlencoded");
        
        if (!response.success || response.status_code != 200) {
            last_error_ = "Token refresh failed: " + response.error;
            return false;
        }
        
        return parse_token_response(response.body);
    }
    
    /**
     * @brief Set tokens directly (from stored values)
     */
    void set_tokens(const TDATokens& tokens) {
        tokens_ = tokens;
        update_auth_header();
    }
    
    // ========================================================================
    // Connection
    // ========================================================================
    
    bool connect() override {
        // Check if we have valid tokens
        if (!tokens_.is_valid()) {
            if (!tokens_.refresh_token.empty() && !tokens_.is_refresh_expired()) {
                if (!refresh_tokens()) {
                    return false;
                }
            } else {
                last_error_ = "No valid tokens. Please authorize at: " + 
                             get_authorization_url();
                return false;
            }
        }
        
        // Test connection with account list
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
        
        ensure_valid_token();
        
        auto response = http_.get("/accounts/" + selected_account_ + 
                                  "?fields=positions,orders");
        
        if (!response.success) {
            return BrokerResponse<UnifiedAccount>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains("securitiesAccount")) {
            return BrokerResponse<UnifiedAccount>::fail("Invalid response");
        }
        
        const auto& acct = json["securitiesAccount"];
        
        UnifiedAccount account;
        account.id = acct.get_string("accountId", "");
        account.broker_account_id = account.id;
        account.broker = BrokerId::TDAmeritrade;
        account.name = "TD Ameritrade " + acct.get_string("type", "");
        account.currency = "USD";
        
        std::string type = acct.get_string("type", "");
        if (type == "CASH") account.type = AccountType::Cash;
        else if (type == "MARGIN") account.type = AccountType::Margin;
        
        if (acct.contains("currentBalances")) {
            const auto& bal = acct["currentBalances"];
            account.cash = bal.get_double("cashBalance", 0) +
                          bal.get_double("moneyMarketFund", 0);
            account.buying_power = bal.get_double("buyingPower", 0);
            account.day_trading_buying_power = bal.get_double("dayTradingBuyingPower", 0);
            account.equity = bal.get_double("equity", 0);
            account.portfolio_value = bal.get_double("liquidationValue", 0);
            account.maintenance_margin = bal.get_double("maintenanceRequirement", 0);
        }
        
        return BrokerResponse<UnifiedAccount>::ok(account);
    }
    
    BrokerResponse<std::vector<UnifiedAccount>> get_accounts() override {
        ensure_valid_token();
        
        auto response = http_.get("/accounts");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array()) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail("Invalid response");
        }
        
        std::vector<UnifiedAccount> accounts;
        for (const auto& item : json.array()) {
            if (!item.contains("securitiesAccount")) continue;
            
            const auto& acct = item["securitiesAccount"];
            
            UnifiedAccount acc;
            acc.id = acct.get_string("accountId", "");
            acc.broker_account_id = acc.id;
            acc.broker = BrokerId::TDAmeritrade;
            acc.name = "TD Ameritrade";
            acc.currency = "USD";
            
            accounts.push_back(acc);
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
        
        ensure_valid_token();
        
        auto response = http_.get("/accounts/" + selected_account_ + "?fields=positions");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedPosition> positions;
        
        if (json.contains("securitiesAccount") &&
            json["securitiesAccount"].contains("positions")) {
            
            for (const auto& item : json["securitiesAccount"]["positions"].array()) {
                TDAPosition pos;
                pos.long_quantity = item.get_double("longQuantity", 0);
                pos.short_quantity = item.get_double("shortQuantity", 0);
                pos.average_price = item.get_double("averagePrice", 0);
                pos.market_value = item.get_double("marketValue", 0);
                pos.current_day_profit_loss = item.get_double("currentDayProfitLoss", 0);
                pos.current_day_profit_loss_pct = item.get_double("currentDayProfitLossPercentage", 0);
                pos.maintenance_requirement = item.get_double("maintenanceRequirement", 0);
                
                if (item.contains("instrument")) {
                    const auto& inst = item["instrument"];
                    pos.symbol = inst.get_string("symbol", "");
                    pos.asset_type = inst.get_string("assetType", "EQUITY");
                    pos.cusip = inst.get_string("cusip", "");
                    
                    if (pos.asset_type == "OPTION") {
                        pos.underlying_symbol = inst.get_string("underlyingSymbol", "");
                        pos.put_call = inst.get_string("putCall", "");
                    }
                }
                
                if (pos.quantity() != 0) {
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
        
        ensure_valid_token();
        
        // Build order JSON
        core::JsonObject order;
        order["orderType"] = tda_order_type(request.type);
        order["session"] = request.extended_hours ? "SEAMLESS" : "NORMAL";
        order["duration"] = tda_duration(request.time_in_force);
        order["orderStrategyType"] = "SINGLE";
        
        if (request.limit_price && *request.limit_price > 0) {
            order["price"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            order["stopPrice"] = std::to_string(*request.stop_price);
        }
        
        // Order leg
        core::JsonObject leg;
        leg["instruction"] = (request.side == OrderSide::Buy) ? "BUY" : "SELL";
        leg["quantity"] = static_cast<int>(request.qty);
        
        core::JsonObject instrument;
        instrument["symbol"] = request.symbol;
        instrument["assetType"] = "EQUITY";
        leg["instrument"] = core::JsonParser::stringify(instrument);
        
        order["orderLegCollection"] = "[" + core::JsonParser::stringify(leg) + "]";
        
        auto response = http_.post("/accounts/" + selected_account_ + "/orders",
                                   core::JsonParser::stringify(order));
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        // TDA returns 201 with Location header containing order ID
        UnifiedOrder result_order;
        result_order.broker = BrokerId::TDAmeritrade;
        result_order.symbol = request.symbol;
        result_order.side = request.side;
        result_order.type = request.type;
        result_order.qty = request.qty;
        result_order.status = OrderStatus::New;
        
        // Extract order ID from Location header
        auto location_it = response.headers.find("Location");
        if (location_it != response.headers.end()) {
            auto pos = location_it->second.rfind('/');
            if (pos != std::string::npos) {
                result_order.id = location_it->second.substr(pos + 1);
                result_order.broker_order_id = result_order.id;
            }
        }
        
        return BrokerResponse<UnifiedOrder>::ok(result_order);
    }
    
    BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("No account selected");
        }
        
        ensure_valid_token();
        
        auto response = http_.get("/accounts/" + selected_account_ + "/orders/" + order_id);
        if (!response.success) {
            return BrokerResponse<UnifiedOrder>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        return BrokerResponse<UnifiedOrder>::ok(parse_order(json));
    }
    
    BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status) override {
        if (selected_account_.empty()) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail("No account selected");
        }
        
        ensure_valid_token();
        
        std::string url = "/accounts/" + selected_account_ + "/orders";
        if (!status.empty()) {
            url += "?status=" + status;
        }
        
        auto response = http_.get(url);
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedOrder> orders;
        
        if (json.is_array()) {
            for (const auto& item : json.array()) {
                orders.push_back(parse_order(item));
            }
        }
        
        return BrokerResponse<std::vector<UnifiedOrder>>::ok(orders);
    }
    
    BrokerResponse<bool> cancel_order(const std::string& order_id) override {
        if (selected_account_.empty()) {
            return BrokerResponse<bool>::fail("No account selected");
        }
        
        ensure_valid_token();
        
        auto response = http_.del("/accounts/" + selected_account_ + "/orders/" + order_id);
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
        
        ensure_valid_token();
        
        // Build replacement order
        core::JsonObject order;
        order["orderType"] = tda_order_type(request.type);
        order["session"] = request.extended_hours ? "SEAMLESS" : "NORMAL";
        order["duration"] = tda_duration(request.time_in_force);
        order["orderStrategyType"] = "SINGLE";
        
        if (request.limit_price && *request.limit_price > 0) {
            order["price"] = std::to_string(*request.limit_price);
        }
        if (request.stop_price && *request.stop_price > 0) {
            order["stopPrice"] = std::to_string(*request.stop_price);
        }
        
        core::JsonObject leg;
        leg["instruction"] = (request.side == OrderSide::Buy) ? "BUY" : "SELL";
        leg["quantity"] = static_cast<int>(request.qty);
        
        core::JsonObject instrument;
        instrument["symbol"] = request.symbol;
        instrument["assetType"] = "EQUITY";
        leg["instrument"] = core::JsonParser::stringify(instrument);
        
        order["orderLegCollection"] = "[" + core::JsonParser::stringify(leg) + "]";
        
        auto response = http_.put("/accounts/" + selected_account_ + "/orders/" + order_id,
                                  core::JsonParser::stringify(order));
        
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
        ensure_valid_token();
        
        auto response = http_.get("/marketdata/" + symbol + "/quotes");
        if (!response.success) {
            return BrokerResponse<Quote>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.contains(symbol)) {
            return BrokerResponse<Quote>::fail("Symbol not found");
        }
        
        const auto& q = json[symbol];
        
        TDAQuote tda_quote;
        tda_quote.symbol = symbol;
        tda_quote.description = q.get_string("description", "");
        tda_quote.exchange = q.get_string("exchangeName", "");
        tda_quote.asset_type = q.get_string("assetType", "EQUITY");
        tda_quote.bid_price = q.get_double("bidPrice", 0);
        tda_quote.ask_price = q.get_double("askPrice", 0);
        tda_quote.last_price = q.get_double("lastPrice", 0);
        tda_quote.bid_size = q.get_int("bidSize", 0);
        tda_quote.ask_size = q.get_int("askSize", 0);
        tda_quote.total_volume = q.get_int("totalVolume", 0);
        tda_quote.open_price = q.get_double("openPrice", 0);
        tda_quote.high_price = q.get_double("highPrice", 0);
        tda_quote.low_price = q.get_double("lowPrice", 0);
        tda_quote.close_price = q.get_double("closePrice", 0);
        tda_quote.net_change = q.get_double("netChange", 0);
        tda_quote.net_change_pct = q.get_double("netPercentChangeInDouble", 0);
        tda_quote.week_52_high = q.get_double("52WkHigh", 0);
        tda_quote.week_52_low = q.get_double("52WkLow", 0);
        tda_quote.pe_ratio = q.get_double("peRatio", 0);
        tda_quote.div_yield = q.get_double("divYield", 0);
        
        return BrokerResponse<Quote>::ok(tda_quote.to_quote());
    }
    
    /**
     * @brief Get multiple quotes
     */
    std::map<std::string, TDAQuote> get_quotes(const std::vector<std::string>& symbols) {
        std::map<std::string, TDAQuote> result;
        
        if (symbols.empty()) return result;
        
        ensure_valid_token();
        
        std::ostringstream symbols_param;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) symbols_param << ",";
            symbols_param << symbols[i];
        }
        
        auto response = http_.get("/marketdata/quotes?symbol=" + 
                                  core::HttpClient::url_encode(symbols_param.str()));
        
        if (!response.success) return result;
        
        auto json = core::JsonParser::parse(response.body);
        
        for (const auto& sym : symbols) {
            if (json.contains(sym)) {
                const auto& q = json[sym];
                
                TDAQuote quote;
                quote.symbol = sym;
                quote.bid_price = q.get_double("bidPrice", 0);
                quote.ask_price = q.get_double("askPrice", 0);
                quote.last_price = q.get_double("lastPrice", 0);
                quote.total_volume = q.get_int("totalVolume", 0);
                quote.net_change = q.get_double("netChange", 0);
                
                result[sym] = quote;
            }
        }
        
        return result;
    }
    
    const TDATokens& tokens() const { return tokens_; }
    std::string last_error() const override { return last_error_; }

private:
    TDAConfig config_;
    core::HttpClient http_;
    TDATokens tokens_;
    bool connected_;
    std::string selected_account_;
    std::string last_error_;
    
    bool parse_token_response(const std::string& body) {
        auto json = core::JsonParser::parse(body);
        
        tokens_.access_token = json.get_string("access_token", "");
        tokens_.token_type = json.get_string("token_type", "Bearer");
        tokens_.expires_in = json.get_int("expires_in", 1800);
        tokens_.scope = json.get_string("scope", "");
        
        // Refresh token only comes with initial auth
        if (json.contains("refresh_token")) {
            tokens_.refresh_token = json.get_string("refresh_token", "");
            tokens_.refresh_expires_in = json.get_int("refresh_token_expires_in", 7776000);
            
            tokens_.refresh_expiry = std::chrono::system_clock::now() +
                std::chrono::seconds(tokens_.refresh_expires_in);
        }
        
        tokens_.access_expiry = std::chrono::system_clock::now() +
            std::chrono::seconds(tokens_.expires_in);
        
        update_auth_header();
        save_tokens();
        
        return !tokens_.access_token.empty();
    }
    
    void update_auth_header() {
        http_.set_header("Authorization", "Bearer " + tokens_.access_token);
    }
    
    void ensure_valid_token() {
        if (tokens_.is_access_expired() && !tokens_.refresh_token.empty()) {
            refresh_tokens();
        }
    }
    
    void save_tokens() {
        if (config_.token_file.empty()) return;
        
        try {
            std::ofstream file(config_.token_file);
            if (file.is_open()) {
                file << tokens_.access_token << "\n";
                file << tokens_.refresh_token << "\n";
                file << tokens_.expires_in << "\n";
                file << tokens_.refresh_expires_in << "\n";
                auto access_time = std::chrono::system_clock::to_time_t(tokens_.access_expiry);
                auto refresh_time = std::chrono::system_clock::to_time_t(tokens_.refresh_expiry);
                file << access_time << "\n";
                file << refresh_time << "\n";
            }
        } catch (...) {
            // Ignore save errors
        }
    }
    
    void load_tokens() {
        if (config_.token_file.empty()) return;
        
        try {
            std::ifstream file(config_.token_file);
            if (file.is_open()) {
                std::getline(file, tokens_.access_token);
                std::getline(file, tokens_.refresh_token);
                
                std::string line;
                if (std::getline(file, line)) tokens_.expires_in = std::stoi(line);
                if (std::getline(file, line)) tokens_.refresh_expires_in = std::stoi(line);
                
                std::time_t access_time, refresh_time;
                if (std::getline(file, line)) {
                    access_time = std::stoll(line);
                    tokens_.access_expiry = std::chrono::system_clock::from_time_t(access_time);
                }
                if (std::getline(file, line)) {
                    refresh_time = std::stoll(line);
                    tokens_.refresh_expiry = std::chrono::system_clock::from_time_t(refresh_time);
                }
                
                update_auth_header();
            }
        } catch (...) {
            // Ignore load errors
        }
    }
    
    std::string tda_order_type(OrderType type) {
        switch (type) {
            case OrderType::Market: return "MARKET";
            case OrderType::Limit: return "LIMIT";
            case OrderType::Stop: return "STOP";
            case OrderType::StopLimit: return "STOP_LIMIT";
            default: return "MARKET";
        }
    }
    
    std::string tda_duration(TimeInForce tif) {
        switch (tif) {
            case TimeInForce::Day: return "DAY";
            case TimeInForce::GTC: return "GOOD_TILL_CANCEL";
            case TimeInForce::FOK: return "FILL_OR_KILL";
            default: return "DAY";
        }
    }
    
    UnifiedOrder parse_order(const core::JsonValue& json) {
        TDAOrder order;
        order.order_id = std::to_string(json.get_int("orderId", 0));
        order.account_id = json.get_string("accountId", selected_account_);
        order.status = json.get_string("status", "");
        order.entered_time = json.get_string("enteredTime", "");
        order.close_time = json.get_string("closeTime", "");
        order.order_type = json.get_string("orderType", "MARKET");
        order.session = json.get_string("session", "NORMAL");
        order.duration = json.get_string("duration", "DAY");
        order.quantity = json.get_double("quantity", 0);
        order.filled_quantity = json.get_double("filledQuantity", 0);
        order.remaining_quantity = json.get_double("remainingQuantity", 0);
        order.price = json.get_double("price", 0);
        order.stop_price = json.get_double("stopPrice", 0);
        
        if (json.contains("orderLegCollection") && json["orderLegCollection"].is_array()) {
            const auto& legs = json["orderLegCollection"].array();
            if (!legs.empty()) {
                const auto& leg = legs[0];
                order.instruction = leg.get_string("instruction", "BUY");
                order.quantity = leg.get_double("quantity", order.quantity);
                
                if (leg.contains("instrument")) {
                    order.symbol = leg["instrument"].get_string("symbol", "");
                    order.asset_type = leg["instrument"].get_string("assetType", "EQUITY");
                }
            }
        }
        
        if (json.contains("orderActivityCollection") && json["orderActivityCollection"].is_array()) {
            double total_fill = 0;
            double weighted_price = 0;
            for (const auto& activity : json["orderActivityCollection"].array()) {
                if (activity.contains("executionLegs")) {
                    for (const auto& exec : activity["executionLegs"].array()) {
                        double qty = exec.get_double("quantity", 0);
                        double price = exec.get_double("price", 0);
                        total_fill += qty;
                        weighted_price += qty * price;
                    }
                }
            }
            if (total_fill > 0) {
                order.average_fill_price = weighted_price / total_fill;
            }
        }
        
        return order.to_unified();
    }
};

/**
 * @brief Create TDA client from environment
 */
inline std::shared_ptr<TDABroker> create_tda_broker_from_env() {
    TDAConfig config;
    
    if (const char* client_id = std::getenv("TDA_CLIENT_ID")) {
        config.client_id = client_id;
    }
    if (const char* redirect = std::getenv("TDA_REDIRECT_URI")) {
        config.redirect_uri = redirect;
    } else {
        config.redirect_uri = "https://localhost:8080/callback";
    }
    if (const char* token_file = std::getenv("TDA_TOKEN_FILE")) {
        config.token_file = token_file;
    }
    
    if (!config.is_valid()) {
        return nullptr;
    }
    
    return std::make_shared<TDABroker>(config);
}

} // namespace genie::trading

#endif // GENIE_TRADING_TDA_CLIENT_HPP
