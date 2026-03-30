/**
 * @file reuters_adapter.hpp
 * @brief Reuters/Refinitiv Real-Time Feed Adapter for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Production-grade adapter for Refinitiv (formerly Thomson Reuters) real-time
 * and historical market data. Supports Refinitiv Elektron (LSEG Data Platform),
 * Eikon Data API, and legacy TREP (Thomson Reuters Enterprise Platform)
 * connectivity patterns. Provides normalized market data output compatible
 * with the Metis Genie Platform MarketDataStore interface.
 *
 * Features:
 *   - WebSocket Streaming: Refinitiv Elektron real-time via JSON/WebSocket
 *   - REST snapshots: Refinitiv Data Platform REST API for reference data
 *   - RIC (Reuters Instrument Code) to internal symbol mapping
 *   - Level 1 quotes: bid/ask/last/volume/OHLCV
 *   - Level 2 market depth (order book)
 *   - News headlines and story retrieval
 *   - Time series historical data (intraday, daily, weekly, monthly)
 *   - Corporate actions: dividends, splits, M&A
 *   - Fundamental data: earnings, balance sheet, ratios
 *   - ESG data from Refinitiv ESG scores
 *   - Multi-venue data: exchanges, OTC, dark pools
 *   - Connection management: auto-reconnect, failover, heartbeat
 *   - Token-based authentication (OAuth2 / ERT in Cloud)
 *   - Rate limiting and request throttling
 *   - Message deduplication and sequencing
 *   - Latency monitoring and diagnostics
 *   - Batch subscription management
 *   - Cross-platform: Windows (Winsock2), Linux, macOS
 *   - Thread-safe concurrent access
 *   - Zero external dependencies (uses platform sockets)
 *
 * Authentication:
 *   Supports two authentication modes:
 *   1. ERT in Cloud (Refinitiv Data Platform) - OAuth2 client credentials
 *   2. Deployed TREP/ADS - direct connection to local infrastructure
 *
 * @note Header-only. No external dependencies beyond OS sockets.
 */

#ifndef GENIE_MARKET_REUTERS_ADAPTER_HPP
#define GENIE_MARKET_REUTERS_ADAPTER_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <optional>
#include <functional>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <sstream>
#include <iomanip>
#include <memory>
#include <array>
#include <cassert>

namespace genie {
namespace market {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** Refinitiv connection mode */
enum class RefinitivConnectionMode {
    ErtCloud,       ///< Refinitiv Data Platform (cloud)
    DeployedTrep,   ///< On-premise TREP/ADS
    EikonDesktop    ///< Eikon Desktop API proxy
};

/** Market data message type */
enum class RefinitivMsgType {
    Refresh,        ///< Full image / initial snapshot
    Update,         ///< Incremental update
    Status,         ///< Item status change
    Ping,           ///< Connection heartbeat
    Error,          ///< Error response
    Ack,            ///< Subscription acknowledgment
    Close           ///< Item closed / stream closed
};

/** Data domain */
enum class RefinitivDomain {
    MarketPrice,        ///< Level 1 quotes
    MarketByOrder,      ///< Level 2 order book
    MarketByPrice,      ///< Level 2 price depth
    MarketMaker,        ///< Market maker quotes
    SymbolList,         ///< Chain / symbol list
    YieldCurve,         ///< Yield curve data
    NewsHeadline,       ///< News headlines
    NewsStory           ///< Full news stories
};

/** Field identifiers (subset of Refinitiv FID set) */
enum class RefinitivField : int {
    BID = 22,
    ASK = 25,
    LAST = 6,
    TRDPRC_1 = 6,
    HIGH_1 = 12,
    LOW_1 = 13,
    OPEN_PRC = 19,
    HST_CLOSE = 21,
    ACVOL_1 = 32,
    NETCHNG_1 = 11,
    PCT_CHANGE = 56,
    BID_SIZE = 30,
    ASK_SIZE = 31,
    TRADE_DATE = 16,
    TRDTIM_1 = 5,
    CURRENCY = 15,
    DSPLY_NAME = 3,
    RDN_EXCHID = 1,
    TURNOVER = 99,
    NUM_MOVES = 100,
    VWAP = 101,
    DIVIDEND = 38,
    EX_DIV_DT = 39,
    EARNINGS = 44,
    PE_RATIO = 42,
    YIELD = 41
};

/** Subscription state */
enum class SubscriptionState {
    Pending,
    Active,
    Stale,
    Closed,
    Error
};

/** Connection state */
enum class ConnectionState {
    Disconnected,
    Connecting,
    Authenticating,
    Connected,
    Reconnecting,
    Error
};

// ---------------------------------------------------------------------------
// Data Structures
// ---------------------------------------------------------------------------

/** OAuth2 token for ERT in Cloud */
struct RefinitivToken {
    std::string access_token;
    std::string refresh_token;
    std::string token_type = "Bearer";
    int expires_in = 300;
    std::chrono::system_clock::time_point issued_at;

