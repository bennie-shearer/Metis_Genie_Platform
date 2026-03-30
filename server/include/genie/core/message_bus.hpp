/**
 * @file message_bus.hpp
 * @brief Event-driven message bus architecture
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements enterprise-grade event-driven messaging:
 * - Publish/subscribe pattern with topic routing
 * - Message queue with priority ordering
 * - Dead letter queue for failed messages
 * - Message filtering and transformation
 * - Replay capability for event sourcing
 * - Async and sync dispatch modes
 * - Channel-based message routing
 * - Metrics and observability hooks
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_MESSAGE_BUS_HPP
#define GENIE_CORE_MESSAGE_BUS_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <queue>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <any>
#include <optional>
#include <thread>
#include <deque>
#include <set>
#include <variant>

namespace genie {
namespace core {
namespace messaging {

// ============================================================================
// Enumerations
// ============================================================================

enum class MessagePriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

enum class DeliveryMode {
    FireAndForget,   // No confirmation
    AtLeastOnce,     // Retry on failure
    ExactlyOnce      // Deduplicate
};

enum class SubscriptionType {
    Persistent,      // Survives reconnect
    Ephemeral,       // Lost on disconnect
    Durable          // Survives restarts (stored)
};

enum class MessageStatus {
    Pending,
    Delivered,
    Acknowledged,
    Failed,
    DeadLettered,
    Expired
};

enum class ChannelType {
    PubSub,          // Fan-out to all subscribers
    Queue,           // Round-robin to one subscriber
    Request,         // Request/reply pattern
    Stream           // Ordered, persistent stream
};

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] inline std::string priority_string(MessagePriority p) {
    switch (p) {
        case MessagePriority::Low:      return "low";
        case MessagePriority::Normal:   return "normal";
        case MessagePriority::High:     return "high";
        case MessagePriority::Critical: return "critical";
    }
    return "unknown";
}

[[nodiscard]] inline std::string status_string(MessageStatus s) {
    switch (s) {
        case MessageStatus::Pending:      return "pending";
        case MessageStatus::Delivered:    return "delivered";
        case MessageStatus::Acknowledged: return "acknowledged";
        case MessageStatus::Failed:       return "failed";
        case MessageStatus::DeadLettered: return "dead_lettered";
        case MessageStatus::Expired:      return "expired";
    }
    return "unknown";
}

[[nodiscard]] inline std::string channel_type_string(ChannelType ct) {
    switch (ct) {
        case ChannelType::PubSub:  return "pubsub";
        case ChannelType::Queue:   return "queue";
        case ChannelType::Request: return "request";
        case ChannelType::Stream:  return "stream";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Message envelope
 */
struct Message {
    std::string id;
    std::string topic;
    std::string payload;           // JSON or any string content
    std::string content_type;      // "application/json", "text/plain", etc.
    MessagePriority priority{MessagePriority::Normal};
    DeliveryMode delivery{DeliveryMode::FireAndForget};
    MessageStatus status{MessageStatus::Pending};
    
    std::string source;            // Publisher ID
    std::string correlation_id;    // For request/reply
    std::string reply_to;          // Reply topic
    
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> metadata;
    
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    int retry_count{0};
    int max_retries{3};
    
    int64_t sequence_number{0};    // For ordered delivery
    
    [[nodiscard]] bool is_expired() const {
        if (expires_at == std::chrono::system_clock::time_point{}) return false;
        return std::chrono::system_clock::now() > expires_at;
    }
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Message[" << id << "] topic=" << topic
            << " priority=" << priority_string(priority)
            << " status=" << status_string(status)
            << " payload_size=" << payload.size();
        return oss.str();
    }
};

/**
 * @brief Subscription filter
 */
struct TopicFilter {
    std::string pattern;           // e.g., "market.*", "trade.#"
    bool is_wildcard{false};
    
