/**
 * @file polygon_client.hpp
 * @brief Polygon.io API and WebSocket client
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Polygon.io integration providing:
 * - REST API for quotes, aggregates, and reference data
 * - WebSocket streaming for real-time trades and quotes
 * - Options and forex data
 * - Market snapshots
 * 
 * API Documentation: https://polygon.io/docs
 */
#pragma once
#ifndef GENIE_MARKET_POLYGON_CLIENT_HPP
#define GENIE_MARKET_POLYGON_CLIENT_HPP

#include "../core/http_client.hpp"
#include "../core/websocket_client.hpp"
#include "market_data.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief Polygon configuration
 */
struct PolygonConfig {
    std::string api_key;
    int timeout_ms{10000};
    bool delayed{false};              // Use delayed (free) data
    
    static constexpr const char* REST_URL = "https://api.polygon.io";
    static constexpr const char* WS_URL = "wss://socket.polygon.io/stocks";
    static constexpr const char* WS_DELAYED_URL = "wss://delayed.polygon.io/stocks";
    
    std::string websocket_url() const {
        return delayed ? WS_DELAYED_URL : WS_URL;
    }
    
    bool is_valid() const {
        return !api_key.empty();
    }
};

/**
 * @brief Polygon aggregate bar
 */
struct PolygonBar {
    std::string ticker;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double vwap{0};                   // Volume-weighted avg price
    int64_t volume{0};
    int64_t timestamp{0};             // Unix milliseconds
    int transactions{0};              // Number of trades
    
    std::string date() const {
        auto tp = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timestamp));
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&time), "%Y-%m-%d");
        return oss.str();
    }
    
    PriceBar to_price_bar() const {
        PriceBar bar;
        bar.date = date();
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.adjusted_close = close;
        bar.volume = volume;
        return bar;
    }
};

/**
 * @brief Polygon ticker details
 */
struct PolygonTicker {
    std::string ticker;
    std::string name;
    std::string market;               // stocks, crypto, fx
    std::string locale;
    std::string primary_exchange;
    std::string type;                 // CS (common stock), ETF, etc.
    bool active{true};
    std::string currency_name;
    std::string cik;
    std::string composite_figi;
    std::string share_class_figi;
    int64_t market_cap{0};
    std::string phone_number;
    std::string address;
    std::string description;
    std::string sic_code;
    std::string sic_description;
    std::string homepage_url;
    int total_employees{0};
    std::string list_date;
    double share_class_shares_outstanding{0};
    double weighted_shares_outstanding{0};
};

/**
 * @brief Polygon snapshot
 */
struct PolygonSnapshot {
    std::string ticker;
    
    // Day data
    double day_open{0};
    double day_high{0};
    double day_low{0};
    double day_close{0};
    double day_vwap{0};
    int64_t day_volume{0};
    
    // Previous day
    double prev_close{0};
    double prev_high{0};
    double prev_low{0};
    double prev_open{0};
    double prev_vwap{0};
    int64_t prev_volume{0};
    
    // Minute bar
    double minute_open{0};
    double minute_high{0};
    double minute_low{0};
    double minute_close{0};
    int64_t minute_volume{0};
    double minute_vwap{0};
    int64_t minute_timestamp{0};
    
    // Change
    double change{0};
    double change_percent{0};
    
    // Last trade
    double last_price{0};
    int last_size{0};
    int64_t last_timestamp{0};
    
    Quote to_quote() const {
        Quote q;
        q.symbol = ticker;
        q.price = day_close > 0 ? day_close : last_price;
        q.open = day_open;
        q.high = day_high;
        q.low = day_low;
        q.previous_close = prev_close;
        q.change = change;
        q.change_percent = change_percent;
        q.volume = day_volume;
        return q;
    }
};

/**
 * @brief Polygon trade event (from WebSocket)
 */
struct PolygonTrade {
    std::string symbol;
    double price{0};
    int size{0};
    int64_t timestamp{0};
    std::string exchange;
    std::string tape;
    int conditions{0};
};

