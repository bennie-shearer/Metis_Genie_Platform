/**
 * @file fix_engine.hpp
 * @brief FIX 4.2/4.4 Protocol Engine for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Financial Information eXchange (FIX) protocol implementation providing
 * message parsing, generation, session management, and simulated order
 * routing for broker connectivity.
 *
 * Features:
 *   - FIX 4.2 and 4.4 message parsing and generation
 *   - Tag-value pair encoding/decoding with SOH delimiter
 *   - Standard message types: Logon(A), Logout(5), Heartbeat(0),
 *     TestRequest(1), NewOrderSingle(D), ExecutionReport(8),
 *     OrderCancelRequest(F), OrderCancelReplaceRequest(G),
 *     OrderStatusRequest(H), MarketDataRequest(V), MarketDataSnapshot(W)
 *   - Session management: logon, heartbeat, sequence numbers, logout
 *   - Message validation: required tags, checksum, body length
 *   - Simulated counterparty for testing (fill simulation with latency)
 *   - Sequence number tracking with gap detection
 *   - Message store for replay (ResendRequest support)
 *   - Configurable sender/target CompID
 *   - Thread-safe session state
 *   - Zero external dependencies
 *
 * FIX Message Format:
 *   8=FIX.4.4|9=BodyLength|35=MsgType|49=SenderCompID|56=TargetCompID|
 *   34=MsgSeqNum|52=SendingTime|...payload...|10=Checksum|
 *   (where | represents SOH character 0x01)
 *
 * Usage:
 *   trading::FixEngine engine("GENIE", "BROKER", trading::FixVersion::FIX_44);
 *   engine.on_execution_report([](const trading::ExecutionReport& er) {
 *       std::cout << "Fill: " << er.symbol << " @ " << er.avg_price << "\n";
 *   });
 *   engine.connect("localhost", 9876);
 *   engine.send_new_order("AAPL", trading::FixSide::BUY, 100, 150.25);
 *
 * Build:
 *   g++ -std=c++20 -O2 -I include -pthread
 *   Windows: add -lws2_32
 */
#pragma once
#ifndef GENIE_TRADING_FIX_ENGINE_HPP
#define GENIE_TRADING_FIX_ENGINE_HPP

#include "../core/logging.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <variant>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <stdexcept>

namespace genie {
namespace trading {

// ============================================================================
// FIX Constants
// ============================================================================

constexpr char FIX_SOH = '\x01';    // Standard delimiter (Start of Header)
constexpr char FIX_PIPE = '|';       // Display delimiter (for logging)

/**
 * @brief FIX protocol version
 */
enum class FixVersion {
    FIX_42,     // FIX 4.2 (most common legacy)
    FIX_44      // FIX 4.4 (current standard)
};

inline std::string fix_version_string(FixVersion v) {
    switch (v) {
        case FixVersion::FIX_42: return "FIX.4.2";
        case FixVersion::FIX_44: return "FIX.4.4";
        default: return "FIX.4.4";
    }
}

// ============================================================================
// FIX Tag Constants (commonly used)
// ============================================================================

namespace tag {
    constexpr int BeginString       = 8;
    constexpr int BodyLength        = 9;
    constexpr int MsgType           = 35;
    constexpr int SenderCompID      = 49;
    constexpr int TargetCompID      = 56;
    constexpr int MsgSeqNum         = 34;
    constexpr int SendingTime       = 52;
    constexpr int CheckSum          = 10;

    // Session
    constexpr int EncryptMethod     = 98;
    constexpr int HeartBtInt        = 108;
    constexpr int TestReqID         = 112;
    constexpr int ResetSeqNumFlag   = 141;
    constexpr int BeginSeqNo        = 7;
    constexpr int EndSeqNo          = 16;
    constexpr int RefSeqNum         = 45;
    constexpr int Text              = 58;
    constexpr int SessionRejectReason = 373;

    // Order entry
    constexpr int ClOrdID           = 11;
    constexpr int OrigClOrdID       = 41;
    constexpr int OrderID           = 37;
    constexpr int ExecID            = 17;
    constexpr int ExecType          = 150;
    constexpr int OrdStatus         = 39;
    constexpr int Symbol            = 55;
    constexpr int Side              = 54;
    constexpr int OrderQty          = 38;
    constexpr int OrdType           = 40;
    constexpr int Price             = 44;
    constexpr int StopPx            = 99;
    constexpr int TimeInForce       = 59;
    constexpr int TransactTime      = 60;
    constexpr int AvgPx             = 6;
    constexpr int CumQty            = 14;
    constexpr int LeavesQty         = 151;
    constexpr int LastPx            = 31;
    constexpr int LastQty           = 32;
    constexpr int Account           = 1;
    constexpr int HandlInst         = 21;
    constexpr int SecurityExchange  = 207;
    constexpr int Currency          = 15;

