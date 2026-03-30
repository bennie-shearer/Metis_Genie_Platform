/**
 * @file websocket_client.hpp
 * @brief WebSocket client for real-time price streaming
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides WebSocket connectivity for real-time data:
 * - Connection management with auto-reconnect
 * - Subscription management for symbols
 * - Message parsing and routing
 * - Heartbeat/ping-pong handling
 * 
 * Supports integration with:
 * - Alpaca real-time trades/quotes
 * - Polygon.io WebSocket
 * - Finnhub real-time data
 */
#pragma once
#ifndef GENIE_CORE_WEBSOCKET_CLIENT_HPP
#define GENIE_CORE_WEBSOCKET_CLIENT_HPP

#include "http_client.hpp"
#include "platform_websocket.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace genie::core {

/**
 * @brief WebSocket connection state
 */
enum class WsState {
    Disconnected,
    Connecting,
    Connected,
    Authenticated,
    Subscribed,
    Reconnecting,
    Error,
    Closed
};

inline std::string ws_state_to_string(WsState state) {
    switch (state) {
        case WsState::Disconnected: return "disconnected";
        case WsState::Connecting: return "connecting";
        case WsState::Connected: return "connected";
        case WsState::Authenticated: return "authenticated";
        case WsState::Subscribed: return "subscribed";
        case WsState::Reconnecting: return "reconnecting";
        case WsState::Error: return "error";
        case WsState::Closed: return "closed";
    }
    return "unknown";
}

/**
 * @brief WebSocket message types
 */
enum class WsMessageType {
    Text,
    Binary,
    Ping,
    Pong,
    Close,
    Error
};

/**
 * @brief Incoming WebSocket message
 */
struct WsMessage {
    WsMessageType type{WsMessageType::Text};
    std::string data;
    std::chrono::system_clock::time_point timestamp;
    std::string channel;
    std::string symbol;
};

/**
 * @brief Real-time trade update
 */
struct TradeUpdate {
    std::string symbol;
    double price{0};
    int64_t size{0};
    std::string exchange;
    std::string timestamp;
    std::string conditions;
    int64_t trade_id{0};
};

/**
 * @brief Real-time quote update
 */
struct QuoteUpdate {
    std::string symbol;
    double bid_price{0};
    double ask_price{0};
    int64_t bid_size{0};
    int64_t ask_size{0};
    std::string bid_exchange;
    std::string ask_exchange;
    std::string timestamp;
};

/**
 * @brief Real-time bar update
 */
struct BarUpdate {
    std::string symbol;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    int64_t volume{0};
    std::string timestamp;
    int64_t trade_count{0};
    double vwap{0};
};

/**
 * @brief WebSocket client configuration
 */
struct WsConfig {
    std::string url;
    std::string api_key;
    std::string api_secret;
    int reconnect_delay_ms{5000};
    int max_reconnect_attempts{10};
    int ping_interval_ms{30000};
    int connection_timeout_ms{10000};
    bool auto_reconnect{true};
    bool compress{false};
};

/**
 * @brief Callback types
 */
using OnTradeCallback = std::function<void(const TradeUpdate&)>;
using OnQuoteCallback = std::function<void(const QuoteUpdate&)>;
using OnBarCallback = std::function<void(const BarUpdate&)>;
using OnMessageCallback = std::function<void(const WsMessage&)>;
using OnStateChangeCallback = std::function<void(WsState, WsState)>;
using OnErrorCallback = std::function<void(const std::string&)>;

/**
 * @brief WebSocket client for real-time streaming
 * 
 * Uses platform-native WebSocket via platform_websocket.hpp:
 *   Windows: WinHTTP WebSocket, macOS/Linux: RFC 6455 over TLS
 */
class WebSocketClient {
public:
    explicit WebSocketClient(const WsConfig& config = {})
        : config_(config)
        , state_(WsState::Disconnected)
        , running_(false)
        , reconnect_attempts_(0) {}
    
    ~WebSocketClient() {
        disconnect();
    }
    
    // === Connection Management ===
    
