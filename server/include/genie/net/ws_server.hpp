/**
 * @file ws_server.hpp
 * @brief WebSocket streaming server abstraction for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Channel-based pub/sub for real-time streaming:
 *   - prices:    Market data ticks
 *   - portfolio: Position/NAV updates
 *   - orders:    Order status changes
 *   - alerts:    System notifications
 *
 * Ready for integration with actual WebSocket libraries (uWebSockets, Beast).
 */
#pragma once
#ifndef GENIE_NET_WS_SERVER_HPP
#define GENIE_NET_WS_SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <functional>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <atomic>

namespace genie::net {

/** WebSocket message types */
enum class WsMessageType { Text, Binary, Ping, Pong, Close };

/** A WebSocket message */
struct WsMessage {
    WsMessageType type{WsMessageType::Text};
    std::string channel;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;
    std::string sender_id;
};

/** Subscription channels */
namespace Channel {
    inline const std::string Prices = "prices";
    inline const std::string Portfolio = "portfolio";
    inline const std::string Orders = "orders";
    inline const std::string Alerts = "alerts";
    inline const std::string System = "system";
}

/** Represents a connected WebSocket client */
struct WsClient {
    std::string client_id;
    std::string user_id;
    std::string username;
    std::set<std::string> subscriptions;
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_activity;
    bool active{true};
    std::queue<WsMessage> outbox;  // messages pending delivery

    [[nodiscard]] bool subscribed_to(const std::string& channel) const {
        return subscriptions.count(channel) > 0;
    }
};

/** WebSocket server with pub/sub channels */
class WsServer {
    std::map<std::string, WsClient> clients_;
    std::vector<WsMessage> message_log_;
    mutable std::mutex mutex_;
    std::atomic<int> client_counter_{0};
    size_t max_log_entries_{10000};
    size_t max_outbox_{1000};

    // Callbacks for actual WebSocket implementations
    std::function<void(const std::string& client_id, const std::string& payload)> on_send_;

public:
    WsServer() = default;

    /** Set send callback for real WebSocket integration */
    void set_send_callback(std::function<void(const std::string&, const std::string&)> cb) {
        on_send_ = std::move(cb);
    }

    /** Register a new client connection */
    std::string connect(const std::string& user_id, const std::string& username) {
        std::lock_guard lock(mutex_);
        std::string client_id = "ws-" + std::to_string(++client_counter_);
        WsClient client;
        client.client_id = client_id;
        client.user_id = user_id;
        client.username = username;
        client.connected_at = std::chrono::system_clock::now();
        client.last_activity = client.connected_at;
        client.active = true;
        clients_[client_id] = client;
        return client_id;
    }

    /** Disconnect a client */
    void disconnect(const std::string& client_id) {
        std::lock_guard lock(mutex_);
        auto it = clients_.find(client_id);
        if (it != clients_.end()) it->second.active = false;
    }

    /** Subscribe a client to a channel */
    bool subscribe(const std::string& client_id, const std::string& channel) {
        std::lock_guard lock(mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end() || !it->second.active) return false;
        it->second.subscriptions.insert(channel);
        it->second.last_activity = std::chrono::system_clock::now();
        return true;
    }

    /** Unsubscribe a client from a channel */
    bool unsubscribe(const std::string& client_id, const std::string& channel) {
        std::lock_guard lock(mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end()) return false;
        it->second.subscriptions.erase(channel);
        return true;
    }

    /** Publish a message to a channel (fans out to all subscribers) */
    size_t publish(const std::string& channel, const std::string& payload,
                   const std::string& sender = "system") {
        std::lock_guard lock(mutex_);
        WsMessage msg;
        msg.type = WsMessageType::Text;
        msg.channel = channel;
        msg.payload = payload;
        msg.timestamp = std::chrono::system_clock::now();
        msg.sender_id = sender;

        // Log
        message_log_.push_back(msg);
        if (message_log_.size() > max_log_entries_)
            message_log_.erase(message_log_.begin());

        // Fan out to subscribers
        size_t delivered = 0;
        for (auto& [id, client] : clients_) {
            if (client.active && client.subscribed_to(channel)) {
                if (client.outbox.size() < max_outbox_) {
                    client.outbox.push(msg);
                    if (on_send_) on_send_(id, payload);
                    delivered++;
                }
            }
        }
        return delivered;
    }

