/**
 * @file alpaca_client.hpp
 * @brief Alpaca Markets API client for paper and live trading
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides complete Alpaca API integration:
 * - Paper trading (unlimited, free)
 * - Live trading (with funded account)
 * - Account information and buying power
 * - Position management
 * - Order submission (market, limit, stop, stop-limit)
 * - Order status and cancellation
 * - Real-time trade updates via WebSocket
 * 
 * API Documentation: https://alpaca.markets/docs/api-references/trading-api/
 * 
 * Rate Limits:
 * - 200 requests per minute for most endpoints
 * - Streaming connections are not rate limited
 */
#pragma once
#ifndef GENIE_TRADING_ALPACA_CLIENT_HPP
#define GENIE_TRADING_ALPACA_CLIENT_HPP

#include "broker_interface.hpp"
#include "../core/http_client.hpp"
#include "../core/websocket_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::trading {

/**
 * @brief Alpaca API configuration
 */
struct AlpacaConfig {
    std::string api_key;
    std::string api_secret;
    bool paper{true};  // Use paper trading endpoint
    
    // Endpoints
    std::string paper_base_url{"https://paper-api.alpaca.markets"};
    std::string live_base_url{"https://api.alpaca.markets"};
    std::string data_base_url{"https://data.alpaca.markets"};
    std::string stream_url{"wss://stream.data.alpaca.markets/v2/iex"};
    std::string trade_stream_url{"wss://paper-api.alpaca.markets/stream"};
    
    // Rate limiting
    int requests_per_minute{200};
    
    std::string base_url() const {
        return paper ? paper_base_url : live_base_url;
    }
    
    bool is_valid() const {
        return !api_key.empty() && !api_secret.empty();
    }
};

/**
 * @brief Alpaca Markets API client
 */
class AlpacaClient : public IBroker {
public:
    explicit AlpacaClient(const AlpacaConfig& config)
        : config_(config)
        , rate_limiter_(std::make_shared<core::RateLimiter>(
            config.requests_per_minute, 60)) {
        
        // Configure HTTP client
        http_.set_base_url(config_.base_url());
        http_.set_default_header("APCA-API-KEY-ID", config_.api_key);
        http_.set_default_header("APCA-API-SECRET-KEY", config_.api_secret);
        http_.set_default_header("Content-Type", "application/json");
        http_.set_rate_limiter(rate_limiter_);
    }
    
    // ========================================================================
    // IBroker Implementation
    // ========================================================================
    
    bool connect() override {
        auto result = get_account();
        connected_ = result.success;
        if (!connected_) {
            last_error_ = result.error;
        }
        return connected_;
    }
    
    void disconnect() override {
        connected_ = false;
        if (trade_stream_) {
            trade_stream_->disconnect();
        }
    }
    
    bool is_connected() const override {
        return connected_;
    }
    
    std::string broker_name() const override {
        return config_.paper ? "Alpaca (Paper)" : "Alpaca (Live)";
    }
    
    bool is_paper_trading() const override {
        return config_.paper;
    }
    
    const std::string& last_error() const override {
        return last_error_;
    }
    
    // ========================================================================
    // Account
    // ========================================================================
    
    BrokerResult<BrokerAccount> get_account() override {
        auto response = http_.get("/v2/account");
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        BrokerAccount account;
        
        account.account_id = json["id"].as_string();
        account.account_number = json["account_number"].as_string();
        account.status = json["status"].as_string();
        account.currency = json["currency"].as_string();
        account.is_paper = config_.paper;
        
        // Cash
        account.cash = safe_stod(json["cash"].as_string());
        account.cash_available = safe_stod(json["cash_withdrawable"].as_string());
        account.cash_withdrawable = safe_stod(json["cash_withdrawable"].as_string());
        
        // Buying power
        account.buying_power = safe_stod(json["buying_power"].as_string());
        account.daytrading_buying_power = safe_stod(json["daytrading_buying_power"].as_string());
        account.regt_buying_power = safe_stod(json["regt_buying_power"].as_string());
        
        // Portfolio values
        account.portfolio_value = safe_stod(json["portfolio_value"].as_string());
        account.equity = safe_stod(json["equity"].as_string());
        account.long_market_value = safe_stod(json["long_market_value"].as_string());
        account.short_market_value = safe_stod(json["short_market_value"].as_string());
        
        // Margin
        account.initial_margin = safe_stod(json["initial_margin"].as_string());
        account.maintenance_margin = safe_stod(json["maintenance_margin"].as_string());
        account.sma = safe_stod(json["sma"].as_string());
        
        // Day trading
        account.daytrade_count = json["daytrade_count"].as_int();
        account.pattern_day_trader = json["pattern_day_trader"].as_bool();
        account.trading_blocked = json["trading_blocked"].as_bool();
        account.transfers_blocked = json["transfers_blocked"].as_bool();
        account.account_blocked = json["account_blocked"].as_bool();
        
        // Timestamps
        account.created_at = json["created_at"].as_string();
        
        return {true, account, "", response.status_code};
    }
    