    /**
     * @brief Connect to WebSocket server
     */
    bool connect() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == WsState::Connected || state_ == WsState::Authenticated) {
            return true;
        }
        
        set_state(WsState::Connecting);
        
        // Connect via platform-native WebSocket
        if (simulate_connection()) {
            set_state(WsState::Connected);
            running_ = true;
            
            // Start message processing thread
            if (!worker_thread_.joinable()) {
                worker_thread_ = std::thread(&WebSocketClient::process_messages, this);
            }
            
            // Authenticate if credentials provided
            if (!config_.api_key.empty()) {
                return authenticate();
            }
            
            return true;
        }
        
        set_state(WsState::Error);
        last_error_ = "Failed to connect to " + config_.url;
        return false;
    }
    
    /**
     * @brief Disconnect from server
     */
    void disconnect() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            set_state(WsState::Closed);
        }
        
        cv_.notify_all();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        subscriptions_.clear();
    }
    
    /**
     * @brief Check if connected
     */
    bool is_connected() const {
        return state_ == WsState::Connected || 
               state_ == WsState::Authenticated ||
               state_ == WsState::Subscribed;
    }
    
    /**
     * @brief Get current state
     */
    WsState state() const { return state_; }
    
    // === Authentication ===
    
    /**
     * @brief Authenticate with API credentials
     */
    bool authenticate() {
        if (config_.api_key.empty()) {
            last_error_ = "No API key configured";
            return false;
        }
        
        // Build authentication message (Alpaca format)
        std::string auth_msg = R"({"action":"auth","key":")" + config_.api_key + 
                               R"(","secret":")" + config_.api_secret + R"("})";
        
        if (send(auth_msg)) {
            set_state(WsState::Authenticated);
            return true;
        }
        
        return false;
    }
    
    // === Subscription Management ===
    
    /**
     * @brief Subscribe to trades for symbols
     */
    bool subscribe_trades(const std::vector<std::string>& symbols) {
        return subscribe("trades", symbols);
    }
    
    /**
     * @brief Subscribe to quotes for symbols
     */
    bool subscribe_quotes(const std::vector<std::string>& symbols) {
        return subscribe("quotes", symbols);
    }
    
    /**
     * @brief Subscribe to bars for symbols
     */
    bool subscribe_bars(const std::vector<std::string>& symbols) {
        return subscribe("bars", symbols);
    }
    
    /**
     * @brief Subscribe to channel with symbols
     */
    bool subscribe(const std::string& channel, const std::vector<std::string>& symbols) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Build subscription message (Alpaca format)
        std::string symbols_json = "[";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) symbols_json += ",";
            symbols_json += "\"" + symbols[i] + "\"";
        }
        symbols_json += "]";
        
        std::string sub_msg = R"({"action":"subscribe",")" + channel + R"(":)" + symbols_json + "}";
        
        if (send(sub_msg)) {
            for (const auto& sym : symbols) {
                subscriptions_[channel].insert(sym);
            }
            set_state(WsState::Subscribed);
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Unsubscribe from channel
     */
    bool unsubscribe(const std::string& channel, const std::vector<std::string>& symbols) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string symbols_json = "[";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) symbols_json += ",";
            symbols_json += "\"" + symbols[i] + "\"";
        }
        symbols_json += "]";
        
        std::string unsub_msg = R"({"action":"unsubscribe",")" + channel + R"(":)" + symbols_json + "}";
        
        if (send(unsub_msg)) {
            for (const auto& sym : symbols) {
                subscriptions_[channel].erase(sym);
            }
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Get subscribed symbols for channel
     */
    std::set<std::string> get_subscriptions(const std::string& channel) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(channel);
        if (it != subscriptions_.end()) {
            return it->second;
        }
        return {};
    }
    
    /**
     * @brief Unsubscribe from trades
     */
    bool unsubscribe_trades(const std::vector<std::string>& symbols) {
        return unsubscribe("trades", symbols);
    }
    
    /**
     * @brief Unsubscribe from quotes
     */
    bool unsubscribe_quotes(const std::vector<std::string>& symbols) {
        return unsubscribe("quotes", symbols);
    }
    
    /**
     * @brief Unsubscribe from bars
     */
    bool unsubscribe_bars(const std::vector<std::string>& symbols) {
        return unsubscribe("bars", symbols);
    }
    
    /**
     * @brief Close connection (alias for disconnect)
     */
    void close() { disconnect(); }
    
    // === Message Sending ===
    
    /**
     * @brief Send raw message
     */
    bool send(const std::string& message) {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return false;
        }
        
        // Send via real WebSocket connection
        if (platform_ws_ && platform_ws_->is_connected()) {
            return platform_ws_->send_text(message);
        }
        
        // Fallback: queue for when connection is established
        {
            std::lock_guard<std::mutex> lock(mutex_);
            outgoing_queue_.push(message);
        }
        
        return true;
    }
    
    // === Callback Registration ===
    
    void on_trade(OnTradeCallback callback) { on_trade_ = callback; }
    void on_quote(OnQuoteCallback callback) { on_quote_ = callback; }
    void on_bar(OnBarCallback callback) { on_bar_ = callback; }
    void on_message(OnMessageCallback callback) { on_message_ = callback; }
    void on_state_change(OnStateChangeCallback callback) { on_state_change_ = callback; }
    void on_error(OnErrorCallback callback) { on_error_ = callback; }
    void on_open(std::function<void()> callback) { on_open_ = callback; }
    void on_close(std::function<void()> callback) { on_close_ = callback; }
    
    // === Simulation/Testing ===
    
    /**
     * @brief Inject simulated trade (for testing)
     */
    void inject_trade(const TradeUpdate& trade) {
        if (on_trade_) {
            on_trade_(trade);
        }
    }
    
    /**
     * @brief Inject simulated quote (for testing)
     */
    void inject_quote(const QuoteUpdate& quote) {
        if (on_quote_) {
            on_quote_(quote);
        }
    }
    
    /**
     * @brief Inject simulated bar (for testing)
     */
    void inject_bar(const BarUpdate& bar) {
        if (on_bar_) {
            on_bar_(bar);
        }
    }
    
    /**
     * @brief Inject raw message (for testing)
     */
    void inject_message(const std::string& json) {
        process_incoming_message(json);
    }
    
    // === Accessors ===
    
    const std::string& last_error() const { return last_error_; }
    const WsConfig& config() const { return config_; }
    void set_config(const WsConfig& config) { config_ = config; }