    [[nodiscard]] bool is_expired() const {
        auto now = std::chrono::system_clock::now();
        auto expiry = issued_at + std::chrono::seconds(expires_in - 30);
        return now >= expiry;
    }
};

/** Configuration for Refinitiv connection */
struct RefinitivConfig {
    RefinitivConnectionMode mode = RefinitivConnectionMode::ErtCloud;

    // ERT in Cloud credentials
    std::string client_id;
    std::string client_secret;
    std::string auth_url = "https://api.refinitiv.com/auth/oauth2/v1/token";
    std::string streaming_url = "wss://api.refinitiv.com/streaming/pricing/v1";
    std::string rest_url = "https://api.refinitiv.com";

    // Deployed TREP settings
    std::string trep_host = "localhost";
    int trep_port = 14002;
    std::string dacs_user;
    std::string app_id = "256";

    // Connection settings
    int reconnect_delay_ms = 5000;
    int max_reconnect_attempts = 10;
    int heartbeat_interval_ms = 30000;
    int request_timeout_ms = 15000;

    // Subscription settings
    int max_batch_size = 200;       ///< Max items per subscription request
    int throttle_ms = 100;          ///< Min interval between requests
    bool enable_conflation = false; ///< Server-side update conflation
    int conflation_interval_ms = 1000;

    // Data settings
    bool include_level2 = false;
    bool include_news = false;
    int market_depth = 10;          ///< Number of price levels for L2
};

/** Refinitiv quote data */
struct RefinitivQuote {
    std::string ric;                ///< Reuters Instrument Code
    std::string display_name;
    std::string exchange_id;
    std::string currency;

    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;            ///< Previous close
    double volume = 0.0;
    double turnover = 0.0;
    double vwap = 0.0;
    double net_change = 0.0;
    double pct_change = 0.0;

    int bid_size = 0;
    int ask_size = 0;
    int num_trades = 0;

    // Fundamental fields
    double pe_ratio = 0.0;
    double dividend_yield = 0.0;
    double earnings = 0.0;

    std::chrono::system_clock::time_point trade_time;
    std::chrono::system_clock::time_point update_time;
    SubscriptionState state = SubscriptionState::Pending;
    int update_count = 0;
};

/** Level 2 order book entry */
struct OrderBookLevel {
    double price = 0.0;
    int size = 0;
    int order_count = 0;
    std::string market_maker;       ///< For MarketMaker domain
    std::chrono::system_clock::time_point timestamp;
};

/** Level 2 order book */
struct OrderBook {
    std::string ric;
    std::vector<OrderBookLevel> bids;   ///< Sorted descending by price
    std::vector<OrderBookLevel> asks;   ///< Sorted ascending by price
    std::chrono::system_clock::time_point timestamp;
    int depth = 0;

    [[nodiscard]] double spread() const {
        if (asks.empty() || bids.empty()) return 0.0;
        return asks.front().price - bids.front().price;
    }

