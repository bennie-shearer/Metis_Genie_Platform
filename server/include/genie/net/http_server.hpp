/**
 * @file http_server.hpp
 * @brief Production-grade HTTP/1.1 server for Metis Genie Platform
 * @version 5.5.4
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Self-contained cross-platform TCP/HTTP server that bridges browser requests
 * to the RestApi route handler. Zero external dependencies -- uses only
 * standard OS socket APIs (Winsock2 on Windows, POSIX on Linux/macOS).
 *
 * Features:
 *   - Cross-platform: Windows (Winsock2), Linux (POSIX), macOS (BSD sockets)
 *   - HTTP/1.1 with keep-alive and pipelining support
 *   - Static file serving with MIME type detection (40+ types)
 *   - Chunked transfer encoding for streaming responses
 *   - Request body parsing: Content-Length and chunked
 *   - CORS headers for browser cross-origin requests
 *   - Connection timeout management (idle + request)
 *   - Thread pool with configurable worker count
 *   - Request queuing with backpressure (max pending)
 *   - Graceful shutdown with drain timeout
 *   - Access logging (Common Log Format)
 *   - Request size limits (headers + body)
 *   - ETag / If-None-Match for static file caching
 *   - HEAD method support
 *   - 100-Continue handling
 *   - X-Request-Id generation for tracing
 *   - Health check endpoint bypass (no auth)
 *   - Configurable bind address and port
 *   - SO_REUSEADDR for fast restart
 *   - No external dependencies (no httplib, no Boost, no OpenSSL)
 *
 * Architecture:
 *   HttpServer owns:
 *     - 1 acceptor thread (accept loop)
 *     - N worker threads (request processing)
 *     - Connection queue with bounded capacity
 *
 * Usage:
 *   net::RestApi api;
 *   api.configure_defaults();
 *   net::HttpServer server(api, 8080);
 *   server.set_static_dir("./web");
 *   server.set_worker_count(4);
 *   server.start();  // blocks until stop() or signal
 *
 * Build:
 *   g++ -std=c++20 -O2 -I include -o metis-genie-platform src/main.cpp -pthread -lsqlite3
 *   Windows (MinGW): add -lws2_32
 *   MSVC:            add ws2_32.lib
 */
#pragma once
#ifndef GENIE_NET_HTTP_SERVER_HPP
#define GENIE_NET_HTTP_SERVER_HPP