private:
    WsConfig config_;
    std::atomic<WsState> state_;
    std::atomic<bool> running_;
    int reconnect_attempts_;
    std::string last_error_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    
    std::map<std::string, std::set<std::string>> subscriptions_;
    std::queue<std::string> incoming_queue_;
    std::queue<std::string> outgoing_queue_;
    
    // Platform-native WebSocket connection
    std::unique_ptr<PlatformWebSocket> platform_ws_;
    
    // Callbacks
    OnTradeCallback on_trade_;
    OnQuoteCallback on_quote_;
    OnBarCallback on_bar_;
    OnMessageCallback on_message_;
    OnStateChangeCallback on_state_change_;
    OnErrorCallback on_error_;
    std::function<void()> on_open_;
    std::function<void()> on_close_;
    
    void set_state(WsState new_state) {
        WsState old_state = state_.exchange(new_state);
        if (on_state_change_ && old_state != new_state) {
            on_state_change_(old_state, new_state);
        }
    }
    
    bool simulate_connection() {
        // Real WebSocket connection via platform-native implementation
        if (config_.url.empty()) {
            last_error_ = "No WebSocket URL configured";
            return false;
        }
        
        platform_ws_ = std::make_unique<PlatformWebSocket>();
        if (!platform_ws_->connect(config_.url, config_.connection_timeout_ms)) {
            last_error_ = "WebSocket connection failed to " + config_.url;
            platform_ws_.reset();
            return false;
        }
        return true;
    }
    
    void process_messages() {
        while (running_) {
            // Send any queued outgoing messages
            {
                std::lock_guard<std::mutex> lock(mutex_);
                while (!outgoing_queue_.empty() && platform_ws_ && platform_ws_->is_connected()) {
                    platform_ws_->send_text(outgoing_queue_.front());
                    outgoing_queue_.pop();
                }
            }
            
            // Receive from real WebSocket
            if (platform_ws_ && platform_ws_->is_connected()) {
                auto frame = platform_ws_->receive(100); // 100ms timeout
                
                if (frame.opcode == WsOpcode::Text && !frame.payload.empty()) {
                    process_incoming_message(frame.payload);
                } else if (frame.opcode == WsOpcode::Close) {
                    if (running_) {
                        attempt_reconnect();
                    }
                }
            } else if (running_) {
                // Not connected - try reconnect
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (running_) attempt_reconnect();
            }
        }
    }
    
    void process_incoming_message(const std::string& json) {
        auto parsed = JsonParser::parse(json);
        
        // Determine message type and route to appropriate callback
        if (parsed.is_array()) {
            for (size_t i = 0; i < parsed.size(); ++i) {
                process_single_message(parsed[i]);
            }
        } else if (parsed.is_object()) {
            process_single_message(parsed);
        }
        
        // Also fire raw message callback
        if (on_message_) {
            WsMessage msg;
            msg.type = WsMessageType::Text;
            msg.data = json;
            msg.timestamp = std::chrono::system_clock::now();
            on_message_(msg);
        }
    }
    
    void process_single_message(const JsonValue& msg) {
        std::string type = msg["T"].as_string();
        
        if (type == "t" && on_trade_) {
            // Trade message
            TradeUpdate trade;
            trade.symbol = msg["S"].as_string();
            trade.price = msg["p"].as_number();
            trade.size = static_cast<int64_t>(msg["s"].as_number());
            trade.exchange = msg["x"].as_string();
            trade.timestamp = msg["t"].as_string();
            trade.trade_id = static_cast<int64_t>(msg["i"].as_number());
            on_trade_(trade);
        }
        else if (type == "q" && on_quote_) {
            // Quote message
            QuoteUpdate quote;
            quote.symbol = msg["S"].as_string();
            quote.bid_price = msg["bp"].as_number();
            quote.ask_price = msg["ap"].as_number();
            quote.bid_size = static_cast<int64_t>(msg["bs"].as_number());
            quote.ask_size = static_cast<int64_t>(msg["as"].as_number());
            quote.bid_exchange = msg["bx"].as_string();
            quote.ask_exchange = msg["ax"].as_string();
            quote.timestamp = msg["t"].as_string();
            on_quote_(quote);
        }
        else if (type == "b" && on_bar_) {
            // Bar message
            BarUpdate bar;
            bar.symbol = msg["S"].as_string();
            bar.open = msg["o"].as_number();
            bar.high = msg["h"].as_number();
            bar.low = msg["l"].as_number();
            bar.close = msg["c"].as_number();
            bar.volume = static_cast<int64_t>(msg["v"].as_number());
            bar.timestamp = msg["t"].as_string();
            bar.trade_count = static_cast<int64_t>(msg["n"].as_number());
            bar.vwap = msg["vw"].as_number();
            on_bar_(bar);
        }
        else if (type == "error" && on_error_) {
            on_error_(msg["msg"].as_string());
        }
    }
    
    void attempt_reconnect() {
        if (!config_.auto_reconnect) return;
        if (reconnect_attempts_ >= config_.max_reconnect_attempts) {
            set_state(WsState::Error);
            last_error_ = "Max reconnection attempts reached";
            if (on_error_) on_error_(last_error_);
            return;
        }
        
        set_state(WsState::Reconnecting);
        reconnect_attempts_++;
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.reconnect_delay_ms * reconnect_attempts_));
        
        if (connect()) {
            reconnect_attempts_ = 0;
            // Resubscribe to all channels
            for (const auto& [channel, symbols] : subscriptions_) {
                std::vector<std::string> sym_vec(symbols.begin(), symbols.end());
                subscribe(channel, sym_vec);
            }
        }
    }
};

