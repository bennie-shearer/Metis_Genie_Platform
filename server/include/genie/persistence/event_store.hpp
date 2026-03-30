/**
 * @file event_store.hpp
 * @brief Event sourcing and CQRS for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Immutable event log, point-in-time state reconstruction,
 * event replay, and snapshot management.
 */
#pragma once
#ifndef GENIE_PERSISTENCE_EVENT_STORE_HPP
#define GENIE_PERSISTENCE_EVENT_STORE_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <atomic>
#include <optional>

namespace genie::persistence {

/** Event types */
enum class EventType {
    PortfolioCreated, PortfolioUpdated, PortfolioClosed,
    PositionOpened, PositionUpdated, PositionClosed,
    OrderSubmitted, OrderFilled, OrderCancelled,
    TradeExecuted,
    CashDeposit, CashWithdrawal,
    PriceUpdated, SecurityAdded,
    ConfigChanged, UserAction,
    Custom
};

inline std::string event_type_name(EventType t) {
    switch (t) {
        case EventType::PortfolioCreated: return "portfolio.created";
        case EventType::PortfolioUpdated: return "portfolio.updated";
        case EventType::PortfolioClosed: return "portfolio.closed";
        case EventType::PositionOpened: return "position.opened";
        case EventType::PositionUpdated: return "position.updated";
        case EventType::PositionClosed: return "position.closed";
        case EventType::OrderSubmitted: return "order.submitted";
        case EventType::OrderFilled: return "order.filled";
        case EventType::OrderCancelled: return "order.cancelled";
        case EventType::TradeExecuted: return "trade.executed";
        case EventType::CashDeposit: return "cash.deposit";
        case EventType::CashWithdrawal: return "cash.withdrawal";
        case EventType::PriceUpdated: return "price.updated";
        case EventType::SecurityAdded: return "security.added";
        case EventType::ConfigChanged: return "config.changed";
        case EventType::UserAction: return "user.action";
        case EventType::Custom: return "custom";
        default: return "unknown";
    }
}

/** An immutable event */
struct Event {
    uint64_t sequence{0};          // global sequence number
    EventType type;
    std::string aggregate_type;    // "portfolio", "order", "security"
    std::string aggregate_id;      // entity ID
    std::string payload;           // JSON-serialized event data
    std::string user_id;           // who caused this event
    std::chrono::system_clock::time_point timestamp;
    uint64_t version{0};           // per-aggregate version
    std::string correlation_id;    // for linking related events
    std::map<std::string, std::string> metadata;

    [[nodiscard]] std::string to_json() const {
        std::ostringstream ss;
        ss << "{\"seq\":" << sequence << ",\"type\":\"" << event_type_name(type)
           << "\",\"aggregate\":\"" << aggregate_type << ":" << aggregate_id
           << "\",\"version\":" << version << ",\"payload\":" << payload << "}";
        return ss.str();
    }
};

/** Snapshot of aggregate state at a point in time */
struct Snapshot {
    std::string aggregate_type;
    std::string aggregate_id;
    uint64_t version{0};
    std::string state;             // serialized state
    std::chrono::system_clock::time_point created;
};

/** Event handler callback */
using EventHandler = std::function<void(const Event&)>;

/** Event store - append-only immutable log */
class EventStore {
    std::vector<Event> events_;
    std::map<std::string, Snapshot> snapshots_;  // key: "type:id"
    std::map<std::string, std::vector<EventHandler>> handlers_; // type -> handlers
    mutable std::mutex mutex_;
    std::atomic<uint64_t> sequence_{0};
    std::map<std::string, uint64_t> aggregate_versions_; // "type:id" -> version
    size_t snapshot_interval_{100}; // snapshot every N events per aggregate

public:
    EventStore() = default;

    /** Append an event (immutable - cannot be modified after append) */
    uint64_t append(EventType type, const std::string& aggregate_type,
                    const std::string& aggregate_id, const std::string& payload,
                    const std::string& user_id = "system",
                    const std::string& correlation_id = "") {
        std::lock_guard lock(mutex_);

        Event event;
        event.sequence = ++sequence_;
        event.type = type;
        event.aggregate_type = aggregate_type;
        event.aggregate_id = aggregate_id;
        event.payload = payload;
        event.user_id = user_id;
        event.timestamp = std::chrono::system_clock::now();
        event.correlation_id = correlation_id;

        // Increment aggregate version
        std::string agg_key = aggregate_type + ":" + aggregate_id;
        event.version = ++aggregate_versions_[agg_key];

        events_.push_back(event);

        // Dispatch to handlers (outside lock would be better in production)
        auto type_name = event_type_name(type);
        auto it = handlers_.find(type_name);
        if (it != handlers_.end())
            for (const auto& handler : it->second) handler(event);

        // Also dispatch to wildcard handlers
        auto all_it = handlers_.find("*");
        if (all_it != handlers_.end())
            for (const auto& handler : all_it->second) handler(event);

        return event.sequence;
    }