/**
 * @brief Polygon quote event (from WebSocket)
 */
struct PolygonQuote {
    std::string symbol;
    double bid{0};
    double ask{0};
    int bid_size{0};
    int ask_size{0};
    int64_t timestamp{0};
    std::string bid_exchange;
    std::string ask_exchange;
    
    double mid() const { return (bid + ask) / 2; }
    double spread() const { return ask - bid; }
};

// Callbacks
using OnPolygonTradeCallback = std::function<void(const PolygonTrade&)>;
using OnPolygonQuoteCallback = std::function<void(const PolygonQuote&)>;
using OnPolygonBarCallback = std::function<void(const PolygonBar&)>;
using OnPolygonStatusCallback = std::function<void(const std::string&)>;

/**
 * @brief Polygon REST API client
 */
class PolygonClient {
public:
    explicit PolygonClient(const PolygonConfig& config)
        : config_(config)
        , http_client_(PolygonConfig::REST_URL) {
        
        http_client_.set_timeout(config.timeout_ms);
    }
    
    // ========================================================================
    // Aggregates (Bars)
    // ========================================================================
    
    /**
     * @brief Get aggregate bars
     * @param ticker Stock ticker
     * @param multiplier Size of the timespan multiplier
     * @param timespan minute, hour, day, week, month, quarter, year
     * @param from Start date (YYYY-MM-DD)
     * @param to End date (YYYY-MM-DD)
     */
    std::vector<PolygonBar> get_aggregates(
        const std::string& ticker,
        int multiplier,
        const std::string& timespan,
        const std::string& from,
        const std::string& to,
        bool adjusted = true,
        const std::string& sort = "asc",
        int limit = 5000) {
        
        std::vector<PolygonBar> bars;
        
        std::ostringstream endpoint;
        endpoint << "/v2/aggs/ticker/" << ticker << "/range/"
                 << multiplier << "/" << timespan << "/" << from << "/" << to;
        
        auto response = request(endpoint.str(), {
            {"adjusted", adjusted ? "true" : "false"},
            {"sort", sort},
            {"limit", std::to_string(limit)}
        });
        
        if (!response) return bars;
        
        auto& json = *response;
        if (!json.contains("results") || !json["results"].is_array()) {
            return bars;
        }
        
        for (const auto& item : json["results"]) {
            PolygonBar bar;
            bar.ticker = ticker;
            bar.open = item.value("o", 0.0);
            bar.high = item.value("h", 0.0);
            bar.low = item.value("l", 0.0);
            bar.close = item.value("c", 0.0);
            bar.vwap = item.value("vw", 0.0);
            bar.volume = item.value("v", 0);
            bar.timestamp = item.value("t", 0);
            bar.transactions = item.value("n", 0);
            
            bars.push_back(bar);
        }
        
        return bars;
    }
    
    /**
     * @brief Get daily bars
     */
    std::vector<PriceBar> get_daily_bars(
        const std::string& ticker,
        const std::string& from,
        const std::string& to,
        bool adjusted = true) {
        
        auto aggs = get_aggregates(ticker, 1, "day", from, to, adjusted);
        
        std::vector<PriceBar> bars;
        bars.reserve(aggs.size());
        
        for (const auto& agg : aggs) {
            bars.push_back(agg.to_price_bar());
        }
        
        return bars;
    }
    
    /**
     * @brief Get previous day's bar
     */
    std::optional<PolygonBar> get_previous_close(const std::string& ticker) {
        auto response = request("/v2/aggs/ticker/" + ticker + "/prev", {
            {"adjusted", "true"}
        });
        
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("results") || !json["results"].is_array() ||
            json["results"].empty()) {
            return std::nullopt;
        }
        
        const auto& item = json["results"][0];
        PolygonBar bar;
        bar.ticker = ticker;
        bar.open = item.value("o", 0.0);
        bar.high = item.value("h", 0.0);
        bar.low = item.value("l", 0.0);
        bar.close = item.value("c", 0.0);
        bar.vwap = item.value("vw", 0.0);
        bar.volume = item.value("v", 0);
        bar.timestamp = item.value("t", 0);
        
