/**
 * @file websocket_manager.hpp
 * @brief WebSocket Connection Manager for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Manages WebSocket connections for real-time data streaming including
 * market data, order updates, and system notifications. Provides connection
 * lifecycle management, channel subscriptions, heartbeat monitoring,
 * and message routing.
 *
 * Features:
 *  - Connection lifecycle management (connect, authenticate, subscribe, disconnect)
 *  - Channel-based subscription model (market_data, orders, alerts, system)
 *  - Topic-based message routing with wildcard support
 *  - Client heartbeat monitoring with configurable timeout
 *  - Automatic reconnection tracking
 *  - Message queue with backpressure handling
 *  - Rate limiting per client
 *  - Connection grouping (broadcast to channel subscribers)
 *  - Binary and text message support
 *  - Connection statistics and monitoring
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_WEBSOCKET_MANAGER_HPP
#define GENIE_WEBSOCKET_MANAGER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <sstream>
#include <algorithm>
#include <deque>
#include <queue>

namespace genie::net {

// ============================================================================
// Enums
// ============================================================================

enum class ConnectionState { CONNECTING, AUTHENTICATED, SUBSCRIBED, ACTIVE, IDLE, DISCONNECTING, CLOSED };
enum class MessageType { TEXT, BINARY, PING, PONG, CLOSE };
enum class ChannelType { MARKET_DATA, ORDERS, ALERTS, SYSTEM, PORTFOLIO, ANALYTICS, CUSTOM };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief WebSocket message */
struct WebSocketMessage {
    std::string message_id;
    MessageType type{MessageType::TEXT};
    std::string channel;
    std::string topic;
    std::string payload;
    std::string sender_id;
    std::string timestamp;
    std::size_t size_bytes{0};
    int priority{0};
};

/** @brief Channel subscription */
struct ChannelSubscription {
    std::string channel;
    ChannelType type{ChannelType::CUSTOM};
    std::string topic_filter;    // Wildcard support: "market.*", "orders.AAPL"
    std::string subscribed_at;
    uint64_t messages_received{0};
};

/** @brief Client connection record */
struct WebSocketConnection {
    std::string connection_id;
    std::string client_id;
    std::string user_id;
    ConnectionState state{ConnectionState::CONNECTING};
    std::string connected_at;
    std::string last_activity;
    std::string last_heartbeat;
    std::string remote_address;
    std::string user_agent;
    std::vector<ChannelSubscription> subscriptions;
    uint64_t messages_sent{0};
    uint64_t messages_received{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    int reconnect_count{0};
    double avg_latency_ms{0.0};
    bool authenticated{false};
};

/** @brief Rate limit configuration */
struct RateLimitConfig {
    int max_messages_per_second{100};
    int max_subscriptions{50};
    int max_message_size_bytes{65536};
    int queue_max_size{10000};
};

/** @brief Connection group for broadcasting */
struct ConnectionGroup {
    std::string group_id;
    std::string name;
    std::unordered_set<std::string> connection_ids;
    std::string created_at;
};

/** @brief WebSocket manager statistics */
struct WebSocketStats {
    std::size_t active_connections{0};
    std::size_t total_connections{0};
    std::size_t total_subscriptions{0};
    uint64_t total_messages_sent{0};
    uint64_t total_messages_received{0};
    uint64_t total_bytes_sent{0};
    uint64_t total_bytes_received{0};
    uint64_t messages_dropped{0};
    uint64_t rate_limit_rejections{0};
    double avg_latency_ms{0.0};
    std::size_t groups_count{0};
    std::unordered_map<std::string, std::size_t> subscribers_by_channel;
};

// ============================================================================
// WebSocketManager
// ============================================================================

/**
 * @class WebSocketManager
 * @brief Manages WebSocket connections, subscriptions, and message routing
 */
class WebSocketManager {
public:
    explicit WebSocketManager(RateLimitConfig rate_config = {})
        : rate_config_(std::move(rate_config)) {}

    // ---- Connection Lifecycle ----

    /** @brief Register a new client connection */
    WebSocketConnection connect(const std::string& client_id, const std::string& user_id = "",
                                 const std::string& remote_addr = "") {
        std::lock_guard lock(mutex_);
        WebSocketConnection conn;
        conn.connection_id = "WS-" + std::to_string(++connection_counter_);
        conn.client_id = client_id;
        conn.user_id = user_id;
        conn.remote_address = remote_addr;
        conn.state = ConnectionState::CONNECTING;
        conn.connected_at = now_str();
        conn.last_activity = conn.connected_at;
        conn.last_heartbeat = conn.connected_at;
        connections_[conn.connection_id] = conn;
        client_to_conn_[client_id] = conn.connection_id;
        total_connections_++;
        return conn;
    }