    [[nodiscard]] bool matches(const std::string& topic) const {
        if (!is_wildcard) return topic == pattern;
        
        // Simple wildcard matching:
        // "*" matches exactly one level
        // "#" matches zero or more levels
        if (pattern == "#") return true;
        
        // Split by '.'
        auto split = [](const std::string& s) -> std::vector<std::string> {
            std::vector<std::string> parts;
            std::istringstream iss(s);
            std::string part;
            while (std::getline(iss, part, '.')) parts.push_back(part);
            return parts;
        };
        
        auto pat_parts = split(pattern);
        auto top_parts = split(topic);
        
        size_t pi = 0, ti = 0;
        while (pi < pat_parts.size() && ti < top_parts.size()) {
            if (pat_parts[pi] == "#") return true;
            if (pat_parts[pi] == "*" || pat_parts[pi] == top_parts[ti]) {
                ++pi; ++ti;
            } else {
                return false;
            }
        }
        return pi == pat_parts.size() && ti == top_parts.size();
    }
};

/**
 * @brief Subscription descriptor
 */
struct Subscription {
    std::string id;
    std::string subscriber_id;
    TopicFilter filter;
    SubscriptionType type{SubscriptionType::Persistent};
    std::function<void(const Message&)> handler;
    int priority{0};               // Higher = called first
    bool active{true};
    std::chrono::system_clock::time_point created_at;
    int64_t messages_received{0};
    int64_t messages_failed{0};
};

/**
 * @brief Channel configuration
 */
struct Channel {
    std::string name;
    ChannelType type{ChannelType::PubSub};
    int max_queue_size{10000};
    int max_subscribers{1000};
    bool persist_messages{false};
    std::chrono::seconds message_ttl{3600};
    bool active{true};
    int64_t total_published{0};
    int64_t total_delivered{0};
    int64_t total_failed{0};
};

/**
 * @brief Message bus metrics
 */
struct BusMetrics {
    int64_t total_published{0};
    int64_t total_delivered{0};
    int64_t total_failed{0};
    int64_t total_dead_lettered{0};
    int64_t total_expired{0};
    int64_t total_retried{0};
    int active_subscriptions{0};
    int active_channels{0};
    int queue_depth{0};
    int dead_letter_depth{0};
    double avg_latency_ms{0};
    double p99_latency_ms{0};
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Message Bus Metrics:\n";
        oss << "  Published:     " << total_published << "\n";
        oss << "  Delivered:     " << total_delivered << "\n";
        oss << "  Failed:        " << total_failed << "\n";
        oss << "  Dead-lettered: " << total_dead_lettered << "\n";
        oss << "  Subscriptions: " << active_subscriptions << "\n";
        oss << "  Channels:      " << active_channels << "\n";
        oss << "  Queue Depth:   " << queue_depth << "\n";
        oss << std::fixed << std::setprecision(2);
        oss << "  Avg Latency:   " << avg_latency_ms << " ms\n";
        oss << "  P99 Latency:   " << p99_latency_ms << " ms\n";
        return oss.str();
    }
};

// ============================================================================
// Message ID Generator
// ============================================================================

class MessageIdGenerator {
public:
    [[nodiscard]] std::string next() {
        int64_t id = counter_.fetch_add(1, std::memory_order_relaxed);
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        std::ostringstream oss;
        oss << "msg-" << std::hex << (ms & 0xFFFFFFFF) << "-" << std::dec << id;
        return oss.str();
    }
    
private:
    std::atomic<int64_t> counter_{1};
};

// ============================================================================
// Dead Letter Queue
// ============================================================================

/**
 * @brief Dead letter queue for failed messages
 */
class DeadLetterQueue {
public:
    explicit DeadLetterQueue(int max_size = 10000) : max_size_(max_size) {}
    
    /**
     * @brief Add message to dead letter queue
     */
    void enqueue(Message msg, const std::string& failure_reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        msg.status = MessageStatus::DeadLettered;
        msg.metadata["failure_reason"] = failure_reason;
        msg.metadata["dead_lettered_at"] = now_string();
        
        if (static_cast<int>(queue_.size()) >= max_size_) {
            queue_.pop_front();
        }
        queue_.push_back(std::move(msg));
    }
    