    // ========================================================================
    // Positions
    // ========================================================================
    
    BrokerResult<std::vector<BrokerPosition>> get_positions() override {
        auto response = http_.get("/v2/positions");
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<BrokerPosition> positions;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                positions.push_back(parse_position(json[i]));
            }
        }
        
        return {true, positions, "", response.status_code};
    }
    
    BrokerResult<BrokerPosition> get_position(const std::string& symbol) override {
        auto response = http_.get("/v2/positions/" + symbol);
        
        if (!response.ok()) {
            if (response.is_not_found()) {
                return {false, {}, "Position not found: " + symbol, response.status_code};
            }
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        return {true, parse_position(json), "", response.status_code};
    }
    
    BrokerResult<bool> close_position(const std::string& symbol) override {
        auto response = http_.del("/v2/positions/" + symbol);
        
        if (!response.ok()) {
            return {false, false, parse_error(response), response.status_code};
        }
        
        return {true, true, "", response.status_code};
    }
    
    BrokerResult<bool> close_all_positions() override {
        auto response = http_.del("/v2/positions");
        
        if (!response.ok() && response.status_code != 207) {  // 207 = Multi-Status
            return {false, false, parse_error(response), response.status_code};
        }
        
        return {true, true, "", response.status_code};
    }
    
    // ========================================================================
    // Orders
    // ========================================================================
    
    BrokerResult<BrokerOrder> submit_order(const OrderRequest& request) override {
        if (!request.is_valid()) {
            return {false, {}, "Invalid order request", 400};
        }
        
        // Build order JSON
        std::string body = build_order_json(request);
        
        auto response = http_.post("/v2/orders", body);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        return {true, parse_order(json), "", response.status_code};
    }
    
    BrokerResult<BrokerOrder> get_order(const std::string& order_id) override {
        auto response = http_.get("/v2/orders/" + order_id);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        return {true, parse_order(json), "", response.status_code};
    }
    
    BrokerResult<std::vector<BrokerOrder>> get_orders(
        const std::string& status,
        int limit,
        const std::string& after,
        const std::string& until) override {
        
        std::map<std::string, std::string> params;
        params["status"] = status.empty() ? "open" : status;
        params["limit"] = std::to_string(limit);
        if (!after.empty()) params["after"] = after;
        if (!until.empty()) params["until"] = until;
        
        auto response = http_.get("/v2/orders", params);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<BrokerOrder> orders;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                orders.push_back(parse_order(json[i]));
            }
        }
        
        return {true, orders, "", response.status_code};
    }
    
    BrokerResult<bool> cancel_order(const std::string& order_id) override {
        auto response = http_.del("/v2/orders/" + order_id);
        
        if (!response.ok() && response.status_code != 204) {
            return {false, false, parse_error(response), response.status_code};
        }
        
        return {true, true, "", response.status_code};
    }
    
    BrokerResult<bool> cancel_all_orders() override {
        auto response = http_.del("/v2/orders");
        
        if (!response.ok() && response.status_code != 207) {
            return {false, false, parse_error(response), response.status_code};
        }
        
        return {true, true, "", response.status_code};
    }
    
    BrokerResult<BrokerOrder> replace_order(
        const std::string& order_id,
        const OrderRequest& new_request) override {
        
        // Build replacement JSON (only qty, time_in_force, limit_price, stop_price, trail)
        std::ostringstream oss;
        oss << "{";
        oss << "\"qty\":\"" << new_request.qty << "\"";
        oss << ",\"time_in_force\":\"" << time_in_force_to_string(new_request.time_in_force) << "\"";
        
        if (new_request.limit_price) {
            oss << ",\"limit_price\":\"" << *new_request.limit_price << "\"";
        }
        if (new_request.stop_price) {
            oss << ",\"stop_price\":\"" << *new_request.stop_price << "\"";
        }
        if (new_request.trail_price) {
            oss << ",\"trail\":\"" << *new_request.trail_price << "\"";
        }
        if (!new_request.client_order_id.empty()) {
            oss << ",\"client_order_id\":\"" << new_request.client_order_id << "\"";
        }
        
        oss << "}";
        
        auto response = http_.execute(core::HttpRequest{
            core::HttpMethod::PATCH,
            "/v2/orders/" + order_id,
            {},
            {},
            oss.str(),
            30000,
            true
        });
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        return {true, parse_order(json), "", response.status_code};
    }
    
    // ========================================================================
    // Assets
    // ========================================================================
    
    BrokerResult<BrokerAsset> get_asset(const std::string& symbol) override {
        auto response = http_.get("/v2/assets/" + symbol);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        return {true, parse_asset(json), "", response.status_code};
    }
    
    BrokerResult<std::vector<BrokerAsset>> get_assets(
        const std::string& status,
        const std::string& asset_class) override {
        
        std::map<std::string, std::string> params;
        if (!status.empty()) params["status"] = status;
        if (!asset_class.empty()) params["asset_class"] = asset_class;
        
        auto response = http_.get("/v2/assets", params);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<BrokerAsset> assets;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                assets.push_back(parse_asset(json[i]));
            }
        }
        
        return {true, assets, "", response.status_code};
    }
    
    // ========================================================================
    // Market Info
    // ========================================================================
    
    BrokerResult<MarketClock> get_clock() override {
        auto response = http_.get("/v2/clock");
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        MarketClock clock;
        
        clock.timestamp = json["timestamp"].as_string();
        clock.is_open = json["is_open"].as_bool();
        clock.next_open = json["next_open"].as_string();
        clock.next_close = json["next_close"].as_string();
        
        return {true, clock, "", response.status_code};
    }
    
    // ========================================================================
    // Activities
    // ========================================================================
    
    BrokerResult<std::vector<BrokerActivity>> get_activities(
        const std::string& activity_type,
        const std::string& date,
        int limit) override {
        
        std::map<std::string, std::string> params;
        if (!activity_type.empty()) params["activity_type"] = activity_type;
        if (!date.empty()) params["date"] = date;
        params["page_size"] = std::to_string(limit);
        
        auto response = http_.get("/v2/account/activities", params);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<BrokerActivity> activities;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                BrokerActivity act;
                act.id = json[i]["id"].as_string();
                act.activity_type = json[i]["activity_type"].as_string();
                
                if (json[i].has("transaction_time")) {
                    act.date = json[i]["transaction_time"].as_string();
                } else if (json[i].has("date")) {
                    act.date = json[i]["date"].as_string();
                }
                
                act.symbol = json[i]["symbol"].as_string();
                act.qty = safe_stod(json[i]["qty"].as_string());
                act.price = safe_stod(json[i]["price"].as_string());
                act.net_amount = safe_stod(json[i]["net_amount"].as_string());
                act.description = json[i]["description"].as_string();
                
                if (json[i].has("side")) {
                    act.side = string_to_order_side(json[i]["side"].as_string());
                }
                
                activities.push_back(act);
            }
        }
        
        return {true, activities, "", response.status_code};
    }
    
    // ========================================================================
    // Alpaca-Specific Methods
    // ========================================================================
    
    /**
     * @brief Get portfolio history
     */
    struct PortfolioHistory {
        std::vector<int64_t> timestamps;
        std::vector<double> equity;
        std::vector<double> profit_loss;
        std::vector<double> profit_loss_pct;
        double base_value{0};
        std::string timeframe;
    };
    
    BrokerResult<PortfolioHistory> get_portfolio_history(
        const std::string& period = "1M",
        const std::string& timeframe = "1D",
        bool extended_hours = false) {
        
        std::map<std::string, std::string> params;
        params["period"] = period;
        params["timeframe"] = timeframe;
        params["extended_hours"] = extended_hours ? "true" : "false";
        
        auto response = http_.get("/v2/account/portfolio/history", params);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        PortfolioHistory history;
        
        history.base_value = json["base_value"].as_number();
        history.timeframe = json["timeframe"].as_string();
        
        if (json["timestamp"].is_array()) {
            for (size_t i = 0; i < json["timestamp"].size(); ++i) {
                history.timestamps.push_back(static_cast<int64_t>(json["timestamp"][i].as_number()));
            }
        }
        if (json["equity"].is_array()) {
            for (size_t i = 0; i < json["equity"].size(); ++i) {
                history.equity.push_back(json["equity"][i].as_number());
            }
        }
        if (json["profit_loss"].is_array()) {
            for (size_t i = 0; i < json["profit_loss"].size(); ++i) {
                history.profit_loss.push_back(json["profit_loss"][i].as_number());
            }
        }
        if (json["profit_loss_pct"].is_array()) {
            for (size_t i = 0; i < json["profit_loss_pct"].size(); ++i) {
                history.profit_loss_pct.push_back(json["profit_loss_pct"][i].as_number());
            }
        }
        
        return {true, history, "", response.status_code};
    }
    
    /**
     * @brief Get calendar (trading days)
     */
    struct CalendarDay {
        std::string date;
        std::string open;
        std::string close;
    };
    
    BrokerResult<std::vector<CalendarDay>> get_calendar(
        const std::string& start = "",
        const std::string& end = "") {
        
        std::map<std::string, std::string> params;
        if (!start.empty()) params["start"] = start;
        if (!end.empty()) params["end"] = end;
        
        auto response = http_.get("/v2/calendar", params);
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<CalendarDay> days;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                CalendarDay day;
                day.date = json[i]["date"].as_string();
                day.open = json[i]["open"].as_string();
                day.close = json[i]["close"].as_string();
                days.push_back(day);
            }
        }
        
        return {true, days, "", response.status_code};
    }
    
    /**
     * @brief Get watchlists
     */
    struct Watchlist {
        std::string id;
        std::string name;
        std::string account_id;
        std::vector<std::string> symbols;
        std::string created_at;
        std::string updated_at;
    };
    
    BrokerResult<std::vector<Watchlist>> get_watchlists() {
        auto response = http_.get("/v2/watchlists");
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<Watchlist> watchlists;
        
        if (json.is_array()) {
            for (size_t i = 0; i < json.size(); ++i) {
                Watchlist wl;
                wl.id = json[i]["id"].as_string();
                wl.name = json[i]["name"].as_string();
                wl.account_id = json[i]["account_id"].as_string();
                wl.created_at = json[i]["created_at"].as_string();
                wl.updated_at = json[i]["updated_at"].as_string();
                
                if (json[i]["assets"].is_array()) {
                    for (size_t j = 0; j < json[i]["assets"].size(); ++j) {
                        wl.symbols.push_back(json[i]["assets"][j]["symbol"].as_string());
                    }
                }
                
                watchlists.push_back(wl);
            }
        }
        
        return {true, watchlists, "", response.status_code};
    }
    
    /**
     * @brief Create watchlist
     */
    BrokerResult<Watchlist> create_watchlist(const std::string& name,
                                              const std::vector<std::string>& symbols = {}) {
        std::ostringstream oss;
        oss << "{\"name\":\"" << name << "\"";
        if (!symbols.empty()) {
            oss << ",\"symbols\":[";
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (i > 0) oss << ",";
                oss << "\"" << symbols[i] << "\"";
            }
            oss << "]";
        }
        oss << "}";
        
        auto response = http_.post("/v2/watchlists", oss.str());
        
        if (!response.ok()) {
            return {false, {}, parse_error(response), response.status_code};
        }
        
        auto json = core::JsonParser::parse(response.body);
        Watchlist wl;
        wl.id = json["id"].as_string();
        wl.name = json["name"].as_string();
        wl.account_id = json["account_id"].as_string();
        wl.created_at = json["created_at"].as_string();
        wl.updated_at = json["updated_at"].as_string();
        
        return {true, wl, "", response.status_code};
    }
    
    // ========================================================================
    // Streaming
    // ========================================================================
    
    /**
     * @brief Start trade updates stream
     */
    bool start_trade_stream() {
        core::WsConfig ws_config;
        ws_config.url = config_.trade_stream_url;
        ws_config.api_key = config_.api_key;
        ws_config.api_secret = config_.api_secret;
        ws_config.auto_reconnect = true;
        
        trade_stream_ = std::make_unique<core::WebSocketClient>(ws_config);
        
        // Set up message handler
        trade_stream_->on_message([this](const core::WsMessage& msg) {
            handle_trade_stream_message(msg.data);
        });
        
        if (!trade_stream_->connect()) {
            last_error_ = "Failed to connect to trade stream";
            return false;
        }
        
        // Subscribe to trade updates
        std::string sub_msg = R"({"action":"listen","data":{"streams":["trade_updates"]}})";
        return trade_stream_->send(sub_msg);
    }
    
    /**
     * @brief Stop trade updates stream
     */
    void stop_trade_stream() {
        if (trade_stream_) {
            trade_stream_->disconnect();
            trade_stream_.reset();
        }
    }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    const AlpacaConfig& config() const { return config_; }
    
    void set_paper_trading(bool paper) {
        config_.paper = paper;
        http_.set_base_url(config_.base_url());
    }
    
    core::HttpClient& http_client() { return http_; }