    // Market data
    constexpr int MDReqID           = 262;
    constexpr int SubscriptionRequestType = 263;
    constexpr int MarketDepth       = 264;
    constexpr int NoMDEntryTypes    = 267;
    constexpr int MDEntryType       = 269;
    constexpr int NoMDEntries       = 268;
    constexpr int MDEntryPx         = 270;
    constexpr int MDEntrySize       = 271;
    constexpr int NoRelatedSym      = 146;
} // namespace tag

// ============================================================================
// FIX Message Types
// ============================================================================

namespace msgtype {
    constexpr const char* Heartbeat             = "0";
    constexpr const char* TestRequest           = "1";
    constexpr const char* ResendRequest         = "2";
    constexpr const char* Reject                = "3";
    constexpr const char* SequenceReset         = "4";
    constexpr const char* Logout                = "5";
    constexpr const char* Logon                 = "A";
    constexpr const char* NewOrderSingle        = "D";
    constexpr const char* OrderCancelRequest    = "F";
    constexpr const char* OrderCancelReplace    = "G";
    constexpr const char* OrderStatusRequest    = "H";
    constexpr const char* ExecutionReport       = "8";
    constexpr const char* OrderCancelReject     = "9";
    constexpr const char* MarketDataRequest     = "V";
    constexpr const char* MarketDataSnapshot    = "W";
    constexpr const char* MarketDataIncRefresh  = "X";
} // namespace msgtype

// ============================================================================
// FIX Enumerations
// ============================================================================

enum class FixSide : char {
    BUY  = '1',
    SELL = '2',
    SHORT_SELL = '5'
};

enum class FixOrdType : char {
    MARKET = '1',
    LIMIT  = '2',
    STOP   = '3',
    STOP_LIMIT = '4'
};

enum class FixTimeInForce : char {
    DAY = '0',
    GTC = '1',     // Good Till Cancel
    IOC = '3',     // Immediate Or Cancel
    FOK = '4',     // Fill Or Kill
    GTD = '6'      // Good Till Date
};

enum class FixExecType : char {
    NEW        = '0',
    PARTIAL    = '1',
    FILL       = '2',
    CANCELLED  = '4',
    REPLACED   = '5',
    PENDING_CANCEL = '6',
    REJECTED   = '8',
    PENDING_NEW = 'A',
    PENDING_REPLACE = 'E',
    TRADE      = 'F'
};

enum class FixOrdStatus : char {
    NEW            = '0',
    PARTIALLY_FILLED = '1',
    FILLED         = '2',
    CANCELLED      = '4',
    REPLACED       = '5',
    PENDING_CANCEL = '6',
    REJECTED       = '8',
    PENDING_NEW    = 'A',
    PENDING_REPLACE = 'E'
};

// ============================================================================
// FIX Message
// ============================================================================

/**
 * @brief Parsed FIX message as ordered tag-value pairs
 */
class FixMessage {
public:
    FixMessage() = default;

    // ---- Builder ----

    FixMessage& set(int tag_num, const std::string& value) {
        fields_[tag_num] = value;
        if (std::find(order_.begin(), order_.end(), tag_num) == order_.end()) {
            order_.push_back(tag_num);
        }
        return *this;
    }

    FixMessage& set(int tag_num, int value) {
        return set(tag_num, std::to_string(value));
    }

    FixMessage& set(int tag_num, double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4) << value;
        // Trim trailing zeros
        std::string s = oss.str();
        auto dot = s.find('.');
        if (dot != std::string::npos) {
            auto last = s.find_last_not_of('0');
            if (last != std::string::npos && last > dot) s = s.substr(0, last + 1);
            else if (last == dot) s = s.substr(0, dot);
        }
        return set(tag_num, s);
    }

    FixMessage& set(int tag_num, char value) {
        return set(tag_num, std::string(1, value));
    }

    // ---- Accessors ----

    bool has(int tag_num) const {
        return fields_.count(tag_num) > 0;
    }

    std::string get(int tag_num, const std::string& def = "") const {
        auto it = fields_.find(tag_num);
        return (it != fields_.end()) ? it->second : def;
    }

    int get_int(int tag_num, int def = 0) const {
        auto v = get(tag_num);
        if (v.empty()) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    }

    double get_double(int tag_num, double def = 0.0) const {
        auto v = get(tag_num);
        if (v.empty()) return def;
        try { return std::stod(v); } catch (...) { return def; }
    }

    char get_char(int tag_num, char def = '\0') const {
        auto v = get(tag_num);
        return v.empty() ? def : v[0];
    }

    std::string msg_type() const { return get(tag::MsgType); }
    int seq_num() const { return get_int(tag::MsgSeqNum); }
    std::string sender() const { return get(tag::SenderCompID); }
    std::string target() const { return get(tag::TargetCompID); }

    const std::map<int, std::string>& fields() const { return fields_; }
    const std::vector<int>& field_order() const { return order_; }

    // ---- Encoding ----