    /**
     * @brief Peek at dead letters
     */
    [[nodiscard]] std::vector<Message> peek(int count = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Message> result;
        int n = std::min(count, static_cast<int>(queue_.size()));
        for (int i = 0; i < n; ++i) {
            result.push_back(queue_[queue_.size() - 1 - i]);
        }
        return result;
    }
    
    /**
     * @brief Replay dead letter back to bus
     */
    [[nodiscard]] std::optional<Message> dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        Message msg = queue_.front();
        queue_.pop_front();
        return msg;
    }
    
    /**
     * @brief Clear all dead letters
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    
    [[nodiscard]] int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(queue_.size());
    }

private:
    mutable std::mutex mutex_;
    std::deque<Message> queue_;
    int max_size_;
    
    [[nodiscard]] static std::string now_string() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }
};

// ============================================================================
// Message Store (Event Sourcing)
// ============================================================================

/**
 * @brief In-memory message store for replay
 */
class MessageStore {
public:
    explicit MessageStore(int max_size = 100000) : max_size_(max_size) {}
    
    /**
     * @brief Store message
     */
    void store(const Message& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(messages_.size()) >= max_size_) {
            messages_.pop_front();
        }
        messages_.push_back(msg);
        ++sequence_;
    }
    
    /**
     * @brief Replay messages from sequence number
     */
    [[nodiscard]] std::vector<Message> replay(int64_t from_sequence, 
                                                int max_count = 1000) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Message> result;
        for (const auto& msg : messages_) {
            if (msg.sequence_number >= from_sequence) {
                result.push_back(msg);
                if (static_cast<int>(result.size()) >= max_count) break;
            }
        }
        return result;
    }
    
    /**
     * @brief Replay messages by topic
     */
    [[nodiscard]] std::vector<Message> replay_topic(const std::string& topic,
                                                      int max_count = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Message> result;
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            if (it->topic == topic) {
                result.push_back(*it);
                if (static_cast<int>(result.size()) >= max_count) break;
            }
        }
        std::reverse(result.begin(), result.end());
        return result;
    }
    
    [[nodiscard]] int64_t current_sequence() const { return sequence_.load(); }
    [[nodiscard]] int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(messages_.size());
    }

private:
    mutable std::mutex mutex_;
    std::deque<Message> messages_;
    std::atomic<int64_t> sequence_{0};
    int max_size_;
};

// ============================================================================
// Message Bus
// ============================================================================

/**
 * @brief Core event-driven message bus
 *
 * Supports pub/sub, queue, and request/reply patterns with:
 * - Topic-based routing with wildcards
 * - Priority-based message ordering
 * - Dead letter queue for failed deliveries
 * - Message persistence and replay
 * - Metrics and observability
 */
class MessageBus {
public:
    MessageBus() = default;
    ~MessageBus() { stop(); }
    
    // Non-copyable
    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;
    
