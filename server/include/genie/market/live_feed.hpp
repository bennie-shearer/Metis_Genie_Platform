/**
 * @file live_feed.hpp
 * @brief Real-time market data feed manager
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Manages real-time price streaming:
 * - WebSocket connections to data providers
 * - Price update distribution to subscribers
 * - Automatic reconnection and failover
 * - Price cache updates
 * - Staleness detection
 * 
 * Supported feeds:
 * - Alpaca real-time trades/quotes (paper and live)
 * - Polygon.io WebSocket (stocks, options, forex, crypto)
 * - Finnhub real-time data
 */
#pragma once
#ifndef GENIE_MARKET_LIVE_FEED_HPP
#define GENIE_MARKET_LIVE_FEED_HPP

#include "../core/websocket_client.hpp"
#include "../core/http_client.hpp"
#include "alpha_vantage.hpp"
#include "price_cache.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <queue>

namespace genie::market {

/**
 * @brief Feed provider type
 */
enum class FeedProvider {
    Alpaca,
    Polygon,
    Finnhub,
    IEXCloud,
    Manual
};

inline std::string feed_provider_to_string(FeedProvider provider) {
    switch (provider) {
        case FeedProvider::Alpaca: return "alpaca";
        case FeedProvider::Polygon: return "polygon";
        case FeedProvider::Finnhub: return "finnhub";
        case FeedProvider::IEXCloud: return "iex_cloud";
        case FeedProvider::Manual: return "manual";
    }
    return "unknown";
}

/**
 * @brief Live price tick
 */
struct PriceTick {
    std::string symbol;
    double price{0};
    double bid{0};
    double ask{0};
    int64_t size{0};
    int64_t volume{0};
    std::string exchange;
    FeedProvider provider{FeedProvider::Manual};
    std::chrono::system_clock::time_point timestamp;
    
    double mid() const {
        if (bid > 0 && ask > 0) return (bid + ask) / 2.0;
        return price;
    }
    
    double spread() const {
        if (bid > 0 && ask > 0) return ask - bid;
        return 0;
    }
};

/**
 * @brief Feed configuration
 */
struct FeedConfig {
    FeedProvider provider{FeedProvider::Alpaca};
    std::string api_key;
    std::string api_secret;
    bool paper{true};  // For Alpaca
    
    // WebSocket URLs by provider
    std::string alpaca_paper_url{"wss://stream.data.alpaca.markets/v2/iex"};
    std::string alpaca_live_url{"wss://stream.data.alpaca.markets/v2/sip"};
    std::string polygon_url{"wss://socket.polygon.io/stocks"};
    std::string finnhub_url{"wss://ws.finnhub.io"};
    
    // Reconnection settings
    bool auto_reconnect{true};
    int reconnect_delay_ms{5000};
    int max_reconnect_attempts{10};
    
    // Heartbeat
    int ping_interval_ms{30000};
    
    std::string get_url() const {
        switch (provider) {
            case FeedProvider::Alpaca:
                return paper ? alpaca_paper_url : alpaca_live_url;
            case FeedProvider::Polygon:
                return polygon_url;
            case FeedProvider::Finnhub:
                return finnhub_url;
            default:
                return "";
        }
    }
};

/**
 * @brief Feed connection status
 */
struct FeedStatus {
    bool connected{false};
    FeedProvider provider{FeedProvider::Manual};
    int subscribed_symbols{0};
    int messages_received{0};
    int errors{0};
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_message;
    std::string last_error;
};

// Callbacks
using OnTickCallback = std::function<void(const PriceTick&)>;
using OnFeedStatusCallback = std::function<void(const FeedStatus&)>;
using OnErrorCallback = std::function<void(const std::string&)>;

/**
 * @brief Live market data feed manager
 */
class LiveFeed {
public:
    explicit LiveFeed(const FeedConfig& config = {})
        : config_(config)
        , running_(false)
        , messages_received_(0)
        , errors_(0) {}
    
    ~LiveFeed() {
        stop();
    }
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * @brief Connect to feed
     */
    bool connect() {
        if (running_) return true;
        
        // Configure WebSocket client
        core::WsConfig ws_config;
        ws_config.url = config_.get_url();
        ws_config.api_key = config_.api_key;
        ws_config.api_secret = config_.api_secret;
        ws_config.auto_reconnect = config_.auto_reconnect;
        ws_config.reconnect_delay_ms = config_.reconnect_delay_ms;
        ws_config.max_reconnect_attempts = config_.max_reconnect_attempts;
        ws_config.ping_interval_ms = config_.ping_interval_ms;
        
        ws_client_ = std::make_unique<core::WebSocketClient>(ws_config);
        
        // Set up callbacks
        ws_client_->on_trade([this](const core::TradeUpdate& trade) {
            handle_trade(trade);
        });
        
        ws_client_->on_quote([this](const core::QuoteUpdate& quote) {
            handle_quote(quote);
        });
        
        ws_client_->on_bar([this](const core::BarUpdate& bar) {
            handle_bar(bar);
        });
        
        ws_client_->on_state_change([this](core::WsState old_state, core::WsState new_state) {
            handle_state_change(old_state, new_state);
        });
        
        ws_client_->on_error([this](const std::string& error) {
            handle_error(error);
        });
        
        // Connect
        if (!ws_client_->connect()) {
            last_error_ = "Failed to connect to " + feed_provider_to_string(config_.provider);
            return false;
        }
        
        running_ = true;
        connected_at_ = std::chrono::system_clock::now();
        
        fire_status_update();
        
        return true;
    }
    
