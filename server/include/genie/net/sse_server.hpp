/**
 * @file sse_server.hpp
 * @brief Server-Sent Events (SSE) for real-time data push without WebSocket
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Two delivery modes:
 *
 * TRUE STREAMING (primary): Register make_sse_streaming_handler() with
 * HttpServer::set_streaming_handler(). Clients open /api/v1/stream/<channel>
 * and receive events pushed directly over the persistent TCP connection.
 *
 * POLL BRIDGE (fallback): /api/v1/stream/poll returns queued events since
 * last_event_id. Fully EventSource-compatible for debugging or proxied setups.
 *
 * Wire up in main.cpp:
 *   server.set_streaming_handler(genie::net::make_sse_streaming_handler());
 *
 * Zero external dependencies. Cross-platform: Windows / Linux / macOS.
 */
#pragma once
#ifndef GENIE_NET_SSE_SERVER_HPP
#define GENIE_NET_SSE_SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <sstream>
#include <chrono>
#include <thread>
#include <memory>
#include <algorithm>
#include <optional>
#include <cstring>

// Portable socket type (mirrors http_server.hpp platform detection)
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using sse_socket_t = SOCKET;
#else
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <unistd.h>
   using sse_socket_t = int;
#endif

namespace genie::net {

// ============================================================================
// SSE Event
// ============================================================================

struct SseEvent {
    std::string event;   // event: <n>  (omitted if empty -> "message")
    std::string data;    // data: <payload>  (required)
    std::string id;      // id: <id>         (optional)
    int retry_ms = 0;    // retry: <ms>      (0 = omit)

    /** Serialize to SSE wire format */
    [[nodiscard]] std::string serialize() const {
        std::ostringstream oss;
        if (!id.empty())    oss << "id: "    << id    << "\n";
        if (!event.empty()) oss << "event: " << event << "\n";
        if (retry_ms > 0)   oss << "retry: " << retry_ms << "\n";
        std::istringstream dss(data);
        std::string line;
        while (std::getline(dss, line)) oss << "data: " << line << "\n";
        oss << "\n";
        return oss.str();
    }

    static SseEvent message(const std::string& data, const std::string& id = "") {
        return {"", data, id, 0};
    }
    static SseEvent named(const std::string& event, const std::string& data,
                          const std::string& id = "") {
        return {event, data, id, 0};
    }
};

// ============================================================================
// SSE Response helper
// ============================================================================

/** Set up a Response as an SSE stream (used with the poll bridge). */
inline void make_sse_response(Response& res, int retry_ms = 3000) {
    res.status = 200;
    res.headers["Content-Type"]      = "text/event-stream; charset=utf-8";
    res.headers["Cache-Control"]     = "no-cache, no-store";
    res.headers["Connection"]        = "keep-alive";
    res.headers["X-Accel-Buffering"] = "no";
    res.body = "retry: " + std::to_string(retry_ms) + "\n\n";
    res.chunked = true;
}

// ============================================================================
// SSE Channel -- manages subscriptions and event queues
// ============================================================================

class SseChannel {
public:
    static SseChannel& instance() {
        static SseChannel inst;
        return inst;
    }

    void configure(bool enabled, int keep_alive_seconds, int max_clients) {
        enabled_        = enabled;
        keep_alive_sec_ = keep_alive_seconds;
        max_clients_    = max_clients;
    }

    bool is_enabled() const { return enabled_; }

    /** Publish an event to the poll queue AND push to all live streaming sockets. */
    void broadcast(const std::string& channel, const SseEvent& event) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lk(mtx_);
        uint64_t id = next_id_++;
        auto& q = queues_[channel];
        q.push_back({id, event});
        if (q.size() > 1000) q.erase(q.begin(), q.begin() + static_cast<long>(q.size() - 1000));
        total_events_.fetch_add(1, std::memory_order_relaxed);
        push_to_live_clients_locked(channel, event, id);
    }