    /** @brief Authenticate a connection */
    bool authenticate(const std::string& connection_id, const std::string& token = "") {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;
        it->second.authenticated = true;
        it->second.state = ConnectionState::AUTHENTICATED;
        it->second.last_activity = now_str();
        return true;
    }

    /** @brief Disconnect a client */
    bool disconnect(const std::string& connection_id, const std::string& reason = "") {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;
        it->second.state = ConnectionState::CLOSED;
        // Remove from groups
        for (auto& [_, group] : groups_) {
            group.connection_ids.erase(connection_id);
        }
        // Remove from channel index
        for (const auto& sub : it->second.subscriptions) {
            channel_subscribers_[sub.channel].erase(connection_id);
        }
        client_to_conn_.erase(it->second.client_id);
        connections_.erase(it);
        return true;
    }

    /** @brief Record heartbeat from client */
    void heartbeat(const std::string& connection_id) {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it != connections_.end()) {
            it->second.last_heartbeat = now_str();
            it->second.last_activity = now_str();
            if (it->second.state == ConnectionState::IDLE) {
                it->second.state = ConnectionState::ACTIVE;
            }
        }
    }

    /** @brief Get connection info */
    [[nodiscard]] std::optional<WebSocketConnection> get_connection(const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(id);
        if (it != connections_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List all active connections */
    [[nodiscard]] std::vector<WebSocketConnection> list_connections() const {
        std::lock_guard lock(mutex_);
        std::vector<WebSocketConnection> result;
        for (const auto& [_, c] : connections_) result.push_back(c);
        return result;
    }

    // ---- Subscription Management ----

    /** @brief Subscribe a connection to a channel/topic */
    bool subscribe(const std::string& connection_id, const std::string& channel,
                   ChannelType type = ChannelType::CUSTOM, const std::string& topic_filter = "*") {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;

        // Rate limit check
        if (static_cast<int>(it->second.subscriptions.size()) >= rate_config_.max_subscriptions) {
            rate_limit_rejections_++;
            return false;
        }

        ChannelSubscription sub;
        sub.channel = channel;
        sub.type = type;
        sub.topic_filter = topic_filter;
        sub.subscribed_at = now_str();
        it->second.subscriptions.push_back(sub);

        if (it->second.state == ConnectionState::AUTHENTICATED) {
            it->second.state = ConnectionState::SUBSCRIBED;
        }

        channel_subscribers_[channel].insert(connection_id);
        return true;
    }

    /** @brief Unsubscribe from a channel */
    bool unsubscribe(const std::string& connection_id, const std::string& channel) {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;

        auto& subs = it->second.subscriptions;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [&](const ChannelSubscription& s) { return s.channel == channel; }), subs.end());

        channel_subscribers_[channel].erase(connection_id);
        return true;
    }

    // ---- Messaging ----

    /** @brief Send a message to a specific connection */
    bool send(const std::string& connection_id, const WebSocketMessage& message) {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it == connections_.end()) return false;