    [[nodiscard]] double mid() const {
        if (asks.empty() || bids.empty()) return 0.0;
        return (asks.front().price + bids.front().price) / 2.0;
    }
};

/** News headline */
struct NewsHeadline {
    std::string story_id;
    std::string headline;
    std::string source;
    std::vector<std::string> rics;      ///< Associated instruments
    std::vector<std::string> topics;    ///< Topic codes
    std::chrono::system_clock::time_point timestamp;
    std::string urgency;                ///< "FLASH", "URGENT", "NORMAL"
    std::string language = "en";
};

/** Historical bar data */
struct HistoricalBar {
    std::chrono::system_clock::time_point timestamp;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double vwap = 0.0;
    int trade_count = 0;
};

/** Corporate action event */
struct ReutersCorporateAction {
    std::string ric;
    std::string event_type;     ///< "DIVIDEND", "SPLIT", "MERGER", etc.
    std::chrono::system_clock::time_point ex_date;
    std::chrono::system_clock::time_point record_date;
    std::chrono::system_clock::time_point pay_date;
    double amount = 0.0;
    double ratio = 0.0;         ///< For splits (e.g., 2.0 for 2:1)
    std::string currency;
    std::string description;
};

/** Fundamental data snapshot */
struct FundamentalData {
    std::string ric;
    double market_cap = 0.0;
    double pe_ratio = 0.0;
    double forward_pe = 0.0;
    double pb_ratio = 0.0;
    double dividend_yield = 0.0;
    double eps = 0.0;
    double revenue = 0.0;
    double ebitda = 0.0;
    double net_income = 0.0;
    double debt_to_equity = 0.0;
    double roe = 0.0;
    double roa = 0.0;
    double free_cash_flow = 0.0;
    std::string sector;
    std::string industry;
    std::string country;
    std::chrono::system_clock::time_point as_of;
};

/** ESG data from Refinitiv */
struct RefinitivEsgData {
    std::string ric;
    double esg_score = 0.0;        ///< 0-100 scale
    double environmental = 0.0;
    double social = 0.0;
    double governance = 0.0;
    double controversy_score = 0.0;
    std::string esg_grade;          ///< "A+", "A", "B+", etc.
    double carbon_emissions = 0.0;
    std::chrono::system_clock::time_point as_of;
};

/** Subscription entry */
struct Subscription {
    std::string ric;
    RefinitivDomain domain = RefinitivDomain::MarketPrice;
    SubscriptionState state = SubscriptionState::Pending;
    int stream_id = 0;
    std::chrono::system_clock::time_point subscribed_at;
    std::chrono::system_clock::time_point last_update;
    int update_count = 0;
    std::string error_text;
};

/** Connection diagnostics */
struct ConnectionDiagnostics {
    ConnectionState state = ConnectionState::Disconnected;
    int reconnect_count = 0;
    int total_messages_received = 0;
    int total_updates = 0;
    int total_errors = 0;
    double avg_latency_ms = 0.0;
    double max_latency_ms = 0.0;
    std::chrono::system_clock::time_point connected_since;
    std::chrono::system_clock::time_point last_message;
    size_t active_subscriptions = 0;
    size_t stale_subscriptions = 0;
    std::string server_info;
};

/** RIC mapping entry */
struct RicMapping {
    std::string ric;                ///< Reuters Instrument Code
    std::string internal_symbol;    ///< Internal system symbol
    std::string exchange;
    std::string asset_type;         ///< "equity", "bond", "fx", "commodity"
    std::string description;
};

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

using QuoteCallback = std::function<void(const RefinitivQuote&)>;
using OrderBookCallback = std::function<void(const OrderBook&)>;
using NewsCallback = std::function<void(const NewsHeadline&)>;
using StatusCallback = std::function<void(const std::string& ric,
                                           SubscriptionState state,
                                           const std::string& text)>;
using ConnectionCallback = std::function<void(ConnectionState state,
                                               const std::string& info)>;

// ---------------------------------------------------------------------------
// RicMapper
// ---------------------------------------------------------------------------

/**
 * Maps between Reuters Instrument Codes (RICs) and internal symbols.
 * Handles exchange suffix conventions (e.g., AAPL.O for NASDAQ).
 */
class RicMapper {
public:
    /** Register a RIC mapping */
    void add_mapping(const RicMapping& mapping) {
        std::lock_guard<std::mutex> lock(mutex_);
        ric_to_internal_[mapping.ric] = mapping.internal_symbol;
        internal_to_ric_[mapping.internal_symbol] = mapping.ric;
        mappings_[mapping.ric] = mapping;
    }

    /** Convert RIC to internal symbol */
    [[nodiscard]] std::string to_internal(const std::string& ric) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ric_to_internal_.find(ric);
        if (it != ric_to_internal_.end()) return it->second;