/**
 * @brief Multi-source price stream aggregator
 */
class PriceStreamAggregator {
public:
    using PriceCallback = std::function<void(const std::string&, double, int64_t)>;
    
    void add_source(const std::string& name, std::shared_ptr<WebSocketClient> client) {
        sources_[name] = client;
        
        // Set up callbacks to route to aggregated handlers
        client->on_trade([this, name](const TradeUpdate& trade) {
            std::lock_guard<std::mutex> lock(mutex_);
            last_prices_[trade.symbol] = {trade.price, trade.size, name,
                std::chrono::system_clock::now()};
            
            if (on_price_) {
                on_price_(trade.symbol, trade.price, trade.size);
            }
        });
        
        client->on_quote([this, name](const QuoteUpdate& quote) {
            std::lock_guard<std::mutex> lock(mutex_);
            double mid = (quote.bid_price + quote.ask_price) / 2.0;
            last_prices_[quote.symbol] = {mid, 0, name,
                std::chrono::system_clock::now()};
        });
    }
    
    void subscribe_all(const std::vector<std::string>& symbols) {
        for (auto& [name, client] : sources_) {
            client->subscribe_trades(symbols);
            client->subscribe_quotes(symbols);
        }
    }
    
    std::optional<double> get_last_price(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_prices_.find(symbol);
        if (it == last_prices_.end()) return std::nullopt;
        return it->second.price;
    }
    
    void on_price(PriceCallback callback) { on_price_ = callback; }

private:
    struct PriceEntry {
        double price;
        int64_t volume;
        std::string source;
        std::chrono::system_clock::time_point timestamp;
    };
    
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<WebSocketClient>> sources_;
    std::map<std::string, PriceEntry> last_prices_;
    PriceCallback on_price_;
};

} // namespace genie::core

#endif // GENIE_CORE_WEBSOCKET_CLIENT_HPP