    /**
     * @brief Encode to FIX wire format (with SOH delimiters)
     */
    std::string encode() const {
        // Build body (everything except 8, 9, 10)
        std::string body;
        for (int t : order_) {
            if (t == tag::BeginString || t == tag::BodyLength || t == tag::CheckSum)
                continue;
            body += std::to_string(t) + "=" + fields_.at(t) + FIX_SOH;
        }

        // Prepend BeginString and BodyLength
        std::string begin_str = std::to_string(tag::BeginString) + "="
                               + get(tag::BeginString, "FIX.4.4") + FIX_SOH;
        std::string body_len = std::to_string(tag::BodyLength) + "="
                              + std::to_string(body.size()) + FIX_SOH;

        // Calculate checksum
        std::string pre_checksum = begin_str + body_len + body;
        int sum = 0;
        for (char c : pre_checksum) sum += static_cast<unsigned char>(c);
        int cs = sum % 256;

        std::ostringstream cs_oss;
        cs_oss << std::setw(3) << std::setfill('0') << cs;

        return pre_checksum + "10=" + cs_oss.str() + FIX_SOH;
    }

    /**
     * @brief Human-readable format (pipe-delimited)
     */
    std::string to_string() const {
        std::string result;
        std::string wire = encode();
        for (char c : wire) {
            result += (c == FIX_SOH) ? FIX_PIPE : c;
        }
        return result;
    }

    // ---- Parsing ----

    /**
     * @brief Parse FIX message from wire format
     */
    static std::optional<FixMessage> parse(const std::string& raw) {
        FixMessage msg;
        size_t pos = 0;

        while (pos < raw.size()) {
            // Find tag=value delimiter
            auto eq = raw.find('=', pos);
            if (eq == std::string::npos) break;

            // Find SOH (or pipe for display format)
            auto soh = raw.find(FIX_SOH, eq + 1);
            if (soh == std::string::npos) {
                soh = raw.find(FIX_PIPE, eq + 1);
                if (soh == std::string::npos) soh = raw.size();
            }

            std::string tag_str = raw.substr(pos, eq - pos);
            std::string value = raw.substr(eq + 1, soh - eq - 1);

            int tag_num = 0;
            try { tag_num = std::stoi(tag_str); } catch (...) { pos = soh + 1; continue; }

            msg.set(tag_num, value);
            pos = soh + 1;
        }

        // Validate required fields
        if (!msg.has(tag::BeginString) || !msg.has(tag::MsgType)) {
            return std::nullopt;
        }

        return msg;
    }

    /**
     * @brief Validate checksum
     */
    bool validate_checksum() const {
        // Recompute and compare
        std::string wire = encode();
        auto cs_pos = wire.rfind("10=");
        if (cs_pos == std::string::npos) return false;

        std::string pre = wire.substr(0, cs_pos);
        int sum = 0;
        for (char c : pre) sum += static_cast<unsigned char>(c);
        int expected = sum % 256;

        return expected == get_int(tag::CheckSum);
    }

    /**
     * @brief Validate body length
     */
    bool validate_body_length() const {
        if (!has(tag::BodyLength)) return false;
        int claimed = get_int(tag::BodyLength);

        // Body = everything from first field after BodyLength to before CheckSum
        std::string wire = encode();
        // Find end of BodyLength field
        auto bl_end = wire.find(FIX_SOH, wire.find("9="));
        if (bl_end == std::string::npos) return false;
        // Find start of CheckSum
        auto cs_start = wire.rfind("10=");
        if (cs_start == std::string::npos) return false;

        int actual = static_cast<int>(cs_start - bl_end - 1);
        return actual == claimed;
    }

private:
    std::map<int, std::string> fields_;
    std::vector<int> order_;
};

// ============================================================================
// Execution Report (parsed from FIX ExecutionReport message)
// ============================================================================

struct ExecutionReport {
    std::string order_id;
    std::string cl_ord_id;
    std::string exec_id;
    std::string symbol;
    FixSide side                = FixSide::BUY;
    FixExecType exec_type       = FixExecType::NEW;
    FixOrdStatus ord_status     = FixOrdStatus::NEW;
    double order_qty            = 0.0;
    double cum_qty              = 0.0;
    double leaves_qty           = 0.0;
    double avg_price            = 0.0;
    double last_price           = 0.0;
    double last_qty             = 0.0;
    std::string text;
    std::string transact_time;
    int64_t timestamp           = 0;

    static ExecutionReport from_fix(const FixMessage& msg) {
        ExecutionReport er;
        er.order_id = msg.get(tag::OrderID);
        er.cl_ord_id = msg.get(tag::ClOrdID);
        er.exec_id = msg.get(tag::ExecID);
        er.symbol = msg.get(tag::Symbol);
        er.side = static_cast<FixSide>(msg.get_char(tag::Side, '1'));
        er.exec_type = static_cast<FixExecType>(msg.get_char(tag::ExecType, '0'));
        er.ord_status = static_cast<FixOrdStatus>(msg.get_char(tag::OrdStatus, '0'));
        er.order_qty = msg.get_double(tag::OrderQty);
        er.cum_qty = msg.get_double(tag::CumQty);
        er.leaves_qty = msg.get_double(tag::LeavesQty);
        er.avg_price = msg.get_double(tag::AvgPx);
        er.last_price = msg.get_double(tag::LastPx);
        er.last_qty = msg.get_double(tag::LastQty);
        er.text = msg.get(tag::Text);
        er.transact_time = msg.get(tag::TransactTime);
        er.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return er;
    }