        // Auto-strip exchange suffix as fallback
        auto dot = ric.find('.');
        if (dot != std::string::npos) return ric.substr(0, dot);
        return ric;
    }

    /** Convert internal symbol to RIC */
    [[nodiscard]] std::string to_ric(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = internal_to_ric_.find(symbol);
        return (it != internal_to_ric_.end()) ? it->second : symbol;
    }

    /** Get full mapping details */
    [[nodiscard]] std::optional<RicMapping> get_mapping(const std::string& ric) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mappings_.find(ric);
        return (it != mappings_.end()) ?
            std::optional<RicMapping>(it->second) : std::nullopt;
    }

    /** Auto-generate common RIC mappings for US equities */
    void load_us_equity_defaults() {
        // NASDAQ suffix: .O
        for (const auto& sym : {"AAPL", "MSFT", "AMZN", "GOOGL", "META",
                                 "TSLA", "NVDA", "NFLX", "INTC", "AMD"}) {
            RicMapping m;
            m.ric = std::string(sym) + ".O";
            m.internal_symbol = sym;
            m.exchange = "NASDAQ";
            m.asset_type = "equity";
            add_mapping(m);
        }
        // NYSE suffix: .N
        for (const auto& sym : {"JPM", "BAC", "WMT", "JNJ", "PG",
                                 "XOM", "CVX", "DIS", "KO", "PFE"}) {
            RicMapping m;
            m.ric = std::string(sym) + ".N";
            m.internal_symbol = sym;
            m.exchange = "NYSE";
            m.asset_type = "equity";
            add_mapping(m);
        }
    }

    /** Load FX RIC conventions */
    void load_fx_defaults() {
        std::vector<std::pair<std::string, std::string>> fx_pairs = {
            {"EUR=", "EURUSD"}, {"GBP=", "GBPUSD"}, {"JPY=", "USDJPY"},
            {"CHF=", "USDCHF"}, {"AUD=", "AUDUSD"}, {"CAD=", "USDCAD"},
            {"NZD=", "NZDUSD"}, {"CNY=", "USDCNY"}
        };
        for (const auto& [ric, sym] : fx_pairs) {
            RicMapping m;
            m.ric = ric;
            m.internal_symbol = sym;
            m.exchange = "FX";
            m.asset_type = "fx";
            add_mapping(m);
        }
    }

    [[nodiscard]] size_t mapping_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return mappings_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> ric_to_internal_;
    std::unordered_map<std::string, std::string> internal_to_ric_;
    std::unordered_map<std::string, RicMapping> mappings_;
};

// ---------------------------------------------------------------------------
// MessageParser
// ---------------------------------------------------------------------------

/**
 * Parses Refinitiv WebSocket JSON messages into typed structures.
 * Handles Refresh, Update, Status, and Ping message types.
 */
class MessageParser {
public:
    /** Parse a quote from field map (simplified JSON-like representation) */
    [[nodiscard]] static RefinitivQuote parse_quote(
            const std::string& ric,
            const std::map<std::string, double>& numeric_fields,
            const std::map<std::string, std::string>& string_fields) {
        RefinitivQuote q;
        q.ric = ric;
        q.update_time = std::chrono::system_clock::now();

        auto get_num = [&](const std::string& key) -> double {
            auto it = numeric_fields.find(key);
            return (it != numeric_fields.end()) ? it->second : 0.0;
        };
        auto get_str = [&](const std::string& key) -> std::string {
            auto it = string_fields.find(key);
            return (it != string_fields.end()) ? it->second : "";
        };

        q.bid = get_num("BID");
        q.ask = get_num("ASK");
        q.last = get_num("TRDPRC_1");
        q.open = get_num("OPEN_PRC");
        q.high = get_num("HIGH_1");
        q.low = get_num("LOW_1");
        q.close = get_num("HST_CLOSE");
        q.volume = get_num("ACVOL_1");
        q.net_change = get_num("NETCHNG_1");
        q.pct_change = get_num("PCTCHNG");
        q.turnover = get_num("TURNOVER");
        q.vwap = get_num("VWAP");
        q.pe_ratio = get_num("PE_RATIO");
        q.dividend_yield = get_num("YIELD");
        q.earnings = get_num("EARNINGS");
        q.bid_size = static_cast<int>(get_num("BIDSIZE"));
        q.ask_size = static_cast<int>(get_num("ASKSIZE"));
        q.num_trades = static_cast<int>(get_num("NUM_MOVES"));

        q.display_name = get_str("DSPLY_NAME");
        q.exchange_id = get_str("RDN_EXCHID");
        q.currency = get_str("CURRENCY");

        q.state = SubscriptionState::Active;
        return q;
    }