    /** Subscribe to events of a type */
    void subscribe(const std::string& event_type, EventHandler handler) {
        std::lock_guard lock(mutex_);
        handlers_[event_type].push_back(std::move(handler));
    }

    /** Get all events for an aggregate */
    [[nodiscard]] std::vector<Event> events_for(const std::string& aggregate_type,
                                                 const std::string& aggregate_id,
                                                 uint64_t from_version = 0) const {
        std::lock_guard lock(mutex_);
        std::vector<Event> result;
        for (const auto& e : events_) {
            if (e.aggregate_type == aggregate_type && e.aggregate_id == aggregate_id
                && e.version > from_version)
                result.push_back(e);
        }
        return result;
    }

    /** Get events in a time range */
    [[nodiscard]] std::vector<Event> events_between(
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to) const {
        std::lock_guard lock(mutex_);
        std::vector<Event> result;
        for (const auto& e : events_)
            if (e.timestamp >= from && e.timestamp <= to) result.push_back(e);
        return result;
    }

    /** Get all events by type */
    [[nodiscard]] std::vector<Event> events_by_type(EventType type) const {
        std::lock_guard lock(mutex_);
        std::vector<Event> result;
        for (const auto& e : events_)
            if (e.type == type) result.push_back(e);
        return result;
    }

    /** Get events by correlation ID (linked events) */
    [[nodiscard]] std::vector<Event> events_by_correlation(const std::string& corr_id) const {
        std::lock_guard lock(mutex_);
        std::vector<Event> result;
        for (const auto& e : events_)
            if (e.correlation_id == corr_id) result.push_back(e);
        return result;
    }

    /** Save a snapshot */
    void save_snapshot(const std::string& aggregate_type, const std::string& aggregate_id,
                       uint64_t version, const std::string& state) {
        std::lock_guard lock(mutex_);
        std::string key = aggregate_type + ":" + aggregate_id;
        snapshots_[key] = {aggregate_type, aggregate_id, version, state,
                           std::chrono::system_clock::now()};
    }

    /** Load latest snapshot */
    [[nodiscard]] std::optional<Snapshot> load_snapshot(const std::string& aggregate_type,
                                                        const std::string& aggregate_id) const {
        std::lock_guard lock(mutex_);
        auto it = snapshots_.find(aggregate_type + ":" + aggregate_id);
        if (it == snapshots_.end()) return std::nullopt;
        return it->second;
    }

    /** Replay events to reconstruct state at a point in time */
    template<typename State, typename Reducer>
    State replay(const std::string& aggregate_type, const std::string& aggregate_id,
                 State initial, Reducer reducer, uint64_t up_to_version = 0) const {
        auto events = events_for(aggregate_type, aggregate_id);
        State state = initial;
        for (const auto& e : events) {
            if (up_to_version > 0 && e.version > up_to_version) break;
            state = reducer(state, e);
        }
        return state;
    }

    // ---- Statistics ----

    [[nodiscard]] uint64_t total_events() const { std::lock_guard lock(mutex_); return events_.size(); }
    [[nodiscard]] uint64_t current_sequence() const { return sequence_.load(); }
    [[nodiscard]] size_t snapshot_count() const { std::lock_guard lock(mutex_); return snapshots_.size(); }

    [[nodiscard]] uint64_t aggregate_version(const std::string& type, const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = aggregate_versions_.find(type + ":" + id);
        return (it != aggregate_versions_.end()) ? it->second : 0;
    }

    [[nodiscard]] std::map<std::string, size_t> event_type_counts() const {
        std::lock_guard lock(mutex_);
        std::map<std::string, size_t> counts;
        for (const auto& e : events_) counts[event_type_name(e.type)]++;
        return counts;
    }
};

} // namespace genie::persistence

#endif // GENIE_PERSISTENCE_EVENT_STORE_HPP