private:
    AlpacaConfig config_;
    core::HttpClient http_;
    std::shared_ptr<core::RateLimiter> rate_limiter_;
    std::unique_ptr<core::WebSocketClient> trade_stream_;
    std::atomic<bool> connected_{false};
    std::string last_error_;
    
    // ========================================================================
    // Parsing Helpers
    // ========================================================================
    
    BrokerPosition parse_position(const core::JsonValue& json) {
        BrokerPosition pos;
        
        pos.asset_id = json["asset_id"].as_string();
        pos.symbol = json["symbol"].as_string();
        pos.exchange = json["exchange"].as_string();
        
        std::string cls = json["asset_class"].as_string();
        if (cls == "us_equity") pos.asset_class = AssetClass::USEquity;
        else if (cls == "crypto") pos.asset_class = AssetClass::Crypto;
        
        pos.qty = safe_stod(json["qty"].as_string());
        pos.qty_available = safe_stod(json["qty_available"].as_string());
        pos.side = pos.qty >= 0 ? OrderSide::Buy : OrderSide::Sell;
        
        pos.avg_entry_price = safe_stod(json["avg_entry_price"].as_string());
        pos.current_price = safe_stod(json["current_price"].as_string());
        pos.lastday_price = safe_stod(json["lastday_price"].as_string());
        
        pos.market_value = safe_stod(json["market_value"].as_string());
        pos.cost_basis = safe_stod(json["cost_basis"].as_string());
        
        pos.unrealized_pl = safe_stod(json["unrealized_pl"].as_string());
        pos.unrealized_plpc = safe_stod(json["unrealized_plpc"].as_string());
        pos.unrealized_intraday_pl = safe_stod(json["unrealized_intraday_pl"].as_string());
        pos.unrealized_intraday_plpc = safe_stod(json["unrealized_intraday_plpc"].as_string());
        pos.change_today = safe_stod(json["change_today"].as_string());
        
        return pos;
    }
    
    BrokerOrder parse_order(const core::JsonValue& json) {
        BrokerOrder order;
        
        order.id = json["id"].as_string();
        order.client_order_id = json["client_order_id"].as_string();
        order.created_at = json["created_at"].as_string();
        order.updated_at = json["updated_at"].as_string();
        order.submitted_at = json["submitted_at"].as_string();
        order.filled_at = json["filled_at"].as_string();
        order.expired_at = json["expired_at"].as_string();
        order.canceled_at = json["canceled_at"].as_string();
        order.failed_at = json["failed_at"].as_string();
        order.replaced_at = json["replaced_at"].as_string();
        order.replaced_by = json["replaced_by"].as_string();
        order.replaces = json["replaces"].as_string();
        
        order.asset_id = json["asset_id"].as_string();
        order.symbol = json["symbol"].as_string();
        
        std::string cls = json["asset_class"].as_string();
        if (cls == "us_equity") order.asset_class = AssetClass::USEquity;
        else if (cls == "crypto") order.asset_class = AssetClass::Crypto;
        
        order.qty = safe_stod(json["qty"].as_string());
        order.filled_qty = safe_stod(json["filled_qty"].as_string());
        order.notional = safe_stod(json["notional"].as_string());
        order.filled_avg_price = safe_stod(json["filled_avg_price"].as_string());
        
        order.side = string_to_order_side(json["side"].as_string());
        order.type = string_to_order_type(json["type"].as_string());
        order.time_in_force = string_to_time_in_force(json["time_in_force"].as_string());
        
        if (!json["limit_price"].is_null()) {
            order.limit_price = safe_stod(json["limit_price"].as_string());
        }
        if (!json["stop_price"].is_null()) {
            order.stop_price = safe_stod(json["stop_price"].as_string());
        }
        if (!json["trail_price"].is_null()) {
            order.trail_price = safe_stod(json["trail_price"].as_string());
        }
        if (!json["trail_percent"].is_null()) {
            order.trail_percent = safe_stod(json["trail_percent"].as_string());
        }
        if (!json["hwm"].is_null()) {
            order.hwm = safe_stod(json["hwm"].as_string());
        }
        
        order.status = string_to_order_status(json["status"].as_string());
        order.extended_hours = json["extended_hours"].as_bool();
        
        // Parse legs for bracket/OCO orders
        if (json["legs"].is_array()) {
            for (size_t i = 0; i < json["legs"].size(); ++i) {
                order.legs.push_back(parse_order(json["legs"][i]));
            }
        }
        
        return order;
    }
    
    BrokerAsset parse_asset(const core::JsonValue& json) {
        BrokerAsset asset;
        
        asset.id = json["id"].as_string();
        asset.symbol = json["symbol"].as_string();
        asset.name = json["name"].as_string();
        asset.exchange = json["exchange"].as_string();
        
        std::string cls = json["class"].as_string();
        if (cls == "us_equity") asset.asset_class = AssetClass::USEquity;
        else if (cls == "crypto") asset.asset_class = AssetClass::Crypto;
        
        asset.tradable = json["tradable"].as_bool();
        asset.marginable = json["marginable"].as_bool();
        asset.shortable = json["shortable"].as_bool();
        asset.easy_to_borrow = json["easy_to_borrow"].as_bool();
        asset.fractionable = json["fractionable"].as_bool();
        
        asset.min_order_size = safe_stod(json["min_order_size"].as_string());
        asset.min_trade_increment = safe_stod(json["min_trade_increment"].as_string());
        asset.price_increment = safe_stod(json["price_increment"].as_string());
        
        asset.status = json["status"].as_string();
        
        return asset;
    }
    
    std::string build_order_json(const OrderRequest& req) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        oss << "{";
        oss << "\"symbol\":\"" << req.symbol << "\"";
        
        if (req.notional) {
            oss << ",\"notional\":\"" << *req.notional << "\"";
        } else {
            oss << ",\"qty\":\"" << req.qty << "\"";
        }
        
        oss << ",\"side\":\"" << order_side_to_string(req.side) << "\"";
        oss << ",\"type\":\"" << order_type_to_string(req.type) << "\"";
        oss << ",\"time_in_force\":\"" << time_in_force_to_string(req.time_in_force) << "\"";
        
        if (req.limit_price) {
            oss << ",\"limit_price\":\"" << *req.limit_price << "\"";
        }
        if (req.stop_price) {
            oss << ",\"stop_price\":\"" << *req.stop_price << "\"";
        }
        if (req.trail_price) {
            oss << ",\"trail_price\":\"" << *req.trail_price << "\"";
        }
        if (req.trail_percent) {
            oss << ",\"trail_percent\":\"" << *req.trail_percent << "\"";
        }
        
        oss << ",\"extended_hours\":" << (req.extended_hours ? "true" : "false");
        
        if (!req.client_order_id.empty()) {
            oss << ",\"client_order_id\":\"" << req.client_order_id << "\"";
        }
        
        if (!req.order_class.empty()) {
            oss << ",\"order_class\":\"" << req.order_class << "\"";
        }
        
        // Bracket order legs
        if (req.take_profit_limit_price || req.stop_loss_stop_price) {
            if (req.take_profit_limit_price) {
                oss << ",\"take_profit\":{\"limit_price\":\"" << *req.take_profit_limit_price << "\"}";
            }
            if (req.stop_loss_stop_price) {
                oss << ",\"stop_loss\":{\"stop_price\":\"" << *req.stop_loss_stop_price << "\"";
                if (req.stop_loss_limit_price) {
                    oss << ",\"limit_price\":\"" << *req.stop_loss_limit_price << "\"";
                }
                oss << "}";
            }
        }
        
        oss << "}";
        
        return oss.str();
    }
    
    std::string parse_error(const core::HttpResponse& response) {
        if (!response.body.empty()) {
            auto json = core::JsonParser::parse(response.body);
            if (json.has("message")) {
                return json["message"].as_string();
            }
            if (json.has("code") && json.has("message")) {
                return json["code"].as_string() + ": " + json["message"].as_string();
            }
        }
        return response.error.empty() ? 
            "HTTP " + std::to_string(response.status_code) : response.error;
    }
    
    double safe_stod(const std::string& s) {
        if (s.empty() || s == "null") return 0;
        try { return std::stod(s); }
        catch (...) { return 0; }
    }
    
    void handle_trade_stream_message(const std::string& json_str) {
        auto json = core::JsonParser::parse(json_str);
        
        std::string stream = json["stream"].as_string();
        if (stream != "trade_updates") return;
        
        auto data = json["data"];
        std::string event = data["event"].as_string();
        
        // Parse order from data
        auto order = parse_order(data["order"]);
        
        // Map Alpaca events to order status
        if (event == "fill") {
            order.status = OrderStatus::Filled;
            
            // Also fire trade callback
            if (on_trade_update_) {
                BrokerFill fill;
                fill.order_id = order.id;
                fill.symbol = order.symbol;
                fill.qty = data["qty"].as_number();
                fill.price = data["price"].as_number();
                fill.side = order.side;
                fill.timestamp = data["timestamp"].as_string();
                on_trade_update_(fill);
            }
        } else if (event == "partial_fill") {
            order.status = OrderStatus::PartiallyFilled;
        } else if (event == "canceled") {
            order.status = OrderStatus::Canceled;
        } else if (event == "rejected") {
            order.status = OrderStatus::Rejected;
        } else if (event == "new") {
            order.status = OrderStatus::New;
        } else if (event == "accepted") {
            order.status = OrderStatus::Accepted;
        } else if (event == "pending_new") {
            order.status = OrderStatus::PendingNew;
        }
        
        if (on_order_update_) {
            on_order_update_(order);
        }
    }
};