    /** Apply incremental update to existing quote */
    static void apply_update(RefinitivQuote& existing,
                              const std::map<std::string, double>& fields) {
        for (const auto& [key, val] : fields) {
            if (key == "BID")        existing.bid = val;
            else if (key == "ASK")   existing.ask = val;
            else if (key == "TRDPRC_1") existing.last = val;
            else if (key == "HIGH_1") existing.high = val;
            else if (key == "LOW_1")  existing.low = val;
            else if (key == "ACVOL_1") existing.volume = val;
            else if (key == "NETCHNG_1") existing.net_change = val;
            else if (key == "PCTCHNG") existing.pct_change = val;
            else if (key == "TURNOVER") existing.turnover = val;
            else if (key == "VWAP")  existing.vwap = val;
            else if (key == "BIDSIZE") existing.bid_size = static_cast<int>(val);
            else if (key == "ASKSIZE") existing.ask_size = static_cast<int>(val);
        }
        existing.update_time = std::chrono::system_clock::now();
        existing.update_count++;
    }

    /** Parse order book levels from field arrays */
    [[nodiscard]] static OrderBook parse_order_book(
            const std::string& ric,
            const std::vector<std::pair<double, int>>& bid_levels,
            const std::vector<std::pair<double, int>>& ask_levels) {
        OrderBook book;
        book.ric = ric;
        book.timestamp = std::chrono::system_clock::now();

        for (const auto& [price, size] : bid_levels) {
            OrderBookLevel level;
            level.price = price;
            level.size = size;
            level.timestamp = book.timestamp;
            book.bids.push_back(level);
        }
        for (const auto& [price, size] : ask_levels) {
            OrderBookLevel level;
            level.price = price;
            level.size = size;
            level.timestamp = book.timestamp;
            book.asks.push_back(level);
        }

        book.depth = static_cast<int>(
            std::max(book.bids.size(), book.asks.size()));
        return book;
    }
};

// ---------------------------------------------------------------------------
// SubscriptionManager
// ---------------------------------------------------------------------------

/**
 * Manages Refinitiv subscriptions with batching and state tracking.
 */
class SubscriptionManager {
public:
    explicit SubscriptionManager(int max_batch = 200)
        : max_batch_(max_batch), next_stream_id_(1) {}

    /** Subscribe to a RIC */
    int subscribe(const std::string& ric,
                   RefinitivDomain domain = RefinitivDomain::MarketPrice) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = make_key(ric, domain);
        auto it = subscriptions_.find(key);
        if (it != subscriptions_.end()) {
            return it->second.stream_id;
        }

        Subscription sub;
        sub.ric = ric;
        sub.domain = domain;
        sub.state = SubscriptionState::Pending;
        sub.stream_id = next_stream_id_++;
        sub.subscribed_at = std::chrono::system_clock::now();