    bool is_fill() const {
        return exec_type == FixExecType::FILL || exec_type == FixExecType::TRADE;
    }

    bool is_partial() const {
        return exec_type == FixExecType::PARTIAL;
    }

    bool is_rejected() const {
        return exec_type == FixExecType::REJECTED;
    }

    bool is_cancelled() const {
        return exec_type == FixExecType::CANCELLED;
    }

    bool is_terminal() const {
        return is_fill() || is_rejected() || is_cancelled();
    }
};

// ============================================================================
// FIX Session State
// ============================================================================

enum class FixSessionState {
    DISCONNECTED,
    CONNECTING,
    LOGON_SENT,
    ACTIVE,
    LOGOUT_SENT,
    ERROR
};

inline std::string session_state_string(FixSessionState s) {
    switch (s) {
        case FixSessionState::DISCONNECTED: return "DISCONNECTED";
        case FixSessionState::CONNECTING:   return "CONNECTING";
        case FixSessionState::LOGON_SENT:   return "LOGON_SENT";
        case FixSessionState::ACTIVE:       return "ACTIVE";
        case FixSessionState::LOGOUT_SENT:  return "LOGOUT_SENT";
        case FixSessionState::ERROR:        return "ERROR";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// FIX Timestamp Utilities
// ============================================================================

inline std::string fix_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    auto gmt = std::gmtime(&t);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H:%M:%S", gmt);

    std::ostringstream oss;
    oss << buf << "." << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

inline std::string fix_order_id() {
    static std::atomic<uint64_t> counter{0};
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t seq = counter.fetch_add(1);
    std::ostringstream oss;
    oss << "ORD-" << std::hex << ms << "-" << seq;
    return oss.str();
}

inline std::string fix_exec_id() {
    static std::atomic<uint64_t> counter{0};
    uint64_t seq = counter.fetch_add(1);
    std::ostringstream oss;
    oss << "EXEC-" << seq;
    return oss.str();
}

// ============================================================================
// Simulated Counterparty
// ============================================================================

/**
 * @brief Simulated broker counterparty for testing
 *
 * Generates realistic fill simulation with configurable latency,
 * partial fills, and rejection probability.
 */
class SimulatedCounterparty {
public:
    struct Config {
        Config() = default;
        int min_latency_ms          = 5;
        int max_latency_ms          = 50;
        double fill_probability     = 0.95;
        double partial_fill_pct     = 0.20;     // 20% chance of partial fill
        double price_slippage_bps   = 2.0;      // 2 bps slippage
        bool simulate_market_hours  = false;
    };

    SimulatedCounterparty() : config_() {}
    explicit SimulatedCounterparty(Config config) : config_(config) {}

    /**
     * @brief Process an incoming order and generate execution reports
     */
    std::vector<FixMessage> process_order(const FixMessage& order,
                                           const std::string& sender,
                                           const std::string& target,
                                           FixVersion version) {
        std::vector<FixMessage> responses;

        std::string cl_ord_id = order.get(tag::ClOrdID);
        std::string symbol = order.get(tag::Symbol);
        char side = order.get_char(tag::Side, '1');
        double qty = order.get_double(tag::OrderQty);
        double price = order.get_double(tag::Price);
        char ord_type = order.get_char(tag::OrdType, '2');

        std::string order_id = fix_order_id();

        // 1. Pending New acknowledgment
        FixMessage ack;
        ack.set(tag::BeginString, fix_version_string(version))
           .set(tag::MsgType, msgtype::ExecutionReport)
           .set(tag::SenderCompID, target)      // Counterparty sends back
           .set(tag::TargetCompID, sender)
           .set(tag::MsgSeqNum, next_seq())
           .set(tag::SendingTime, fix_timestamp())
           .set(tag::OrderID, order_id)
           .set(tag::ClOrdID, cl_ord_id)
           .set(tag::ExecID, fix_exec_id())
           .set(tag::ExecType, static_cast<char>(FixExecType::PENDING_NEW))
           .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::PENDING_NEW))
           .set(tag::Symbol, symbol)
           .set(tag::Side, side)
           .set(tag::OrderQty, qty)
           .set(tag::CumQty, 0.0)
           .set(tag::LeavesQty, qty)
           .set(tag::AvgPx, 0.0)
           .set(tag::TransactTime, fix_timestamp());
        responses.push_back(ack);

        // 2. Decide fill/reject
        double rand_val = static_cast<double>(rand() % 1000) / 1000.0;

        if (rand_val > config_.fill_probability) {
            // Rejected
            FixMessage reject;
            reject.set(tag::BeginString, fix_version_string(version))
                  .set(tag::MsgType, msgtype::ExecutionReport)
                  .set(tag::SenderCompID, target)
                  .set(tag::TargetCompID, sender)
                  .set(tag::MsgSeqNum, next_seq())
                  .set(tag::SendingTime, fix_timestamp())
                  .set(tag::OrderID, order_id)
                  .set(tag::ClOrdID, cl_ord_id)
                  .set(tag::ExecID, fix_exec_id())
                  .set(tag::ExecType, static_cast<char>(FixExecType::REJECTED))
                  .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::REJECTED))
                  .set(tag::Symbol, symbol)
                  .set(tag::Side, side)
                  .set(tag::OrderQty, qty)
                  .set(tag::CumQty, 0.0)
                  .set(tag::LeavesQty, 0.0)
                  .set(tag::AvgPx, 0.0)
                  .set(tag::Text, "Insufficient liquidity")
                  .set(tag::TransactTime, fix_timestamp());
            responses.push_back(reject);
            return responses;
        }