        return bar;
    }
    
    // ========================================================================
    // Snapshots
    // ========================================================================
    
    /**
     * @brief Get snapshot for single ticker
     */
    std::optional<PolygonSnapshot> get_snapshot(const std::string& ticker) {
        auto response = request("/v2/snapshot/locale/us/markets/stocks/tickers/" + ticker);
        
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("ticker")) return std::nullopt;
        
        return parse_snapshot(json["ticker"]);
    }
    
    /**
     * @brief Get snapshots for multiple tickers
     */
    std::map<std::string, PolygonSnapshot> get_snapshots(
        const std::vector<std::string>& tickers) {
        
        std::map<std::string, PolygonSnapshot> result;
        
        // Build ticker list
        std::ostringstream oss;
        for (size_t i = 0; i < tickers.size(); ++i) {
            if (i > 0) oss << ",";
            oss << tickers[i];
        }
        
        auto response = request("/v2/snapshot/locale/us/markets/stocks/tickers", {
            {"tickers", oss.str()}
        });
        
        if (!response) return result;
        
        auto& json = *response;
        if (!json.contains("tickers") || !json["tickers"].is_array()) {
            return result;
        }
        
        for (const auto& item : json["tickers"]) {
            auto snap = parse_snapshot(item);
            if (snap) {
                result[snap->ticker] = *snap;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Get all snapshots (gainers/losers)
     */
    std::vector<PolygonSnapshot> get_gainers_losers(bool gainers = true) {
        std::vector<PolygonSnapshot> result;
        
        std::string direction = gainers ? "gainers" : "losers";
        auto response = request("/v2/snapshot/locale/us/markets/stocks/" + direction);
        
        if (!response) return result;
        
        auto& json = *response;
        if (!json.contains("tickers") || !json["tickers"].is_array()) {
            return result;
        }
        
        for (const auto& item : json["tickers"]) {
            auto snap = parse_snapshot(item);
            if (snap) {
                result.push_back(*snap);
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // Ticker Details
    // ========================================================================
    
    /**
     * @brief Get ticker details
     */
    std::optional<PolygonTicker> get_ticker_details(const std::string& ticker) {
        auto response = request("/v3/reference/tickers/" + ticker);
        
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("results")) return std::nullopt;
        
        const auto& item = json["results"];
        PolygonTicker t;
        
        t.ticker = item.value("ticker", "");
        t.name = item.value("name", "");
        t.market = item.value("market", "");
        t.locale = item.value("locale", "");
        t.primary_exchange = item.value("primary_exchange", "");
        t.type = item.value("type", "");
        t.active = item.value("active", true);
        t.currency_name = item.value("currency_name", "");
        t.cik = item.value("cik", "");
        t.composite_figi = item.value("composite_figi", "");
        t.share_class_figi = item.value("share_class_figi", "");
        t.market_cap = item.value("market_cap", 0);
        t.description = item.value("description", "");
        t.homepage_url = item.value("homepage_url", "");
        t.total_employees = item.value("total_employees", 0);
        t.list_date = item.value("list_date", "");
        t.share_class_shares_outstanding = item.value("share_class_shares_outstanding", 0.0);
        t.weighted_shares_outstanding = item.value("weighted_shares_outstanding", 0.0);
        
        return t;
    }
    
    /**
     * @brief Search tickers
     */
    std::vector<PolygonTicker> search_tickers(
        const std::string& query,
        int limit = 20) {
        
        std::vector<PolygonTicker> results;
        
        auto response = request("/v3/reference/tickers", {
            {"search", query},
            {"limit", std::to_string(limit)}
        });
        
        if (!response) return results;
        
        auto& json = *response;
        if (!json.contains("results") || !json["results"].is_array()) {
            return results;
        }
        
        for (const auto& item : json["results"]) {
            PolygonTicker t;
            t.ticker = item.value("ticker", "");
            t.name = item.value("name", "");
            t.market = item.value("market", "");
            t.type = item.value("type", "");
            t.active = item.value("active", true);
            
            results.push_back(t);
        }
        
        return results;
    }
    
    // ========================================================================
    // Dividends & Splits
    // ========================================================================
    
    /**
     * @brief Get dividends
     */
    std::vector<Dividend> get_dividends(const std::string& ticker) {
        std::vector<Dividend> dividends;
        
        auto response = request("/v3/reference/dividends", {
            {"ticker", ticker}
        });
        
        if (!response) return dividends;
        
        auto& json = *response;
        if (!json.contains("results") || !json["results"].is_array()) {
            return dividends;
        }
        
        for (const auto& item : json["results"]) {
            Dividend div;
            div.symbol = item.value("ticker", ticker);
            div.date = item.value("ex_dividend_date", "");
            div.amount = item.value("cash_amount", 0.0);
            div.currency = item.value("currency", "USD");
            
            dividends.push_back(div);
        }
        
        return dividends;
    }
    
    /**
     * @brief Get stock splits
     */
    std::vector<Split> get_splits(const std::string& ticker) {
        std::vector<Split> splits;
        
        auto response = request("/v3/reference/splits", {
            {"ticker", ticker}
        });
        
        if (!response) return splits;
        
        auto& json = *response;
        if (!json.contains("results") || !json["results"].is_array()) {
            return splits;
        }
        
        for (const auto& item : json["results"]) {
            Split split;
            split.symbol = item.value("ticker", ticker);
            split.date = item.value("execution_date", "");
            split.from_factor = item.value("split_from", 1.0);
            split.to_factor = item.value("split_to", 1.0);
            split.ratio = split.to_factor / split.from_factor;
            
            splits.push_back(split);
        }
        
        return splits;
    }
    
    // ========================================================================
    // Market Status
    // ========================================================================
    
    /**
     * @brief Get market status
     */
    std::string get_market_status() {
        auto response = request("/v1/marketstatus/now");
        if (!response) return "unknown";
        
        return response->value("market", "unknown");
    }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    const PolygonConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }

private:
    PolygonConfig config_;
    core::HttpClient http_client_;
    std::string last_error_;
    
    std::optional<core::JsonValue> request(
        const std::string& endpoint,
        const std::map<std::string, std::string>& params = {}) {
        
        // Build URL
        std::ostringstream url;
        url << endpoint << "?apiKey=" << config_.api_key;
        
        for (const auto& [key, value] : params) {
            url << "&" << key << "=" << core::url_encode(value);
        }
        
        auto response = http_client_.get(url.str());
        
        if (!response.success) {
            last_error_ = response.error;
            return std::nullopt;
        }
        
        try {
            return core::JsonParser::parse(response.body);
        }
        catch (const std::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return std::nullopt;
        }
    }
    
    std::optional<PolygonSnapshot> parse_snapshot(const core::JsonValue& item) {
        PolygonSnapshot snap;
        snap.ticker = item.value("ticker", "");
        
        if (item.contains("day")) {
            const auto& day = item["day"];
            snap.day_open = day.value("o", 0.0);
            snap.day_high = day.value("h", 0.0);
            snap.day_low = day.value("l", 0.0);
            snap.day_close = day.value("c", 0.0);
            snap.day_vwap = day.value("vw", 0.0);
            snap.day_volume = day.value("v", 0);
        }
        
        if (item.contains("prevDay")) {
            const auto& prev = item["prevDay"];
            snap.prev_open = prev.value("o", 0.0);
            snap.prev_high = prev.value("h", 0.0);
            snap.prev_low = prev.value("l", 0.0);
            snap.prev_close = prev.value("c", 0.0);
            snap.prev_vwap = prev.value("vw", 0.0);
            snap.prev_volume = prev.value("v", 0);
        }
        
        if (item.contains("min")) {
            const auto& min = item["min"];
            snap.minute_open = min.value("o", 0.0);
            snap.minute_high = min.value("h", 0.0);
            snap.minute_low = min.value("l", 0.0);
            snap.minute_close = min.value("c", 0.0);
            snap.minute_vwap = min.value("vw", 0.0);
            snap.minute_volume = min.value("v", 0);
            snap.minute_timestamp = min.value("t", 0);
        }
        
        if (item.contains("lastTrade")) {
            const auto& lt = item["lastTrade"];
            snap.last_price = lt.value("p", 0.0);
            snap.last_size = lt.value("s", 0);
            snap.last_timestamp = lt.value("t", 0);
        }
        
        snap.change = item.value("todaysChange", 0.0);
        snap.change_percent = item.value("todaysChangePerc", 0.0);
        
        return snap;
    }
};

/**
 * @brief Polygon WebSocket streaming client
 */
class PolygonStream {
public:
    explicit PolygonStream(const PolygonConfig& config)
        : config_(config)
        , connected_(false)
        , authenticated_(false) {}
    
    ~PolygonStream() {
        stop();
    }
    
    /**
     * @brief Connect to WebSocket
     */
    bool connect() {
        if (connected_) return true;
        
        core::WsConfig ws_config;
        ws_config.url = config_.websocket_url();
        ws_client_ = std::make_unique<core::WebSocketClient>(ws_config);
        
        ws_client_->on_message([this](const core::WsMessage& msg) {
            handle_message(msg.data);
        });
        
        ws_client_->on_open([this]() {
            connected_ = true;
            authenticate();
        });
        
        ws_client_->on_close([this]() {
            connected_ = false;
            authenticated_ = false;
            if (on_status_) {
                on_status_("disconnected");
            }
        });
        
        ws_client_->on_error([this](const std::string& error) {
            last_error_ = error;
            if (on_status_) {
                on_status_("error: " + error);
            }
        });
        
        return ws_client_->connect();
    }
    
    /**
     * @brief Stop connection
     */
    void stop() {
        if (ws_client_) {
            ws_client_->close();
            ws_client_.reset();
        }
        connected_ = false;
        authenticated_ = false;
    }
    
    /**
     * @brief Subscribe to trades
     */
    bool subscribe_trades(const std::vector<std::string>& symbols) {
        if (!authenticated_) return false;
        
        std::ostringstream oss;
        oss << R"({"action":"subscribe","params":")";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "T." << symbols[i];
        }
        oss << "\"}";
        
        ws_client_->send(oss.str());
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_trades_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Subscribe to quotes
     */
    bool subscribe_quotes(const std::vector<std::string>& symbols) {
        if (!authenticated_) return false;
        
        std::ostringstream oss;
        oss << R"({"action":"subscribe","params":")";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "Q." << symbols[i];
        }
        oss << "\"}";
        
        ws_client_->send(oss.str());
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_quotes_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Subscribe to minute bars
     */
    bool subscribe_bars(const std::vector<std::string>& symbols) {
        if (!authenticated_) return false;
        
        std::ostringstream oss;
        oss << R"({"action":"subscribe","params":")";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "AM." << symbols[i];
        }
        oss << "\"}";
        
        ws_client_->send(oss.str());
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_bars_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Unsubscribe from trades
     */
    bool unsubscribe_trades(const std::vector<std::string>& symbols) {
        if (!authenticated_) return false;
        
        std::ostringstream oss;
        oss << R"({"action":"unsubscribe","params":")";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "T." << symbols[i];
        }
        oss << "\"}";
        
        ws_client_->send(oss.str());
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_trades_.erase(sym);
        }
        
        return true;
    }
    
    // Callbacks
    void on_trade(OnPolygonTradeCallback callback) { on_trade_ = callback; }
    void on_quote(OnPolygonQuoteCallback callback) { on_quote_ = callback; }
    void on_bar(OnPolygonBarCallback callback) { on_bar_ = callback; }
    void on_status(OnPolygonStatusCallback callback) { on_status_ = callback; }
    
    // Status
    bool is_connected() const { return connected_; }
    bool is_authenticated() const { return authenticated_; }
    const std::string& last_error() const { return last_error_; }

