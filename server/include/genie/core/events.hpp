/**
 * @file events.hpp
 * @brief Event system for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CORE_EVENTS_HPP
#define GENIE_CORE_EVENTS_HPP
#include "types.hpp"

namespace genie {
struct EventData {
    EventType type{EventType::Custom};
    TimePoint timestamp{std::chrono::system_clock::now()};
    std::string source, description;
    std::map<std::string, std::any> payload;
    EventData() = default;
    explicit EventData(EventType t) : type(t) {}
    template<typename T> void set(const std::string& k, T v) { payload[k] = std::move(v); }
    template<typename T> [[nodiscard]] T get(const std::string& k, T def = T{}) const {
        auto it = payload.find(k); if (it == payload.end()) return def;
        try { return std::any_cast<T>(it->second); } catch (...) { return def; }
    }
};

using EventHandler = std::function<void(const EventData&)>;
using SubscriptionId = size_t;

class EventBus {
    std::map<EventType, std::vector<std::pair<SubscriptionId, EventHandler>>> handlers_;
    SubscriptionId next_id_{0};
    mutable std::mutex mutex_;
public:
    SubscriptionId subscribe(EventType t, EventHandler h) {
        std::lock_guard<std::mutex> lk(mutex_);
        SubscriptionId id = next_id_++; handlers_[t].emplace_back(id, std::move(h)); return id;
    }
    void unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [t, hs] : handlers_) hs.erase(std::remove_if(hs.begin(), hs.end(), [id](auto& p) { return p.first == id; }), hs.end());
    }
    void publish(const EventData& e) {
        std::vector<EventHandler> to_call;
        { std::lock_guard<std::mutex> lk(mutex_); auto it = handlers_.find(e.type); if (it != handlers_.end()) for (auto& [id, h] : it->second) to_call.push_back(h); }
        for (auto& h : to_call) h(e);
    }
    void clear() { std::lock_guard<std::mutex> lk(mutex_); handlers_.clear(); }
};

inline EventBus& events() { static EventBus inst; return inst; }
} // namespace genie
#endif