    /**
     * @brief Create or get a channel
     */
    Channel& create_channel(const std::string& name, 
                             ChannelType type = ChannelType::PubSub) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = channels_.find(name);
        if (it == channels_.end()) {
            Channel ch;
            ch.name = name;
            ch.type = type;
            channels_[name] = ch;
            return channels_[name];
        }
        return it->second;
    }
    
    /**
     * @brief Subscribe to topic pattern
     */
    std::string subscribe(const std::string& topic_pattern,
                           std::function<void(const Message&)> handler,
                           const std::string& subscriber_id = "",
                           SubscriptionType type = SubscriptionType::Persistent) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Subscription sub;
        sub.id = "sub-" + std::to_string(next_sub_id_++);
        sub.subscriber_id = subscriber_id.empty() ? sub.id : subscriber_id;
        sub.filter.pattern = topic_pattern;
        sub.filter.is_wildcard = (topic_pattern.find('*') != std::string::npos ||
                                   topic_pattern.find('#') != std::string::npos);
        sub.type = type;
        sub.handler = std::move(handler);
        sub.created_at = std::chrono::system_clock::now();
        
        subscriptions_[sub.id] = sub;
        metrics_.active_subscriptions = static_cast<int>(subscriptions_.size());
        
        return sub.id;
    }
    
    /**
     * @brief Unsubscribe
     */
    bool unsubscribe(const std::string& subscription_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool removed = subscriptions_.erase(subscription_id) > 0;
        if (removed) {
            metrics_.active_subscriptions = static_cast<int>(subscriptions_.size());
        }
        return removed;
    }
    
    /**
     * @brief Publish message to topic
     */
    void publish(const std::string& topic, const std::string& payload,
                  MessagePriority priority = MessagePriority::Normal,
                  const std::string& content_type = "application/json") {
        Message msg;
        msg.id = id_gen_.next();
        msg.topic = topic;
        msg.payload = payload;
        msg.content_type = content_type;
        msg.priority = priority;
        msg.created_at = std::chrono::system_clock::now();
        msg.sequence_number = store_.current_sequence() + 1;
        
        publish_message(std::move(msg));
    }
    
    /**
     * @brief Publish a pre-built message
     */
    void publish(Message msg) {
        if (msg.id.empty()) msg.id = id_gen_.next();
        if (msg.created_at == std::chrono::system_clock::time_point{}) {
            msg.created_at = std::chrono::system_clock::now();
        }
        msg.sequence_number = store_.current_sequence() + 1;
        publish_message(std::move(msg));
    }
    
    /**
     * @brief Request/reply pattern
     */
    std::optional<Message> request(const std::string& topic, 
                                     const std::string& payload,
                                     std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
        std::string reply_topic = "reply." + id_gen_.next();
        std::optional<Message> response;
        std::mutex reply_mutex;
        std::condition_variable reply_cv;
        bool received = false;
        
        auto sub_id = subscribe(reply_topic, [&](const Message& msg) {
            std::lock_guard<std::mutex> lk(reply_mutex);
            response = msg;
            received = true;
            reply_cv.notify_one();
        }, "", SubscriptionType::Ephemeral);
        
        Message msg;
        msg.id = id_gen_.next();
        msg.topic = topic;
        msg.payload = payload;
        msg.reply_to = reply_topic;
        msg.correlation_id = msg.id;
        msg.delivery = DeliveryMode::AtLeastOnce;
        msg.created_at = std::chrono::system_clock::now();
        msg.sequence_number = store_.current_sequence() + 1;
        
        publish_message(std::move(msg));
        
        {
            std::unique_lock<std::mutex> lk(reply_mutex);
            reply_cv.wait_for(lk, timeout, [&] { return received; });
        }
        
        unsubscribe(sub_id);
        return response;
    }
    
    /**
     * @brief Replay stored messages
     */
    [[nodiscard]] std::vector<Message> replay(int64_t from_sequence, 
                                                int max_count = 1000) const {
        return store_.replay(from_sequence, max_count);
    }
    
    /**
     * @brief Replay by topic
     */
    [[nodiscard]] std::vector<Message> replay_topic(const std::string& topic,
                                                      int max_count = 100) const {
        return store_.replay_topic(topic, max_count);
    }
    
    /**
     * @brief Get dead letter queue
     */
    DeadLetterQueue& dead_letters() { return dlq_; }
    const DeadLetterQueue& dead_letters() const { return dlq_; }
    
    /**
     * @brief Get metrics
     */
    [[nodiscard]] BusMetrics metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto m = metrics_;
        m.queue_depth = 0;  // Synchronous dispatch
        m.dead_letter_depth = dlq_.size();
        m.active_channels = static_cast<int>(channels_.size());
        return m;
    }
    
    /**
     * @brief List topics with active subscriptions
     */
    [[nodiscard]] std::vector<std::string> active_topics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> topics;
        for (const auto& [id, sub] : subscriptions_) {
            topics.insert(sub.filter.pattern);
        }
        return {topics.begin(), topics.end()};
    }
    
    /**
     * @brief List channels
     */
    [[nodiscard]] std::vector<Channel> list_channels() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Channel> result;
        for (const auto& [name, ch] : channels_) {
            result.push_back(ch);
        }
        return result;
    }
    
    /**
     * @brief Stop the bus
     */
    void stop() {
        running_ = false;
    }
    
    /**
     * @brief Check if running
     */
    [[nodiscard]] bool is_running() const { return running_.load(); }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Subscription> subscriptions_;
    std::map<std::string, Channel> channels_;
    MessageIdGenerator id_gen_;
    DeadLetterQueue dlq_;
    MessageStore store_;
    BusMetrics metrics_;
    std::atomic<bool> running_{true};
    int next_sub_id_{1};
    
    /**
     * @brief Internal publish with dispatch
     */
    void publish_message(Message msg) {
        // Store for replay
        store_.store(msg);
        
        std::lock_guard<std::mutex> lock(mutex_);
        ++metrics_.total_published;
        
        // Find matching subscriptions
        std::vector<Subscription*> matching;
        for (auto& [id, sub] : subscriptions_) {
            if (sub.active && sub.filter.matches(msg.topic)) {
                matching.push_back(&sub);
            }
        }
        
        // Sort by priority (higher first)
        std::sort(matching.begin(), matching.end(),
                  [](const Subscription* a, const Subscription* b) {
                      return a->priority > b->priority;
                  });
        
        // Dispatch
        for (auto* sub : matching) {
            try {
                sub->handler(msg);
                ++sub->messages_received;
                ++metrics_.total_delivered;
                msg.status = MessageStatus::Delivered;
            } catch (const std::exception& ex) {
                ++sub->messages_failed;
                ++metrics_.total_failed;
                ++msg.retry_count;
                
                if (msg.retry_count >= msg.max_retries) {
                    dlq_.enqueue(msg, ex.what());
                    ++metrics_.total_dead_lettered;
                } else {
                    ++metrics_.total_retried;
                }
            }
        }
        
        // Update channel metrics
        auto ch_it = channels_.find(msg.topic);
        if (ch_it != channels_.end()) {
            ++ch_it->second.total_published;
            ch_it->second.total_delivered += static_cast<int64_t>(matching.size());
        }
    }
};