        // 3. New order accepted
        FixMessage new_ack;
        new_ack.set(tag::BeginString, fix_version_string(version))
               .set(tag::MsgType, msgtype::ExecutionReport)
               .set(tag::SenderCompID, target)
               .set(tag::TargetCompID, sender)
               .set(tag::MsgSeqNum, next_seq())
               .set(tag::SendingTime, fix_timestamp())
               .set(tag::OrderID, order_id)
               .set(tag::ClOrdID, cl_ord_id)
               .set(tag::ExecID, fix_exec_id())
               .set(tag::ExecType, static_cast<char>(FixExecType::NEW))
               .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::NEW))
               .set(tag::Symbol, symbol)
               .set(tag::Side, side)
               .set(tag::OrderQty, qty)
               .set(tag::CumQty, 0.0)
               .set(tag::LeavesQty, qty)
               .set(tag::AvgPx, 0.0)
               .set(tag::TransactTime, fix_timestamp());
        responses.push_back(new_ack);

        // 4. Fill (possibly partial then full)
        double fill_price = price;
        if (ord_type == '1') { // Market order
            // Simulate market price with slippage
            fill_price = 150.0; // Placeholder
        }
        // Apply slippage
        double slippage = fill_price * (config_.price_slippage_bps / 10000.0);
        if (side == '1') fill_price += slippage;  // Buy pays more
        else fill_price -= slippage;               // Sell receives less

        double rand_partial = static_cast<double>(rand() % 1000) / 1000.0;
        if (rand_partial < config_.partial_fill_pct && qty > 1) {
            // Partial fill first
            double partial_qty = std::max(1.0, std::floor(qty * 0.6));
            double remaining = qty - partial_qty;

            FixMessage partial;
            partial.set(tag::BeginString, fix_version_string(version))
                   .set(tag::MsgType, msgtype::ExecutionReport)
                   .set(tag::SenderCompID, target)
                   .set(tag::TargetCompID, sender)
                   .set(tag::MsgSeqNum, next_seq())
                   .set(tag::SendingTime, fix_timestamp())
                   .set(tag::OrderID, order_id)
                   .set(tag::ClOrdID, cl_ord_id)
                   .set(tag::ExecID, fix_exec_id())
                   .set(tag::ExecType, static_cast<char>(FixExecType::PARTIAL))
                   .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::PARTIALLY_FILLED))
                   .set(tag::Symbol, symbol)
                   .set(tag::Side, side)
                   .set(tag::OrderQty, qty)
                   .set(tag::LastQty, partial_qty)
                   .set(tag::LastPx, fill_price)
                   .set(tag::CumQty, partial_qty)
                   .set(tag::LeavesQty, remaining)
                   .set(tag::AvgPx, fill_price)
                   .set(tag::TransactTime, fix_timestamp());
            responses.push_back(partial);

            // Then fill the rest
            FixMessage fill;
            fill.set(tag::BeginString, fix_version_string(version))
                .set(tag::MsgType, msgtype::ExecutionReport)
                .set(tag::SenderCompID, target)
                .set(tag::TargetCompID, sender)
                .set(tag::MsgSeqNum, next_seq())
                .set(tag::SendingTime, fix_timestamp())
                .set(tag::OrderID, order_id)
                .set(tag::ClOrdID, cl_ord_id)
                .set(tag::ExecID, fix_exec_id())
                .set(tag::ExecType, static_cast<char>(FixExecType::FILL))
                .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::FILLED))
                .set(tag::Symbol, symbol)
                .set(tag::Side, side)
                .set(tag::OrderQty, qty)
                .set(tag::LastQty, remaining)
                .set(tag::LastPx, fill_price)
                .set(tag::CumQty, qty)
                .set(tag::LeavesQty, 0.0)
                .set(tag::AvgPx, fill_price)
                .set(tag::TransactTime, fix_timestamp());
            responses.push_back(fill);
        } else {
            // Single full fill
            FixMessage fill;
            fill.set(tag::BeginString, fix_version_string(version))
                .set(tag::MsgType, msgtype::ExecutionReport)
                .set(tag::SenderCompID, target)
                .set(tag::TargetCompID, sender)
                .set(tag::MsgSeqNum, next_seq())
                .set(tag::SendingTime, fix_timestamp())
                .set(tag::OrderID, order_id)
                .set(tag::ClOrdID, cl_ord_id)
                .set(tag::ExecID, fix_exec_id())
                .set(tag::ExecType, static_cast<char>(FixExecType::FILL))
                .set(tag::OrdStatus, static_cast<char>(FixOrdStatus::FILLED))
                .set(tag::Symbol, symbol)
                .set(tag::Side, side)
                .set(tag::OrderQty, qty)
                .set(tag::LastQty, qty)
                .set(tag::LastPx, fill_price)
                .set(tag::CumQty, qty)
                .set(tag::LeavesQty, 0.0)
                .set(tag::AvgPx, fill_price)
                .set(tag::TransactTime, fix_timestamp());
            responses.push_back(fill);
        }

        return responses;
    }

    /**
     * @brief Generate logon response
     */
    FixMessage logon_response(const std::string& sender, const std::string& target,
                               FixVersion version, int heartbeat_interval) {
        FixMessage msg;
        msg.set(tag::BeginString, fix_version_string(version))
           .set(tag::MsgType, msgtype::Logon)
           .set(tag::SenderCompID, target)
           .set(tag::TargetCompID, sender)
           .set(tag::MsgSeqNum, next_seq())
           .set(tag::SendingTime, fix_timestamp())
           .set(tag::EncryptMethod, 0)
           .set(tag::HeartBtInt, heartbeat_interval);
        return msg;
    }

    /**
     * @brief Generate heartbeat response
     */
    FixMessage heartbeat_response(const std::string& sender, const std::string& target,
                                   FixVersion version, const std::string& test_req_id = "") {
        FixMessage msg;
        msg.set(tag::BeginString, fix_version_string(version))
           .set(tag::MsgType, msgtype::Heartbeat)
           .set(tag::SenderCompID, target)
           .set(tag::TargetCompID, sender)
           .set(tag::MsgSeqNum, next_seq())
           .set(tag::SendingTime, fix_timestamp());
        if (!test_req_id.empty()) {
            msg.set(tag::TestReqID, test_req_id);
        }
        return msg;
    }