    /**
     * @brief Disconnect from feed
     */
    void stop() {
        running_ = false;
        
        if (ws_client_) {
            ws_client_->disconnect();
            ws_client_.reset();
        }
        
        fire_status_update();
    }
    
    /**
     * @brief Check if connected
     */
    bool is_connected() const {
        return running_ && ws_client_ && ws_client_->is_connected();
    }
    
    // ========================================================================
    // Subscriptions
    // ========================================================================
    
    /**
     * @brief Subscribe to trades for symbols
     */
    bool subscribe_trades(const std::vector<std::string>& symbols) {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return false;
        }
        
        if (!ws_client_->subscribe_trades(symbols)) {
            last_error_ = "Failed to subscribe to trades";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Subscribe to quotes for symbols
     */
    bool subscribe_quotes(const std::vector<std::string>& symbols) {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return false;
        }
        
        if (!ws_client_->subscribe_quotes(symbols)) {
            last_error_ = "Failed to subscribe to quotes";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Subscribe to bars for symbols
     */
    bool subscribe_bars(const std::vector<std::string>& symbols) {
        if (!is_connected()) {
            last_error_ = "Not connected";
            return false;
        }
        
        if (!ws_client_->subscribe_bars(symbols)) {
            last_error_ = "Failed to subscribe to bars";
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.insert(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Subscribe to all data types for symbols
     */
    bool subscribe(const std::vector<std::string>& symbols) {
        bool success = true;
        success &= subscribe_trades(symbols);
        success &= subscribe_quotes(symbols);
        return success;
    }
    
    /**
     * @brief Unsubscribe from symbols
     */
    bool unsubscribe(const std::vector<std::string>& symbols) {
        if (!is_connected()) return false;
        
        if (!ws_client_->unsubscribe_trades(symbols) ||
            !ws_client_->unsubscribe_quotes(symbols)) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sym : symbols) {
            subscribed_symbols_.erase(sym);
        }
        
        return true;
    }
    
    /**
     * @brief Get subscribed symbols
     */
    std::vector<std::string> get_subscribed_symbols() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::string>(subscribed_symbols_.begin(), subscribed_symbols_.end());
    }
    
    // ========================================================================
    // Price Access
    // ========================================================================
    
    /**
     * @brief Get last price for symbol
     */
    std::optional<double> get_last_price(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_prices_.find(symbol);
        if (it == last_prices_.end()) return std::nullopt;
        return it->second.price;
    }
    
    /**
     * @brief Get last tick for symbol
     */
    std::optional<PriceTick> get_last_tick(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_prices_.find(symbol);
        if (it == last_prices_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get all last prices
     */
    std::map<std::string, PriceTick> get_all_prices() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_prices_;
    }
    
    /**
     * @brief Get price age in seconds
     */
    int get_price_age(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = last_prices_.find(symbol);
        if (it == last_prices_.end()) return -1;
        
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - it->second.timestamp).count();
        return static_cast<int>(age);
    }
    
    // ========================================================================
    // Manual Price Injection (for testing/simulation)
    // ========================================================================
    
    /**
     * @brief Inject a price tick manually
     */
    void inject_tick(const PriceTick& tick) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_prices_[tick.symbol] = tick;
            last_message_ = tick.timestamp;
            messages_received_++;
        }
        
        if (on_tick_) {
            on_tick_(tick);
        }
    }
    
    /**
     * @brief Inject price for symbol
     */
    void inject_price(const std::string& symbol, double price,
                      double bid = 0, double ask = 0) {
        PriceTick tick;
        tick.symbol = symbol;
        tick.price = price;
        tick.bid = bid;
        tick.ask = ask;
        tick.provider = FeedProvider::Manual;
        tick.timestamp = std::chrono::system_clock::now();
        
        inject_tick(tick);
    }
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    void on_tick(OnTickCallback callback) { on_tick_ = callback; }
    void on_status(OnFeedStatusCallback callback) { on_status_ = callback; }
    void on_error(OnErrorCallback callback) { on_error_ = callback; }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    /**
     * @brief Get feed status
     */
    FeedStatus get_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        FeedStatus status;
        status.connected = is_connected();
        status.provider = config_.provider;
        status.subscribed_symbols = static_cast<int>(subscribed_symbols_.size());
        status.messages_received = messages_received_;
        status.errors = errors_;
        status.connected_at = connected_at_;
        status.last_message = last_message_;
        status.last_error = last_error_;
        
        return status;
    }
    
    const FeedConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }

private:
    FeedConfig config_;
    std::unique_ptr<core::WebSocketClient> ws_client_;
    