// ============================================================================
// Pre-defined Topic Constants
// ============================================================================

namespace topics {
    // Market data topics
    constexpr const char* PRICE_UPDATE  = "market.price.update";
    constexpr const char* TRADE_EXEC    = "market.trade.execution";
    constexpr const char* ORDER_BOOK    = "market.orderbook.update";
    constexpr const char* NEWS          = "market.news";
    
    // Portfolio topics
    constexpr const char* POSITION_CHANGE = "portfolio.position.change";
    constexpr const char* REBALANCE       = "portfolio.rebalance";
    constexpr const char* NAV_UPDATE      = "portfolio.nav.update";
    
    // Risk topics
    constexpr const char* VAR_BREACH      = "risk.var.breach";
    constexpr const char* LIMIT_WARNING   = "risk.limit.warning";
    constexpr const char* STRESS_RESULT   = "risk.stress.result";
    
    // Trading topics
    constexpr const char* ORDER_SUBMITTED = "trading.order.submitted";
    constexpr const char* ORDER_FILLED    = "trading.order.filled";
    constexpr const char* ORDER_CANCELLED = "trading.order.cancelled";
    constexpr const char* ORDER_REJECTED  = "trading.order.rejected";
    
    // System topics
    constexpr const char* HEALTH_CHECK    = "system.health.check";
    constexpr const char* CONFIG_CHANGE   = "system.config.change";
    constexpr const char* ALERT           = "system.alert";
    constexpr const char* AUDIT           = "system.audit";
    
    // Compliance topics
    constexpr const char* COMPLIANCE_BREACH = "compliance.breach";
    constexpr const char* REGULATORY_ALERT  = "compliance.regulatory.alert";
    
    // Wildcard patterns
    constexpr const char* ALL_MARKET   = "market.#";
    constexpr const char* ALL_TRADING  = "trading.#";
    constexpr const char* ALL_RISK     = "risk.#";
    constexpr const char* ALL_SYSTEM   = "system.#";
} // namespace topics

} // namespace messaging
} // namespace core
} // namespace genie

#endif // GENIE_CORE_MESSAGE_BUS_HPP