private:
    int next_seq() { return seq_num_.fetch_add(1); }

    Config config_;
    std::atomic<int> seq_num_{1};
};

// ============================================================================
// FIX Engine
// ============================================================================

/**
 * @brief FIX protocol engine with session management
 */
class FixEngine {
public:
    /**
     * @brief Construct FIX engine
     * @param sender_comp_id Our CompID (e.g., "GENIE")
     * @param target_comp_id Broker CompID (e.g., "BROKER")
     * @param version FIX protocol version
     */
    FixEngine(const std::string& sender_comp_id,
              const std::string& target_comp_id,
              FixVersion version = FixVersion::FIX_44)
        : sender_comp_id_(sender_comp_id)
        , target_comp_id_(target_comp_id)
        , version_(version) {}

    ~FixEngine() {
        disconnect();
    }

    // Non-copyable
    FixEngine(const FixEngine&) = delete;
    FixEngine& operator=(const FixEngine&) = delete;

    // ---- Callbacks ----

    using ExecutionReportCallback = std::function<void(const ExecutionReport&)>;
    using MessageCallback = std::function<void(const FixMessage&)>;
    using StateCallback = std::function<void(FixSessionState)>;

    void on_execution_report(ExecutionReportCallback cb) { er_callback_ = std::move(cb); }
    void on_message(MessageCallback cb) { msg_callback_ = std::move(cb); }
    void on_state_change(StateCallback cb) { state_callback_ = std::move(cb); }

    // ---- Connection ----

    /**
     * @brief Connect to FIX counterparty (or start simulated session)
     * @param host Broker hostname
     * @param port Broker FIX port
     * @param simulated If true, use SimulatedCounterparty
     */
    bool connect(const std::string& host = "localhost", int port = 9876,
                 bool simulated = true) {
        if (state_ != FixSessionState::DISCONNECTED) return false;

        host_ = host;
        port_num_ = port;
        simulated_ = simulated;

        set_state(FixSessionState::CONNECTING);

        if (simulated) {
            // No real TCP connection needed
            set_state(FixSessionState::LOGON_SENT);

            // Send logon
            FixMessage logon = build_logon();
            store_message(logon);

            // Simulated logon response
            auto response = counterparty_.logon_response(
                sender_comp_id_, target_comp_id_, version_, heartbeat_interval_);
            process_incoming(response);

            // Start heartbeat thread
            running_.store(true);
            heartbeat_thread_ = std::thread([this]() { heartbeat_loop(); });

            return true;
        }

        // Real TCP connection would go here:
        // 1. Create socket, connect to host:port
        // 2. Send Logon message
        // 3. Wait for Logon response
        // 4. Start heartbeat thread and receive thread
        // For now, return false for non-simulated
        set_state(FixSessionState::ERROR);
        return false;
    }

    /**
     * @brief Disconnect and logout
     */
    void disconnect() {
        if (state_ == FixSessionState::DISCONNECTED) return;

        if (state_ == FixSessionState::ACTIVE) {
            // Send logout
            FixMessage logout;
            logout.set(tag::BeginString, fix_version_string(version_))
                  .set(tag::MsgType, msgtype::Logout)
                  .set(tag::SenderCompID, sender_comp_id_)
                  .set(tag::TargetCompID, target_comp_id_)
                  .set(tag::MsgSeqNum, next_outgoing_seq())
                  .set(tag::SendingTime, fix_timestamp());
            store_message(logout);
            set_state(FixSessionState::LOGOUT_SENT);
        }

        running_.store(false);
        if (heartbeat_thread_.joinable()) heartbeat_thread_.join();

        set_state(FixSessionState::DISCONNECTED);
    }