// ============================================================================
// Factory Function
// ============================================================================

/**
 * @brief Create Alpaca client from credentials
 */
inline std::unique_ptr<AlpacaClient> create_alpaca_client(
    const std::string& api_key,
    const std::string& api_secret,
    bool paper = true) {
    
    AlpacaConfig config;
    config.api_key = api_key;
    config.api_secret = api_secret;
    config.paper = paper;
    
    return std::make_unique<AlpacaClient>(config);
}

/**
 * @brief Create Alpaca client from environment variables
 * 
 * Looks for:
 * - APCA_API_KEY_ID
 * - APCA_API_SECRET_KEY
 * - APCA_API_BASE_URL (optional, to determine paper vs live)
 */
inline std::unique_ptr<AlpacaClient> create_alpaca_client_from_env() {
    AlpacaConfig config;
    
    if (const char* key = std::getenv("APCA_API_KEY_ID")) {
        config.api_key = key;
    }
    if (const char* secret = std::getenv("APCA_API_SECRET_KEY")) {
        config.api_secret = secret;
    }
    if (const char* url = std::getenv("APCA_API_BASE_URL")) {
        std::string base_url = url;
        config.paper = (base_url.find("paper") != std::string::npos);
    }
    
    if (!config.is_valid()) {
        return nullptr;
    }
    
    return std::make_unique<AlpacaClient>(config);
}

} // namespace genie::trading

#endif // GENIE_TRADING_ALPACA_CLIENT_HPP