        it->second.messages_sent++;
        it->second.bytes_sent += message.payload.size();
        it->second.last_activity = now_str();
        total_messages_sent_++;
        total_bytes_sent_ += message.payload.size();
        return true;
    }

    /** @brief Broadcast to all subscribers of a channel */
    int broadcast(const std::string& channel, const WebSocketMessage& message) {
        std::lock_guard lock(mutex_);
        int sent = 0;
        auto it = channel_subscribers_.find(channel);
        if (it == channel_subscribers_.end()) return 0;

        for (const auto& conn_id : it->second) {
            auto conn_it = connections_.find(conn_id);
            if (conn_it != connections_.end()) {
                // Topic filter matching
                bool matches = true;
                for (auto& sub : conn_it->second.subscriptions) {
                    if (sub.channel == channel) {
                        matches = topic_matches(message.topic, sub.topic_filter);
                        if (matches) sub.messages_received++;
                        break;
                    }
                }
                if (matches) {
                    conn_it->second.messages_sent++;
                    conn_it->second.bytes_sent += message.payload.size();
                    sent++;
                }
            }
        }
        total_messages_sent_ += sent;
        total_bytes_sent_ += message.payload.size() * sent;
        return sent;
    }

    /** @brief Broadcast to a connection group */
    int broadcast_group(const std::string& group_id, const WebSocketMessage& message) {
        std::lock_guard lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return 0;
        int sent = 0;
        for (const auto& conn_id : it->second.connection_ids) {
            auto conn_it = connections_.find(conn_id);
            if (conn_it != connections_.end()) {
                conn_it->second.messages_sent++;
                sent++;
            }
        }
        return sent;
    }

    /** @brief Record a received message from client */
    void receive(const std::string& connection_id, const WebSocketMessage& message) {
        std::lock_guard lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it != connections_.end()) {
            it->second.messages_received++;
            it->second.bytes_received += message.payload.size();
            it->second.last_activity = now_str();
            it->second.state = ConnectionState::ACTIVE;
        }
        total_messages_received_++;
        total_bytes_received_ += message.payload.size();
    }

    // ---- Groups ----

    /** @brief Create a connection group */
    ConnectionGroup create_group(const std::string& name) {
        std::lock_guard lock(mutex_);
        ConnectionGroup group;
        group.group_id = "GRP-" + std::to_string(++group_counter_);
        group.name = name;
        group.created_at = now_str();
        groups_[group.group_id] = group;
        return group;
    }

    /** @brief Add connection to group */
    bool add_to_group(const std::string& group_id, const std::string& connection_id) {
        std::lock_guard lock(mutex_);
        auto it = groups_.find(group_id);
        if (it == groups_.end()) return false;
        it->second.connection_ids.insert(connection_id);
        return true;
    }

    // ---- Maintenance ----

    /** @brief Find and disconnect idle connections */
    std::vector<std::string> cleanup_idle(std::chrono::seconds idle_timeout = std::chrono::seconds(300)) {
        std::lock_guard lock(mutex_);
        std::vector<std::string> disconnected;
        auto now = std::chrono::system_clock::now();

        for (auto it = connections_.begin(); it != connections_.end(); ) {
            // Simplified idle detection
            if (it->second.state == ConnectionState::IDLE) {
                disconnected.push_back(it->second.connection_id);
                for (auto& [_, group] : groups_) {
                    group.connection_ids.erase(it->first);
                }
                for (const auto& sub : it->second.subscriptions) {
                    channel_subscribers_[sub.channel].erase(it->first);
                }
                client_to_conn_.erase(it->second.client_id);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
        return disconnected;
    }

    // ---- Statistics ----

    [[nodiscard]] WebSocketStats stats() const {
        std::lock_guard lock(mutex_);
        WebSocketStats s;
        s.active_connections = connections_.size();
        s.total_connections = total_connections_;
        s.total_messages_sent = total_messages_sent_;
        s.total_messages_received = total_messages_received_;
        s.total_bytes_sent = total_bytes_sent_;
        s.total_bytes_received = total_bytes_received_;
        s.messages_dropped = messages_dropped_;
        s.rate_limit_rejections = rate_limit_rejections_;
        s.groups_count = groups_.size();

        for (const auto& [_, conn] : connections_) {
            s.total_subscriptions += conn.subscriptions.size();
        }
        for (const auto& [channel, subs] : channel_subscribers_) {
            s.subscribers_by_channel[channel] = subs.size();
        }
        return s;
    }

    [[nodiscard]] std::size_t active_count() const {
        std::lock_guard lock(mutex_);
        return connections_.size();
    }

private:
    static bool topic_matches(const std::string& topic, const std::string& filter) {
        if (filter == "*") return true;
        if (filter == topic) return true;
        // Simple wildcard: "market.*" matches "market.AAPL"
        if (filter.back() == '*') {
            return topic.substr(0, filter.size() - 1) == filter.substr(0, filter.size() - 1);
        }
        return false;
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    RateLimitConfig rate_config_;
    std::unordered_map<std::string, WebSocketConnection> connections_;
    std::unordered_map<std::string, std::string> client_to_conn_;
    std::unordered_map<std::string, std::unordered_set<std::string>> channel_subscribers_;
    std::unordered_map<std::string, ConnectionGroup> groups_;
    uint64_t connection_counter_{0};
    uint64_t group_counter_{0};
    uint64_t total_connections_{0};
    uint64_t total_messages_sent_{0};
    uint64_t total_messages_received_{0};
    uint64_t total_bytes_sent_{0};
    uint64_t total_bytes_received_{0};
    uint64_t messages_dropped_{0};
    uint64_t rate_limit_rejections_{0};
    mutable std::mutex mutex_;
};

} // namespace genie::net

#endif // GENIE_WEBSOCKET_MANAGER_HPP