    // ---- Order Entry ----

    /**
     * @brief Send a new order
     * @return ClOrdID for tracking
     */
    std::string send_new_order(const std::string& symbol,
                                FixSide side,
                                double quantity,
                                double price = 0.0,
                                FixOrdType ord_type = FixOrdType::LIMIT,
                                FixTimeInForce tif = FixTimeInForce::DAY,
                                const std::string& account = "") {
        if (state_ != FixSessionState::ACTIVE) {
            return ""; // Not connected
        }

        std::string cl_ord_id = fix_order_id();

        FixMessage order;
        order.set(tag::BeginString, fix_version_string(version_))
             .set(tag::MsgType, msgtype::NewOrderSingle)
             .set(tag::SenderCompID, sender_comp_id_)
             .set(tag::TargetCompID, target_comp_id_)
             .set(tag::MsgSeqNum, next_outgoing_seq())
             .set(tag::SendingTime, fix_timestamp())
             .set(tag::ClOrdID, cl_ord_id)
             .set(tag::Symbol, symbol)
             .set(tag::Side, static_cast<char>(side))
             .set(tag::OrderQty, quantity)
             .set(tag::OrdType, static_cast<char>(ord_type))
             .set(tag::TimeInForce, static_cast<char>(tif))
             .set(tag::HandlInst, '1')  // Automated, no intervention
             .set(tag::TransactTime, fix_timestamp());

        if (ord_type == FixOrdType::LIMIT || ord_type == FixOrdType::STOP_LIMIT) {
            order.set(tag::Price, price);
        }
        if (ord_type == FixOrdType::STOP || ord_type == FixOrdType::STOP_LIMIT) {
            order.set(tag::StopPx, price);
        }
        if (!account.empty()) {
            order.set(tag::Account, account);
        }

        store_message(order);
        stats_.orders_sent++;

        // Simulated: process immediately
        if (simulated_) {
            auto responses = counterparty_.process_order(
                order, sender_comp_id_, target_comp_id_, version_);
            for (auto& resp : responses) {
                process_incoming(resp);
            }
        }

        return cl_ord_id;
    }

    /**
     * @brief Cancel an existing order
     */
    std::string send_cancel(const std::string& orig_cl_ord_id,
                             const std::string& symbol,
                             FixSide side) {
        if (state_ != FixSessionState::ACTIVE) return "";

        std::string cl_ord_id = fix_order_id();

        FixMessage cancel;
        cancel.set(tag::BeginString, fix_version_string(version_))
              .set(tag::MsgType, msgtype::OrderCancelRequest)
              .set(tag::SenderCompID, sender_comp_id_)
              .set(tag::TargetCompID, target_comp_id_)
              .set(tag::MsgSeqNum, next_outgoing_seq())
              .set(tag::SendingTime, fix_timestamp())
              .set(tag::ClOrdID, cl_ord_id)
              .set(tag::OrigClOrdID, orig_cl_ord_id)
              .set(tag::Symbol, symbol)
              .set(tag::Side, static_cast<char>(side))
              .set(tag::TransactTime, fix_timestamp());

        store_message(cancel);
        stats_.cancels_sent++;

        return cl_ord_id;
    }

    /**
     * @brief Request order status
     */
    void send_order_status_request(const std::string& cl_ord_id,
                                    const std::string& symbol,
                                    FixSide side) {
        if (state_ != FixSessionState::ACTIVE) return;

        FixMessage req;
        req.set(tag::BeginString, fix_version_string(version_))
           .set(tag::MsgType, msgtype::OrderStatusRequest)
           .set(tag::SenderCompID, sender_comp_id_)
           .set(tag::TargetCompID, target_comp_id_)
           .set(tag::MsgSeqNum, next_outgoing_seq())
           .set(tag::SendingTime, fix_timestamp())
           .set(tag::ClOrdID, cl_ord_id)
           .set(tag::Symbol, symbol)
           .set(tag::Side, static_cast<char>(side));

        store_message(req);
    }

    // ---- Session Info ----

    FixSessionState state() const { return state_; }
    std::string state_string() const { return session_state_string(state_); }
    std::string sender_comp_id() const { return sender_comp_id_; }
    std::string target_comp_id() const { return target_comp_id_; }
    FixVersion version() const { return version_; }

    struct SessionStats {
        int64_t messages_sent       = 0;
        int64_t messages_received   = 0;
        int64_t orders_sent         = 0;
        int64_t fills_received      = 0;
        int64_t cancels_sent        = 0;
        int64_t rejects_received    = 0;
        int64_t heartbeats_sent     = 0;
        int64_t heartbeats_received = 0;
        int outgoing_seq_num        = 0;
        int incoming_seq_num        = 0;
    };