private:
    PolygonConfig config_;
    std::unique_ptr<core::WebSocketClient> ws_client_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> authenticated_;
    std::string last_error_;
    
    std::mutex mutex_;
    std::set<std::string> subscribed_trades_;
    std::set<std::string> subscribed_quotes_;
    std::set<std::string> subscribed_bars_;
    
    OnPolygonTradeCallback on_trade_;
    OnPolygonQuoteCallback on_quote_;
    OnPolygonBarCallback on_bar_;
    OnPolygonStatusCallback on_status_;
    
    void authenticate() {
        std::ostringstream oss;
        oss << R"({"action":"auth","params":")" << config_.api_key << "\"}";
        ws_client_->send(oss.str());
    }
    
    void handle_message(const std::string& msg) {
        try {
            auto json = core::JsonParser::parse(msg);
            
            if (!json.is_array()) return;
            
            for (const auto& item : json) {
                std::string ev = item.value("ev", "");
                
                if (ev == "status") {
                    std::string status = item.value("status", "");
                    if (status == "auth_success") {
                        authenticated_ = true;
                        if (on_status_) {
                            on_status_("authenticated");
                        }
                        resubscribe_all();
                    } else if (status == "connected") {
                        if (on_status_) {
                            on_status_("connected");
                        }
                    }
                }
                else if (ev == "T") {
                    // Trade
                    PolygonTrade trade;
                    trade.symbol = item.value("sym", "");
                    trade.price = item.value("p", 0.0);
                    trade.size = item.value("s", 0);
                    trade.timestamp = item.value("t", 0);
                    trade.exchange = item.value("x", "");
                    trade.conditions = item.value("c", 0);
                    
                    if (on_trade_) {
                        on_trade_(trade);
                    }
                }
                else if (ev == "Q") {
                    // Quote
                    PolygonQuote quote;
                    quote.symbol = item.value("sym", "");
                    quote.bid = item.value("bp", 0.0);
                    quote.ask = item.value("ap", 0.0);
                    quote.bid_size = item.value("bs", 0);
                    quote.ask_size = item.value("as", 0);
                    quote.timestamp = item.value("t", 0);
                    
                    if (on_quote_) {
                        on_quote_(quote);
                    }
                }
                else if (ev == "AM") {
                    // Minute bar
                    PolygonBar bar;
                    bar.ticker = item.value("sym", "");
                    bar.open = item.value("o", 0.0);
                    bar.high = item.value("h", 0.0);
                    bar.low = item.value("l", 0.0);
                    bar.close = item.value("c", 0.0);
                    bar.vwap = item.value("vw", 0.0);
                    bar.volume = item.value("v", 0);
                    bar.timestamp = item.value("e", 0);
                    
                    if (on_bar_) {
                        on_bar_(bar);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            last_error_ = std::string("Parse error: ") + e.what();
        }
    }
    
    void resubscribe_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!subscribed_trades_.empty()) {
            std::vector<std::string> symbols(subscribed_trades_.begin(), 
                                             subscribed_trades_.end());
            subscribe_trades(symbols);
        }
        
        if (!subscribed_quotes_.empty()) {
            std::vector<std::string> symbols(subscribed_quotes_.begin(),
                                             subscribed_quotes_.end());
            subscribe_quotes(symbols);
        }
        
        if (!subscribed_bars_.empty()) {
            std::vector<std::string> symbols(subscribed_bars_.begin(),
                                             subscribed_bars_.end());
            subscribe_bars(symbols);
        }
    }
};

/**
 * @brief Create Polygon client from environment variable
 */
inline std::unique_ptr<PolygonClient> create_polygon_client_from_env() {
    const char* key = std::getenv("POLYGON_API_KEY");
    if (!key) return nullptr;
    
    PolygonConfig config;
    config.api_key = key;
    
    return std::make_unique<PolygonClient>(config);
}

} // namespace genie::market

#endif // GENIE_MARKET_POLYGON_CLIENT_HPP