    /** Poll pending events since last_id (poll bridge). */
    [[nodiscard]] std::vector<SseEvent> poll(const std::string& channel,
                                              uint64_t last_id = 0) const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<SseEvent> out;
        auto it = queues_.find(channel);
        if (it == queues_.end()) return out;
        for (const auto& [id, ev] : it->second) {
            if (id > last_id) {
                auto copy = ev;
                copy.id = std::to_string(id);
                out.push_back(std::move(copy));
            }
        }
        return out;
    }

    /** Register a live streaming socket for a channel. Returns client id. */
    uint64_t add_client(const std::string& channel, sse_socket_t sock) {
        std::lock_guard<std::mutex> lk(mtx_);
        uint64_t id = next_client_id_++;
        clients_[channel].push_back({id, sock});
        active_clients_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    /** Remove a live streaming socket. */
    void remove_client(const std::string& channel, uint64_t client_id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = clients_.find(channel);
        if (it == clients_.end()) return;
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(),
            [client_id](const SseClient& c){ return c.id == client_id; }), v.end());
        if (v.empty()) clients_.erase(it);
        if (active_clients_.load() > 0) active_clients_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] std::vector<std::string> channels() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::string> names;
        for (const auto& [k, _] : queues_) names.push_back(k);
        return names;
    }

    struct Stats {
        uint64_t total_events  = 0;
        int      channel_count = 0;
        int      max_clients   = 0;
        int      active_clients = 0;
    };
    [[nodiscard]] Stats stats() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return {total_events_.load(), static_cast<int>(queues_.size()),
                max_clients_, active_clients_.load()};
    }

    void clear(const std::string& channel) {
        std::lock_guard<std::mutex> lk(mtx_);
        queues_.erase(channel);
    }

private:
    struct SseClient { uint64_t id; sse_socket_t sock; };

    void push_to_live_clients_locked(const std::string& channel,
                                      const SseEvent& event, uint64_t event_id) {
        auto it = clients_.find(channel);
        if (it == clients_.end()) return;
        auto wire = event;
        wire.id = std::to_string(event_id);
        std::string payload = wire.serialize();
        std::vector<uint64_t> dead;
        for (auto& c : it->second) {
            if (::send(c.sock, payload.data(), (int)payload.size(), 0)
                    != static_cast<int>(payload.size())) {
                dead.push_back(c.id);
            }
        }
        for (auto id : dead) {
            auto& v = it->second;
            v.erase(std::remove_if(v.begin(), v.end(),
                [id](const SseClient& c){ return c.id == id; }), v.end());
            if (active_clients_.load() > 0) active_clients_.fetch_sub(1);
        }
    }

    SseChannel() = default;
    SseChannel(const SseChannel&) = delete;
    SseChannel& operator=(const SseChannel&) = delete;

    bool enabled_{true};
    int  keep_alive_sec_{15};
    int  max_clients_{200};

    mutable std::mutex mtx_;
    std::map<std::string, std::vector<std::pair<uint64_t, SseEvent>>> queues_;
    std::map<std::string, std::vector<SseClient>> clients_;
    uint64_t next_id_{1};
    uint64_t next_client_id_{1};
    std::atomic<uint64_t> total_events_{0};
    std::atomic<int> active_clients_{0};
};

// ============================================================================
// True streaming handler -- register with HttpServer::set_streaming_handler()
// ============================================================================

/** Callable type matching HttpServer::StreamingHandler */
using SseStreamingHandler = std::function<bool(
    const std::map<std::string,std::string>&,
    const std::string&,
    sse_socket_t)>;

/**
 * @brief Factory for the SSE streaming handler.
 *
 * Returns a callable for HttpServer::set_streaming_handler(). Intercepts
 * GET /api/v1/stream/<channel>, sends SSE headers directly on the socket,
 * registers the client with SseChannel, and blocks in a keep-alive loop.
 * Events broadcast() ed from any thread are delivered immediately.
 */
inline SseStreamingHandler make_sse_streaming_handler(int keep_alive_sec = 15) {
    return [keep_alive_sec](
        const std::map<std::string,std::string>& /*headers*/,
        const std::string& path,
        sse_socket_t sock) -> bool
    {
        const std::string prefix = "/api/v1/stream/";
        if (path.rfind(prefix, 0) != 0) return false;
        std::string channel = path.substr(prefix.size());
        if (channel.empty() || channel == "poll") return false;
        if (!SseChannel::instance().is_enabled()) return false;

        // Send SSE response headers
        std::string hdrs =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream; charset=utf-8\r\n"
            "Cache-Control: no-cache, no-store\r\n"
            "Connection: keep-alive\r\n"
            "X-Accel-Buffering: no\r\n"
            "\r\n"
            "retry: 3000\n\n";
        if (::send(sock, hdrs.data(), (int)hdrs.size(), 0)
                != static_cast<int>(hdrs.size())) {
            return true;
        }

        auto client_id = SseChannel::instance().add_client(channel, sock);

        // Keep-alive loop: select() with timeout, send comment on timeout
        while (true) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv{keep_alive_sec, 0};
            int sel = ::select((int)sock + 1, &rfds, nullptr, nullptr, &tv);
            if (sel < 0) break;
            if (sel > 0) break;
            const char* ping = ": keep-alive\n\n";
            if (::send(sock, ping, (int)std::strlen(ping), 0) <= 0) break;
        }

        SseChannel::instance().remove_client(channel, client_id);
        return true;
    };
}

} // namespace genie::net

#endif // GENIE_NET_SSE_SERVER_HPP