    SessionStats session_stats() const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto s = stats_;
        s.outgoing_seq_num = outgoing_seq_.load();
        s.incoming_seq_num = incoming_seq_.load();
        return s;
    }

    /**
     * @brief Get stored messages (for replay/audit)
     */
    std::vector<FixMessage> message_store() const {
        std::lock_guard<std::mutex> lock(store_mtx_);
        return sent_messages_;
    }

    /**
     * @brief Get received execution reports
     */
    std::vector<ExecutionReport> execution_reports() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return exec_reports_;
    }

private:
    // ---- Session Management ----

    FixMessage build_logon() {
        FixMessage msg;
        msg.set(tag::BeginString, fix_version_string(version_))
           .set(tag::MsgType, msgtype::Logon)
           .set(tag::SenderCompID, sender_comp_id_)
           .set(tag::TargetCompID, target_comp_id_)
           .set(tag::MsgSeqNum, next_outgoing_seq())
           .set(tag::SendingTime, fix_timestamp())
           .set(tag::EncryptMethod, 0)
           .set(tag::HeartBtInt, heartbeat_interval_);

        if (reset_on_logon_) {
            msg.set(tag::ResetSeqNumFlag, "Y");
        }

        return msg;
    }

    void heartbeat_loop() {
        while (running_.load()) {
            // Sleep for heartbeat interval
            for (int i = 0; i < heartbeat_interval_ * 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!running_.load()) break;

            // Send heartbeat
            FixMessage hb;
            hb.set(tag::BeginString, fix_version_string(version_))
              .set(tag::MsgType, msgtype::Heartbeat)
              .set(tag::SenderCompID, sender_comp_id_)
              .set(tag::TargetCompID, target_comp_id_)
              .set(tag::MsgSeqNum, next_outgoing_seq())
              .set(tag::SendingTime, fix_timestamp());

            store_message(hb);
            stats_.heartbeats_sent++;

            // In simulated mode, get heartbeat response
            if (simulated_) {
                auto resp = counterparty_.heartbeat_response(
                    sender_comp_id_, target_comp_id_, version_);
                stats_.heartbeats_received++;
                stats_.messages_received++;
            }
        }
    }

    // ---- Message Processing ----

    void process_incoming(const FixMessage& msg) {
        std::lock_guard<std::mutex> lock(mtx_);

        stats_.messages_received++;
        incoming_seq_.fetch_add(1);

        std::string mt = msg.msg_type();

        if (mt == msgtype::Logon) {
            set_state(FixSessionState::ACTIVE);
        } else if (mt == msgtype::Logout) {
            set_state(FixSessionState::DISCONNECTED);
        } else if (mt == msgtype::Heartbeat) {
            stats_.heartbeats_received++;
        } else if (mt == msgtype::ExecutionReport) {
            ExecutionReport er = ExecutionReport::from_fix(msg);
            exec_reports_.push_back(er);

            if (er.is_fill() || er.is_partial()) stats_.fills_received++;
            if (er.is_rejected()) stats_.rejects_received++;

            // Fire callback (outside lock ideally, but simplified here)
            if (er_callback_) {
                try { er_callback_(er); } catch (const std::exception& e) {
                    Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "ER callback error: " + std::string(e.what()));
                } catch (...) {
                    Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "Unknown ER callback error");
                }
            }
        }

        // Fire generic message callback
        if (msg_callback_) {
            try { msg_callback_(msg); } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "Message callback error: " + std::string(e.what()));
            } catch (...) {
                Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "Unknown message callback error");
            }
        }
    }

    void store_message(const FixMessage& msg) {
        std::lock_guard<std::mutex> lock(store_mtx_);
        sent_messages_.push_back(msg);
        stats_.messages_sent++;
    }

    int next_outgoing_seq() {
        return outgoing_seq_.fetch_add(1);
    }

    void set_state(FixSessionState new_state) {
        state_ = new_state;
        if (state_callback_) {
            try { state_callback_(new_state); } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "State callback error: " + std::string(e.what()));
            } catch (...) {
                Logger::instance().log(LogLevel::DEBUG, "FIXEngine", "Unknown state callback error");
            }
        }
    }

    // ---- Configuration ----
    std::string sender_comp_id_;
    std::string target_comp_id_;
    FixVersion version_;
    int heartbeat_interval_         = 30;   // seconds
    bool reset_on_logon_            = true;
    std::string host_;
    int port_num_                   = 9876;
    bool simulated_                 = true;

    // ---- Session State ----
    FixSessionState state_          = FixSessionState::DISCONNECTED;
    std::atomic<int> outgoing_seq_{1};
    std::atomic<int> incoming_seq_{1};
    std::atomic<bool> running_{false};

    // ---- Storage ----
    mutable std::mutex mtx_;
    mutable std::mutex store_mtx_;
    SessionStats stats_;
    std::vector<FixMessage> sent_messages_;
    std::vector<ExecutionReport> exec_reports_;

    // ---- Threads ----
    std::thread heartbeat_thread_;

    // ---- Callbacks ----
    ExecutionReportCallback er_callback_;
    MessageCallback msg_callback_;
    StateCallback state_callback_;

    // ---- Simulated Counterparty ----
    SimulatedCounterparty counterparty_;
};

} // namespace trading
} // namespace genie

#endif // GENIE_TRADING_FIX_ENGINE_HPP