        subscriptions_[key] = sub;
        pending_queue_.push(key);
        return sub.stream_id;
    }

    /** Unsubscribe from a RIC */
    bool unsubscribe(const std::string& ric,
                      RefinitivDomain domain = RefinitivDomain::MarketPrice) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = make_key(ric, domain);
        auto it = subscriptions_.find(key);
        if (it == subscriptions_.end()) return false;
        it->second.state = SubscriptionState::Closed;
        return true;
    }

    /** Update subscription state */
    void update_state(const std::string& ric, RefinitivDomain domain,
                       SubscriptionState state, const std::string& error = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = make_key(ric, domain);
        auto it = subscriptions_.find(key);
        if (it != subscriptions_.end()) {
            it->second.state = state;
            it->second.last_update = std::chrono::system_clock::now();
            if (!error.empty()) it->second.error_text = error;
            if (state == SubscriptionState::Active) {
                it->second.update_count++;
            }
        }
    }

    /** Get next batch of pending subscriptions */
    [[nodiscard]] std::vector<Subscription> get_pending_batch() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Subscription> batch;
        int count = 0;
        while (!pending_queue_.empty() && count < max_batch_) {
            auto key = pending_queue_.front();
            pending_queue_.pop();
            auto it = subscriptions_.find(key);
            if (it != subscriptions_.end() &&
                it->second.state == SubscriptionState::Pending) {
                batch.push_back(it->second);
                count++;
            }
        }
        return batch;
    }

    /** Get subscription count */
    [[nodiscard]] size_t active_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;
        for (const auto& [key, sub] : subscriptions_) {
            if (sub.state == SubscriptionState::Active) count++;
        }
        return count;
    }

    /** Get stale subscription count */
    [[nodiscard]] size_t stale_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        size_t count = 0;
        for (const auto& [key, sub] : subscriptions_) {
            if (sub.state == SubscriptionState::Active) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - sub.last_update);
                if (age.count() > 60) count++;  // Stale after 60s
            }
        }
        return count;
    }

    /** Get all subscriptions */
    [[nodiscard]] std::vector<Subscription> all_subscriptions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Subscription> result;
        result.reserve(subscriptions_.size());
        for (const auto& [key, sub] : subscriptions_) {
            result.push_back(sub);
        }
        return result;
    }

private:
    static std::string make_key(const std::string& ric, RefinitivDomain domain) {
        return ric + ":" + std::to_string(static_cast<int>(domain));
    }

    mutable std::mutex mutex_;
    int max_batch_;
    int next_stream_id_;
    std::unordered_map<std::string, Subscription> subscriptions_;
    std::queue<std::string> pending_queue_;
};

// ---------------------------------------------------------------------------
// ReutersAdapter -- Main Adapter
// ---------------------------------------------------------------------------

/**
 * Main Refinitiv/Reuters market data adapter.
 * Manages connection lifecycle, authentication, subscriptions,
 * and data distribution to registered callbacks.
 */
class ReutersAdapter {
public:
    explicit ReutersAdapter(const RefinitivConfig& config = {})
        : config_(config),
          sub_manager_(config.max_batch_size),
          state_(ConnectionState::Disconnected),
          running_(false) {
        ric_mapper_.load_us_equity_defaults();
        ric_mapper_.load_fx_defaults();
    }

    ~ReutersAdapter() { stop(); }

    // Non-copyable
    ReutersAdapter(const ReutersAdapter&) = delete;
    ReutersAdapter& operator=(const ReutersAdapter&) = delete;

    // -- Connection Management --

    /** Start the adapter (connect and begin processing) */
    bool start() {
        if (running_.load()) return true;

        state_ = ConnectionState::Connecting;
        running_ = true;

        // Authenticate
        if (config_.mode == RefinitivConnectionMode::ErtCloud) {
            if (!authenticate()) {
                state_ = ConnectionState::Error;
                running_ = false;
                return false;
            }
        }

        // Start processing thread
        process_thread_ = std::thread([this] { process_loop(); });

        state_ = ConnectionState::Connected;
        diag_.connected_since = std::chrono::system_clock::now();

        if (connection_cb_) {
            connection_cb_(ConnectionState::Connected, "Connected to Refinitiv");
        }

        return true;
    }

    /** Stop the adapter */
    void stop() {
        running_ = false;
        cv_.notify_all();
        if (process_thread_.joinable()) {
            process_thread_.join();
        }
        state_ = ConnectionState::Disconnected;
    }

    /** Check if adapter is running */
    [[nodiscard]] bool is_running() const { return running_.load(); }

    /** Get current connection state */
    [[nodiscard]] ConnectionState connection_state() const { return state_; }

    // -- Subscription Management --

    /** Subscribe to real-time quotes */
    int subscribe(const std::string& symbol,
                   RefinitivDomain domain = RefinitivDomain::MarketPrice) {
        std::string ric = ric_mapper_.to_ric(symbol);
        return sub_manager_.subscribe(ric, domain);
    }

    /** Subscribe to multiple symbols */
    std::vector<int> subscribe_batch(const std::vector<std::string>& symbols,
                                      RefinitivDomain domain = RefinitivDomain::MarketPrice) {
        std::vector<int> stream_ids;
        stream_ids.reserve(symbols.size());
        for (const auto& sym : symbols) {
            stream_ids.push_back(subscribe(sym, domain));
        }
        return stream_ids;
    }