    /** Send a direct message to a specific client */
    bool send_to(const std::string& client_id, const std::string& payload) {
        std::lock_guard lock(mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end() || !it->second.active) return false;
        WsMessage msg;
        msg.type = WsMessageType::Text;
        msg.channel = "direct";
        msg.payload = payload;
        msg.timestamp = std::chrono::system_clock::now();
        it->second.outbox.push(msg);
        if (on_send_) on_send_(client_id, payload);
        return true;
    }

    /** Drain outbox for a client (returns pending messages) */
    std::vector<WsMessage> drain(const std::string& client_id) {
        std::lock_guard lock(mutex_);
        std::vector<WsMessage> msgs;
        auto it = clients_.find(client_id);
        if (it == clients_.end()) return msgs;
        while (!it->second.outbox.empty()) {
            msgs.push_back(it->second.outbox.front());
            it->second.outbox.pop();
        }
        it->second.last_activity = std::chrono::system_clock::now();
        return msgs;
    }

    // ---- Convenience publishers ----

    /** Publish a price update */
    size_t publish_price(const std::string& security_id, double price, double change_pct = 0) {
        std::string payload = "{\"type\":\"price\",\"id\":\"" + security_id
                            + "\",\"price\":" + std::to_string(price)
                            + ",\"change\":" + std::to_string(change_pct) + "}";
        return publish(Channel::Prices, payload, "market");
    }

    /** Publish a portfolio update */
    size_t publish_portfolio_update(const std::string& portfolio_id, double nav, int positions) {
        std::string payload = "{\"type\":\"portfolio\",\"id\":\"" + portfolio_id
                            + "\",\"nav\":" + std::to_string(nav)
                            + ",\"positions\":" + std::to_string(positions) + "}";
        return publish(Channel::Portfolio, payload, "portfolio");
    }

    /** Publish an order status change */
    size_t publish_order_status(const std::string& order_id, const std::string& status) {
        std::string payload = "{\"type\":\"order\",\"id\":\"" + order_id
                            + "\",\"status\":\"" + status + "\"}";
        return publish(Channel::Orders, payload, "oms");
    }

    /** Publish a system alert */
    size_t publish_alert(const std::string& severity, const std::string& message) {
        std::string payload = "{\"type\":\"alert\",\"severity\":\"" + severity
                            + "\",\"message\":\"" + message + "\"}";
        return publish(Channel::Alerts, payload, "system");
    }

    // ---- Statistics ----

    [[nodiscard]] size_t client_count() const {
        std::lock_guard lock(mutex_);
        size_t count = 0;
        for (const auto& [k, c] : clients_) if (c.active) count++;
        return count;
    }

    [[nodiscard]] size_t subscriber_count(const std::string& channel) const {
        std::lock_guard lock(mutex_);
        size_t count = 0;
        for (const auto& [k, c] : clients_)
            if (c.active && c.subscribed_to(channel)) count++;
        return count;
    }

    [[nodiscard]] size_t message_count() const { std::lock_guard lock(mutex_); return message_log_.size(); }

    /** Clean up inactive clients */
    size_t cleanup(std::chrono::seconds idle_timeout = std::chrono::seconds(300)) {
        std::lock_guard lock(mutex_);
        auto cutoff = std::chrono::system_clock::now() - idle_timeout;
        size_t removed = 0;
        for (auto it = clients_.begin(); it != clients_.end(); ) {
            if (!it->second.active || it->second.last_activity < cutoff) {
                it = clients_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }
};

} // namespace genie::net

#endif // GENIE_NET_WS_SERVER_HPP