// Platform socket includes
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  using socket_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define SOCK_ERROR SOCKET_ERROR
  inline int sock_close(socket_t s) { return closesocket(s); }
  inline int sock_errno() { return WSAGetLastError(); }
  inline void sock_init() {
      WSADATA wsa;
      WSAStartup(MAKEWORD(2, 2), &wsa);
  }
  inline void sock_cleanup() { WSACleanup(); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <cerrno>
  #include <csignal>
  using socket_t = int;
  #define INVALID_SOCK (-1)
  #define SOCK_ERROR (-1)
  inline int sock_close(socket_t s) { return close(s); }
  inline int sock_errno() { return errno; }
  inline void sock_init() {}
  inline void sock_cleanup() {}
#endif

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <stdexcept>

namespace genie {
namespace net {

// ============================================================================
// HTTP Types
// ============================================================================

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query_string;
    std::string http_version;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string remote_addr;
    int remote_port = 0;
    std::string request_id;
    std::chrono::steady_clock::time_point received_at;

    std::string header(const std::string& name, const std::string& def = "") const {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        for (const auto& [k, v] : headers) {
            std::string lower_k = k;
            std::transform(lower_k.begin(), lower_k.end(), lower_k.begin(), ::tolower);
            if (lower_k == lower_name) return v;
        }
        return def;
    }

    bool keep_alive() const {
        auto conn = header("connection");
        std::string lc = conn;
        std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
        if (http_version == "HTTP/1.0") return lc == "keep-alive";
        return lc != "close";
    }

    int64_t content_length() const {
        auto cl = header("content-length");
        if (cl.empty()) return -1;
        try { return std::stoll(cl); } catch (...) { return -1; }
    }

    bool expects_continue() const {
        auto e = header("expect");
        std::string le = e;
        std::transform(le.begin(), le.end(), le.begin(), ::tolower);
        return le == "100-continue";
    }
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    bool chunked = false;

    void set_header(const std::string& n, const std::string& v) { headers[n] = v; }

    void set_json(const std::string& j, int c = 200) {
        status_code = c; status_text = status_text_for(c);
        body = j; headers["Content-Type"] = "application/json; charset=utf-8";
    }

    void set_status(int c) { status_code = c; status_text = status_text_for(c); }

    static std::string status_text_for(int c) {
        switch (c) {
            case 100: return "Continue";       case 200: return "OK";
            case 201: return "Created";        case 204: return "No Content";
            case 301: return "Moved Permanently"; case 302: return "Found";
            case 304: return "Not Modified";   case 400: return "Bad Request";
            case 401: return "Unauthorized";   case 403: return "Forbidden";
            case 404: return "Not Found";      case 405: return "Method Not Allowed";
            case 408: return "Request Timeout"; case 413: return "Payload Too Large";
            case 429: return "Too Many Requests"; case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";    case 503: return "Service Unavailable";
            default:  return "Unknown";
        }
    }

    std::string serialize(bool include_body = true) const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        auto hdrs = headers;
        if (!chunked && include_body) hdrs["Content-Length"] = std::to_string(body.size());
        if (chunked) hdrs["Transfer-Encoding"] = "chunked";
        for (const auto& [n, v] : hdrs) oss << n << ": " << v << "\r\n";
        oss << "\r\n";
        if (include_body && !body.empty()) oss << body;
        return oss.str();
    }
};

// ============================================================================
// MIME Type Detection (40+ types)
// ============================================================================

inline std::string mime_type(const std::string& path) {
    auto p = path.rfind('.');
    if (p == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(p);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".xml") return "application/xml; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".csv") return "text/csv; charset=utf-8";
    if (ext == ".md") return "text/markdown; charset=utf-8";
    if (ext == ".yaml" || ext == ".yml") return "text/yaml; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".webp") return "image/webp";
    if (ext == ".avif") return "image/avif";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".otf") return "font/otf";
    if (ext == ".eot") return "application/vnd.ms-fontobject";
    if (ext == ".zip") return "application/zip";
    if (ext == ".gz" || ext == ".gzip") return "application/gzip";
    if (ext == ".tar") return "application/x-tar";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".map") return "application/json";
    return "application/octet-stream";
}

// ============================================================================
// Request ID Generator
// ============================================================================

inline std::string generate_request_id() {
    static std::atomic<uint64_t> counter{0};
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t seq = counter.fetch_add(1);
    std::ostringstream oss;
    oss << std::hex << ms << "-" << std::setw(6) << std::setfill('0') << (seq % 1000000);
    return oss.str();
}

// ============================================================================
// Access Logger (Common Log Format)
// ============================================================================

class AccessLogger {
public:
    void log(const HttpRequest& req, const HttpResponse& resp, double elapsed_ms) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto lt = std::localtime(&t);
        std::ostringstream oss;
        oss << req.remote_addr << " - - ["
            << std::put_time(lt, "%d/%b/%Y:%H:%M:%S %z")
            << "] \"" << req.method << " " << req.path;
        if (!req.query_string.empty()) oss << "?" << req.query_string;
        oss << " " << req.http_version << "\" " << resp.status_code
            << " " << resp.body.size() << " "
            << std::fixed << std::setprecision(1) << elapsed_ms << "ms"
            << " [" << req.request_id << "]";
        lines_.push_back(oss.str());
        if (lines_.size() > 10000)
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<long>(lines_.size() - 10000));
    }

    std::vector<std::string> recent(int n = 100) const {
        std::lock_guard<std::mutex> lock(mtx_);
        int s = std::max(0, static_cast<int>(lines_.size()) - n);
        return {lines_.begin() + s, lines_.end()};
    }

    int64_t total() const { std::lock_guard<std::mutex> lock(mtx_); return (int64_t)lines_.size(); }

private:
    mutable std::mutex mtx_;
    std::vector<std::string> lines_;
};

// ============================================================================
// Connection Queue (bounded, thread-safe)
// ============================================================================

struct PendingConnection {
    socket_t socket = INVALID_SOCK;
    std::string remote_addr;
    int remote_port = 0;
    std::chrono::steady_clock::time_point accepted_at;
};

class ConnectionQueue {
public:
    explicit ConnectionQueue(size_t max = 1024) : max_(max) {}