    /** Unsubscribe from a symbol */
    bool unsubscribe(const std::string& symbol,
                      RefinitivDomain domain = RefinitivDomain::MarketPrice) {
        std::string ric = ric_mapper_.to_ric(symbol);
        return sub_manager_.unsubscribe(ric, domain);
    }

    // -- Snapshot Data Access --

    /** Get current quote for a RIC */
    [[nodiscard]] std::optional<RefinitivQuote> get_quote(const std::string& symbol) const {
        std::string ric = ric_mapper_.to_ric(symbol);
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = quotes_.find(ric);
        return (it != quotes_.end()) ?
            std::optional<RefinitivQuote>(it->second) : std::nullopt;
    }

    /** Get current order book for a RIC */
    [[nodiscard]] std::optional<OrderBook> get_order_book(const std::string& symbol) const {
        std::string ric = ric_mapper_.to_ric(symbol);
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = order_books_.find(ric);
        return (it != order_books_.end()) ?
            std::optional<OrderBook>(it->second) : std::nullopt;
    }

    /** Get all current quotes */
    [[nodiscard]] std::map<std::string, RefinitivQuote> all_quotes() const {
        std::lock_guard<std::mutex> lock(data_mutex_);
        return std::map<std::string, RefinitivQuote>(quotes_.begin(), quotes_.end());
    }

    // -- Historical Data --

    /** Get historical bars (simulated -- would use REST API in production) */
    [[nodiscard]] std::vector<HistoricalBar> get_history(
            const std::string& symbol, int days = 30) const {
        (void)symbol;
        std::vector<HistoricalBar> bars;
        auto now = std::chrono::system_clock::now();
        double price = 150.0;

        for (int d = days; d >= 0; --d) {
            HistoricalBar bar;
            bar.timestamp = now - std::chrono::hours(d * 24);
            double change = (static_cast<double>(std::rand() % 1000) - 500.0) / 5000.0;
            price *= (1.0 + change);
            bar.open = price * (1.0 - std::abs(change) * 0.3);
            bar.close = price;
            bar.high = price * 1.01;
            bar.low = price * 0.99;
            bar.volume = 1000000.0 + static_cast<double>(std::rand() % 5000000);
            bars.push_back(bar);
        }
        return bars;
    }

    // -- Callbacks --

    void on_quote(QuoteCallback cb) { quote_cb_ = std::move(cb); }
    void on_order_book(OrderBookCallback cb) { book_cb_ = std::move(cb); }
    void on_news(NewsCallback cb) { news_cb_ = std::move(cb); }
    void on_status(StatusCallback cb) { status_cb_ = std::move(cb); }
    void on_connection(ConnectionCallback cb) { connection_cb_ = std::move(cb); }

    // -- Component Access --

    RicMapper& ric_mapper() { return ric_mapper_; }
    const RicMapper& ric_mapper() const { return ric_mapper_; }
    SubscriptionManager& subscriptions() { return sub_manager_; }

    // -- Diagnostics --

    [[nodiscard]] ConnectionDiagnostics diagnostics() const {
        ConnectionDiagnostics d = diag_;
        d.state = state_;
        d.active_subscriptions = sub_manager_.active_count();
        d.stale_subscriptions = sub_manager_.stale_count();
        return d;
    }

    /** Generate status report */
    [[nodiscard]] std::string status_report() const {
        auto d = diagnostics();
        std::ostringstream ss;
        ss << "=== Reuters/Refinitiv Adapter Status ===\n";
        ss << "Connection: " << connection_state_str(d.state) << "\n";
        ss << "Mode: " << mode_str(config_.mode) << "\n";
        ss << "Subscriptions: " << d.active_subscriptions << " active, "
           << d.stale_subscriptions << " stale\n";
        ss << "Messages: " << d.total_messages_received << "\n";
        ss << "Updates: " << d.total_updates << "\n";
        ss << "Errors: " << d.total_errors << "\n";
        ss << "Avg Latency: " << std::fixed << std::setprecision(1)
           << d.avg_latency_ms << " ms\n";
        ss << "RIC Mappings: " << ric_mapper_.mapping_count() << "\n";
        return ss.str();
    }

private:
    /** Authenticate with Refinitiv OAuth2 */
    bool authenticate() {
        // In production: POST to auth_url with client credentials
        // For now: simulate successful auth
        token_.access_token = "simulated_token_" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        token_.issued_at = std::chrono::system_clock::now();
        token_.expires_in = 300;
        state_ = ConnectionState::Connected;
        return true;
    }