    std::atomic<bool> running_;
    std::chrono::system_clock::time_point connected_at_;
    std::chrono::system_clock::time_point last_message_;
    int messages_received_;
    int errors_;
    std::string last_error_;
    
    std::set<std::string> subscribed_symbols_;
    std::map<std::string, PriceTick> last_prices_;
    
    mutable std::mutex mutex_;
    
    OnTickCallback on_tick_;
    OnFeedStatusCallback on_status_;
    OnErrorCallback on_error_;
    
    void handle_trade(const core::TradeUpdate& trade) {
        PriceTick tick;
        tick.symbol = trade.symbol;
        tick.price = trade.price;
        tick.size = trade.size;
        tick.exchange = trade.exchange;
        tick.provider = config_.provider;
        tick.timestamp = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_prices_[trade.symbol] = tick;
            last_message_ = tick.timestamp;
            messages_received_++;
        }
        
        if (on_tick_) {
            on_tick_(tick);
        }
    }
    
    void handle_quote(const core::QuoteUpdate& quote) {
        PriceTick tick;
        tick.symbol = quote.symbol;
        tick.bid = quote.bid_price;
        tick.ask = quote.ask_price;
        tick.price = tick.mid();
        tick.provider = config_.provider;
        tick.timestamp = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_prices_[quote.symbol] = tick;
            last_message_ = tick.timestamp;
            messages_received_++;
        }
        
        if (on_tick_) {
            on_tick_(tick);
        }
    }
    
    void handle_bar(const core::BarUpdate& bar) {
        PriceTick tick;
        tick.symbol = bar.symbol;
        tick.price = bar.close;
        tick.volume = bar.volume;
        tick.provider = config_.provider;
        tick.timestamp = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_prices_[bar.symbol] = tick;
            last_message_ = tick.timestamp;
            messages_received_++;
        }
        
        if (on_tick_) {
            on_tick_(tick);
        }
    }
    
    void handle_state_change([[maybe_unused]] core::WsState old_state, core::WsState new_state) {
        if (new_state == core::WsState::Connected || 
            new_state == core::WsState::Authenticated) {
            connected_at_ = std::chrono::system_clock::now();
        }
        
        fire_status_update();
    }
    
    void handle_error(const std::string& error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_ = error;
            errors_++;
        }
        
        if (on_error_) {
            on_error_(error);
        }
        
        fire_status_update();
    }
    
    void fire_status_update() {
        if (on_status_) {
            on_status_(get_status());
        }
    }
};

/**
 * @brief Multi-feed aggregator for redundancy
 */
class FeedAggregator {
public:
    /**
     * @brief Add a feed
     */
    void add_feed(const std::string& name, std::shared_ptr<LiveFeed> feed) {
        std::lock_guard<std::mutex> lock(mutex_);
        feeds_[name] = feed;
        
        // Set up tick forwarding
        feed->on_tick([this, name](const PriceTick& tick) {
            handle_tick(name, tick);
        });
    }
    
    /**
     * @brief Remove a feed
     */
    void remove_feed(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        feeds_.erase(name);
    }
    
    /**
     * @brief Connect all feeds
     */
    int connect_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        int connected = 0;
        for (auto& [name, feed] : feeds_) {
            if (feed->connect()) connected++;
        }
        return connected;
    }
    
    /**
     * @brief Stop all feeds
     */
    void stop_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, feed] : feeds_) {
            feed->stop();
        }
    }
    
    /**
     * @brief Subscribe on all feeds
     */
    int subscribe_all(const std::vector<std::string>& symbols) {
        std::lock_guard<std::mutex> lock(mutex_);
        int subscribed = 0;
        for (auto& [name, feed] : feeds_) {
            if (feed->subscribe(symbols)) subscribed++;
        }
        return subscribed;
    }
    
    /**
     * @brief Get best price (most recent) for symbol
     */
    std::optional<PriceTick> get_best_price(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = best_prices_.find(symbol);
        if (it == best_prices_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Set tick callback
     */
    void on_tick(OnTickCallback callback) { on_tick_ = callback; }
    
    /**
     * @brief Get all feed statuses
     */
    std::map<std::string, FeedStatus> get_all_statuses() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, FeedStatus> statuses;
        for (const auto& [name, feed] : feeds_) {
            statuses[name] = feed->get_status();
        }
        return statuses;
    }

private:
    std::map<std::string, std::shared_ptr<LiveFeed>> feeds_;
    std::map<std::string, PriceTick> best_prices_;
    mutable std::mutex mutex_;
    OnTickCallback on_tick_;
    
    void handle_tick([[maybe_unused]] const std::string& feed_name, const PriceTick& tick) {
        bool is_better = false;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = best_prices_.find(tick.symbol);
            if (it == best_prices_.end() || tick.timestamp > it->second.timestamp) {
                best_prices_[tick.symbol] = tick;
                is_better = true;
            }
        }
        
        // Only fire callback for better/newer prices
        if (is_better && on_tick_) {
            on_tick_(tick);
        }
    }
};

} // namespace genie::market

#endif // GENIE_MARKET_LIVE_FEED_HPP