    bool push(PendingConnection c) {
        std::unique_lock<std::mutex> lk(m_);
        if (q_.size() >= max_) return false;
        q_.push(std::move(c));
        cv_.notify_one();
        return true;
    }

    std::optional<PendingConnection> pop(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(m_);
        if (!cv_.wait_for(lk, timeout, [this]{ return !q_.empty() || closed_; }))
            return std::nullopt;
        if (q_.empty()) return std::nullopt;
        auto c = std::move(q_.front()); q_.pop();
        return c;
    }

    void close() { std::lock_guard<std::mutex> lk(m_); closed_ = true; cv_.notify_all(); }
    size_t size() const { std::lock_guard<std::mutex> lk(m_); return q_.size(); }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::queue<PendingConnection> q_;
    size_t max_;
    bool closed_ = false;
};

// ============================================================================
// HTTP Server
// ============================================================================

using SimpleRouteHandler = std::function<std::pair<int, std::string>(
    const std::string&, const std::string&,
    const std::map<std::string, std::string>&, const std::string&)>;

class HttpServer {
public:
    HttpServer(SimpleRouteHandler handler, int port = 8080, const std::string& bind = "0.0.0.0")
        : handler_(std::move(handler)), port_(port), bind_addr_(bind),
          conn_queue_(max_pending_) {}

    template<typename RestApiT>
    HttpServer(RestApiT& api, int port = 8080, const std::string& bind = "0.0.0.0")
        : port_(port), bind_addr_(bind), conn_queue_(max_pending_) {
        handler_ = [&api](const std::string& m, const std::string& p,
                          const std::map<std::string,std::string>& h, const std::string& b)
            -> std::pair<int, std::string> {
            auto resp = api.handle(m, p, b, h);
            return {resp.status, resp.body};
        };
    }

    ~HttpServer() { stop(); }
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Configuration
    void set_port(int p) { port_ = p; }
    void set_bind_addr(const std::string& a) { bind_addr_ = a; }
    void set_worker_count(int n) { worker_count_ = std::max(1, n); }
    void set_static_dir(const std::string& d) { static_dir_ = d; }
    void set_cors_origin(const std::string& o) { cors_origin_ = o; }
    void set_max_body_size(size_t b) { max_body_ = b; }
    void set_max_header_size(size_t h) { max_header_ = h; }
    void set_idle_timeout_ms(int ms) { idle_timeout_ = ms; }
    void set_request_timeout_ms(int ms) { req_timeout_ = ms; }
    void set_access_logging(bool e) { access_log_on_ = e; }

    /** StreamingHandler: called before normal request/response cycle.
     *  Receives (headers, path, socket). Returns true to take over the connection. */
    using StreamingHandler = std::function<bool(
        const std::map<std::string,std::string>&,
        const std::string&,
        socket_t)>;

    /** Register a handler for streaming responses (SSE, chunked).
     *  Called before the normal request/response cycle.
     *  If it returns true the connection is taken over (no further processing). */
    void set_streaming_handler(StreamingHandler h) { streaming_handler_ = std::move(h); }

    int port() const { return port_; }
    std::string bind_addr() const { return bind_addr_; }
    int worker_count() const { return worker_count_; }

    // Lifecycle
    bool start() {
        sock_init();
        listen_sock_ = create_listen_socket();
        if (listen_sock_ == INVALID_SOCK) return false;
        running_.store(true);
        start_time_ = steady_ms();
        for (int i = 0; i < worker_count_; ++i)
            workers_.emplace_back([this]() { worker_loop(); });
        accept_loop();
        for (auto& w : workers_) if (w.joinable()) w.join();
        workers_.clear();
        if (listen_sock_ != INVALID_SOCK) { sock_close(listen_sock_); listen_sock_ = INVALID_SOCK; }
        sock_cleanup();
        return true;
    }

    void start_async() { server_thread_ = std::thread([this]() { start(); }); }

    void stop() {
        if (!running_.exchange(false)) return;
        if (listen_sock_ != INVALID_SOCK) { sock_close(listen_sock_); listen_sock_ = INVALID_SOCK; }
        conn_queue_.close();
        if (server_thread_.joinable()) server_thread_.join();
    }

    void request_stop() {
        running_.store(false);
        auto s = listen_sock_;
        if (s != INVALID_SOCK) { sock_close(s); listen_sock_ = INVALID_SOCK; }
    }

    bool is_running() const { return running_.load(); }