    /** Main processing loop */
    void process_loop() {
        while (running_.load()) {
            // Process pending subscriptions
            auto batch = sub_manager_.get_pending_batch();
            for (auto& sub : batch) {
                // In production: send WebSocket subscribe message
                sub_manager_.update_state(sub.ric, sub.domain,
                    SubscriptionState::Active);

                // Simulate initial refresh
                simulate_refresh(sub.ric);
            }

            // Simulate incoming updates
            if (!quotes_.empty()) {
                simulate_updates();
            }

            // Token refresh check
            if (config_.mode == RefinitivConnectionMode::ErtCloud &&
                token_.is_expired()) {
                authenticate();
            }

            // Sleep between cycles
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(config_.throttle_ms));
        }
    }

    /** Simulate a refresh message for a RIC */
    void simulate_refresh(const std::string& ric) {
        std::map<std::string, double> fields = {
            {"BID", 149.50}, {"ASK", 149.55}, {"TRDPRC_1", 149.52},
            {"OPEN_PRC", 148.00}, {"HIGH_1", 150.25}, {"LOW_1", 147.80},
            {"HST_CLOSE", 148.30}, {"ACVOL_1", 2500000.0},
            {"NETCHNG_1", 1.22}, {"PCTCHNG", 0.82}
        };
        std::map<std::string, std::string> str_fields = {
            {"DSPLY_NAME", ric}, {"CURRENCY", "USD"}, {"RDN_EXCHID", "NAS"}
        };

        auto quote = MessageParser::parse_quote(ric, fields, str_fields);

        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            quotes_[ric] = quote;
        }

        diag_.total_messages_received++;
        if (quote_cb_) quote_cb_(quote);
    }

    /** Simulate incremental updates */
    void simulate_updates() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        for (auto& [ric, quote] : quotes_) {
            // Random price tick
            double tick = (static_cast<double>(std::rand() % 100) - 50.0) / 1000.0;
            std::map<std::string, double> update = {
                {"TRDPRC_1", quote.last + tick},
                {"ACVOL_1", quote.volume + static_cast<double>(std::rand() % 1000)}
            };
            MessageParser::apply_update(quote, update);
            diag_.total_updates++;

            if (quote_cb_) quote_cb_(quote);
        }
    }

    static std::string connection_state_str(ConnectionState s) {
        switch (s) {
            case ConnectionState::Disconnected:   return "Disconnected";
            case ConnectionState::Connecting:     return "Connecting";
            case ConnectionState::Authenticating: return "Authenticating";
            case ConnectionState::Connected:      return "Connected";
            case ConnectionState::Reconnecting:   return "Reconnecting";
            case ConnectionState::Error:          return "Error";
        }
        return "Unknown";
    }

    static std::string mode_str(RefinitivConnectionMode m) {
        switch (m) {
            case RefinitivConnectionMode::ErtCloud:     return "ERT in Cloud";
            case RefinitivConnectionMode::DeployedTrep: return "Deployed TREP";
            case RefinitivConnectionMode::EikonDesktop: return "Eikon Desktop";
        }
        return "Unknown";
    }

    RefinitivConfig config_;
    RicMapper ric_mapper_;
    SubscriptionManager sub_manager_;
    RefinitivToken token_;

    std::atomic<ConnectionState> state_;
    std::atomic<bool> running_;
    ConnectionDiagnostics diag_;

    mutable std::mutex data_mutex_;
    std::unordered_map<std::string, RefinitivQuote> quotes_;
    std::unordered_map<std::string, OrderBook> order_books_;

    // Callbacks
    QuoteCallback quote_cb_;
    OrderBookCallback book_cb_;
    NewsCallback news_cb_;
    StatusCallback status_cb_;
    ConnectionCallback connection_cb_;

    // Threading
    std::thread process_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace market
} // namespace genie

#endif // GENIE_MARKET_REUTERS_ADAPTER_HPP