    // Stats
    struct ServerStats {
        int64_t total_requests = 0, active_connections = 0, total_connections = 0;
        int64_t bytes_sent = 0, bytes_received = 0;
        int64_t errors_4xx = 0, errors_5xx = 0;
        int64_t static_served = 0, api_served = 0, uptime_ms = 0;
        int pending = 0;
    };

    ServerStats stats() const {
        ServerStats s;
        s.total_requests = total_req_.load(); s.active_connections = active_conn_.load();
        s.total_connections = total_conn_.load();
        s.bytes_sent = bytes_out_.load(); s.bytes_received = bytes_in_.load();
        s.errors_4xx = err_4xx_.load(); s.errors_5xx = err_5xx_.load();
        s.static_served = stat_served_.load(); s.api_served = api_served_.load();
        s.pending = (int)conn_queue_.size();
        if (start_time_ > 0) s.uptime_ms = steady_ms() - start_time_;
        return s;
    }

    AccessLogger& access_logger() { return alog_; }
    const AccessLogger& access_logger() const { return alog_; }

private:
    socket_t create_listen_socket() {
        socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCK) return INVALID_SOCK;
        int opt = 1;
#ifdef _WIN32
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port_);
        if (bind_addr_ == "0.0.0.0" || bind_addr_.empty())
            addr.sin_addr.s_addr = INADDR_ANY;
        else
            inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr);
        if (::bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCK_ERROR) { sock_close(s); return INVALID_SOCK; }
        if (::listen(s, SOMAXCONN) == SOCK_ERROR) { sock_close(s); return INVALID_SOCK; }
        return s;
    }

    void accept_loop() {
        while (running_.load()) {
            struct sockaddr_in ca{}; socklen_t cl = sizeof(ca);
            socket_t c = ::accept(listen_sock_, (struct sockaddr*)&ca, &cl);
            if (c == INVALID_SOCK) { if (!running_.load()) break; continue; }
            total_conn_.fetch_add(1);
            int nd = 1;
#ifdef _WIN32
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
#else
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
#endif
            char ab[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &ca.sin_addr, ab, sizeof(ab));
            PendingConnection pc;
            pc.socket = c; pc.remote_addr = ab; pc.remote_port = ntohs(ca.sin_port);
            pc.accepted_at = std::chrono::steady_clock::now();
            if (!conn_queue_.push(std::move(pc))) {
                send_error(c, 503, "Service Unavailable"); sock_close(c);
            }
        }
    }

    void worker_loop() {
        while (running_.load()) {
            auto mc = conn_queue_.pop(std::chrono::milliseconds(100));
            if (!mc) continue;
            active_conn_.fetch_add(1);
            handle_connection(mc.value());
            active_conn_.fetch_sub(1);
        }
    }

    void handle_connection(PendingConnection& conn) {
        bool ka = true; int reqs = 0;
        while (ka && running_.load()) {
            auto mreq = read_request(conn.socket, conn.remote_addr, conn.remote_port);
            if (!mreq) break;
            auto& req = mreq.value();
            reqs++; total_req_.fetch_add(1);
            bytes_in_.fetch_add(req.body.size());
            auto t0 = std::chrono::steady_clock::now();
            if (req.expects_continue()) send_all(conn.socket, "HTTP/1.1 100 Continue\r\n\r\n");

            // SSE / streaming: if the handler takes over the socket, skip
            // the normal request/response cycle and end keep-alive for this conn.
            if (streaming_handler_) {
                auto hdrs = req.headers;
                hdrs["X-Request-Id"] = req.request_id;
                hdrs["X-Real-IP"]    = req.remote_addr;
                if (streaming_handler_(hdrs, req.path, conn.socket)) {
                    ka = false; // SSE holds the connection; worker exits after
                    break;
                }
            }

            HttpResponse resp;
            process_request(req, resp);
            add_standard_headers(req, resp);
            ka = req.keep_alive() && reqs < 100;
            resp.set_header("Connection", ka ? "keep-alive" : "close");
            if (ka) resp.set_header("Keep-Alive", "timeout=30, max=100");

            bool head = (req.method == "HEAD");
            std::string raw = resp.serialize(!head);
            send_all(conn.socket, raw);
            bytes_out_.fetch_add(raw.size());
            if (resp.status_code >= 400 && resp.status_code < 500) err_4xx_.fetch_add(1);
            if (resp.status_code >= 500) err_5xx_.fetch_add(1);
            if (access_log_on_) {
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                alog_.log(req, resp, us / 1000.0);
            }
        }
        sock_close(conn.socket);
    }

    std::optional<HttpRequest> read_request(socket_t sock, const std::string& ra, int rp) {
        HttpRequest req;
        req.remote_addr = ra; req.remote_port = rp;
        req.request_id = generate_request_id();
        req.received_at = std::chrono::steady_clock::now();

        std::string hbuf; hbuf.reserve(4096);
        char b[1];
        while (hbuf.size() < max_header_) {
            fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
            struct timeval tv; tv.tv_sec = req_timeout_ / 1000; tv.tv_usec = (req_timeout_ % 1000) * 1000;
            if (::select((int)sock + 1, &fds, nullptr, nullptr, &tv) <= 0) return std::nullopt;
            if (::recv(sock, b, 1, 0) <= 0) return std::nullopt;
            hbuf += b[0];
            if (hbuf.size() >= 4 && hbuf.substr(hbuf.size()-4) == "\r\n\r\n") break;
        }
        if (hbuf.size() >= max_header_) return std::nullopt;

        auto fle = hbuf.find("\r\n");
        if (fle == std::string::npos) return std::nullopt;
        std::string rl = hbuf.substr(0, fle);
        auto s1 = rl.find(' '), s2 = rl.find(' ', s1+1);
        if (s1 == std::string::npos || s2 == std::string::npos) return std::nullopt;
        req.method = rl.substr(0, s1);
        std::string fp = rl.substr(s1+1, s2-s1-1);
        req.http_version = rl.substr(s2+1);
        auto qm = fp.find('?');
        if (qm != std::string::npos) { req.path = fp.substr(0,qm); req.query_string = fp.substr(qm+1); }
        else req.path = fp;
        req.path = url_decode(req.path);

        std::string hs = hbuf.substr(fle+2);
        std::istringstream hss(hs); std::string line;
        while (std::getline(hss, line)) {
            if (line.empty() || line == "\r") break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto c = line.find(':');
            if (c == std::string::npos) continue;
            std::string n = line.substr(0,c), v = line.substr(c+1);
            auto vs = v.find_first_not_of(" \t");
            if (vs != std::string::npos) v = v.substr(vs);
            req.headers[n] = v;
        }

        int64_t cl = req.content_length();
        if (cl > 0) {
            if ((size_t)cl > max_body_) return std::nullopt;
            req.body.resize((size_t)cl);
            size_t tr = 0;
            while (tr < (size_t)cl) {
                int n = ::recv(sock, &req.body[tr], (int)(cl-(int64_t)tr), 0);
                if (n <= 0) return std::nullopt;
                tr += (size_t)n;
            }
        }
        return req;
    }

    void process_request(const HttpRequest& req, HttpResponse& resp) {
        if (req.method == "OPTIONS") { resp.set_status(204); return; }
        if (!static_dir_.empty() && (req.method == "GET" || req.method == "HEAD")) {
            if (try_serve_static(req, resp)) { stat_served_.fetch_add(1); return; }
        }
        api_served_.fetch_add(1);
        try {
            // Propagate request metadata to REST API via internal headers
            auto hdrs = req.headers;
            hdrs["X-Request-Id"] = req.request_id;
            hdrs["X-Real-IP"] = req.remote_addr;
            auto [st, bd] = handler_(req.method, req.path, hdrs, req.body);
            resp.status_code = st; resp.status_text = HttpResponse::status_text_for(st);
            resp.body = bd;
            resp.set_header("Content-Type", "application/json; charset=utf-8");
        } catch (...) { resp.set_json("{\"error\":\"Internal server error\"}", 500); }
    }

    bool try_serve_static(const HttpRequest& req, HttpResponse& resp) {
        namespace fs = std::filesystem;
        std::string cp = req.path;
        if (cp.find("..") != std::string::npos) return false;
        if (cp.empty() || cp[0] != '/') return false;
        if (cp == "/") cp = "/index.html";
        fs::path fp = fs::path(static_dir_) / cp.substr(1);
        std::error_code ec;
        auto can = fs::canonical(fp, ec);
        if (ec) return false;
        auto scan = fs::canonical(fs::path(static_dir_), ec);
        if (ec) return false;
        if (can.string().substr(0, scan.string().size()) != scan.string()) return false;
        if (!fs::is_regular_file(can)) return false;

        auto sz = fs::file_size(can);
        auto mt = fs::last_write_time(can).time_since_epoch().count();
        std::ostringstream eo; eo << "\"" << std::hex << mt << "-" << sz << "\"";
        std::string etag = eo.str();
        auto inm = req.header("if-none-match");
        if (!inm.empty() && inm == etag) { resp.set_status(304); return true; }

        std::ifstream f(can, std::ios::binary);
        if (!f) return false;
        resp.set_status(200);
        resp.body = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        resp.set_header("Content-Type", mime_type(can.string()));
        resp.set_header("ETag", etag);
        auto mtype = mime_type(can.string());
        resp.set_header("Cache-Control",
            mtype.find("text/html") != std::string::npos ? "public, max-age=300" : "public, max-age=3600");
        return true;
    }

    void add_standard_headers(const HttpRequest& req, HttpResponse& resp) {
        resp.set_header("Server", std::string(::genie::VERSION_FULL));
        resp.set_header("X-Request-Id", req.request_id);
        if (!cors_origin_.empty()) {
            resp.set_header("Access-Control-Allow-Origin", cors_origin_);
            resp.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
            resp.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            resp.set_header("Access-Control-Max-Age", "86400");
            resp.set_header("Access-Control-Expose-Headers", "X-Request-Id, X-Total-Count, X-Page, X-Page-Size");
        }
        resp.set_header("X-Content-Type-Options", "nosniff");
        resp.set_header("X-Frame-Options", "DENY");
        resp.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
        resp.set_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
        resp.set_header("Content-Security-Policy",
            "default-src 'self'; script-src 'self' 'unsafe-inline'; "
            "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
            "connect-src 'self' ws: wss:");
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto g = std::gmtime(&t);
        char db[64]; std::strftime(db, sizeof(db), "%a, %d %b %Y %H:%M:%S GMT", g);
        resp.set_header("Date", db);
    }

    bool send_all(socket_t s, const std::string& d) {
        size_t sent = 0;
        while (sent < d.size()) {
            int n = ::send(s, d.c_str() + sent, (int)(d.size() - sent), 0);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        return true;
    }

    void send_error(socket_t s, int c, const std::string& m) {
        HttpResponse r; r.set_json("{\"error\":\"" + m + "\"}", c);
        r.set_header("Connection", "close");
        send_all(s, r.serialize());
    }

    static std::string url_decode(const std::string& e) {
        std::string d; d.reserve(e.size());
        for (size_t i = 0; i < e.size(); ++i) {
            if (e[i] == '%' && i+2 < e.size()) {
                int v = 0; std::istringstream is(e.substr(i+1,2));
                if (is >> std::hex >> v) { d += (char)v; i += 2; } else d += e[i];
            } else if (e[i] == '+') d += ' ';
            else d += e[i];
        }
        return d;
    }

    int64_t steady_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // State
    SimpleRouteHandler handler_;
    // Streaming handler: receives (headers, path, raw socket) and writes
    // directly to the TCP connection for SSE/chunked streams.
    // Returns true if the request was handled (normal response path skipped).
    StreamingHandler streaming_handler_;
    int port_ = 8080;
    std::string bind_addr_ = "0.0.0.0";
    int worker_count_ = 4;
    std::string static_dir_;
    std::string cors_origin_ = "*";
    size_t max_body_ = 10*1024*1024;
    size_t max_header_ = 64*1024;
    int idle_timeout_ = 30000;
    int req_timeout_ = 30000;
    bool access_log_on_ = true;
    int max_pending_ = 1024;

    socket_t listen_sock_ = INVALID_SOCK;
    std::atomic<bool> running_{false};
    int64_t start_time_ = 0;
    ConnectionQueue conn_queue_;
    std::vector<std::thread> workers_;
    std::thread server_thread_;
    AccessLogger alog_;

    std::atomic<int64_t> total_req_{0}, active_conn_{0}, total_conn_{0};
    std::atomic<int64_t> bytes_out_{0}, bytes_in_{0};
    std::atomic<int64_t> err_4xx_{0}, err_5xx_{0};
    std::atomic<int64_t> stat_served_{0}, api_served_{0};
};

} // namespace net
} // namespace genie

#endif // GENIE_NET_HTTP_SERVER_HPP
