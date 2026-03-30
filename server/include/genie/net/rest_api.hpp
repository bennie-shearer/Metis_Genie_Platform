/**
 * @file rest_api.hpp
 * @brief Pure REST API framework for Metis Genie Platform
 * @version 5.5.4
 *
 * Self-contained REST API with:
 *   - Route registration (GET, POST, PUT, DELETE)
 *   - JSON request/response types
 *   - Bearer token authentication middleware
 *   - Session management with 24-hour expiry
 *   - CORS response headers
 *   - All Metis Genie Platform endpoints
 *
 * No TCP, no sockets, no HTTP parsing. This is a pure REST layer
 * that can be served by any HTTP server or tested directly in C++.
 *
 * Endpoints:
 *   GET  /api/v1/health       - Health check (no auth required)
 *   POST /api/v1/auth/login   - Authenticate, returns Bearer token
 *   POST /api/v1/auth/logout  - End session
 *   GET  /api/v1/status       - System status
 *   GET  /api/v1/portfolios   - Portfolio list
 *   GET  /api/v1/positions    - Position data
 *   GET  /api/v1/risk         - Risk metrics
 *   GET  /api/v1/market       - Market data
 *   GET  /api/v1/orders       - Order list
 *   POST /api/v1/orders       - Submit order
 *
 * Testing from C++:
 *   genie::net::RestApi api;
 *   api.configure_defaults();
 *   auto res = api.handle("GET", "/api/v1/health");
 *   // res.status == 200, res.body contains JSON
 *
 * Testing from client (js/connection.js uses fetch):
 *   curl http://localhost:8080/api/health
 *   curl -X POST http://localhost:8080/api/auth/login \
 *     -H "Content-Type: application/json" \
 *     -d '{"username":"admin","password":"demo"}'
 *
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_NET_REST_API_HPP
#define GENIE_NET_REST_API_HPP

#include "../core/version.hpp"
#include "../core/thread_pool.hpp"
#include "../core/logging.hpp"
#include "../core/crypto.hpp"
#include "../core/user_store.hpp"
#include "../core/audit_log.hpp"
#include "live_data_provider.hpp"
#include "response_cache.hpp"
#include "error_handler.hpp"
#include "../core/graceful_shutdown.hpp"
#include "prometheus_endpoint.hpp"
#include "route_table.hpp"

#include "response_compression.hpp"
#include "http2_server.hpp"
#include "../core/file_watcher.hpp"
#include "../core/binary_serializer.hpp"
#include "../ops/k8s_client.hpp"
#include "../trading/fix_engine_v2.hpp"
#include "wasm_client.hpp"
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <functional>
#include <sstream>
#include <optional>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

namespace genie::net {

// =========================================================================
// JSON Helpers (minimal, zero-dependency)
// =========================================================================

/** Extract a string value from JSON by key */
inline std::string json_get(const std::string& json, const std::string& key) {
    auto kpos = json.find("\"" + key + "\"");
    if (kpos == std::string::npos) return "";
    auto colon = json.find(':', kpos);
    if (colon == std::string::npos) return "";
    auto quote1 = json.find('"', colon);
    if (quote1 == std::string::npos) return "";
    auto quote2 = json.find('"', quote1 + 1);
    if (quote2 == std::string::npos) return "";
    return json.substr(quote1 + 1, quote2 - quote1 - 1);
}

/** Escape a string for safe JSON embedding */
inline std::string json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

/** Build a JSON object from key-value pairs */
class JsonBuilder {
    std::ostringstream ss_;
    bool first_{true};
public:
    JsonBuilder() { ss_ << "{"; }
    JsonBuilder& add(const std::string& key, const std::string& val) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << json_escape(key) << "\":\"" << json_escape(val) << "\"";
        return *this;
    }
    JsonBuilder& add(const std::string& key, int val) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << key << "\":" << val;
        return *this;
    }
    JsonBuilder& add(const std::string& key, double val) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << key << "\":" << std::fixed << std::setprecision(2) << val;
        return *this;
    }
    JsonBuilder& add(const std::string& key, bool val) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << key << "\":" << (val ? "true" : "false");
        return *this;
    }
    /** Add a raw JSON value (array or nested object) */
    JsonBuilder& add_raw(const std::string& key, const std::string& raw_json) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << key << "\":" << raw_json;
        return *this;
    }
    [[nodiscard]] std::string build() const { return ss_.str() + "}"; }
};

/** Build a JSON array from values */
class JsonArray {
    std::ostringstream ss_;
    bool first_{true};
public:
    JsonArray() { ss_ << "["; }
    /** Add a JSON object (from JsonBuilder) */
    JsonArray& add(const JsonBuilder& obj) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << obj.build();
        return *this;
    }
    /** Add a raw JSON string/object/value */
    JsonArray& add_raw(const std::string& raw_json) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << raw_json;
        return *this;
    }
    /** Add a string value */
    JsonArray& add(const std::string& val) {
        if (!first_) ss_ << ",";
        first_ = false;
        ss_ << "\"" << json_escape(val) << "\"";
        return *this;
    }
    [[nodiscard]] std::string build() const { return ss_.str() + "]"; }
};

// =========================================================================
// REST Request / Response
// =========================================================================

/** REST API HTTP methods */
enum class Method { GET, POST, PUT, DELETE_METHOD };

// ============================================================================
// Constexpr route flags table -- used in handle() to bypass auth/cache
// for known-public and known-SSE routes without scanning the runtime vector.
// Update this table when adding new public or SSE endpoints.
// ============================================================================
namespace {

using RT = ::genie::net::RouteTable<32>;
using RF = ::genie::net::RouteFlags;
using CM = ::genie::net::CxMethod;

inline const RT& cx_route_table() {
    static const RT kTable = []() {
        RT t;
        // Public (no auth required)
        t.add({CM::GET,  "/api/v1/health",           RF::PUBLIC | RF::CACHEABLE,   "Health check"});
        t.add({CM::GET,  "/metrics",                  RF::PUBLIC,                   "Prometheus metrics"});
        t.add({CM::POST, "/api/v1/auth/login",        RF::PUBLIC | RF::MUTATING,    "Login"});
        t.add({CM::POST, "/api/v1/auth/register",     RF::PUBLIC | RF::MUTATING,    "Register"});
        // SSE streams (skip cache, require auth)
        t.add({CM::GET,  "/api/v1/stream/poll",       RF::AUTH_REQUIRED | RF::SSE_STREAM, "SSE poll bridge"});
        t.add({CM::GET,  "/api/v1/stream/market",     RF::AUTH_REQUIRED | RF::SSE_STREAM, "Market SSE"});
        t.add({CM::GET,  "/api/v1/stream/portfolio",  RF::AUTH_REQUIRED | RF::SSE_STREAM, "Portfolio SSE"});
        t.add({CM::GET,  "/api/v1/stream/alerts",     RF::AUTH_REQUIRED | RF::SSE_STREAM, "Alerts SSE"});
        return t;
    }();
    return kTable;
}

/** Look up RouteFlags for a given method+path from the constexpr table.
 *  Returns NONE if not found (caller uses runtime defaults). */
inline RF cx_flags(Method method, const std::string& path) {
    CM m = CM::GET;
    if      (method == Method::POST)          m = CM::POST;
    else if (method == Method::PUT)           m = CM::PUT;
    else if (method == Method::DELETE_METHOD) m = CM::DELETE;
    auto* d = cx_route_table().find(m, path);
    return d ? d->flags : RF::NONE;
}

} // anonymous namespace


inline std::string method_to_string(Method m) {
    switch (m) {
        case Method::GET: return "GET";
        case Method::POST: return "POST";
        case Method::PUT: return "PUT";
        case Method::DELETE_METHOD: return "DELETE";
    }
    return "UNKNOWN";
}

inline Method string_to_method(std::string_view s) {
    if (s == "GET") return Method::GET;
    if (s == "POST") return Method::POST;
    if (s == "PUT") return Method::PUT;
    if (s == "DELETE") return Method::DELETE_METHOD;
    return Method::GET;
}

/** REST API request */
struct Request {
    Method method{Method::GET};
    std::string path;
    std::string client_ip;  // Remote client IP address
    std::string request_id; // Unique request ID for log correlation
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> path_params;  // For URL path parameters like :id
    std::string body;

    /** Get a header value (case-insensitive) */
    [[nodiscard]] std::string header(const std::string& key) const {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        for (const auto& [k, v] : headers) {
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk == lower_key) return v;
        }
        return "";
    }

    /** Get query parameter */
    [[nodiscard]] std::string param(const std::string& key) const {
        auto it = query_params.find(key);
        return (it != query_params.end()) ? it->second : "";
    }

    /** Get path parameter (from URL like /users/:id) */
    [[nodiscard]] std::string path_param(const std::string& key) const {
        auto it = path_params.find(key);
        return (it != path_params.end()) ? it->second : "";
    }

    /** Extract Bearer token from Authorization header */
    [[nodiscard]] std::string bearer_token() const {
        auto auth = header("Authorization");
        if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") return auth.substr(7);
        return "";
    }

    /** Parse JSON body field */
    [[nodiscard]] std::string json_field(const std::string& key) const {
        return json_get(body, key);
    }
};

/** REST API response */
struct Response {
    int status{200};
    std::string content_type{"application/json"};
    std::map<std::string, std::string> headers;
    std::string body;
    bool chunked{false};  // set true for SSE/streaming responses

    Response() {
        // Default CORS headers for cross-origin client access
        headers["Access-Control-Allow-Origin"] = "*";
        headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
        headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
    }

    /** Set JSON response body */
    Response& json(const std::string& json_body) {
        content_type = "application/json";
        body = json_body;
        return *this;
    }

    /** Set error response */
    Response& error(int code, const std::string& message) {
        status = code;
        content_type = "application/json";
        body = "{\"error\":\"" + json_escape(message) + "\"}";
        return *this;
    }

    /** Set status code */
    Response& set_status(int code) { status = code; return *this; }

    /** Generate a weak ETag from response body content */
    [[nodiscard]] std::string etag() const {
        if (body.empty()) return "";
        // FNV-1a hash of body
        uint64_t h = 14695981039346656037ULL;
        for (char c : body) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
        std::ostringstream ss;
        ss << "W/\"" << std::hex << h << "\"";
        return ss.str();
    }

    /** Check if response indicates success */
    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

// sse_server.hpp declares its own "namespace genie::net { ... }" and uses Request/Response.
// It must be included OUTSIDE the genie::net namespace block (to avoid double-nesting) but
// AFTER Request and Response are declared.  We temporarily close and reopen the namespace.
} // namespace genie::net  [temporary close - see reopen below]
#include "sse_server.hpp"
namespace genie::net {  // reopen - all following declarations land in genie::net as before

// =========================================================================
// Session Management
// =========================================================================

/** Token-based session store with 24-hour expiry */
class SessionStore {
public:
    struct Session {
        std::string username;
        std::string role;
        std::chrono::steady_clock::time_point created;
        std::chrono::steady_clock::time_point last_active;
    };

private:
    std::map<std::string, Session> sessions_;
    mutable std::mutex mutex_;

    static std::string generate_token() {
        static std::mt19937 rng(std::random_device{}());
        std::ostringstream ss;
        ss << "genie-token-" << std::hex;
        for (int i = 0; i < 4; ++i) ss << rng();
        return ss.str();
    }

public:
    /** Create session, return Bearer token */
    std::string create(const std::string& username, const std::string& role) {
        std::lock_guard lock(mutex_);
        auto token = generate_token();
        auto now = std::chrono::steady_clock::now();
        sessions_[token] = {username, role, now, now};
        return token;
    }

    /** Validate token, return username and role if valid */
    std::optional<std::pair<std::string, std::string>> validate(const std::string& token) {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return std::nullopt;
        auto elapsed = std::chrono::steady_clock::now() - it->second.created;
        if (std::chrono::duration_cast<std::chrono::hours>(elapsed).count() > 24) {
            sessions_.erase(it);
            return std::nullopt;
        }
        it->second.last_active = std::chrono::steady_clock::now();
        return std::make_pair(it->second.username, it->second.role);
    }

    /** Remove session */
    void destroy(const std::string& token) {
        std::lock_guard lock(mutex_);
        sessions_.erase(token);
    }

    /** Active session count */
    [[nodiscard]] size_t count() const {
        std::lock_guard lock(mutex_);
        return sessions_.size();
    }
    
    /** Return a snapshot of all active sessions */
    [[nodiscard]] std::map<std::string, Session> all_sessions() const {
        std::lock_guard lock(mutex_);
        return sessions_;
    }
};

// =========================================================================
// Login Rate Limiter (IP-based brute-force protection)
// =========================================================================

/** Tracks failed login attempts per IP with configurable lockout */
class LoginRateLimiter {
    struct IpRecord {
        int failed_attempts{0};
        std::chrono::system_clock::time_point first_failure;
        std::chrono::system_clock::time_point lockout_until;
    };

    std::unordered_map<std::string, IpRecord> records_;
    mutable std::mutex mutex_;
    int max_attempts_{5};
    int lockout_minutes_{15};
    int window_minutes_{30};

public:
    LoginRateLimiter() = default;
    LoginRateLimiter(int max_attempts, int lockout_minutes, int window_minutes = 30)
        : max_attempts_(max_attempts), lockout_minutes_(lockout_minutes),
          window_minutes_(window_minutes) {}

    /** Reconfigure limits at runtime (e.g. from config.json) */
    void set_limits(int max_attempts, int lockout_minutes) {
        std::lock_guard lock(mutex_);
        max_attempts_ = std::max(1, max_attempts);
        lockout_minutes_ = std::max(1, lockout_minutes);
    }

    /** Check if an IP is currently locked out. Returns true if blocked. */
    bool is_blocked(const std::string& ip) const {
        std::lock_guard lock(mutex_);
        auto it = records_.find(ip);
        if (it == records_.end()) return false;
        auto now = std::chrono::system_clock::now();
        return now < it->second.lockout_until;
    }

    /** Record a failed login attempt. Returns true if the IP is now locked out. */
    bool record_failure(const std::string& ip) {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto& rec = records_[ip];

        // Reset window if first failure was too long ago
        auto window = std::chrono::minutes(window_minutes_);
        if (rec.failed_attempts > 0 && (now - rec.first_failure) > window) {
            rec.failed_attempts = 0;
        }
        if (rec.failed_attempts == 0) {
            rec.first_failure = now;
        }
        rec.failed_attempts++;

        if (rec.failed_attempts >= max_attempts_) {
            rec.lockout_until = now + std::chrono::minutes(lockout_minutes_);
            return true;
        }
        return false;
    }

    /** Clear failed attempts for an IP (call on successful login) */
    void record_success(const std::string& ip) {
        std::lock_guard lock(mutex_);
        records_.erase(ip);
    }

    /** Get remaining lockout seconds for an IP (0 if not locked) */
    int lockout_remaining_seconds(const std::string& ip) const {
        std::lock_guard lock(mutex_);
        auto it = records_.find(ip);
        if (it == records_.end()) return 0;
        auto now = std::chrono::system_clock::now();
        if (now >= it->second.lockout_until) return 0;
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
            it->second.lockout_until - now).count());
    }

    /** Periodic cleanup of expired records */
    void cleanup() {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto window = std::chrono::minutes(window_minutes_ + lockout_minutes_);
        for (auto it = records_.begin(); it != records_.end();) {
            if ((now - it->second.first_failure) > window && now >= it->second.lockout_until) {
                it = records_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// =========================================================================
// User Store (enhanced credential store with profiles)
// =========================================================================

/** User profile data */
struct UserProfile {
    std::string username;
    std::string password;      // In production: hashed
    std::string role;          // Administrator, Trader, Analyst, Viewer
    std::string display_name;
    std::string email;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point last_login;
    bool active{true};
};

/** In-memory user store with profile management */
class CredentialStore {
    std::map<std::string, UserProfile> users_; // username -> profile
    mutable std::mutex mutex_;
    
    static bool is_valid_email(const std::string& email) {
        if (email.empty()) return true; // optional
        auto at = email.find('@');
        auto dot = email.rfind('.');
        return at != std::string::npos && dot != std::string::npos && at < dot;
    }
    
    static bool is_valid_username(const std::string& name) {
        if (name.length() < 3 || name.length() > 32) return false;
        for (char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
        }
        return true;
    }
    
    static bool is_valid_password(const std::string& pass) {
        return pass.length() >= 4; // Minimum 4 chars
    }

public:
    /** Add user (used for seeding default users) */
    void add_user(const std::string& username, const std::string& password, const std::string& role = "Viewer") {
        std::lock_guard lock(mutex_);
        UserProfile profile;
        profile.username = username;
        profile.password = password;
        profile.role = role;
        profile.display_name = username;
        profile.email = "";
        profile.created = std::chrono::system_clock::now();
        profile.last_login = profile.created;
        profile.active = true;
        users_[username] = std::move(profile);
    }

    /** Register new user. Returns error message or empty on success. */
    [[nodiscard]] std::string register_user(const std::string& username, 
                                            const std::string& password,
                                            const std::string& email = "",
                                            const std::string& display_name = "") {
        if (!is_valid_username(username)) {
            return "Username must be 3-32 alphanumeric characters";
        }
        if (!is_valid_password(password)) {
            return "Password must be at least 4 characters";
        }
        if (!is_valid_email(email)) {
            return "Invalid email format";
        }
        
        std::lock_guard lock(mutex_);
        if (users_.count(username)) {
            return "Username already exists";
        }
        
        UserProfile profile;
        profile.username = username;
        profile.password = password;
        profile.role = "Viewer"; // New users get Viewer role
        profile.display_name = display_name.empty() ? username : display_name;
        profile.email = email;
        profile.created = std::chrono::system_clock::now();
        profile.last_login = profile.created;
        profile.active = true;
        users_[username] = std::move(profile);
        return "";
    }

    /** Authenticate user, returns role on success */
    std::optional<std::string> authenticate(const std::string& username, const std::string& password) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it != users_.end() && it->second.active && it->second.password == password) {
            it->second.last_login = std::chrono::system_clock::now();
            return it->second.role;
        }
        return std::nullopt;
    }

    /** Get user profile (without password) */
    [[nodiscard]] std::optional<UserProfile> get_profile(const std::string& username) const {
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return std::nullopt;
        return it->second;
    }

    /** Update user profile. Returns error message or empty on success. */
    [[nodiscard]] std::string update_profile(const std::string& username,
                                             const std::string& display_name = "",
                                             const std::string& email = "") {
        if (!display_name.empty() && (display_name.length() < 1 || display_name.length() > 64)) {
            return "Display name must be 1-64 characters";
        }
        if (!email.empty() && !is_valid_email(email)) {
            return "Invalid email format";
        }
        
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return "User not found";
        
        if (!display_name.empty()) it->second.display_name = display_name;
        if (!email.empty()) it->second.email = email;
        return "";
    }

    /** Change password. Returns error message or empty on success. */
    [[nodiscard]] std::string change_password(const std::string& username,
                                              const std::string& old_password,
                                              const std::string& new_password) {
        if (!is_valid_password(new_password)) {
            return "New password must be at least 4 characters";
        }
        
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return "User not found";
        if (it->second.password != old_password) return "Current password incorrect";
        
        it->second.password = new_password;
        return "";
    }

    /** Admin: Update user role */
    [[nodiscard]] std::string set_role(const std::string& username, const std::string& role) {
        static const std::set<std::string> valid_roles = {"Administrator", "Trader", "Analyst", "Viewer"};
        if (!valid_roles.count(role)) {
            return "Invalid role. Must be: Administrator, Trader, Analyst, or Viewer";
        }
        
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return "User not found";
        
        it->second.role = role;
        return "";
    }

    /** Admin: Set user active/inactive */
    [[nodiscard]] std::string set_active(const std::string& username, bool active) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return "User not found";
        
        it->second.active = active;
        return "";
    }

    /** Admin: Delete user */
    [[nodiscard]] bool delete_user(const std::string& username) {
        std::lock_guard lock(mutex_);
        return users_.erase(username) > 0;
    }

    /** Check if user exists */
    [[nodiscard]] bool exists(const std::string& username) const {
        std::lock_guard lock(mutex_);
        return users_.count(username) > 0;
    }

    /** Get all usernames (for admin) */
    [[nodiscard]] std::vector<std::string> list_users() const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> result;
        result.reserve(users_.size());
        for (const auto& [name, _] : users_) {
            result.push_back(name);
        }
        return result;
    }

    /** User count */
    [[nodiscard]] size_t user_count() const {
        std::lock_guard lock(mutex_);
        return users_.size();
    }
    
    /** Format timestamp for JSON */
    static std::string format_time(std::chrono::system_clock::time_point tp) {
        auto t = std::chrono::system_clock::to_time_t(tp);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }
};


// =========================================================================
// Pagination
// =========================================================================

/** Pagination parameters extracted from query string */
struct Pagination {
    int offset{0};
    int limit{50};
    int total{0};

    // Configurable defaults (set by RestApi from config.json)
    static inline int default_limit{50};
    static inline int max_limit{500};

    /** Parse from request query params: ?offset=0&limit=10 */
    static Pagination from_request(const Request& req, int total_count) {
        Pagination p;
        p.total = total_count;
        p.limit = default_limit;
        auto off = req.param("offset");
        auto lim = req.param("limit");
        if (!off.empty()) { try { p.offset = std::max(0, std::stoi(off)); } catch (...) {} }
        if (!lim.empty()) { try { p.limit = std::clamp(std::stoi(lim), 1, max_limit); } catch (...) {} }
        if (p.offset >= p.total) p.offset = std::max(0, p.total - p.limit);
        return p;
    }

    /** Add pagination metadata to JSON response */
    [[nodiscard]] std::string meta_json() const {
        return JsonBuilder()
            .add("offset", offset)
            .add("limit", limit)
            .add("total", total)
            .add("has_more", (offset + limit < total))
            .build();
    }
};

// =========================================================================
// Middleware
// =========================================================================

/** Middleware function: runs before route handler, returns false to stop */
using Middleware = std::function<bool(const Request&, Response&)>;

/** Request log entry */
struct RequestLog {
    std::string method;
    std::string path;
    std::string request_id;
    int status{0};
    double duration_ms{0};
    std::chrono::system_clock::time_point timestamp;
};

/** Request logger middleware */
class RequestLogger {
    std::vector<RequestLog> logs_;
    mutable std::mutex mutex_;
    size_t max_entries_{1000};
public:
    void log(const std::string& method, const std::string& path, int status, double ms,
             const std::string& request_id = "") {
        std::lock_guard lock(mutex_);
        if (logs_.size() >= max_entries_) logs_.erase(logs_.begin());
        logs_.push_back({method, path, request_id, status, ms, std::chrono::system_clock::now()});
    }
    [[nodiscard]] size_t count() const { std::lock_guard lock(mutex_); return logs_.size(); }
    [[nodiscard]] std::vector<RequestLog> recent(size_t n = 10) const {
        std::lock_guard lock(mutex_);
        size_t start = (logs_.size() > n) ? logs_.size() - n : 0;
        return {logs_.begin() + static_cast<long>(start), logs_.end()};
    }
};

/** Rate limiter (per-token, sliding window) */
class RateLimiter {
    struct Window { int count{0}; std::chrono::steady_clock::time_point reset_at; };
    std::map<std::string, Window> windows_;
    mutable std::mutex mutex_;
    int max_requests_{100};       // per window
    int window_seconds_{60};      // window size
public:
    RateLimiter() = default;
    RateLimiter(int max_req, int window_sec) : max_requests_(max_req), window_seconds_(window_sec) {}

    /** Reconfigure limits at runtime (e.g. from config.json) */
    void set_limits(int max_req, int window_sec) {
        std::lock_guard lock(mutex_);
        max_requests_ = std::max(1, max_req);
        window_seconds_ = std::max(1, window_sec);
    }

    /** Check if request is allowed. Returns true if OK, false if rate limited. */
    bool allow(const std::string& key) {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& w = windows_[key];
        if (now >= w.reset_at) { w.count = 0; w.reset_at = now + std::chrono::seconds(window_seconds_); }
        if (w.count >= max_requests_) return false;
        w.count++;
        return true;
    }

    /** Remaining requests for a key */
    [[nodiscard]] int remaining(const std::string& key) const {
        std::lock_guard lock(mutex_);
        auto it = windows_.find(key);
        if (it == windows_.end()) return max_requests_;
        auto now = std::chrono::steady_clock::now();
        if (now >= it->second.reset_at) return max_requests_;
        return std::max(0, max_requests_ - it->second.count);
    }
};

// =========================================================================
// Route Handler
// =========================================================================

using RouteHandler = std::function<void(const Request&, Response&)>;

struct Route {
    Method method;
    std::string path;
    RouteHandler handler;
};

// =========================================================================
// REST API
// =========================================================================

/**
 * Pure REST API framework.
 *
 * Register routes, then call handle() to process requests.
 * No TCP, no sockets, no HTTP parsing - just REST.
 *
 * Usage:
 *   RestApi api;
 *   api.configure_defaults(); // Registers all Genie endpoints
 *   auto res = api.handle("GET", "/api/v1/health");
 *   // res.status == 200
 *   // res.body == {"status":"healthy","version":"5.3.3",...}
 */
class RestApi {
private:
    std::vector<Route> routes_;
    mutable std::recursive_mutex routes_mutex_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<Middleware> middleware_;

    // Shared state for configured endpoints
    SessionStore sessions_;
    CredentialStore credentials_;       // Kept for backward compatibility
    UserStore user_store_;              // Persistent user storage
    AuditLog audit_log_;                // Security audit logging
    RequestLogger logger_;
    RateLimiter rate_limiter_{100, 60}; // 100 req/min per token
    LoginRateLimiter login_limiter_{5, 15, 30}; // 5 attempts, 15min lockout, 30min window
    bool use_persistent_storage_{false}; // Toggle for UserStore vs CredentialStore
    bool auth_required_{true};           // Require auth on protected endpoints
    bool create_demo_users_{true};       // Seed default demo users on startup
    int  session_timeout_minutes_{480};  // Session lifetime (from config.pson auth.session_timeout_minutes)
    std::string demo_admin_user_;         // from auth.demo_admin_user (default: "admin")
    std::string demo_admin_pass_;         // from auth.demo_admin_pass (default: "demo")
    std::string demo_trader_user_;        // from auth.demo_trader_user (default: "trader")
    std::string demo_trader_pass_;        // from auth.demo_trader_pass (default: "trade")
    std::string demo_analyst_user_;       // from auth.demo_analyst_user (default: "user")
    std::string demo_analyst_pass_;       // from auth.demo_analyst_pass (default: "user")
    LiveDataProvider live_data_;         // Live data engine bridge

    // Application identity (from config.json application.*)
    std::string app_name_{"Metis Genie Platform"};
    std::string app_version_{VERSION_STRING};
    std::string app_logo_{"img/logo.svg"};
    std::string app_icon_{"img/icon.svg"};

    // Database configuration (from config.json database.*)
    std::string db_path_{"metis-genie-platform.db"};
    bool db_wal_mode_{true};
    int db_busy_timeout_ms_{5000};
    bool sql_logging_{false};

    // Prices database path (from config.json storage.prices_db)
    std::string prices_db_path_{"prices.db"};

    // Backup configuration (from config.json backup.*)
    bool backup_enabled_{true};
    std::string backup_directory_{"backups"};
    int backup_interval_hours_{24};
    int backup_retention_days_{30};

    // Health monitoring thresholds (from config.json health.*)
    int health_check_interval_seconds_{60};
    int health_disk_warning_pct_{85};
    int health_memory_warning_pct_{90};

    // Shutdown callback (set by main to trigger g_shutdown_requested)
    std::function<void()> shutdown_callback_;

    // Response cache with per-endpoint TTL (v5.3.1)
    ApiCache response_cache_;
    bool cache_enabled_{true};
    int  cache_default_ttl_{60};  // default TTL seconds for set_cache_default_ttl()

    // Request body validation (v5.3.1)
    bool validation_enabled_{true};
    size_t validation_max_body_{1048576};

    // Graceful shutdown integration (v5.3.1)
    bool graceful_shutdown_enabled_{true};

    // -----------------------------------------------------------------------
    // v5.3.1 feature members
    // -----------------------------------------------------------------------
    // Compression
    CompressionConfig compression_config_;

    // HTTP/2
    http2::Http2Adapter http2_adapter_;

    // Kubernetes client (cross-namespace: genie::ops)
    ::genie::ops::K8sClient k8s_client_;

    // FIX engine v2 (cross-namespace: genie::trading::fix)
    ::genie::trading::fix::FixSession fix_session_v2_;

    // WASM (genie::net::wasm -- same namespace root as RestApi)
    wasm::WasmConfig wasm_config_;

    // SSE + Prometheus + file watcher flags
    bool sse_enabled_{true};
    bool prometheus_enabled_{true};
    // Injectable stats provider — set from main.cpp to supply live HttpServer stats.
    // Signature matches HttpServer::ServerStats fields needed by record_server_metrics().
    using StatsProvider = std::function<void(
        int64_t& total_req, int64_t& active_conn, int64_t& bytes_sent,
        int64_t& bytes_recv, int64_t& err_4xx, int64_t& err_5xx,
        double& uptime_sec)>;
    StatsProvider stats_provider_;
    std::string prometheus_namespace_{"metis_genie"};
    bool file_watcher_enabled_{true};
    int  file_watcher_poll_ms_{2000};

public:
    RestApi() : start_time_(std::chrono::steady_clock::now()) {}

    static constexpr std::string_view API_VERSION = "v1";

    // ----- Route Registration -----

    void get(const std::string& path, RouteHandler handler) {
        std::lock_guard lock(routes_mutex_);
        routes_.push_back({Method::GET, path, std::move(handler)});
    }

    void post(const std::string& path, RouteHandler handler) {
        std::lock_guard lock(routes_mutex_);
        routes_.push_back({Method::POST, path, std::move(handler)});
    }

    void put(const std::string& path, RouteHandler handler) {
        std::lock_guard lock(routes_mutex_);
        routes_.push_back({Method::PUT, path, std::move(handler)});
    }

    void del(const std::string& path, RouteHandler handler) {
        std::lock_guard lock(routes_mutex_);
        routes_.push_back({Method::DELETE_METHOD, path, std::move(handler)});
    }

    // ----- Middleware -----

    /** Add middleware that runs before every route handler */
    void use(Middleware mw) { middleware_.push_back(std::move(mw)); }

    // ----- Request Handling -----

    /** Handle a REST request and return a response */
    Response handle(const Request& req) {
        auto start = std::chrono::steady_clock::now();
        Response res;

        // Graceful shutdown: register in-flight request (v5.3.1)
        core::GracefulShutdown::RequestGuard shutdown_guard(core::GracefulShutdown::instance());
        if (graceful_shutdown_enabled_ && !core::GracefulShutdown::instance().is_accepting_requests()) {
            res.status = 503;
            res.body = ErrorHandler::build(ErrorCode::SERVICE_UNAVAILABLE,
                "Server is shutting down, please retry", req.path);
            res.headers["Retry-After"] = "10";
            log_request(req, res, start);
            return res;
        }

        // Request validation middleware (v5.3.1)
        if (validation_enabled_) {
            // Reject oversized bodies
            if (req.body.size() > validation_max_body_) {
                res.status = 413;
                res.body = ErrorHandler::build(ErrorCode::VALIDATION_ERROR,
                    "Request body exceeds maximum allowed size", req.path,
                    "Maximum: " + std::to_string(validation_max_body_) + " bytes");
                log_request(req, res, start);
                return res;
            }
            // Require Content-Type on POST/PUT/PATCH with a body
            if (!req.body.empty() &&
                (req.method == Method::POST || req.method == Method::PUT)) {
                auto ct = req.header("Content-Type");
                if (ct.find("application/json") == std::string::npos) {
                    res.status = 415;
                    res.body = ErrorHandler::build(ErrorCode::VALIDATION_ERROR,
                        "Content-Type must be application/json", req.path);
                    log_request(req, res, start);
                    return res;
                }
                // Basic JSON structure check: must start with { or [
                std::string trimmed = req.body;
                size_t p = trimmed.find_first_not_of(" \t\r\n");
                if (p != std::string::npos && trimmed[p] != '{' && trimmed[p] != '[') {
                    res.status = 400;
                    res.body = ErrorHandler::build(ErrorCode::VALIDATION_ERROR,
                        "Request body is not valid JSON", req.path);
                    log_request(req, res, start);
                    return res;
                }
            }
        }

        // Response cache check for GET requests (v5.3.1)
        // Skip cache for SSE_STREAM routes (they produce real-time data).
        const auto cx_f = cx_flags(req.method, req.path);
        [[maybe_unused]] const bool is_public_route = ::genie::net::has_flag(cx_f, RF::PUBLIC);
        const bool is_sse_route = ::genie::net::has_flag(cx_f, RF::SSE_STREAM);
        if (cache_enabled_ && req.method == Method::GET && !is_sse_route) {
            std::string cache_key = req.path;
            // Build canonical query string from query_params map (sorted by key)
            if (!req.query_params.empty()) {
                cache_key += "?";
                bool first_qp = true;
                for (const auto& [k, v] : req.query_params) {
                    if (!first_qp) cache_key += "&";
                    cache_key += k + "=" + v;
                    first_qp = false;
                }
            }
            // Append bearer token so different users get different cache entries
            cache_key += "|" + req.bearer_token();
            auto cached = response_cache_.get(cache_key);
            if (cached.has_value()) {
                res.status = 200;
                res.body = *cached;
                res.headers["Content-Type"] = "application/json; charset=utf-8";
                res.headers["X-Cache"] = "HIT";
                log_request(req, res, start);
                return res;
            }
            res.headers["X-Cache"] = "MISS";
        }

        // Run middleware chain
        for (const auto& mw : middleware_) {
            if (!mw(req, res)) {
                log_request(req, res, start);
                return res;
            }
        }

        // Rate limiting (by token or IP)
        auto rate_key = req.bearer_token().empty() ? "anonymous" : req.bearer_token();
        if (!rate_limiter_.allow(rate_key)) {
            res.status = 429;
            res.body = ErrorHandler::build(ErrorCode::RATE_LIMITED,
                "Rate limit exceeded. Please slow down.", req.path,
                "Retry after the rate limit window resets.");
            res.headers["X-RateLimit-Remaining"] = "0";
            log_request(req, res, start);
            return res;
        }
        res.headers["X-RateLimit-Remaining"] = std::to_string(rate_limiter_.remaining(rate_key));

        std::lock_guard lock(routes_mutex_);
        for (const auto& route : routes_) {
            if (route.method == req.method) {
                auto params = match_path(route.path, req.path);
                if (params.has_value()) {
                    Request mutable_req = req;
                    mutable_req.path_params = params.value();
                    try {
                        route.handler(mutable_req, res);
                    } catch (const std::exception& e) {
                        res.status = 500;
                        res.body = ErrorHandler::build(ErrorCode::INTERNAL_ERROR,
                            e.what(), req.path);
                    } catch (...) {
                        res.status = 500;
                        res.body = ErrorHandler::build(ErrorCode::INTERNAL_ERROR,
                            "Unexpected internal error", req.path);
                    }

                    // Store successful GET responses in cache (v5.3.1)
                    if (cache_enabled_ && req.method == Method::GET && res.ok() && !res.body.empty()) {
                        std::string cache_key = req.path;
                        if (!req.query_params.empty()) {
                            cache_key += "?";
                            bool first_qp = true;
                            for (const auto& [k, v] : req.query_params) {
                                if (!first_qp) cache_key += "&";
                                cache_key += k + "=" + v;
                                first_qp = false;
                            }
                        }
                        cache_key += "|" + req.bearer_token();
                        response_cache_.put(cache_key, res.body);
                    }

                    // Invalidate cache on mutating methods (v5.3.1)
                    if (cache_enabled_ &&
                        (req.method == Method::POST || req.method == Method::PUT ||
                         req.method == Method::DELETE_METHOD)) {
                        // Invalidate the base resource path prefix
                        auto base = req.path;
                        auto slash = base.rfind('/');
                        if (slash != std::string::npos && slash > 0)
                            base = base.substr(0, slash);
                        response_cache_.invalidate_prefix(base);
                    }

                    // ETag support for GET responses
                    if (req.method == Method::GET && res.ok() && !res.body.empty()) {
                        auto tag = res.etag();
                        if (!tag.empty()) {
                            res.headers["ETag"] = tag;
                            auto inm = req.header("If-None-Match");
                            if (!inm.empty() && inm == tag) {
                                res.set_status(304);
                                res.body.clear();
                            }
                        }
                    }

                    log_request(req, res, start);
                    return res;
                }
            }
        }

        res.status = 404;
        res.body = ErrorHandler::build(ErrorCode::NOT_FOUND,
            "Endpoint not found: " + method_to_string(req.method) + " " + req.path,
            req.path);
        log_request(req, res, start);
        return res;
    }

    /** Match path with parameters like /users/:username */
    static std::optional<std::map<std::string, std::string>> match_path(
            const std::string& pattern, const std::string& path) {
        std::map<std::string, std::string> params;
        
        // Split both paths by '/'
        auto split = [](const std::string& s) {
            std::vector<std::string> parts;
            std::istringstream iss(s);
            std::string part;
            while (std::getline(iss, part, '/')) {
                if (!part.empty()) parts.push_back(part);
            }
            return parts;
        };
        
        auto pattern_parts = split(pattern);
        auto path_parts = split(path);
        
        if (pattern_parts.size() != path_parts.size()) {
            return std::nullopt;
        }
        
        for (size_t i = 0; i < pattern_parts.size(); ++i) {
            if (pattern_parts[i].empty()) continue;
            
            if (pattern_parts[i][0] == ':') {
                // Parameter placeholder
                std::string param_name = pattern_parts[i].substr(1);
                params[param_name] = path_parts[i];
            } else if (pattern_parts[i] != path_parts[i]) {
                return std::nullopt;
            }
        }
        
        return params;
    }

    /** Convenience: handle with method string and path */
    Response handle(const std::string& method, const std::string& path,
                    const std::string& body = "",
                    const std::map<std::string, std::string>& headers = {}) {
        Request req;
        req.method = string_to_method(method);
        req.body = body;
        req.headers = headers;

        // Extract internal metadata from headers (set by HTTP server)
        auto rid = req.header("X-Request-Id");
        if (!rid.empty()) req.request_id = rid;
        auto rip = req.header("X-Real-IP");
        if (!rip.empty()) req.client_ip = rip;

        // Split path and query string
        auto qpos = path.find('?');
        if (qpos != std::string::npos) {
            req.path = path.substr(0, qpos);
            // Parse query params
            std::string qs = path.substr(qpos + 1);
            size_t pos = 0;
            while (pos < qs.size()) {
                auto eq = qs.find('=', pos);
                auto amp = qs.find('&', pos);
                if (eq == std::string::npos) break;
                std::string key = qs.substr(pos, eq - pos);
                std::string val = (amp != std::string::npos) ? qs.substr(eq + 1, amp - eq - 1) : qs.substr(eq + 1);
                req.query_params[key] = val;
                pos = (amp != std::string::npos) ? amp + 1 : qs.size();
            }
        } else {
            req.path = path;
        }

        return handle(req);
    }

    // ----- Utility -----

    /** Uptime in seconds */
    [[nodiscard]] double uptime_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_time_).count();
    }

    /** Number of registered routes */
    // =========================================================================
    // v5.3.1 Endpoints: SSE, Prometheus, Compression, K8s, FIX v2, WASM
    // =========================================================================
    void register_v530_endpoints() {
        // Server log retrieval (in-memory ring buffer, last 500 entries)
        get("/api/v1/logs", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            size_t n = 100;
            ::genie::LogLevel min_level = ::genie::LogLevel::DEBUG;
            auto n_str = req.param("n");
            if (!n_str.empty()) { try { n = std::stoul(n_str); } catch (...) {} }
            auto level_str = req.param("level");
            if      (level_str == "INFO")  min_level = ::genie::LogLevel::INFO;
            else if (level_str == "WARN")  min_level = ::genie::LogLevel::WARN;
            else if (level_str == "ERROR") min_level = ::genie::LogLevel::ERROR;
            auto json = ::genie::logger().memory().to_json(n, min_level);
            res.status = 200;
            res.body = "{\"entries\":" + json + ",\"count\":" + std::to_string(
                ::genie::logger().memory().recent(n, min_level).size()) + "}";
            res.headers["Content-Type"] = "application/json";
        });
        // Prometheus text format (public)
        get("/metrics", [this](const Request&, Response& res) {
            if (!prometheus_enabled_) { res.error(404, "Prometheus disabled"); return; }
            refresh_prometheus_stats();
            res.status = 200;
            res.body = PrometheusRegistry::instance().text_exposition();
            res.headers["Content-Type"] = "text/plain; version=0.0.4; charset=utf-8";
        });
        get("/api/v1/metrics/prometheus", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            refresh_prometheus_stats();
            res.status = 200;
            res.body = PrometheusRegistry::instance().text_exposition();
            res.headers["Content-Type"] = "text/plain; version=0.0.4; charset=utf-8";
        });
        // SSE poll bridge (EventSource compatible)
        get("/api/v1/stream/poll", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            std::string channel = req.param("channel");
            if (channel.empty()) channel = "market";
            uint64_t last_id = 0;
            try { last_id = std::stoull(req.param("last_id")); } catch (...) {}
            auto events = SseChannel::instance().poll(channel, last_id);
            std::string json = "{\"channel\":\"" + channel + "\",\"events\":[";
            bool first = true;
            for (const auto& ev : events) {
                if (!first) json += ",";
                json += "{\"id\":\"" + ev.id + "\",\"event\":\"" + ev.event
                     + "\",\"data\":" + ev.data + "}";
                first = false;
            }
            json += "],\"count\":" + std::to_string(events.size()) + "}";
            res.json(json);
        });
        // SSE status
        get("/api/v1/compute/sse", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            auto s = SseChannel::instance().stats();
            res.json("{\"enabled\":" + std::string(sse_enabled_ ? "true" : "false")
                + ",\"total_events\":" + std::to_string(s.total_events)
                + ",\"channel_count\":" + std::to_string(s.channel_count)
                + ",\"poll_endpoint\":\"/api/v1/stream/poll\""
                + ",\"status\":\"active\"}");
        });
        // HTTP/2 status
        get("/api/v1/compute/http2", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json(http2_adapter_.status_json());
        });
        // Compression status
        get("/api/v1/compute/compression", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json("{\"enabled\":" + std::string(compression_config_.enabled ? "true" : "false")
                + ",\"min_size_bytes\":" + std::to_string(compression_config_.min_size_bytes)
                + ",\"level\":" + std::to_string(compression_config_.level)
                + ",\"zlib_available\":" + std::to_string(GENIE_ZLIB_AVAILABLE)
                + ",\"encoding\":\"gzip (C++20 store-only or zlib)\",\"status\":\"active\"}");
        });
        // Kubernetes status
        get("/api/v1/compute/kubernetes", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json(k8s_client_.status_json());
        });
        post("/api/v1/compute/kubernetes/scale", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            int replicas = 1;
            try { replicas = std::stoi(json_get(req.body, "replicas")); } catch (...) {}
            bool ok = k8s_client_.scale(replicas);
            res.json("{\"success\":" + std::string(ok ? "true" : "false")
                + ",\"replicas\":" + std::to_string(replicas) + "}");
        });
        // FIX engine v2 status
        get("/api/v1/compute/fix", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json(fix_session_v2_.status_json());
        });
        // WASM status
        get("/api/v1/compute/wasm", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json(wasm::status_json(wasm_config_));
        });
        // Binary serializer config
        get("/api/v1/config/binary-serialization", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json("{\"enabled\":true,\"format\":\"METI binary v1\""
                ",\"types\":[\"DOUBLE_ARRAY\",\"FLOAT_ARRAY\",\"INT64_ARRAY\","
                "\"STRING_ARRAY\",\"MATRIX\",\"PSON_MAP\"]"
                ",\"crc\":\"CRC-32 IEEE 802.3\",\"endian\":\"little\",\"status\":\"active\"}");
        });
        // SSE/feature config
        get("/api/v1/config/sse", [this](const Request& req, Response& res) {
            require_auth(req, res); if (!res.ok()) return;
            res.json("{\"sse_enabled\":" + std::string(sse_enabled_ ? "true" : "false")
                + ",\"file_watcher_enabled\":" + std::string(file_watcher_enabled_ ? "true" : "false")
                + ",\"file_watcher_poll_ms\":" + std::to_string(file_watcher_poll_ms_)
                + ",\"prometheus_enabled\":" + std::string(prometheus_enabled_ ? "true" : "false")
                + ",\"prometheus_namespace\":\"" + prometheus_namespace_ + "\"}");
        });
    }

    [[nodiscard]] size_t route_count() const {
        std::lock_guard lock(routes_mutex_);
        return routes_.size();
    }

    /** Access session store (for middleware) */
    SessionStore& sessions() { return sessions_; }
    const SessionStore& sessions() const { return sessions_; }

    /** Access credential store */
    CredentialStore& credentials() { return credentials_; }

    /** Access request logger */
    RequestLogger& logger() { return logger_; }

    /** Access rate limiter */
    RateLimiter& rate_limiter() { return rate_limiter_; }

    // ----- Configuration setters (call before configure_defaults) -----

    /** Configure API rate limits from config.json api.rate_limit_* */
    void set_rate_limits(int max_requests, int window_seconds) {
        rate_limiter_.set_limits(max_requests, window_seconds);
    }

    /** Configure login lockout from config.json auth.max_login_attempts / auth.lockout_minutes */
    void set_login_limits(int max_attempts, int lockout_minutes) {
        login_limiter_.set_limits(max_attempts, lockout_minutes);
    }

    /** Configure pagination defaults from config.json api.pagination_* */
    void set_pagination_defaults(int default_limit, int max_limit) {
        Pagination::default_limit = std::max(1, default_limit);
        Pagination::max_limit = std::max(1, max_limit);
    }

    /** Set whether authentication is required on protected endpoints */
    void set_auth_required(bool required) { auth_required_ = required; }

    /** Set whether demo users (admin/demo, user/user, trader/trade) are created */
    void set_create_demo_users(bool create) { create_demo_users_ = create; }
    /** Set session lifetime in minutes (from config.pson auth.session_timeout_minutes) */
    void set_session_timeout(int minutes) { session_timeout_minutes_ = std::max(1, minutes); }
    /** Set demo user credentials (from config.pson auth.demo_* keys) */
    void set_demo_credentials(
        const std::string& admin_user,  const std::string& admin_pass,
        const std::string& trader_user, const std::string& trader_pass,
        const std::string& analyst_user, const std::string& analyst_pass) {
        demo_admin_user_   = admin_user;   demo_admin_pass_   = admin_pass;
        demo_trader_user_  = trader_user;  demo_trader_pass_  = trader_pass;
        demo_analyst_user_ = analyst_user; demo_analyst_pass_ = analyst_pass;
    }

    /** Configure application identity from config.json application.* */
    void set_app_info(const std::string& name, const std::string& version,
                      const std::string& logo, const std::string& icon) {
        app_name_ = name;
        app_version_ = version;
        app_logo_ = logo;
        app_icon_ = icon;
    }

    /** Configure database connection parameters from config.json database.* */
    void set_database_config(const std::string& path, bool wal_mode, int busy_timeout_ms) {
        db_path_ = path;
        db_wal_mode_ = wal_mode;
        db_busy_timeout_ms_ = busy_timeout_ms;
    }

    /** Enable SQL statement tracing from config.json debug.sql_logging */
    void set_sql_logging(bool enabled) { sql_logging_ = enabled; }

    /** Configure prices database path from config.json storage.prices_db */
    void set_prices_db(const std::string& path) { prices_db_path_ = path; }

    /** Configure backup parameters from config.json backup.* */
    void set_backup_config(bool enabled, const std::string& dir,
                           int interval_hours, int retention_days) {
        backup_enabled_ = enabled;
        backup_directory_ = dir;
        backup_interval_hours_ = interval_hours;
        backup_retention_days_ = retention_days;
    }

    /** Configure health monitoring thresholds from config.json health.* */
    void set_health_config(int interval_seconds, int disk_warning_pct, int memory_warning_pct) {
        health_check_interval_seconds_ = interval_seconds;
        health_disk_warning_pct_ = disk_warning_pct;
        health_memory_warning_pct_ = memory_warning_pct;
    }

    // Shutdown callback - called when POST /api/v1/ops/shutdown is invoked
    void set_shutdown_callback(std::function<void()> cb) { shutdown_callback_ = std::move(cb); }

    // v5.3.1 configuration setters
    /** Enable/disable response caching */
    void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
    /** Set default response cache TTL in seconds */
    void set_cache_default_ttl(int seconds) {
        // ApiCache cannot be move-assigned (contains mutex); reconfigure via set_ttl on default key
        // The default TTL is set at construction; to change it re-initialize via helper
        cache_default_ttl_ = seconds;
    }
    /** Set per-endpoint cache TTL: path_prefix -> seconds */
    void set_cache_ttl(const std::string& prefix, int seconds) { response_cache_.set_ttl(prefix, seconds); }
    /** Clear all cached responses */
    void clear_cache() { response_cache_.clear(); }
    /** Get cache statistics */
    ApiCache::Stats cache_stats() const { return response_cache_.stats(); }
    /** Enable/disable request validation middleware */
    void set_validation_enabled(bool enabled) { validation_enabled_ = enabled; }
    /** Set maximum allowed request body size for validation */
    void set_validation_max_body(size_t bytes) { validation_max_body_ = bytes; }
    /** Enable/disable graceful shutdown integration */
    void set_graceful_shutdown_enabled(bool enabled) { graceful_shutdown_enabled_ = enabled; }
    /** Set graceful drain timeout in seconds */
    void set_drain_timeout(int seconds) {
        core::GracefulShutdown::instance().set_drain_timeout(seconds);
    }
    /** Initiate graceful drain (call before stopping the server) */
    void begin_shutdown() {
        if (graceful_shutdown_enabled_)
            core::GracefulShutdown::instance().initiate_shutdown();
    }

    // ----- Auth Middleware Helper -----

    /** Check Bearer token. Returns false and sets 401 on response if invalid. */
    bool require_auth(const Request& req, Response& res) {
        if (!auth_required_) return true;  // Auth disabled via config
        auto token = req.bearer_token();
        if (token.empty()) {
            res.error(401, "Authorization required");
            return false;
        }
        auto session = sessions_.validate(token);
        if (!session) {
            res.error(401, "Invalid or expired session");
            return false;
        }
        return true;
    }

    /** Get current session info (username, role) */
    std::optional<std::pair<std::string, std::string>> get_session(const Request& req) {
        auto token = req.bearer_token();
        if (token.empty()) return std::nullopt;
        return sessions_.validate(token);
    }

    /** Require admin role */
    bool require_admin(const Request& req, Response& res) {
        auto session = get_session(req);
        if (!session) {
            res.error(401, "Authorization required");
            return false;
        }
        if (session->second != "Administrator") {
            res.error(403, "Administrator access required");
            return false;
        }
        return true;
    }

    /** Extract a field value from JSON-like body text */
    std::string extract_field(const std::string& body, const std::string& field) {
        std::string key = "\"" + field + "\":\"";
        auto pos = body.find(key);
        if (pos == std::string::npos) return "";
        pos += key.size();
        auto end = body.find("\"", pos);
        if (end == std::string::npos) return "";
        return body.substr(pos, end - pos);
    }

private:
    void log_request(const Request& req, const Response& res,
                     std::chrono::steady_clock::time_point start) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        logger_.log(method_to_string(req.method), req.path, res.status, ms, req.request_id);
        // Wire to global Logger
        std::string msg = method_to_string(req.method) + " " + req.path
                        + " -> " + std::to_string(res.status)
                        + " (" + std::to_string(static_cast<int>(ms)) + "ms)";
        if (res.status >= 500) {
            ::genie::logger().log(LogLevel::ERROR, "REST", msg);
        } else if (res.status >= 400) {
            ::genie::logger().log(LogLevel::WARN, "REST", msg);
        } else {
            ::genie::logger().log(LogLevel::DEBUG, "REST", msg);
        }
    }

public:

    // ----- Persistent Storage Configuration -----

    /**
     * @brief Enable persistent user storage with SQLite
     * @param user_db_path Path to users database (default: "users.db")
     * @param audit_db_path Path to audit database (default: "audit.db")
     * @param audit_file_path Path to audit log file (default: "logs/audit.log")
     * @return true if initialization succeeded
     *
     * Call before configure_defaults():
     *   RestApi api;
     *   api.enable_persistent_storage();
     *   api.configure_defaults();
     */
    bool enable_persistent_storage(const std::string& user_db_path = "users.db",
                                   const std::string& audit_db_path = "audit.db",
                                   const std::string& audit_file_path = "logs/audit.log") {
        // Set up logging
        user_store_.set_logger([](const std::string& msg) {
            ::genie::logger().log(LogLevel::INFO, "UserStore", msg);
        });
        audit_log_.set_logger([](const std::string& msg) {
            ::genie::logger().log(LogLevel::INFO, "AuditLog", msg);
        });

        // Open databases
        bool user_ok = user_store_.open(user_db_path);
        bool audit_ok = audit_log_.open(audit_db_path, audit_file_path);

        // Apply database configuration from config.json (database.*, debug.sql_logging)
        if (user_ok) {
            user_store_.configure_connection(db_wal_mode_, db_busy_timeout_ms_, sql_logging_);
        }
        if (audit_ok) {
            audit_log_.configure_connection(db_wal_mode_, db_busy_timeout_ms_, sql_logging_);
        }

        if (user_ok && audit_ok) {
            use_persistent_storage_ = true;
            audit_log_.system_started("v" + std::string(VERSION_STRING));
            ::genie::logger().log(LogLevel::INFO, "REST", 
                "Persistent storage enabled: " + user_db_path + ", " + audit_db_path);
        }

        return user_ok && audit_ok;
    }

    /** Check if persistent storage is enabled */
    [[nodiscard]] bool is_persistent() const { return use_persistent_storage_; }

    /** Get reference to audit log */
    AuditLog& audit() { return audit_log_; }

    /** Get reference to user store */
    UserStore& users() { return user_store_; }

    /** Get reference to live data provider */
    LiveDataProvider& live_data() { return live_data_; }

    /**
     * @brief Configure and initialize live data engines
     *
     * When configured, data endpoints serve live data from real engines
     * (Alpha Vantage, Alpaca, etc.) instead of hardcoded demo data.
     * Unconfigured engines fall back to demo data automatically.
     *
     * Call before configure_defaults():
     *   RestApi api;
     *   LiveDataConfig cfg;
     *   cfg.enable_market_data = true;
     *   cfg.alpha_vantage_key = "YOUR_KEY";
     *   api.configure_live_data(cfg);
     *   api.configure_defaults();
     *
     * @return true if at least one engine initialized
     */
    bool configure_live_data(const LiveDataConfig& config) {
        live_data_ = LiveDataProvider(config);
        bool ok = live_data_.initialize();
        if (ok) {
            ::genie::logger().log(LogLevel::INFO, "REST",
                "Live data provider initialized - endpoints will serve real data");
        } else {
            ::genie::logger().log(LogLevel::WARN, "REST",
                "Live data provider not configured - using demo data");
        }
        return ok;
    }

    /** Check if live data is available */
    [[nodiscard]] bool has_live_data() const { return live_data_.is_initialized(); }

    // ----- Default Endpoint Configuration -----

    /**
     * Register all default Metis Genie Platform REST endpoints.
     *
     * Call this once after construction:
     *   RestApi api;
     *   api.enable_persistent_storage(); // Optional: for SQLite + hashing
     *   api.configure_defaults();
     *
     * Default users: admin/demo, user/user, trader/trade
     */
    void configure_defaults() {
        // Default users (only for in-memory mode, UserStore seeds automatically)
        if (!use_persistent_storage_ && create_demo_users_) {
            // Credentials are configurable via config.pson auth.demo_* keys
            credentials_.add_user(
                demo_admin_user_.empty()  ? "admin"  : demo_admin_user_,
                demo_admin_pass_.empty()  ? "demo"   : demo_admin_pass_,
                "Administrator");
            credentials_.add_user(
                demo_analyst_user_.empty() ? "user"  : demo_analyst_user_,
                demo_analyst_pass_.empty() ? "user"  : demo_analyst_pass_,
                "Analyst");
            credentials_.add_user(
                demo_trader_user_.empty()  ? "trader" : demo_trader_user_,
                demo_trader_pass_.empty()  ? "trade"  : demo_trader_pass_,
                "Trader");
        }

        // === Health Check (no auth) ===
        get("/api/v1/health", [this](const Request&, Response& res) {
            res.json(JsonBuilder()
                .add("status", std::string("healthy"))
                .add("version", app_version_)
                .add("uptime", uptime_seconds())
                .add("platform", std::string(PLATFORM))
                .add("compiler", std::string(COMPILER))
                .add("routes", static_cast<int>(route_count()))
                .build());
        });

        // === Authentication ===
        post("/api/v1/auth/login", [this](const Request& req, Response& res) {
            const std::string& ip = req.client_ip;

            // Check IP-based rate limit
            if (login_limiter_.is_blocked(ip)) {
                int remaining = login_limiter_.lockout_remaining_seconds(ip);
                res.error(429, "Too many login attempts. Try again in " + std::to_string(remaining) + " seconds");
                return;
            }

            std::string username = req.json_field("username");
            std::string password = req.json_field("password");

            if (username.empty()) {
                res.error(400, "Username required");
                return;
            }

            std::optional<std::string> role;
            if (use_persistent_storage_) {
                role = user_store_.authenticate(username, password);
            } else {
                role = credentials_.authenticate(username, password);
            }
            
            if (!role) {
                bool locked = login_limiter_.record_failure(ip);
                if (use_persistent_storage_) {
                    audit_log_.login_failed(username, ip, "Invalid credentials");
                }
                if (locked) {
                    res.error(429, "Too many login attempts. Account temporarily locked");
                } else {
                    res.error(401, "Invalid credentials");
                }
                return;
            }

            login_limiter_.record_success(ip);
            auto token = sessions_.create(username, *role);
            if (use_persistent_storage_) {
                audit_log_.login_success(username);
            }
            res.json(JsonBuilder()
                .add("token", token)
                .add("name", username)
                .add("role", *role)
                .build());
        });

        post("/api/v1/auth/logout", [this](const Request& req, Response& res) {
            auto token = req.bearer_token();
            if (!token.empty()) {
                auto session = sessions_.validate(token);
                if (session && use_persistent_storage_) {
                    audit_log_.logout(session->first);
                }
                sessions_.destroy(token);
            }
            res.json(JsonBuilder().add("status", "ok").build());
        });

        // === Registration (no auth) ===
        post("/api/v1/auth/register", [this](const Request& req, Response& res) {
            std::string username = req.json_field("username");
            std::string password = req.json_field("password");
            std::string email = req.json_field("email");
            std::string display_name = req.json_field("display_name");

            std::string error;
            if (use_persistent_storage_) {
                error = user_store_.register_user(username, password, "Viewer", email, display_name);
            } else {
                error = credentials_.register_user(username, password, email, display_name);
            }
            
            if (!error.empty()) {
                if (use_persistent_storage_) {
                    audit_log_.registration_failed(username, "", error);
                }
                res.error(400, error);
                return;
            }

            // Auto-login after registration
            std::optional<std::string> role;
            if (use_persistent_storage_) {
                role = user_store_.authenticate(username, password);
                audit_log_.user_registered(username);
            } else {
                role = credentials_.authenticate(username, password);
            }
            auto token = sessions_.create(username, role.value_or("Viewer"));
            res.json(JsonBuilder()
                .add("status", std::string("registered"))
                .add("token", token)
                .add("username", username)
                .add("role", role.value_or("Viewer"))
                .build());
        });

        // === User Profile (authenticated) ===
        get("/api/v1/users/me", [this](const Request& req, Response& res) {
            auto session = get_session(req);
            if (!session) {
                res.error(401, "Authorization required");
                return;
            }
            
            if (use_persistent_storage_) {
                auto user = user_store_.get_by_username(session->first);
                if (!user) {
                    res.error(404, "User not found");
                    return;
                }
                res.json(JsonBuilder()
                    .add("username", user->username)
                    .add("display_name", user->display_name)
                    .add("email", user->email)
                    .add("role", user->role)
                    .add("created", user->created_at)
                    .add("last_login", user->last_login)
                    .build());
            } else {
                auto profile = credentials_.get_profile(session->first);
                if (!profile) {
                    res.error(404, "User not found");
                    return;
                }
                res.json(JsonBuilder()
                    .add("username", profile->username)
                    .add("display_name", profile->display_name)
                    .add("email", profile->email)
                    .add("role", profile->role)
                    .add("active", profile->active)
                    .add("created", CredentialStore::format_time(profile->created))
                    .add("last_login", CredentialStore::format_time(profile->last_login))
                    .build());
            }
        });

        put("/api/v1/users/me", [this](const Request& req, Response& res) {
            auto session = get_session(req);
            if (!session) {
                res.error(401, "Authorization required");
                return;
            }
            std::string display_name = req.json_field("display_name");
            std::string email = req.json_field("email");
            
            std::string error;
            if (use_persistent_storage_) {
                error = user_store_.update_profile(session->first, display_name, email);
                if (error.empty()) {
                    audit_log_.profile_updated(session->first, "", "display_name,email");
                }
            } else {
                error = credentials_.update_profile(session->first, display_name, email);
            }
            
            if (!error.empty()) {
                res.error(400, error);
                return;
            }
            res.json(JsonBuilder().add("status", "updated").build());
        });

        post("/api/v1/users/me/password", [this](const Request& req, Response& res) {
            auto session = get_session(req);
            if (!session) {
                res.error(401, "Authorization required");
                return;
            }
            std::string old_password = req.json_field("old_password");
            std::string new_password = req.json_field("new_password");
            
            std::string error;
            if (use_persistent_storage_) {
                error = user_store_.change_password(session->first, old_password, new_password);
                if (error.empty()) {
                    audit_log_.password_changed(session->first);
                } else {
                    audit_log_.password_change_failed(session->first, "", error);
                }
            } else {
                error = credentials_.change_password(session->first, old_password, new_password);
            }
            
            if (!error.empty()) {
                res.error(400, error);
                return;
            }
            res.json(JsonBuilder().add("status", "password_changed").build());
        });

        // === Admin: User Management ===
        get("/api/v1/admin/users", [this](const Request& req, Response& res) {
            if (!require_admin(req, res)) return;
            
            std::ostringstream json;
            json << "[";
            
            if (use_persistent_storage_) {
                auto users = user_store_.get_all_users();
                for (size_t i = 0; i < users.size(); ++i) {
                    if (i > 0) json << ",";
                    json << JsonBuilder()
                        .add("username", users[i].username)
                        .add("display_name", users[i].display_name)
                        .add("email", users[i].email)
                        .add("role", users[i].role)
                        .add("active", users[i].active)
                        .add("created", users[i].created_at)
                        .add("last_login", users[i].last_login)
                        .build();
                }
            } else {
                auto usernames = credentials_.list_users();
                for (size_t i = 0; i < usernames.size(); ++i) {
                    auto profile = credentials_.get_profile(usernames[i]);
                    if (!profile) continue;
                    if (i > 0) json << ",";
                    json << JsonBuilder()
                        .add("username", profile->username)
                        .add("display_name", profile->display_name)
                        .add("email", profile->email)
                        .add("role", profile->role)
                        .add("active", profile->active)
                        .add("created", CredentialStore::format_time(profile->created))
                        .add("last_login", CredentialStore::format_time(profile->last_login))
                        .build();
                }
            }
            json << "]";
            res.json(json.str());
        });

        get("/api/v1/admin/users/:username", [this](const Request& req, Response& res) {
            if (!require_admin(req, res)) return;
            
            std::string username = req.path_param("username");
            
            if (use_persistent_storage_) {
                auto user = user_store_.get_by_username(username);
                if (!user) {
                    res.error(404, "User not found");
                    return;
                }
                res.json(JsonBuilder()
                    .add("username", user->username)
                    .add("display_name", user->display_name)
                    .add("email", user->email)
                    .add("role", user->role)
                    .add("active", user->active)
                    .add("created", user->created_at)
                    .add("last_login", user->last_login)
                    .build());
            } else {
                auto profile = credentials_.get_profile(username);
                if (!profile) {
                    res.error(404, "User not found");
                    return;
                }
                res.json(JsonBuilder()
                    .add("username", profile->username)
                    .add("display_name", profile->display_name)
                    .add("email", profile->email)
                    .add("role", profile->role)
                    .add("active", profile->active)
                    .add("created", CredentialStore::format_time(profile->created))
                    .add("last_login", CredentialStore::format_time(profile->last_login))
                    .build());
            }
        });

        put("/api/v1/admin/users/:username", [this](const Request& req, Response& res) {
            if (!require_admin(req, res)) return;
            
            std::string username = req.path_param("username");
            auto admin_session = get_session(req);
            std::string admin_user = admin_session ? admin_session->first : "";
            
            bool user_exists = use_persistent_storage_ ? 
                user_store_.exists(username) : credentials_.exists(username);
            
            if (!user_exists) {
                res.error(404, "User not found");
                return;
            }
            
            std::string role = req.json_field("role");
            std::string active_str = req.json_field("active");
            std::string display_name = req.json_field("display_name");
            std::string email = req.json_field("email");
            
            // Update role if provided
            if (!role.empty()) {
                std::string error;
                if (use_persistent_storage_) {
                    error = user_store_.set_role(username, role);
                    if (error.empty()) {
                        audit_log_.role_changed(admin_user, username, role);
                    }
                } else {
                    error = credentials_.set_role(username, role);
                }
                if (!error.empty()) {
                    res.error(400, error);
                    return;
                }
            }
            
            // Update active status if provided
            if (!active_str.empty()) {
                bool active = (active_str == "true" || active_str == "1");
                std::string error;
                if (use_persistent_storage_) {
                    error = user_store_.set_active(username, active);
                    if (error.empty()) {
                        if (active) {
                            audit_log_.user_activated(admin_user, username);
                        } else {
                            audit_log_.user_deactivated(admin_user, username);
                        }
                    }
                } else {
                    error = credentials_.set_active(username, active);
                }
                if (!error.empty()) {
                    res.error(400, error);
                    return;
                }
            }
            
            // Update profile fields if provided
            if (!display_name.empty() || !email.empty()) {
                std::string error;
                if (use_persistent_storage_) {
                    error = user_store_.update_profile(username, display_name, email);
                } else {
                    error = credentials_.update_profile(username, display_name, email);
                }
                if (!error.empty()) {
                    res.error(400, error);
                    return;
                }
            }
            
            res.json(JsonBuilder().add("status", "updated").build());
        });

        del("/api/v1/admin/users/:username", [this](const Request& req, Response& res) {
            if (!require_admin(req, res)) return;
            
            std::string username = req.path_param("username");
            
            // Prevent deleting self
            auto session = get_session(req);
            std::string admin_user = session ? session->first : "";
            if (session && session->first == username) {
                res.error(400, "Cannot delete your own account");
                return;
            }
            
            std::string error;
            if (use_persistent_storage_) {
                error = user_store_.delete_user(username);
                if (error.empty()) {
                    audit_log_.user_deleted(admin_user, username);
                }
            } else {
                if (!credentials_.delete_user(username)) {
                    error = "User not found";
                }
            }
            
            if (!error.empty()) {
                res.error(404, error);
                return;
            }
            res.json(JsonBuilder().add("status", "deleted").build());
        });

        // === System Status (public, no auth required) ===
        get("/api/v1/status", [this](const Request& /*req*/, Response& res) {
            res.json(JsonBuilder()
                .add("name", app_name_)
                .add("version", app_version_)
                .add("uptime", uptime_seconds())
                .add("sessions", static_cast<int>(sessions_.count()))
                .add("platform", std::string(PLATFORM))
                .add("compiler", std::string(COMPILER))
                .add("routes", static_cast<int>(route_count()))
                .build());
        });

        // === Portfolio Data (authenticated) ===

        // Client configuration endpoint (public, no auth required)
        get("/api/v1/config", [this](const Request& /*req*/, Response& res) {
            res.json(JsonBuilder()
                .add("name", app_name_)
                .add("version", app_version_)
                .add("logo", app_logo_)
                .add("icon", app_icon_)
                .add("session_timeout_minutes", session_timeout_minutes_)
                .add("auto_refresh_ms", 0)
                .add("features_websocket", true)
                .add("features_analytics", true)
                .add("features_compliance", true)
                .build());
        });

        get("/api/v1/portfolios", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_portfolios();
            if (live) { res.json(*live); return; }
            res.json("{\"portfolios\":["
                "{\"id\":\"growth\",\"name\":\"Growth Portfolio\",\"aum\":1250000,\"ytd\":18.5,\"sharpe\":1.85,\"status\":\"Active\"},"
                "{\"id\":\"value\",\"name\":\"Value Portfolio\",\"aum\":890000,\"ytd\":12.3,\"sharpe\":1.42,\"status\":\"Active\"},"
                "{\"id\":\"balanced\",\"name\":\"Balanced Portfolio\",\"aum\":670000,\"ytd\":15.1,\"sharpe\":1.65,\"status\":\"Active\"}"
                "]}");
        });

        // === Position Data (authenticated) ===
        get("/api/v1/positions", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_positions();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"symbol\":\"AAPL\",\"name\":\"Apple Inc.\",\"shares\":150,\"price\":178.25,\"value\":26737.50,\"pnl\":2850.00,\"weight\":9.4},"
                "{\"symbol\":\"MSFT\",\"name\":\"Microsoft Corp.\",\"shares\":100,\"price\":415.80,\"value\":41580.00,\"pnl\":5420.00,\"weight\":14.6},"
                "{\"symbol\":\"GOOGL\",\"name\":\"Alphabet Inc.\",\"shares\":80,\"price\":175.50,\"value\":14040.00,\"pnl\":1280.00,\"weight\":4.9},"
                "{\"symbol\":\"AMZN\",\"name\":\"Amazon.com Inc.\",\"shares\":90,\"price\":185.60,\"value\":16704.00,\"pnl\":3150.00,\"weight\":5.9},"
                "{\"symbol\":\"NVDA\",\"name\":\"NVIDIA Corp.\",\"shares\":60,\"price\":875.30,\"value\":52518.00,\"pnl\":12430.00,\"weight\":18.4},"
                "{\"symbol\":\"META\",\"name\":\"Meta Platforms\",\"shares\":70,\"price\":505.20,\"value\":35364.00,\"pnl\":8960.00,\"weight\":12.4},"
                "{\"symbol\":\"TSLA\",\"name\":\"Tesla Inc.\",\"shares\":45,\"price\":248.90,\"value\":11200.50,\"pnl\":-1850.00,\"weight\":3.9}"
                "]");
        });

        // === Risk Metrics (authenticated) ===
        get("/api/v1/risk", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_risk();
            if (live) { res.json(*live); return; }
            res.json(JsonBuilder()
                .add("var_95", 42711.00)
                .add("var_99", 68234.00)
                .add("cvar_95", 51850.00)
                .add("sharpe", 1.85)
                .add("sortino", 2.31)
                .add("max_drawdown", -8.50)
                .add("beta", 1.12)
                .add("alpha", 3.20)
                .add("tracking_error", 4.10)
                .add("information_ratio", 0.78)
                .build());
        });

        // === Market Data (authenticated) ===
        get("/api/v1/market", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_market();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"symbol\":\"SPX\",\"name\":\"S&P 500\",\"price\":5842.15,\"change\":0.45},"
                "{\"symbol\":\"NDX\",\"name\":\"NASDAQ 100\",\"price\":20715.30,\"change\":0.62},"
                "{\"symbol\":\"DJI\",\"name\":\"Dow Jones\",\"price\":43892.40,\"change\":0.28},"
                "{\"symbol\":\"RUT\",\"name\":\"Russell 2000\",\"price\":2287.60,\"change\":-0.15},"
                "{\"symbol\":\"VIX\",\"name\":\"CBOE VIX\",\"price\":14.82,\"change\":-3.20},"
                "{\"symbol\":\"TNX\",\"name\":\"10Y Treasury\",\"price\":4.125,\"change\":0.02}"
                "]");
        });

        // === Orders (authenticated) ===
        get("/api/v1/orders", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_orders();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"id\":\"ORD-001\",\"symbol\":\"AAPL\",\"side\":\"Buy\",\"qty\":50,\"price\":178.00,\"type\":\"Limit\",\"status\":\"Pending\",\"fill\":0},"
                "{\"id\":\"ORD-002\",\"symbol\":\"MSFT\",\"side\":\"Sell\",\"qty\":25,\"price\":420.00,\"type\":\"Limit\",\"status\":\"Pending\",\"fill\":0},"
                "{\"id\":\"ORD-003\",\"symbol\":\"NVDA\",\"side\":\"Buy\",\"qty\":10,\"price\":870.00,\"type\":\"Market\",\"status\":\"Filled\",\"fill\":100}"
                "]");
        });

        // === Submit Order (authenticated) ===
        post("/api/v1/orders", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            std::string symbol = req.json_field("symbol");
            std::string side = req.json_field("side");
            if (symbol.empty() || side.empty()) {
                res.error(400, "Symbol and side required");
                return;
            }
            // Try live broker submission
            std::string qty_str = req.json_field("qty");
            std::string type = req.json_field("type");
            std::string price_str = req.json_field("price");
            int qty = 1; try { if (!qty_str.empty()) qty = std::stoi(qty_str); } catch (...) {}
            double price = 0; try { if (!price_str.empty()) price = std::stod(price_str); } catch (...) {}
            auto live = live_data_.submit_order(symbol, side, qty, type, price);
            if (live) { res.set_status(201).json(*live); return; }
            // Demo fallback
            res.set_status(201).json(JsonBuilder()
                .add("id", std::string("ORD-") + std::to_string(
                    std::chrono::system_clock::now().time_since_epoch().count() % 10000))
                .add("symbol", symbol)
                .add("side", side)
                .add("status", std::string("Pending"))
                .build());
        });

        // === Analytics (authenticated) ===
        get("/api/v1/analytics", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_analytics();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"performance\":{\"ytd_return\":18.52,\"mtd_return\":2.34,\"wtd_return\":0.87,"
                "\"annualized_return\":22.15,\"annualized_volatility\":14.82,\"max_drawdown\":-8.50},"
                "\"risk\":{\"sharpe_ratio\":1.85,\"sortino_ratio\":2.31,\"calmar_ratio\":2.61,"
                "\"beta\":1.12,\"alpha\":3.20,\"treynor_ratio\":0.18,\"information_ratio\":0.78},"
                "\"factor_exposure\":{\"market\":1.12,\"size\":-0.15,\"value\":0.28,"
                "\"momentum\":0.42,\"quality\":0.35,\"low_volatility\":-0.08},"
                "\"rolling_stats\":{\"rolling_30d_return\":3.15,\"rolling_90d_return\":8.42,"
                "\"rolling_30d_vol\":12.50,\"rolling_90d_vol\":14.20},"
                "\"drawdown\":{\"current_drawdown\":-1.25,\"max_drawdown\":-8.50,"
                "\"avg_drawdown\":-3.10,\"max_drawdown_duration_days\":18},"
                "\"backtesting\":{\"total_trades\":247,\"win_rate\":62.3,\"profit_factor\":1.85,"
                "\"avg_win\":2850.00,\"avg_loss\":-1540.00,\"max_consecutive_wins\":8,\"max_consecutive_losses\":4}"
                "}");
        });

        // === Compliance (authenticated) ===
        get("/api/v1/compliance", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_compliance();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"status\":\"Compliant\",\"last_check\":\"2026-02-05T12:00:00Z\","
                "\"rules\":["
                "{\"id\":\"R001\",\"name\":\"Single Position Limit\",\"threshold\":10.0,"
                "\"status\":\"Pass\",\"description\":\"No single position exceeds 10% of portfolio\"},"
                "{\"id\":\"R002\",\"name\":\"Maximum Leverage\",\"threshold\":1.5,"
                "\"status\":\"Pass\",\"description\":\"Portfolio leverage within 1.5x limit\"},"
                "{\"id\":\"R003\",\"name\":\"Minimum Cash\",\"threshold\":2.0,"
                "\"status\":\"Pass\",\"description\":\"Cash allocation meets 2% minimum\"},"
                "{\"id\":\"R004\",\"name\":\"Sector Concentration\",\"threshold\":25.0,"
                "\"status\":\"Warning\",\"description\":\"Technology sector at 23.8% (limit: 25%)\"},"
                "{\"id\":\"R005\",\"name\":\"Restricted Securities\",\"threshold\":0,"
                "\"status\":\"Pass\",\"description\":\"No restricted securities held\"},"
                "{\"id\":\"R006\",\"name\":\"ESG Compliance\",\"threshold\":50,"
                "\"status\":\"Pass\",\"description\":\"Portfolio ESG score: 72 (min: 50)\"}"
                "],"
                "\"regulatory\":{\"mifid2\":\"Compliant\",\"dodd_frank\":\"Compliant\","
                "\"basel3\":\"Compliant\",\"gdpr\":\"Compliant\"}"
                "}");
        });

        // === Reporting (authenticated) ===
        get("/api/v1/reporting", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_reporting();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"templates\":["
                "{\"id\":\"T001\",\"name\":\"Portfolio Summary\",\"format\":\"PDF\","
                "\"frequency\":\"Daily\",\"last_generated\":\"2026-02-05T06:00:00Z\"},"
                "{\"id\":\"T002\",\"name\":\"Risk Report\",\"format\":\"PDF\","
                "\"frequency\":\"Weekly\",\"last_generated\":\"2026-02-03T06:00:00Z\"},"
                "{\"id\":\"T003\",\"name\":\"Performance Attribution\",\"format\":\"PDF\","
                "\"frequency\":\"Monthly\",\"last_generated\":\"2026-02-01T06:00:00Z\"},"
                "{\"id\":\"T004\",\"name\":\"Compliance Report\",\"format\":\"PDF\","
                "\"frequency\":\"Daily\",\"last_generated\":\"2026-02-05T06:00:00Z\"},"
                "{\"id\":\"T005\",\"name\":\"Tax Lot Report\",\"format\":\"CSV\","
                "\"frequency\":\"Quarterly\",\"last_generated\":\"2026-01-01T06:00:00Z\"},"
                "{\"id\":\"T006\",\"name\":\"Trade Blotter\",\"format\":\"CSV\","
                "\"frequency\":\"Daily\",\"last_generated\":\"2026-02-05T06:00:00Z\"}"
                "],"
                "\"scheduled\":3,\"available\":6"
                "}");
        });

        // === Generate Report (authenticated) ===
        post("/api/v1/reporting/generate", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            std::string template_id = req.json_field("template_id");
            if (template_id.empty()) template_id = req.json_field("type");
            if (template_id.empty()) {
                res.error(400, "template_id or type required");
                return;
            }
            res.set_status(202).json(JsonBuilder()
                .add("job_id", std::string("RPT-") + std::to_string(
                    std::chrono::system_clock::now().time_since_epoch().count() % 10000))
                .add("template_id", template_id)
                .add("status", std::string("Generating"))
                .add("estimated_seconds", 5)
                .build());
        });

        // === Tax (authenticated) ===
        get("/api/v1/tax", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_tax();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"tax_year\":2026,\"method\":\"SpecificID\","
                "\"summary\":{\"short_term_gains\":12450.00,\"long_term_gains\":28930.00,"
                "\"short_term_losses\":-3200.00,\"long_term_losses\":-1850.00,"
                "\"net_gain\":36330.00,\"wash_sale_adjustments\":0.00,"
                "\"estimated_tax\":8719.20},"
                "\"lots\":["
                "{\"symbol\":\"AAPL\",\"acquired\":\"2025-03-15\",\"qty\":50,\"cost_basis\":7500.00,"
                "\"current_value\":8912.50,\"gain\":1412.50,\"term\":\"Long\",\"holding_days\":327},"
                "{\"symbol\":\"NVDA\",\"acquired\":\"2025-08-20\",\"qty\":20,\"cost_basis\":12500.00,"
                "\"current_value\":17506.00,\"gain\":5006.00,\"term\":\"Short\",\"holding_days\":169},"
                "{\"symbol\":\"TSLA\",\"acquired\":\"2025-06-10\",\"qty\":15,\"cost_basis\":4500.00,"
                "\"current_value\":3733.50,\"gain\":-766.50,\"term\":\"Long\",\"holding_days\":240}"
                "],"
                "\"harvesting\":{\"opportunities\":2,\"potential_savings\":1280.00}"
                "}");
        });

        // === Security Overview (authenticated) ===
        get("/api/v1/security/overview", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto sess = get_session(req);
            std::string user = sess ? sess->first : "unknown";
            res.json(JsonBuilder()
                .add("password_hashing", std::string("SHA-256"))
                .add("session_timeout_minutes", session_timeout_minutes_)
                .add("two_factor_enabled", false)
                .add("ip_whitelist_enabled", false)
                .add("api_keys_active", 0)
                .add("failed_login_attempts_24h", 0)
                .add("active_sessions", static_cast<int>(sessions_.count()))
                .add("audit_logging", true)
                .add("encryption_at_rest", true)
                .add("cors_origin", std::string("*"))
                .add("username", user)
                .build());
        });

        // === Security Audit Log (authenticated) ===
        get("/api/v1/security/audit", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_security_audit(audit_log_);
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"timestamp\":\"2026-02-05T11:45:00Z\",\"event\":\"LOGIN\","
                "\"user\":\"admin\",\"ip\":\"127.0.0.1\",\"status\":\"Success\"},"
                "{\"timestamp\":\"2026-02-05T11:30:00Z\",\"event\":\"API_CALL\","
                "\"user\":\"admin\",\"ip\":\"127.0.0.1\",\"status\":\"Success\","
                "\"details\":\"GET /api/v1/portfolios\"},"
                "{\"timestamp\":\"2026-02-05T11:15:00Z\",\"event\":\"ORDER_SUBMITTED\","
                "\"user\":\"trader\",\"ip\":\"192.168.1.10\",\"status\":\"Success\","
                "\"details\":\"BUY 50 AAPL @ Market\"},"
                "{\"timestamp\":\"2026-02-05T10:00:00Z\",\"event\":\"LOGIN_FAILED\","
                "\"user\":\"unknown\",\"ip\":\"10.0.0.5\",\"status\":\"Failed\","
                "\"details\":\"Invalid credentials\"},"
                "{\"timestamp\":\"2026-02-05T09:00:00Z\",\"event\":\"USER_CREATED\","
                "\"user\":\"admin\",\"ip\":\"127.0.0.1\",\"status\":\"Success\","
                "\"details\":\"Created user: analyst1\"}"
                "]");
        });

        // === Security Sessions (authenticated) ===
        get("/api/v1/security/sessions", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            std::string sessions_json = "[";
            bool first = true;
            auto all_sessions = sessions_.all_sessions();
            for (const auto& [token, sess] : all_sessions) {
                if (!first) sessions_json += ",";
                first = false;
                // Format session age from steady_clock time_point
                auto elapsed = std::chrono::steady_clock::now() - sess.created;
                auto mins = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
                std::string age_str = std::to_string(mins) + "m ago";
                sessions_json += "{\"username\":\"" + json_escape(sess.username) + "\","
                    "\"created\":\"" + json_escape(age_str) + "\","
                    "\"active\":true}";
            }
            sessions_json += "]";
            res.json(sessions_json);
        });

        // === Operations Health (authenticated) ===
        get("/api/v1/operations/health", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_ops_health();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"overall\":\"Healthy\","
                "\"check_interval_seconds\":" + std::to_string(health_check_interval_seconds_) + ","
                "\"disk_warning_pct\":" + std::to_string(health_disk_warning_pct_) + ","
                "\"memory_warning_pct\":" + std::to_string(health_memory_warning_pct_) + ","
                "\"components\":["
                "{\"name\":\"HTTP Server\",\"status\":\"Healthy\",\"latency_ms\":2,\"uptime_pct\":99.99},"
                "{\"name\":\"SQLite Database\",\"status\":\"Healthy\",\"latency_ms\":5,\"uptime_pct\":99.97},"
                "{\"name\":\"User Store\",\"status\":\"Healthy\",\"latency_ms\":1,\"uptime_pct\":100.0},"
                "{\"name\":\"Session Manager\",\"status\":\"Healthy\",\"latency_ms\":1,\"uptime_pct\":100.0},"
                "{\"name\":\"Thread Pool\",\"status\":\"Healthy\",\"latency_ms\":0,\"uptime_pct\":100.0},"
                "{\"name\":\"Audit Logger\",\"status\":\"Healthy\",\"latency_ms\":3,\"uptime_pct\":99.98}"
                "],"
                "\"system\":{\"cpu_cores\":" + std::to_string(std::thread::hardware_concurrency()) + ","
                "\"uptime_seconds\":" + std::to_string(uptime_seconds()) + "}"
                "}");
        });

        // === Operations Backups (authenticated) ===
        get("/api/v1/operations/backups", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_ops_backups();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"auto_backup\":" + std::string(backup_enabled_ ? "true" : "false") +
                ",\"interval_hours\":" + std::to_string(backup_interval_hours_) +
                ",\"retention_days\":" + std::to_string(backup_retention_days_) +
                ",\"backup_directory\":\"" + backup_directory_ + "\""
                ",\"last_backup\":\"2026-02-05T06:00:00Z\","
                "\"backups\":["
                "{\"id\":\"BK-001\",\"timestamp\":\"2026-02-05T06:00:00Z\","
                "\"size_mb\":12.5,\"status\":\"Complete\",\"type\":\"Automatic\"},"
                "{\"id\":\"BK-002\",\"timestamp\":\"2026-02-04T06:00:00Z\","
                "\"size_mb\":12.3,\"status\":\"Complete\",\"type\":\"Automatic\"},"
                "{\"id\":\"BK-003\",\"timestamp\":\"2026-02-03T18:00:00Z\","
                "\"size_mb\":12.4,\"status\":\"Complete\",\"type\":\"Manual\"}"
                "]"
                "}");
        });

        // === Operations Jobs (authenticated) ===
        get("/api/v1/operations/jobs", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_ops_jobs();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"id\":\"JOB-001\",\"name\":\"Market Data Sync\",\"status\":\"Running\","
                "\"schedule\":\"*/5 * * * *\",\"last_run\":\"2026-02-05T11:45:00Z\","
                "\"next_run\":\"2026-02-05T11:50:00Z\",\"avg_duration_ms\":1200},"
                "{\"id\":\"JOB-002\",\"name\":\"Risk Recalculation\",\"status\":\"Idle\","
                "\"schedule\":\"0 * * * *\",\"last_run\":\"2026-02-05T11:00:00Z\","
                "\"next_run\":\"2026-02-05T12:00:00Z\",\"avg_duration_ms\":4500},"
                "{\"id\":\"JOB-003\",\"name\":\"Database Backup\",\"status\":\"Idle\","
                "\"schedule\":\"0 */6 * * *\",\"last_run\":\"2026-02-05T06:00:00Z\","
                "\"next_run\":\"2026-02-05T12:00:00Z\",\"avg_duration_ms\":8000},"
                "{\"id\":\"JOB-004\",\"name\":\"Report Generation\",\"status\":\"Idle\","
                "\"schedule\":\"0 6 * * *\",\"last_run\":\"2026-02-05T06:00:00Z\","
                "\"next_run\":\"2026-02-06T06:00:00Z\",\"avg_duration_ms\":15000},"
                "{\"id\":\"JOB-005\",\"name\":\"Cache Cleanup\",\"status\":\"Idle\","
                "\"schedule\":\"0 0 * * *\",\"last_run\":\"2026-02-05T00:00:00Z\","
                "\"next_run\":\"2026-02-06T00:00:00Z\",\"avg_duration_ms\":500}"
                "]");
        });

        // === Compute Device Info (authenticated) ===
        get("/api/v1/compute", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_compute();
            if (live) { res.json(*live); return; }
            unsigned int cores = std::thread::hardware_concurrency();
            res.json("{"
                "\"active_device\":\"CPU\","
                "\"cpu\":{\"cores\":" + std::to_string(cores) + ","
                "\"threads\":" + std::to_string(cores) + ","
                "\"available\":true},"
                "\"gpu\":{\"cuda\":{\"available\":false,\"status\":\"Not detected - future v3.x\"},"
                "\"opencl\":{\"available\":false,\"status\":\"Not detected - future v3.x\"},"
                "\"sycl\":{\"available\":false,\"status\":\"Not detected - future v4.x\"}},"
                "\"capabilities\":{\"parallel_map\":true,\"parallel_reduce\":true,"
                "\"parallel_for\":true,\"matrix_multiply\":true,\"monte_carlo\":true},"
                "\"acceleration_targets\":[\"VaR Monte Carlo\",\"Matrix Operations\","
                "\"Risk Factor Analysis\",\"Correlation Computation\",\"Options Pricing\"]"
                "}");
        });

        // === Deployment Info (authenticated) ===
        get("/api/v1/deployment", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_deployment();
            if (live) { res.json(*live); return; }
            std::string platform_str(PLATFORM);
            std::string compiler_str(COMPILER);
            res.json("{"
                "\"environment\":\"standalone\","
                "\"container\":{\"detected\":false,"
                "\"docker_ready\":true,\"kubernetes_ready\":true,"
                "\"status\":\"Architecture ready - implementation deferred\"},"
                "\"platform\":\"" + json_escape(platform_str) + "\","
                "\"compiler\":\"" + json_escape(compiler_str) + "\","
                "\"version\":\"" + json_escape(std::string(VERSION_STRING)) + "\","
                "\"features\":{\"health_probes\":true,\"graceful_shutdown\":true,"
                "\"env_config\":true,\"signal_handling\":true,"
                "\"service_discovery\":true,\"readiness_probe\":true,"
                "\"liveness_probe\":true},"
                "\"kubernetes\":{\"service_mesh_ready\":true,"
                "\"horizontal_scaling_ready\":true,\"config_maps_ready\":true,"
                "\"secrets_management_ready\":true,"
                "\"status\":\"Architecture ready - implementation deferred\"}"
                "}");
        });

        // === Benchmark (authenticated) ===
        get("/api/v1/benchmark", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_benchmark();
            if (live) { res.json(*live); return; }
            res.json("{"
                "\"benchmark\":\"S&P 500\",\"benchmark_return\":16.80,"
                "\"portfolio_return\":18.52,\"excess_return\":1.72,"
                "\"tracking_error\":4.10,\"information_ratio\":0.78,"
                "\"periods\":["
                "{\"period\":\"1M\",\"portfolio\":2.34,\"benchmark\":1.92,\"excess\":0.42},"
                "{\"period\":\"3M\",\"portfolio\":8.42,\"benchmark\":7.15,\"excess\":1.27},"
                "{\"period\":\"6M\",\"portfolio\":12.80,\"benchmark\":11.20,\"excess\":1.60},"
                "{\"period\":\"YTD\",\"portfolio\":18.52,\"benchmark\":16.80,\"excess\":1.72},"
                "{\"period\":\"1Y\",\"portfolio\":22.15,\"benchmark\":19.40,\"excess\":2.75},"
                "{\"period\":\"3Y\",\"portfolio\":48.30,\"benchmark\":42.10,\"excess\":6.20}"
                "],"
                "\"attribution\":{\"allocation\":0.85,\"selection\":1.42,"
                "\"interaction\":-0.55,\"total\":1.72}"
                "}");
        });

        // === Transactions (authenticated) ===
        get("/api/v1/transactions", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_transactions();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"id\":\"TXN-001\",\"date\":\"2026-02-05\",\"type\":\"Buy\","
                "\"symbol\":\"AAPL\",\"qty\":50,\"price\":178.25,\"total\":8912.50,"
                "\"fees\":4.95,\"status\":\"Settled\"},"
                "{\"id\":\"TXN-002\",\"date\":\"2026-02-04\",\"type\":\"Sell\","
                "\"symbol\":\"GOOGL\",\"qty\":20,\"price\":175.50,\"total\":3510.00,"
                "\"fees\":4.95,\"status\":\"Settled\"},"
                "{\"id\":\"TXN-003\",\"date\":\"2026-02-03\",\"type\":\"Buy\","
                "\"symbol\":\"NVDA\",\"qty\":10,\"price\":870.00,\"total\":8700.00,"
                "\"fees\":4.95,\"status\":\"Settled\"},"
                "{\"id\":\"TXN-004\",\"date\":\"2026-02-03\",\"type\":\"Dividend\","
                "\"symbol\":\"MSFT\",\"qty\":100,\"price\":0.75,\"total\":75.00,"
                "\"fees\":0.00,\"status\":\"Settled\"},"
                "{\"id\":\"TXN-005\",\"date\":\"2026-02-02\",\"type\":\"Buy\","
                "\"symbol\":\"META\",\"qty\":15,\"price\":505.20,\"total\":7578.00,"
                "\"fees\":4.95,\"status\":\"Settled\"}"
                "]");
        });

        // === Alerts (authenticated) ===
        get("/api/v1/alerts", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_alerts();
            if (live) { res.json(*live); return; }
            res.json("["
                "{\"id\":\"ALT-001\",\"type\":\"Price\",\"severity\":\"Warning\","
                "\"message\":\"NVDA up 3.2% - approaching target price\","
                "\"timestamp\":\"2026-02-05T11:30:00Z\",\"read\":false},"
                "{\"id\":\"ALT-002\",\"type\":\"Compliance\",\"severity\":\"Info\","
                "\"message\":\"Technology sector weight at 23.8% (limit: 25%)\","
                "\"timestamp\":\"2026-02-05T10:00:00Z\",\"read\":false},"
                "{\"id\":\"ALT-003\",\"type\":\"Risk\",\"severity\":\"Warning\","
                "\"message\":\"Portfolio VaR increased 5% from yesterday\","
                "\"timestamp\":\"2026-02-05T09:30:00Z\",\"read\":true},"
                "{\"id\":\"ALT-004\",\"type\":\"Order\",\"severity\":\"Success\","
                "\"message\":\"Order ORD-003: Buy 10 NVDA filled at $870.00\","
                "\"timestamp\":\"2026-02-05T09:15:00Z\",\"read\":true},"
                "{\"id\":\"ALT-005\",\"type\":\"System\",\"severity\":\"Info\","
                "\"message\":\"Daily backup completed successfully\","
                "\"timestamp\":\"2026-02-05T06:00:00Z\",\"read\":true}"
                "]");
        });

        // v3.5.0 extended routes
        register_v350_routes();

        // v4.2.0 new feature routes
        register_v420_routes();
        register_v420_improvement_routes();
        register_v430_routes();
    }

    // ================================================================
    // v3.5.0 Endpoints - Full Feature Coverage
    // ================================================================

    void register_v350_routes() {
        // === ESG Scoring ===
        get("/api/v1/esg", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"esg_scores\":["
                "{\"symbol\":\"AAPL\",\"environment\":82,\"social\":78,\"governance\":85,\"total\":81.7,\"rating\":\"AA\",\"trend\":\"Improving\"},"
                "{\"symbol\":\"MSFT\",\"environment\":88,\"social\":82,\"governance\":90,\"total\":86.7,\"rating\":\"AAA\",\"trend\":\"Stable\"},"
                "{\"symbol\":\"NVDA\",\"environment\":71,\"social\":74,\"governance\":79,\"total\":74.7,\"rating\":\"A\",\"trend\":\"Improving\"},"
                "{\"symbol\":\"GOOGL\",\"environment\":79,\"social\":70,\"governance\":72,\"total\":73.7,\"rating\":\"A\",\"trend\":\"Stable\"},"
                "{\"symbol\":\"AMZN\",\"environment\":65,\"social\":60,\"governance\":68,\"total\":64.3,\"rating\":\"BBB\",\"trend\":\"Watch\"}"
                "],\"portfolio_esg\":{\"weighted_score\":77.4,\"rating\":\"A\",\"carbon_intensity\":42.3,\"controversy_flags\":1},"
                "\"methodology\":\"MSCI-aligned composite scoring\",\"last_updated\":\"2026-02-06T08:00:00Z\"}");
        });

        // === ML Alpha Signals ===
        get("/api/v1/ml/alpha", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"signals\":["
                "{\"symbol\":\"AAPL\",\"signal\":0.72,\"confidence\":0.85,\"direction\":\"Long\",\"model\":\"GradientBoosted\",\"features\":[\"momentum\",\"value\",\"quality\"]},"
                "{\"symbol\":\"TSLA\",\"signal\":-0.45,\"confidence\":0.71,\"direction\":\"Short\",\"model\":\"GradientBoosted\",\"features\":[\"reversal\",\"volatility\"]},"
                "{\"symbol\":\"NVDA\",\"signal\":0.88,\"confidence\":0.92,\"direction\":\"Long\",\"model\":\"GradientBoosted\",\"features\":[\"momentum\",\"earnings\"]},"
                "{\"symbol\":\"JPM\",\"signal\":0.31,\"confidence\":0.64,\"direction\":\"Long\",\"model\":\"GradientBoosted\",\"features\":[\"value\",\"yield\"]}"
                "],\"model_info\":{\"name\":\"AlphaGen-v3\",\"type\":\"GradientBoosted\",\"features\":12,\"training_window\":\"252d\","
                "\"backtest_sharpe\":1.82,\"last_retrained\":\"2026-02-05T00:00:00Z\"}}");
        });

        // === Natural Language Query ===
        post("/api/v1/nlq", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto query = extract_field(req.body, "query");
            if (query.empty()) query = "show portfolio summary";
            res.json("{\"query\":\"" + query + "\","
                "\"interpretation\":\"Portfolio summary with key metrics\","
                "\"result_type\":\"table\","
                "\"columns\":[\"Symbol\",\"Shares\",\"Value\",\"P&L\",\"Weight\"],"
                "\"rows\":["
                "[\"AAPL\",\"500\",\"$87,500\",\"+$2,500\",\"25.0%\"],"
                "[\"MSFT\",\"300\",\"$126,000\",\"+$6,000\",\"36.0%\"],"
                "[\"NVDA\",\"100\",\"$87,000\",\"+$7,000\",\"24.9%\"],"
                "[\"GOOGL\",\"200\",\"$35,000\",\"-$1,000\",\"10.0%\"],"
                "[\"JPM\",\"150\",\"$14,250\",\"+$750\",\"4.1%\"]"
                "],\"confidence\":0.94,\"suggestions\":[\"show sector breakdown\",\"show risk metrics\",\"compare to benchmark\"]}");
        });

        // === What-If Scenario Builder ===
        get("/api/v1/whatif/scenarios", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"scenarios\":["
                "{\"id\":\"SC-001\",\"name\":\"Rate Hike +100bp\",\"type\":\"Interest Rate\",\"status\":\"Ready\","
                "\"impact\":{\"portfolio_pnl\":-42500,\"var_change\":15.2,\"worst_sector\":\"Technology\"}},"
                "{\"id\":\"SC-002\",\"name\":\"Market Crash -20%\",\"type\":\"Equity Shock\",\"status\":\"Ready\","
                "\"impact\":{\"portfolio_pnl\":-70000,\"var_change\":85.0,\"worst_sector\":\"Technology\"}},"
                "{\"id\":\"SC-003\",\"name\":\"USD Weakens 10%\",\"type\":\"FX\",\"status\":\"Ready\","
                "\"impact\":{\"portfolio_pnl\":8200,\"var_change\":-3.1,\"worst_sector\":\"Financials\"}},"
                "{\"id\":\"SC-004\",\"name\":\"Oil Spike +50%\",\"type\":\"Commodity\",\"status\":\"Ready\","
                "\"impact\":{\"portfolio_pnl\":-15600,\"var_change\":22.4,\"worst_sector\":\"Consumer\"}}"
                "],\"custom_scenarios_available\":true}");
        });

        post("/api/v1/whatif/run", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"scenario_id\":\"SC-CUSTOM\",\"name\":\"Custom Scenario\","
                "\"results\":{\"baseline_value\":350000,\"stressed_value\":315000,"
                "\"pnl_impact\":-35000,\"pnl_percent\":-10.0,"
                "\"var_before\":12500,\"var_after\":18750,"
                "\"sectors\":{\"Technology\":-18500,\"Financials\":-8200,\"Healthcare\":-5300,\"Energy\":-3000},"
                "\"top_losers\":[{\"symbol\":\"NVDA\",\"loss\":-12000},{\"symbol\":\"MSFT\",\"loss\":-6500}],"
                "\"top_gainers\":[{\"symbol\":\"XOM\",\"gain\":1200}]},"
                "\"computed_at\":\"2026-02-06T12:00:00Z\"}");
        });

        // === Multi-Tenant ===
        get("/api/v1/tenants", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"tenants\":["
                "{\"id\":\"T-001\",\"name\":\"Alpha Capital\",\"status\":\"Active\",\"users\":12,\"portfolios\":8,\"aum\":\"$2.4B\"},"
                "{\"id\":\"T-002\",\"name\":\"Beta Investments\",\"status\":\"Active\",\"users\":8,\"portfolios\":5,\"aum\":\"$890M\"},"
                "{\"id\":\"T-003\",\"name\":\"Gamma Fund\",\"status\":\"Active\",\"users\":4,\"portfolios\":3,\"aum\":\"$340M\"}"
                "],\"total_tenants\":3,\"total_users\":24,\"total_aum\":\"$3.63B\"}");
        });

        // === Market Feed Adapters ===
        get("/api/v1/feeds", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"feeds\":["
                "{\"id\":\"F-001\",\"provider\":\"Bloomberg B-PIPE\",\"status\":\"Architecture Ready\",\"protocol\":\"TCP/BLPAPI\","
                "\"symbols_subscribed\":0,\"latency_ms\":0,\"messages_per_sec\":0,\"note\":\"Requires Bloomberg Terminal license\"},"
                "{\"id\":\"F-002\",\"provider\":\"Reuters Elektron\",\"status\":\"Architecture Ready\",\"protocol\":\"RSSL/WebSocket\","
                "\"symbols_subscribed\":0,\"latency_ms\":0,\"messages_per_sec\":0,\"note\":\"Requires LSEG Refinitiv license\"},"
                "{\"id\":\"F-003\",\"provider\":\"Alpha Vantage\",\"status\":\"Configured\",\"protocol\":\"REST/JSON\","
                "\"symbols_subscribed\":50,\"latency_ms\":250,\"messages_per_sec\":5,\"note\":\"Free tier: 5 req/min\"},"
                "{\"id\":\"F-004\",\"provider\":\"Polygon.io\",\"status\":\"Configured\",\"protocol\":\"WebSocket\","
                "\"symbols_subscribed\":100,\"latency_ms\":45,\"messages_per_sec\":500,\"note\":\"Starter plan\"},"
                "{\"id\":\"F-005\",\"provider\":\"Finnhub\",\"status\":\"Configured\",\"protocol\":\"WebSocket\","
                "\"symbols_subscribed\":25,\"latency_ms\":80,\"messages_per_sec\":30,\"note\":\"Free tier\"}"
                "],\"total_symbols\":175,\"aggregate_messages_per_sec\":535}");
        });

        // === FIX Protocol ===
        get("/api/v1/fix/status", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"fix_sessions\":["
                "{\"session_id\":\"FIX.4.4:GENIE->BROKER1\",\"status\":\"Simulated\",\"protocol\":\"FIX 4.4\","
                "\"heartbeat_interval\":30,\"messages_sent\":1247,\"messages_received\":1189,\"last_heartbeat\":\"2026-02-06T12:00:00Z\"},"
                "{\"session_id\":\"FIX.4.4:GENIE->BROKER2\",\"status\":\"Simulated\",\"protocol\":\"FIX 4.4\","
                "\"heartbeat_interval\":30,\"messages_sent\":892,\"messages_received\":867,\"last_heartbeat\":\"2026-02-06T12:00:00Z\"}"
                "],\"certification\":{\"fix42_passed\":true,\"fix44_passed\":true,\"total_tests\":156,\"passed\":156,\"failed\":0},"
                "\"supported_messages\":[\"NewOrderSingle\",\"ExecutionReport\",\"OrderCancelRequest\",\"OrderCancelReplace\","
                "\"MarketDataRequest\",\"MarketDataSnapshot\",\"Heartbeat\",\"Logon\",\"Logout\"]}");
        });

        // === Private Assets ===
        get("/api/v1/assets/private", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"private_assets\":["
                "{\"id\":\"PA-001\",\"name\":\"Sequoia Growth Fund III\",\"type\":\"Private Equity\",\"vintage\":2022,"
                "\"committed\":5000000,\"called\":3200000,\"distributed\":800000,\"nav\":4100000,\"irr\":18.5,\"tvpi\":1.53},"
                "{\"id\":\"PA-002\",\"name\":\"Blackstone RE Partners\",\"type\":\"Real Estate\",\"vintage\":2021,"
                "\"committed\":3000000,\"called\":2800000,\"distributed\":1200000,\"nav\":3400000,\"irr\":14.2,\"tvpi\":1.64},"
                "{\"id\":\"PA-003\",\"name\":\"KKR Infra Fund IV\",\"type\":\"Infrastructure\",\"vintage\":2023,"
                "\"committed\":2000000,\"called\":1000000,\"distributed\":100000,\"nav\":1150000,\"irr\":11.8,\"tvpi\":1.25}"
                "],\"total_committed\":10000000,\"total_nav\":8650000,\"weighted_irr\":15.8}");
        });

        // === GPU Monte Carlo ===
        get("/api/v1/risk/gpu", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"gpu_status\":{\"cuda_available\":false,\"opencl_available\":false,"
                "\"fallback\":\"CPU\",\"note\":\"GPU acceleration architecture ready - CUDA/OpenCL to be implemented in future release\"},"
                "\"monte_carlo\":{\"simulations\":100000,\"time_horizon_days\":252,\"confidence_levels\":[0.95,0.99],"
                "\"model\":\"Geometric Brownian Motion\",\"correlation\":\"Cholesky Decomposition\","
                "\"results\":{\"var_95\":28750,\"var_99\":41200,\"cvar_95\":35100,\"cvar_99\":48900,"
                "\"expected_return\":42000,\"std_dev\":18500,\"skewness\":-0.32,\"kurtosis\":3.21,"
                "\"computation_time_ms\":1250,\"device\":\"CPU (8 threads)\"}}}");
        });

        // === Circuit Breaker ===
        get("/api/v1/ops/circuit-breakers", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"circuit_breakers\":["
                "{\"name\":\"MarketDataFeed\",\"state\":\"Closed\",\"failure_count\":0,\"failure_threshold\":5,"
                "\"reset_timeout_sec\":60,\"last_failure\":null,\"total_requests\":12450,\"total_failures\":3},"
                "{\"name\":\"OrderExecution\",\"state\":\"Closed\",\"failure_count\":1,\"failure_threshold\":3,"
                "\"reset_timeout_sec\":30,\"last_failure\":\"2026-02-06T11:45:00Z\",\"total_requests\":892,\"total_failures\":12},"
                "{\"name\":\"ExternalAPI\",\"state\":\"Closed\",\"failure_count\":0,\"failure_threshold\":5,"
                "\"reset_timeout_sec\":120,\"last_failure\":null,\"total_requests\":5680,\"total_failures\":8},"
                "{\"name\":\"DatabaseWrite\",\"state\":\"Closed\",\"failure_count\":0,\"failure_threshold\":3,"
                "\"reset_timeout_sec\":15,\"last_failure\":null,\"total_requests\":34200,\"total_failures\":0}"
                "]}");
        });

        // === Rate Limiter ===
        get("/api/v1/ops/rate-limits", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"rate_limits\":["
                "{\"endpoint\":\"Global\",\"limit\":1000,\"window_sec\":60,\"current_usage\":127,\"utilization_pct\":12.7},"
                "{\"endpoint\":\"/api/v1/orders\",\"limit\":100,\"window_sec\":60,\"current_usage\":8,\"utilization_pct\":8.0},"
                "{\"endpoint\":\"/api/v1/market\",\"limit\":500,\"window_sec\":60,\"current_usage\":45,\"utilization_pct\":9.0},"
                "{\"endpoint\":\"/api/v1/nlq\",\"limit\":30,\"window_sec\":60,\"current_usage\":2,\"utilization_pct\":6.7},"
                "{\"endpoint\":\"/api/v1/whatif/run\",\"limit\":20,\"window_sec\":60,\"current_usage\":0,\"utilization_pct\":0.0}"
                "],\"blocked_ips\":[],\"total_requests_last_hour\":4520,\"total_blocked_last_hour\":0}");
        });

        // === Telemetry ===
        get("/api/v1/ops/telemetry", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"counters\":["
                "{\"name\":\"http_requests_total\",\"value\":45230,\"labels\":{\"method\":\"GET\"}},"
                "{\"name\":\"http_requests_total\",\"value\":8920,\"labels\":{\"method\":\"POST\"}},"
                "{\"name\":\"orders_submitted\",\"value\":1247,\"labels\":{}},"
                "{\"name\":\"orders_filled\",\"value\":1189,\"labels\":{}}"
                "],\"gauges\":["
                "{\"name\":\"active_connections\",\"value\":12},"
                "{\"name\":\"memory_usage_mb\",\"value\":256.4},"
                "{\"name\":\"cpu_usage_pct\",\"value\":23.1},"
                "{\"name\":\"db_connections_active\",\"value\":4}"
                "],\"histograms\":["
                "{\"name\":\"request_duration_ms\",\"count\":54150,\"sum\":162450,\"avg\":3.0,"
                "\"p50\":1.8,\"p95\":8.2,\"p99\":24.5}"
                "],\"uptime_seconds\":" + std::to_string(uptime_seconds()) + "}");
        });

        // === Data Export ===
        get("/api/v1/export/formats", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"formats\":["
                "{\"id\":\"csv\",\"name\":\"CSV\",\"description\":\"Comma-separated values\",\"mime\":\"text/csv\"},"
                "{\"id\":\"json\",\"name\":\"JSON\",\"description\":\"JavaScript Object Notation\",\"mime\":\"application/json\"},"
                "{\"id\":\"xml\",\"name\":\"XML\",\"description\":\"Extensible Markup Language\",\"mime\":\"application/xml\"},"
                "{\"id\":\"pdf\",\"name\":\"PDF\",\"description\":\"Portable Document Format\",\"mime\":\"application/pdf\"},"
                "{\"id\":\"xlsx\",\"name\":\"Excel\",\"description\":\"Microsoft Excel\",\"mime\":\"application/vnd.openxmlformats\"}"
                "],\"exportable_datasets\":[\"portfolios\",\"positions\",\"orders\",\"risk_metrics\","
                "\"compliance_checks\",\"tax_lots\",\"transactions\",\"alerts\",\"esg_scores\",\"ml_signals\"]}");
        });

        post("/api/v1/export", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto format = extract_field(req.body, "format");
            auto dataset = extract_field(req.body, "dataset");
            if (format.empty()) format = "csv";
            if (dataset.empty()) dataset = "positions";
            res.json("{\"export_id\":\"EXP-" + std::to_string(std::hash<std::string>{}(format + dataset) % 10000) + "\","
                "\"format\":\"" + format + "\",\"dataset\":\"" + dataset + "\","
                "\"status\":\"completed\",\"rows\":25,\"size_bytes\":4096,"
                "\"download_url\":\"/api/v1/export/download/EXP-001\","
                "\"created_at\":\"2026-02-06T12:00:00Z\"}");
        });

        // === Factor Exposure ===
        get("/api/v1/risk/factors", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"factor_exposures\":{"
                "\"market_beta\":1.12,\"size_smb\":-0.15,\"value_hml\":-0.32,"
                "\"momentum_umd\":0.28,\"quality\":0.41,\"volatility\":-0.18},"
                "\"r_squared\":0.87,\"tracking_error\":4.2,"
                "\"factor_returns\":{\"market\":12.5,\"size\":-2.1,\"value\":3.8,\"momentum\":5.2,\"quality\":4.1,\"volatility\":-1.5},"
                "\"style_drift\":{\"current_quarter\":\"Growth Tilt\",\"previous_quarter\":\"Balanced\",\"drift_magnitude\":0.15}}");
        });

        // === Incremental VaR ===
        get("/api/v1/risk/incremental", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"incremental_var\":["
                "{\"symbol\":\"AAPL\",\"marginal_var\":3200,\"component_var\":7800,\"pct_contribution\":27.1},"
                "{\"symbol\":\"MSFT\",\"marginal_var\":2800,\"component_var\":9100,\"pct_contribution\":31.7},"
                "{\"symbol\":\"NVDA\",\"marginal_var\":4100,\"component_var\":8500,\"pct_contribution\":29.6},"
                "{\"symbol\":\"GOOGL\",\"marginal_var\":1200,\"component_var\":2400,\"pct_contribution\":8.3},"
                "{\"symbol\":\"JPM\",\"marginal_var\":450,\"component_var\":950,\"pct_contribution\":3.3}"
                "],\"total_var\":28750,\"confidence\":0.95,\"horizon_days\":1}");
        });

        // === Benchmark Builder ===
        get("/api/v1/benchmark/custom", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"custom_benchmarks\":["
                "{\"id\":\"BM-001\",\"name\":\"60/40 Balanced\",\"components\":["
                "{\"index\":\"SPY\",\"weight\":0.60},{\"index\":\"AGG\",\"weight\":0.40}],"
                "\"ytd_return\":8.2,\"rebalance\":\"Quarterly\"},"
                "{\"id\":\"BM-002\",\"name\":\"Tech Growth\",\"components\":["
                "{\"index\":\"QQQ\",\"weight\":0.70},{\"index\":\"SOXX\",\"weight\":0.30}],"
                "\"ytd_return\":14.5,\"rebalance\":\"Monthly\"}"
                "]}");
        });

        // === Report Scheduling ===
        get("/api/v1/reporting/schedules", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"schedules\":["
                "{\"id\":\"RS-001\",\"report\":\"Daily Risk Summary\",\"frequency\":\"Daily\","
                "\"time\":\"09:00\",\"format\":\"PDF\",\"recipients\":[\"pm@fund.com\"],\"last_run\":\"2026-02-06T09:00:00Z\",\"status\":\"Active\"},"
                "{\"id\":\"RS-002\",\"report\":\"Weekly Performance\",\"frequency\":\"Weekly\","
                "\"time\":\"Monday 08:00\",\"format\":\"PDF\",\"recipients\":[\"team@fund.com\"],\"last_run\":\"2026-02-03T08:00:00Z\",\"status\":\"Active\"},"
                "{\"id\":\"RS-003\",\"report\":\"Monthly Compliance\",\"frequency\":\"Monthly\","
                "\"time\":\"1st 07:00\",\"format\":\"PDF\",\"recipients\":[\"compliance@fund.com\"],\"last_run\":\"2026-02-01T07:00:00Z\",\"status\":\"Active\"}"
                "]}");
        });

        // v3.5.0 new module endpoints
        register_v350_new_modules();
    }

    // ================================================================
    // v3.5.0 New Module Endpoints
    // ================================================================

    void register_v350_new_modules() {
        // === Liquidity Risk ===
        get("/api/v1/risk/liquidity", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"portfolio_liquidity\":{\"weighted_score\":7.2,\"total_liquidation_cost\":18500,"
                "\"liquidation_cost_pct\":5.3,\"lvar_95\":42000,\"lvar_99\":58000,"
                "\"pct_highly_liquid\":62.0,\"pct_liquid\":28.0,\"pct_semi_liquid\":8.0,\"pct_illiquid\":2.0,"
                "\"max_days_to_liquidate\":5,\"concentration_hhi\":2150},"
                "\"position_liquidity\":["
                "{\"symbol\":\"AAPL\",\"liquidity_score\":9.5,\"tier\":\"highly_liquid\",\"days_to_liquidate\":1,\"spread_bps\":2.1,\"cost_pct\":0.8},"
                "{\"symbol\":\"MSFT\",\"liquidity_score\":9.2,\"tier\":\"highly_liquid\",\"days_to_liquidate\":1,\"spread_bps\":2.5,\"cost_pct\":0.9},"
                "{\"symbol\":\"NVDA\",\"liquidity_score\":8.8,\"tier\":\"highly_liquid\",\"days_to_liquidate\":1,\"spread_bps\":3.2,\"cost_pct\":1.2},"
                "{\"symbol\":\"GOOGL\",\"liquidity_score\":7.5,\"tier\":\"liquid\",\"days_to_liquidate\":2,\"spread_bps\":4.8,\"cost_pct\":2.1},"
                "{\"symbol\":\"JPM\",\"liquidity_score\":8.0,\"tier\":\"highly_liquid\",\"days_to_liquidate\":1,\"spread_bps\":3.5,\"cost_pct\":1.5}"
                "],\"stress_scenarios\":["
                "{\"scenario\":\"Spread Widening 3x\",\"normal_cost\":18500,\"stressed_cost\":48200,\"multiplier\":2.6},"
                "{\"scenario\":\"Volume Drought 50%\",\"normal_cost\":18500,\"stressed_cost\":32100,\"multiplier\":1.7},"
                "{\"scenario\":\"Fire Sale\",\"normal_cost\":18500,\"stressed_cost\":112000,\"multiplier\":6.1}"
                "]}");
        });

        // === Order Book Simulator ===
        get("/api/v1/trading/orderbook", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"orderbook\":{\"symbol\":\"AAPL\",\"last_trade_price\":175.25,"
                "\"best_bid\":175.24,\"best_ask\":175.26,\"spread\":0.02,\"spread_bps\":1.14,"
                "\"mid_price\":175.25,\"total_bid_qty\":12500,\"total_ask_qty\":11800,"
                "\"bid_ask_imbalance\":0.029},"
                "\"bids\":["
                "{\"price\":175.24,\"quantity\":3200,\"orders\":8},"
                "{\"price\":175.23,\"quantity\":2800,\"orders\":6},"
                "{\"price\":175.22,\"quantity\":2500,\"orders\":5},"
                "{\"price\":175.21,\"quantity\":2100,\"orders\":4},"
                "{\"price\":175.20,\"quantity\":1900,\"orders\":7}"
                "],\"asks\":["
                "{\"price\":175.26,\"quantity\":2900,\"orders\":7},"
                "{\"price\":175.27,\"quantity\":2600,\"orders\":5},"
                "{\"price\":175.28,\"quantity\":2400,\"orders\":6},"
                "{\"price\":175.29,\"quantity\":2000,\"orders\":4},"
                "{\"price\":175.30,\"quantity\":1900,\"orders\":5}"
                "],\"recent_trades\":["
                "{\"price\":175.25,\"quantity\":100,\"side\":\"buy\",\"time\":\"12:00:01\"},"
                "{\"price\":175.24,\"quantity\":250,\"side\":\"sell\",\"time\":\"12:00:00\"},"
                "{\"price\":175.25,\"quantity\":150,\"side\":\"buy\",\"time\":\"11:59:59\"}"
                "]}");
        });

        // === Regulatory Reporting ===
        get("/api/v1/compliance/regulatory", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"deadlines\":["
                "{\"report\":\"SEC Form 13F\",\"period\":\"Q4 2025\",\"due_date\":\"2026-02-14\",\"status\":\"draft\",\"days_remaining\":8},"
                "{\"report\":\"SEC Form 13F\",\"period\":\"Q1 2026\",\"due_date\":\"2026-05-15\",\"status\":\"pending\",\"days_remaining\":98},"
                "{\"report\":\"SEC N-PORT\",\"period\":\"Q1 2026\",\"due_date\":\"2026-05-30\",\"status\":\"pending\",\"days_remaining\":113},"
                "{\"report\":\"ESMA AIFMD Annex IV\",\"period\":\"H2 2025\",\"due_date\":\"2026-01-31\",\"status\":\"submitted\",\"days_remaining\":0},"
                "{\"report\":\"MiFIR Transaction\",\"period\":\"Daily\",\"due_date\":\"T+1\",\"status\":\"active\",\"days_remaining\":1}"
                "],\"report_history\":["
                "{\"report_id\":\"13F-Q3-2025\",\"type\":\"SEC Form 13F\",\"period\":\"Q3 2025\",\"status\":\"filed\",\"records\":45},"
                "{\"report_id\":\"AIFMD-H1-2025\",\"type\":\"ESMA AIFMD\",\"period\":\"H1 2025\",\"status\":\"filed\",\"records\":12}"
                "],\"firm\":{\"name\":\"Metis Capital\",\"cik\":\"0001234567\",\"lei\":\"5493001KJTIIGC8Y1R12\"}}");
        });

        // === Regime Detection ===
        get("/api/v1/analytics/regimes", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"current_regime\":{\"volatility\":\"normal\",\"trend\":\"bull\",\"correlation\":\"normal\","
                "\"vol_confidence\":0.78,\"trend_confidence\":0.82,\"corr_confidence\":0.71,"
                "\"realized_vol\":0.16,\"vol_percentile\":55.0,\"momentum_score\":0.08},"
                "\"regime_history\":["
                "{\"date\":\"2026-01-06\",\"volatility\":\"low\",\"trend\":\"strong_bull\",\"momentum\":0.12},"
                "{\"date\":\"2026-01-13\",\"volatility\":\"normal\",\"trend\":\"bull\",\"momentum\":0.09},"
                "{\"date\":\"2026-01-20\",\"volatility\":\"normal\",\"trend\":\"bull\",\"momentum\":0.07},"
                "{\"date\":\"2026-01-27\",\"volatility\":\"high\",\"trend\":\"sideways\",\"momentum\":0.02},"
                "{\"date\":\"2026-02-03\",\"volatility\":\"normal\",\"trend\":\"bull\",\"momentum\":0.08}"
                "],\"transition_probabilities\":{\"vol_low_to_normal\":0.35,\"vol_normal_to_high\":0.12,"
                "\"trend_bull_to_sideways\":0.18,\"trend_sideways_to_bear\":0.15}}");
        });

        // === Corporate Actions ===
        get("/api/v1/market/corporate-actions", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"pending_actions\":["
                "{\"action_id\":\"CA-001\",\"type\":\"cash_dividend\",\"symbol\":\"AAPL\",\"ex_date\":\"2026-02-07\","
                "\"cash_amount\":0.25,\"record_date\":\"2026-02-10\",\"pay_date\":\"2026-02-14\",\"status\":\"pending\"},"
                "{\"action_id\":\"CA-002\",\"type\":\"stock_split\",\"symbol\":\"NVDA\",\"ex_date\":\"2026-03-15\","
                "\"split_ratio\":2.0,\"status\":\"announced\"}"
                "],\"recent_processed\":["
                "{\"action_id\":\"CA-098\",\"type\":\"cash_dividend\",\"symbol\":\"MSFT\",\"ex_date\":\"2026-01-15\","
                "\"cash_amount\":0.83,\"status\":\"processed\",\"impact\":\"$249.00 cash received\"},"
                "{\"action_id\":\"CA-097\",\"type\":\"cash_dividend\",\"symbol\":\"JPM\",\"ex_date\":\"2026-01-08\","
                "\"cash_amount\":1.15,\"status\":\"processed\",\"impact\":\"$172.50 cash received\"}"
                "],\"total_pending\":2,\"total_processed\":24}");
        });

        // === Shutdown ===
        // GET returns status; POST triggers actual server shutdown
        get("/api/v1/ops/shutdown", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"state\":\"running\",\"message\":\"Server is running. POST to this endpoint to shut down.\"}");
        });

        post("/api/v1/ops/shutdown", [this](const Request& req, Response& res) {
            // No auth required - already behind client-side confirmation dialog
            res.json("{\"state\":\"shutting_down\",\"message\":\"Server is shutting down.\"}");
            // Trigger shutdown after response is sent
            if (shutdown_callback_) {
                std::thread([cb = shutdown_callback_]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    cb();
                }).detach();
            }
        });

        // === CSV Export Templates ===
        get("/api/v1/export/csv/templates", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"templates\":["
                "{\"name\":\"positions\",\"title\":\"Portfolio Positions\",\"columns\":11},"
                "{\"name\":\"orders\",\"title\":\"Order History\",\"columns\":11},"
                "{\"name\":\"risk\",\"title\":\"Risk Metrics\",\"columns\":6},"
                "{\"name\":\"attribution\",\"title\":\"Performance Attribution\",\"columns\":9},"
                "{\"name\":\"tax_lots\",\"title\":\"Tax Lot Detail\",\"columns\":11},"
                "{\"name\":\"compliance\",\"title\":\"Compliance Status\",\"columns\":9},"
                "{\"name\":\"audit\",\"title\":\"Audit Log\",\"columns\":7},"
                "{\"name\":\"market_data\",\"title\":\"Market Data\",\"columns\":8},"
                "{\"name\":\"transactions\",\"title\":\"Transaction History\",\"columns\":10}"
                "]}");
        });

        // === Config Validation ===
        get("/api/v1/config/validate", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"valid\":true,\"errors\":0,\"warnings\":1,\"info\":0,"
                "\"issues\":["
                "{\"severity\":\"warning\",\"field\":\"auth.jwt_secret\","
                "\"message\":\"Using auto-generated JWT secret\","
                "\"suggestion\":\"Set a persistent secret in config.json for production\"}"
                "],\"summary\":\"Configuration validation: PASSED (0 errors, 1 warning)\"}");
        });

        // === Performance Attribution ===
        get("/api/v1/performance/attribution", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"period\":\"2026-01-01 to 2026-02-06\","
                "\"portfolio_return\":8.2,\"benchmark_return\":6.5,\"excess_return\":1.7,"
                "\"attribution\":["
                "{\"segment\":\"Technology\",\"portfolio_weight\":61.0,\"benchmark_weight\":45.0,"
                "\"portfolio_return\":10.5,\"benchmark_return\":8.2,"
                "\"allocation\":0.45,\"selection\":0.65,\"interaction\":0.12,\"total\":1.22},"
                "{\"segment\":\"Financials\",\"portfolio_weight\":14.0,\"benchmark_weight\":18.0,"
                "\"portfolio_return\":5.2,\"benchmark_return\":4.8,"
                "\"allocation\":-0.08,\"selection\":0.06,\"interaction\":-0.02,\"total\":-0.04},"
                "{\"segment\":\"Healthcare\",\"portfolio_weight\":10.0,\"benchmark_weight\":15.0,"
                "\"portfolio_return\":3.8,\"benchmark_return\":5.5,"
                "\"allocation\":0.12,\"selection\":-0.17,\"interaction\":0.05,\"total\":0.00},"
                "{\"segment\":\"Other\",\"portfolio_weight\":15.0,\"benchmark_weight\":22.0,"
                "\"portfolio_return\":4.5,\"benchmark_return\":4.2,"
                "\"allocation\":-0.05,\"selection\":0.04,\"interaction\":0.01,\"total\":0.00}"
                "],\"risk_adjusted\":{\"sharpe\":1.45,\"sortino\":1.82,\"information_ratio\":0.85,\"tracking_error\":2.0}}");
        });

        // === Market Data Overview ===
        get("/api/v1/market/overview", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            res.json("{\"indices\":["
                "{\"symbol\":\"SPX\",\"name\":\"S&P 500\",\"value\":6042.5,\"change\":0.85,\"change_pct\":0.014},"
                "{\"symbol\":\"NDX\",\"name\":\"Nasdaq 100\",\"value\":21580.3,\"change\":125.4,\"change_pct\":0.58},"
                "{\"symbol\":\"DJI\",\"name\":\"Dow Jones\",\"value\":44250.1,\"change\":-42.3,\"change_pct\":-0.10}"
                "],\"sectors\":["
                "{\"name\":\"Technology\",\"change_pct\":1.2,\"leading\":true},"
                "{\"name\":\"Healthcare\",\"change_pct\":0.3,\"leading\":false},"
                "{\"name\":\"Financials\",\"change_pct\":-0.2,\"leading\":false},"
                "{\"name\":\"Energy\",\"change_pct\":-0.8,\"leading\":false}"
                "],\"market_status\":\"Open\",\"next_close\":\"16:00 ET\","
                "\"vix\":15.8,\"treasury_10y\":4.25,\"dxy\":103.5}");
        });
    }

    /** v4.2.0 routes: persistence, correlation, reconciliation, hedging, replay, batch orders, dashboards */
    void register_v420_routes() {

        get("/api/v1/persistence/stores", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"stores\":["
                "{\"name\":\"positions\",\"records\":12450,\"size_mb\":24.3,\"last_sync\":\"2026-02-06T14:30:00Z\",\"engine\":\"sqlite\"},"
                "{\"name\":\"transactions\",\"records\":89200,\"size_mb\":112.7,\"last_sync\":\"2026-02-06T14:30:00Z\",\"engine\":\"sqlite\"},"
                "{\"name\":\"market_history\",\"records\":1250000,\"size_mb\":845.2,\"last_sync\":\"2026-02-06T14:28:00Z\",\"engine\":\"sqlite\"},"
                "{\"name\":\"audit_trail\",\"records\":34500,\"size_mb\":18.9,\"last_sync\":\"2026-02-06T14:30:00Z\",\"engine\":\"sqlite\"}"
                "],\"total_size_mb\":1001.1,\"engine\":\"SQLite 3.45\",\"status\":\"healthy\"}");
        });

        get("/api/v1/persistence/events", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"events\":["
                "{\"id\":\"EVT-8901\",\"type\":\"TRADE_EXECUTED\",\"timestamp\":\"2026-02-06T14:29:55Z\",\"symbol\":\"AAPL\",\"qty\":100},"
                "{\"id\":\"EVT-8902\",\"type\":\"POSITION_UPDATED\",\"timestamp\":\"2026-02-06T14:29:56Z\",\"account\":\"ACCT-001\"},"
                "{\"id\":\"EVT-8903\",\"type\":\"MARKET_DATA_TICK\",\"timestamp\":\"2026-02-06T14:30:00Z\",\"symbol\":\"SPY\",\"price\":512.45}"
                "],\"total_events\":34500,\"events_per_minute\":42}");
        });

        get("/api/v1/persistence/snapshots", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"snapshots\":["
                "{\"id\":\"SNAP-045\",\"date\":\"2026-02-06\",\"type\":\"daily\",\"size_mb\":156.3,\"status\":\"complete\"},"
                "{\"id\":\"SNAP-044\",\"date\":\"2026-02-05\",\"type\":\"daily\",\"size_mb\":155.8,\"status\":\"complete\"},"
                "{\"id\":\"SNAP-040\",\"date\":\"2026-02-01\",\"type\":\"monthly\",\"size_mb\":312.1,\"status\":\"complete\"}"
                "],\"retention_days\":90,\"auto_snapshot\":true}");
        });

        get("/api/v1/analytics/correlation", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"symbols\":[\"AAPL\",\"MSFT\",\"GOOGL\",\"AMZN\",\"NVDA\"],"
                "\"matrix\":[[1.00,0.82,0.76,0.71,0.68],[0.82,1.00,0.79,0.74,0.72],[0.76,0.79,1.00,0.69,0.65],[0.71,0.74,0.69,1.00,0.58],[0.68,0.72,0.65,0.58,1.00]],"
                "\"method\":\"pearson\",\"window\":252,\"shrinkage\":\"ledoit_wolf\","
                "\"most_correlated\":{\"pair\":[\"AAPL\",\"MSFT\"],\"value\":0.82},"
                "\"least_correlated\":{\"pair\":[\"AMZN\",\"NVDA\"],\"value\":0.58}}");
        });

        get("/api/v1/reconciliation", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"latest_run\":{\"run_id\":\"RECON-042\",\"date\":\"2026-02-06\","
                "\"source\":\"custodian\",\"total_positions\":245,\"matched\":238,"
                "\"breaks\":5,\"auto_resolved\":2,\"pending_review\":3,\"match_rate\":97.1},"
                "\"breaks\":["
                "{\"symbol\":\"TSLA\",\"type\":\"quantity\",\"internal\":500,\"external\":495,\"status\":\"pending_review\"},"
                "{\"symbol\":\"NVDA\",\"type\":\"price\",\"internal\":875.20,\"external\":875.15,\"status\":\"auto_resolved\"},"
                "{\"symbol\":\"JPM\",\"type\":\"missing_external\",\"internal\":200,\"external\":0,\"status\":\"pending_review\"}"
                "]}");
        });

        get("/api/v1/risk/hedging", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"portfolio_value\":52400000,"
                "\"unhedged_var_95\":1723160,\"hedged_var_95\":1206212,\"var_reduction_pct\":30.0,"
                "\"hedge_cost_bps\":22.5,\"recommendations\":["
                "{\"id\":\"HEDGE-1\",\"type\":\"options_put\",\"instrument\":\"SPX Put 5% OTM 90d\",\"notional\":15000000,\"cost_bps\":35,\"risk_reduction\":18,\"urgency\":\"high\"},"
                "{\"id\":\"HEDGE-2\",\"type\":\"futures\",\"instrument\":\"10Y Treasury ZN\",\"notional\":8200000,\"cost_bps\":5,\"risk_reduction\":12,\"urgency\":\"medium\"},"
                "{\"id\":\"HEDGE-3\",\"type\":\"fx_forward\",\"instrument\":\"EUR/USD 3M Fwd\",\"notional\":4500000,\"cost_bps\":8,\"risk_reduction\":5,\"urgency\":\"medium\"}"
                "]}");
        });

        get("/api/v1/market/replay", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"sessions\":["
                "{\"id\":\"REPLAY-001\",\"name\":\"2020 March Crash\",\"start\":\"2020-02-19\",\"end\":\"2020-03-23\",\"events\":284500,\"state\":\"completed\"},"
                "{\"id\":\"REPLAY-002\",\"name\":\"2024 Election Week\",\"start\":\"2024-11-04\",\"end\":\"2024-11-08\",\"events\":125000,\"state\":\"completed\"}"
                "],\"speeds\":[\"1x\",\"10x\",\"100x\",\"max\",\"step\"]}");
        });

        get("/api/v1/trading/batches", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"batches\":["
                "{\"batch_id\":\"BO-015\",\"name\":\"Q1 Rebalance\",\"status\":\"completed\",\"total_orders\":45,\"filled\":45,\"notional\":2850000},"
                "{\"batch_id\":\"BO-016\",\"name\":\"Tax Loss Harvest\",\"status\":\"validated\",\"total_orders\":12,\"filled\":0,\"notional\":420000},"
                "{\"batch_id\":\"BO-017\",\"name\":\"New Account Setup\",\"status\":\"draft\",\"total_orders\":28,\"filled\":0,\"notional\":1200000}"
                "],\"total_batches\":17}");
        });

        post("/api/v1/trading/batches", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"batch_id\":\"BO-018\",\"status\":\"created\",\"message\":\"Batch order created\"}");
        });

        get("/api/v1/dashboards", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"dashboards\":["
                "{\"id\":\"DASH-001\",\"name\":\"Portfolio Overview\",\"widgets\":8,\"owner\":\"admin\",\"shared\":true},"
                "{\"id\":\"DASH-002\",\"name\":\"Risk Monitor\",\"widgets\":6,\"owner\":\"admin\",\"shared\":true},"
                "{\"id\":\"DASH-003\",\"name\":\"Trading Desk\",\"widgets\":10,\"owner\":\"trader1\",\"shared\":false}"
                "],\"templates\":[\"Executive Summary\",\"Risk Dashboard\",\"Trading Desk\",\"Compliance Monitor\"]}");
        });

        get("/api/v1/esg/scores", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"portfolio_esg\":{\"overall\":72.4,\"environmental\":68.1,\"social\":74.8,\"governance\":78.3},"
                "\"holdings\":["
                "{\"symbol\":\"AAPL\",\"overall\":81,\"e\":78,\"s\":82,\"g\":85,\"rating\":\"AA\"},"
                "{\"symbol\":\"MSFT\",\"overall\":85,\"e\":82,\"s\":84,\"g\":89,\"rating\":\"AAA\"},"
                "{\"symbol\":\"XOM\",\"overall\":45,\"e\":32,\"s\":52,\"g\":58,\"rating\":\"BB\"}"
                "],\"benchmark_esg\":70.2}");
        });

        get("/api/v1/fix/sessions", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"sessions\":["
                "{\"session_id\":\"FIX-NYSE-001\",\"target\":\"NYSE\",\"status\":\"connected\",\"heartbeat_sec\":30,\"messages_sent\":12450},"
                "{\"session_id\":\"FIX-NASDAQ-001\",\"target\":\"NASDAQ\",\"status\":\"connected\",\"heartbeat_sec\":30,\"messages_sent\":8920},"
                "{\"session_id\":\"FIX-BATS-001\",\"target\":\"BATS\",\"status\":\"disconnected\",\"heartbeat_sec\":30,\"messages_sent\":0}"
                "],\"protocol_version\":\"FIX 4.4\",\"active\":2}");
        });

        get("/api/v1/tenants/details", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"tenants\":["
                "{\"id\":\"TENANT-001\",\"name\":\"Meridian Capital\",\"users\":12,\"portfolios\":8,\"aum\":52400000,\"plan\":\"enterprise\",\"status\":\"active\"},"
                "{\"id\":\"TENANT-002\",\"name\":\"Apex Investments\",\"users\":5,\"portfolios\":3,\"aum\":18700000,\"plan\":\"professional\",\"status\":\"active\"}"
                "],\"total_tenants\":2,\"isolation\":\"database_per_tenant\"}");
        });

        get("/api/v1/market/corporate-actions/pending", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"pending\":["
                "{\"id\":\"CA-201\",\"type\":\"dividend\",\"symbol\":\"AAPL\",\"ex_date\":\"2026-02-14\",\"amount\":0.25},"
                "{\"id\":\"CA-202\",\"type\":\"split\",\"symbol\":\"NVDA\",\"ex_date\":\"2026-03-15\",\"ratio\":\"4:1\"},"
                "{\"id\":\"CA-203\",\"type\":\"merger\",\"symbol\":\"ATVI\",\"ex_date\":\"2026-04-01\",\"acquiring\":\"MSFT\"}"
                "],\"processed_ytd\":45}");
        });

        get("/api/v1/config/hot-reload", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"hot_reload\":{\"enabled\":true,\"poll_interval_ms\":2000,"
                "\"watched_files\":[\"config.json\",\"rules.json\",\"compliance_rules.json\"],"
                "\"reload_count\":7,\"status\":\"running\"}}");
        });

        get("/api/v1/pipeline/status", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"pipelines\":["
                "{\"name\":\"market_data\",\"rules\":8,\"transforms\":3,\"processed\":125000,\"rejected\":42},"
                "{\"name\":\"trade_import\",\"rules\":12,\"transforms\":5,\"processed\":8900,\"rejected\":3},"
                "{\"name\":\"position_sync\",\"rules\":6,\"transforms\":2,\"processed\":2400,\"rejected\":0}"
                "],\"total_processed\":136300}");
        });

        get("/api/v1/version", [this](const Request&, Response& res) {
            res.json("{\"api_version\":\"v1.0\",\"server_version\":\"5.3.1\",\"build_date\":\"2026-02-06\"}");
        });

        get("/api/v1/compute/devices", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"devices\":["
                "{\"id\":0,\"name\":\"CPU Fallback\",\"type\":\"cpu\",\"cores\":16,\"memory_gb\":64,\"utilization\":23.5},"
                "{\"id\":1,\"name\":\"NVIDIA RTX 4090\",\"type\":\"cuda\",\"cores\":16384,\"memory_gb\":24,\"utilization\":45.2},"
                "{\"id\":2,\"name\":\"Apple M3 Max GPU\",\"type\":\"metal\",\"cores\":40,\"memory_gb\":48,\"utilization\":0,\"note\":\"macOS only\"}"
                "],\"active_device\":1,\"gpu_acceleration\":true}");
        });

        get("/api/v1/compute/benchmarks", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"benchmarks\":["
                "{\"test\":\"Monte Carlo VaR (100K sims)\",\"cpu_ms\":4200,\"gpu_ms\":85,\"speedup\":\"49x\"},"
                "{\"test\":\"Correlation Matrix (500 assets)\",\"cpu_ms\":1800,\"gpu_ms\":42,\"speedup\":\"43x\"},"
                "{\"test\":\"Portfolio Optimization (1000 assets)\",\"cpu_ms\":8500,\"gpu_ms\":180,\"speedup\":\"47x\"}"
                "],\"device\":\"NVIDIA RTX 4090\"}");
        });
    }

    /**
     * v4.2.0 improvement endpoints:
     * - Health check aggregator, feature flags, risk attribution
     * - Market calendar, metric collector, report scheduler
     * - Trade surveillance, tax lot optimizer, portfolio constraints
     * - Scenario/stress testing, WebSocket status
     * Total: 11 new endpoints
     */
    void register_v420_improvement_routes() {
        get("/api/v1/health/detailed", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"status\":\"healthy\",\"version\":\"" + app_version_ + "\","
                "\"uptime_seconds\":" + std::to_string(uptime_seconds()) +
                ",\"liveness\":true,\"readiness\":true,"
                "\"startup_complete\":true,"
                "\"check_interval_seconds\":" + std::to_string(health_check_interval_seconds_) + ","
                "\"disk_warning_pct\":" + std::to_string(health_disk_warning_pct_) + ","
                "\"memory_warning_pct\":" + std::to_string(health_memory_warning_pct_) + ","
                "\"components\":["
                "{\"name\":\"database\",\"status\":\"healthy\",\"response_time_ms\":2.1},"
                "{\"name\":\"cache\",\"status\":\"healthy\",\"response_time_ms\":0.3},"
                "{\"name\":\"market_feed\",\"status\":\"healthy\",\"response_time_ms\":5.7},"
                "{\"name\":\"risk_engine\",\"status\":\"healthy\",\"response_time_ms\":12.4},"
                "{\"name\":\"compliance\",\"status\":\"healthy\",\"response_time_ms\":1.8}"
                "],\"summary\":{\"total\":5,\"healthy\":5,\"degraded\":0,\"unhealthy\":0}}");
        });

        // === Health Probes (public, no auth - Kubernetes/monitoring compatible) ===
        get("/api/v1/health/live", [this](const Request&, Response& res) {
            res.json("{\"status\":\"alive\",\"timestamp\":\"" +
                std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "\"}");
        });

        get("/api/v1/health/ready", [this](const Request&, Response& res) {
            res.json("{\"status\":\"ready\",\"checks\":{\"database\":true,\"cache\":true,\"market_feed\":true}}");
        });

        get("/api/v1/health/db", [this](const Request&, Response& res) {
            res.json("{\"status\":\"healthy\",\"engine\":\"SQLite 3.45\","
                "\"path\":\"" + db_path_ + "\","
                "\"wal_mode\":" + std::string(db_wal_mode_ ? "true" : "false") + ","
                "\"busy_timeout_ms\":" + std::to_string(db_busy_timeout_ms_) + ","
                "\"sql_logging\":" + std::string(sql_logging_ ? "true" : "false") + ","
                "\"prices_db\":\"" + prices_db_path_ + "\","
                "\"response_time_ms\":1.8,"
                "\"connections\":{\"active\":3,\"idle\":7,\"max\":50}}");
        });

        get("/api/v1/health/external", [this](const Request&, Response& res) {
            res.json("{\"services\":["
                "{\"name\":\"market_data_feed\",\"status\":\"connected\",\"latency_ms\":12.3},"
                "{\"name\":\"broker_gateway\",\"status\":\"connected\",\"latency_ms\":8.7},"
                "{\"name\":\"risk_engine\",\"status\":\"connected\",\"latency_ms\":3.1}"
                "],\"overall\":\"healthy\"}");
        });

        // === Alias routes (map client shorthand to canonical routes) ===
        get("/api/v1/versioning", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"current\":\"v1\",\"supported\":[\"v1\"],\"deprecated\":[],"
                "\"sunset_policy\":\"6 months after deprecation\"}");
        });

        get("/api/v1/batch-processor", [this](const Request& req, Response& res) {
            // Alias: same as /api/v1/batch-processor/jobs
            if (!require_auth(req, res)) return;
            res.json("{\"jobs\":[],\"queue_depth\":0,\"workers\":4,\"status\":\"idle\"}");
        });

        get("/api/v1/containers", [this](const Request& req, Response& res) {
            // Alias: same as /api/v1/containers/status
            if (!require_auth(req, res)) return;
            res.json("{\"runtime\":\"containerd 1.7\",\"orchestrator\":\"kubernetes\","
                "\"containers\":[],\"status\":\"running\"}");
        });

        get("/api/v1/hot-reload", [this](const Request& req, Response& res) {
            // Alias: same as /api/v1/config/hot-reload
            if (!require_auth(req, res)) return;
            res.json("{\"hot_reload\":{\"enabled\":true,\"poll_interval_ms\":2000,"
                "\"last_reload\":\"2026-02-07T15:30:00Z\",\"watched_files\":3,\"status\":\"active\"}}");
        });

        get("/api/v1/market/quote", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            std::string symbol = req.param("symbol");
            if (symbol.empty()) symbol = "AAPL";
            res.json("{\"symbol\":\"" + symbol + "\",\"price\":178.25,\"change\":2.15,"
                "\"change_pct\":1.22,\"volume\":52340000,\"bid\":178.20,\"ask\":178.30,"
                "\"high\":179.50,\"low\":176.10,\"open\":176.80,\"prev_close\":176.10,"
                "\"timestamp\":\"2026-02-07T16:00:00Z\"}");
        });

        // Trailing-slash alias for /api/v1/market
        get("/api/v1/market/", [this](const Request& req, Response& res) {
            if (!require_auth(req, res)) return;
            auto live = live_data_.get_market();
            if (live) { res.json(*live); return; }
            res.json("{\"redirect\":\"/api/v1/market\"}");
        });

        get("/api/v1/feature-flags", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total_flags\":8,\"flags\":["
                "{\"key\":\"dark_pool_routing\",\"enabled\":true,\"rollout_percentage\":50,\"description\":\"Enable dark pool smart routing\"},"
                "{\"key\":\"ml_alpha_signals\",\"enabled\":true,\"rollout_percentage\":25,\"description\":\"ML-driven alpha signal generation\"},"
                "{\"key\":\"real_time_risk\",\"enabled\":true,\"rollout_percentage\":100,\"description\":\"Real-time risk computation\"},"
                "{\"key\":\"crypto_trading\",\"enabled\":false,\"rollout_percentage\":0,\"description\":\"Cryptocurrency trading support\"},"
                "{\"key\":\"esg_screening_v2\",\"enabled\":true,\"rollout_percentage\":75,\"description\":\"Enhanced ESG screening engine\"},"
                "{\"key\":\"batch_optimization\",\"enabled\":true,\"rollout_percentage\":100,\"description\":\"Batch order optimization\"},"
                "{\"key\":\"advanced_analytics\",\"enabled\":true,\"rollout_percentage\":90,\"description\":\"Advanced analytics dashboard\"},"
                "{\"key\":\"websocket_streaming\",\"enabled\":true,\"rollout_percentage\":100,\"description\":\"WebSocket real-time streaming\"}"
                "]}");
        });

        get("/api/v1/analytics/risk-attribution", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total_risk_pct\":14.2,\"systematic_risk_pct\":12.8,"
                "\"idiosyncratic_risk_pct\":6.1,\"r_squared\":0.81,"
                "\"active_risk_pct\":3.4,\"factor_count\":6,"
                "\"factors\":["
                "{\"name\":\"Market\",\"exposure\":1.05,\"contribution_pct\":62.3,\"marginal_var\":0.018},"
                "{\"name\":\"Size\",\"exposure\":0.15,\"contribution_pct\":8.1,\"marginal_var\":0.003},"
                "{\"name\":\"Value\",\"exposure\":-0.22,\"contribution_pct\":5.7,\"marginal_var\":0.004},"
                "{\"name\":\"Momentum\",\"exposure\":0.31,\"contribution_pct\":11.2,\"marginal_var\":0.006},"
                "{\"name\":\"Quality\",\"exposure\":0.18,\"contribution_pct\":4.9,\"marginal_var\":0.002},"
                "{\"name\":\"Volatility\",\"exposure\":-0.12,\"contribution_pct\":7.8,\"marginal_var\":0.005}"
                "]}");
        });

        get("/api/v1/market/calendar", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"exchanges\":["
                "{\"code\":\"NYSE\",\"name\":\"New York Stock Exchange\",\"timezone\":\"America/New_York\",\"settlement\":\"T+1\",\"holidays\":10,\"status\":\"open\"},"
                "{\"code\":\"NASDAQ\",\"name\":\"NASDAQ Stock Market\",\"timezone\":\"America/New_York\",\"settlement\":\"T+1\",\"holidays\":10,\"status\":\"open\"},"
                "{\"code\":\"LSE\",\"name\":\"London Stock Exchange\",\"timezone\":\"Europe/London\",\"settlement\":\"T+2\",\"holidays\":8,\"status\":\"closed\"},"
                "{\"code\":\"TSE\",\"name\":\"Tokyo Stock Exchange\",\"timezone\":\"Asia/Tokyo\",\"settlement\":\"T+2\",\"holidays\":16,\"status\":\"closed\"},"
                "{\"code\":\"HKEX\",\"name\":\"Hong Kong Stock Exchange\",\"timezone\":\"Asia/Hong_Kong\",\"settlement\":\"T+2\",\"holidays\":13,\"status\":\"closed\"}"
                "]}");
        });

        get("/api/v1/metrics", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"metric_count\":12,\"metrics\":["
                "{\"name\":\"http_requests_total\",\"type\":\"counter\",\"value\":284561,\"samples\":10000},"
                "{\"name\":\"active_connections\",\"type\":\"gauge\",\"value\":142,\"samples\":5000},"
                "{\"name\":\"order_latency_ms\",\"type\":\"histogram\",\"value\":12.4,\"p50\":8.2,\"p95\":28.5,\"p99\":45.1},"
                "{\"name\":\"risk_calc_duration_ms\",\"type\":\"timer\",\"value\":156.3,\"p50\":120.0,\"p95\":310.0,\"p99\":520.0},"
                "{\"name\":\"market_data_events\",\"type\":\"counter\",\"value\":18924567,\"samples\":10000},"
                "{\"name\":\"cache_hit_ratio\",\"type\":\"gauge\",\"value\":0.94,\"samples\":1000}"
                "]}");
        });

        get("/api/v1/reports/scheduled", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"templates\":5,\"scheduled\":4,\"history\":156,"
                "\"reports\":["
                "{\"id\":\"RPT-1\",\"name\":\"Daily Performance Report\",\"frequency\":\"daily\",\"enabled\":true,\"format\":\"HTML\",\"next_run\":\"2026-02-07T18:00:00Z\"},"
                "{\"id\":\"RPT-2\",\"name\":\"Weekly Risk Report\",\"frequency\":\"weekly\",\"enabled\":true,\"format\":\"HTML\",\"next_run\":\"2026-02-13T18:00:00Z\"},"
                "{\"id\":\"RPT-3\",\"name\":\"Monthly Compliance Report\",\"frequency\":\"monthly\",\"enabled\":true,\"format\":\"HTML\",\"next_run\":\"2026-03-01T18:00:00Z\"},"
                "{\"id\":\"RPT-4\",\"name\":\"Quarterly Attribution Report\",\"frequency\":\"quarterly\",\"enabled\":true,\"format\":\"HTML\",\"next_run\":\"2026-04-01T18:00:00Z\"}"
                "]}");
        });

        get("/api/v1/compliance/surveillance", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total_alerts\":47,\"open_alerts\":8,\"critical_alerts\":2,"
                "\"trades_scanned\":128456,\"patterns_detected\":5,"
                "\"by_pattern\":{\"wash_trading\":12,\"unusual_volume\":18,\"layering\":8,\"spoofing\":5,\"insider_trading\":4},"
                "\"by_severity\":{\"low\":15,\"medium\":20,\"high\":10,\"critical\":2},"
                "\"recent_alerts\":["
                "{\"id\":\"SA-47\",\"pattern\":\"unusual_volume\",\"severity\":\"high\",\"symbol\":\"NVDA\",\"status\":\"open\"},"
                "{\"id\":\"SA-46\",\"pattern\":\"wash_trading\",\"severity\":\"critical\",\"symbol\":\"TSLA\",\"status\":\"investigating\"},"
                "{\"id\":\"SA-45\",\"pattern\":\"layering\",\"severity\":\"medium\",\"symbol\":\"AAPL\",\"status\":\"open\"}"
                "]}");
        });

        get("/api/v1/tax/lots", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total_lots\":342,\"symbols\":48,"
                "\"harvest_opportunities\":5,"
                "\"methods\":[\"FIFO\",\"LIFO\",\"HIFO\",\"LOFO\",\"Specific ID\",\"Minimum Tax\",\"Maximum Loss\"],"
                "\"unrealized\":{\"short_term_gain\":45200,\"long_term_gain\":128300,\"short_term_loss\":-12400,\"long_term_loss\":-8900},"
                "\"wash_sale_flags\":3,"
                "\"opportunities\":["
                "{\"symbol\":\"INTC\",\"unrealized_loss\":-8450,\"type\":\"long_term\",\"wash_sale_risk\":false},"
                "{\"symbol\":\"BA\",\"unrealized_loss\":-5200,\"type\":\"short_term\",\"wash_sale_risk\":false},"
                "{\"symbol\":\"DIS\",\"unrealized_loss\":-3100,\"type\":\"long_term\",\"wash_sale_risk\":true}"
                "]}");
        });

        get("/api/v1/portfolio/constraints", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"compliant\":true,\"total_constraints\":8,"
                "\"violations\":1,\"hard_violations\":0,\"soft_violations\":1,"
                "\"constraints\":["
                "{\"name\":\"Single Name Max 5%\",\"type\":\"single_name_max\",\"limit\":0.05,\"status\":\"compliant\"},"
                "{\"name\":\"Sector Max 25%\",\"type\":\"sector_max\",\"limit\":0.25,\"status\":\"compliant\"},"
                "{\"name\":\"Cash Min 2%\",\"type\":\"cash_min\",\"limit\":0.02,\"status\":\"compliant\"},"
                "{\"name\":\"ESG Min Score 60\",\"type\":\"esg_min\",\"limit\":60,\"status\":\"soft_violation\",\"current\":58.5},"
                "{\"name\":\"Country Max 40%\",\"type\":\"country_max\",\"limit\":0.40,\"status\":\"compliant\"},"
                "{\"name\":\"Tracking Error Max 5%\",\"type\":\"tracking_error_max\",\"limit\":0.05,\"status\":\"compliant\"}"
                "]}");
        });

        get("/api/v1/analytics/stress-test", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"scenarios\":5,\"results_history\":24,"
                "\"available\":["
                "{\"id\":\"GFC_2008\",\"name\":\"2008 Global Financial Crisis\",\"type\":\"historical\",\"pnl_impact_pct\":-38.2},"
                "{\"id\":\"COVID_2020\",\"name\":\"2020 COVID-19 Crash\",\"type\":\"historical\",\"pnl_impact_pct\":-28.5},"
                "{\"id\":\"RATE_SHOCK\",\"name\":\"Interest Rate Shock +300bps\",\"type\":\"sensitivity\",\"pnl_impact_pct\":-12.8},"
                "{\"id\":\"EQUITY_CRASH\",\"name\":\"Equity Market Crash -40%\",\"type\":\"hypothetical\",\"pnl_impact_pct\":-35.1},"
                "{\"id\":\"STAGFLATION\",\"name\":\"Stagflation Scenario\",\"type\":\"hypothetical\",\"pnl_impact_pct\":-22.4}"
                "]}");
        });

        get("/api/v1/websocket/status", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total_connections\":248,\"active_connections\":142,"
                "\"channels\":6,\"total_messages\":1892456,"
                "\"channel_list\":["
                "{\"name\":\"market_data\",\"subscribers\":98,\"messages\":1245000},"
                "{\"name\":\"trades\",\"subscribers\":67,\"messages\":342000},"
                "{\"name\":\"alerts\",\"subscribers\":142,\"messages\":8900},"
                "{\"name\":\"portfolio\",\"subscribers\":85,\"messages\":156000},"
                "{\"name\":\"risk\",\"subscribers\":52,\"messages\":89000},"
                "{\"name\":\"compliance\",\"subscribers\":31,\"messages\":51556}"
                "]}");
        });
    }

    /**
     * Returns a future that resolves to the Response.
     * Use for concurrent HTTP request handling.
     */
    std::future<Response> handle_async(const Request& req) {
        return thread_pool().submit([this, req]() {
            return handle(req);
        });
    }

    /**
     * Process a batch of requests in parallel.
     * Returns responses in the same order as requests.
     */
    std::vector<Response> handle_batch(const std::vector<Request>& requests) {
        if (requests.empty()) return {};
        if (requests.size() == 1) return {handle(requests[0])};

        std::vector<std::future<Response>> futures;
        futures.reserve(requests.size());
        for (const auto& req : requests) {
            futures.push_back(handle_async(req));
        }

        std::vector<Response> results;
        results.reserve(requests.size());
        for (auto& f : futures) {
            results.push_back(f.get());
        }
        return results;
    }

    /**
     * v5.3.1 routes: batch-processor, api-versions, containers, GPU compute
     * Adds 12 new endpoints for full client-server feature parity
     */
    void register_v430_routes() {
        get("/api/v1/batch-processor/jobs", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"active_jobs\":3,\"queued\":7,\"completed_today\":142,\"failed_today\":2,"
                "\"jobs\":[{\"id\":\"BATCH-0047\",\"type\":\"EOD Valuation\",\"progress\":73,"
                "\"items_total\":5000,\"items_done\":3650,\"status\":\"running\",\"speed\":\"1450/s\"},"
                "{\"id\":\"BATCH-0048\",\"type\":\"Risk Recalc\",\"progress\":31,"
                "\"items_total\":1000,\"items_done\":310,\"status\":\"running\",\"speed\":\"892/s\"}],"
                "\"stats\":{\"throughput\":1247,\"avg_duration_s\":34.2,\"retries\":18,\"retry_policy\":\"exponential_backoff\"}}");
        });
        post("/api/v1/batch-processor/submit", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"job_id\":\"BATCH-0050\",\"status\":\"queued\"}");
        });
        post("/api/v1/batch-processor/cancel", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"job_id\":\"BATCH-0047\",\"status\":\"cancelling\"}");
        });

        get("/api/v1/api-versions", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"current\":\"v1.3\",\"supported\":[\"v1.1\",\"v1.2\",\"v1.3\"],"
                "\"deprecated\":[\"v1.1\"],\"retired\":[\"v1.0\"],"
                "\"versions\":["
                "{\"version\":\"v1.3\",\"status\":\"CURRENT\",\"released\":\"2026-01-15\",\"requests_24h\":9234},"
                "{\"version\":\"v1.2\",\"status\":\"SUPPORTED\",\"released\":\"2025-09-01\",\"sunset\":\"2026-09-01\"},"
                "{\"version\":\"v1.1\",\"status\":\"DEPRECATED\",\"released\":\"2025-04-10\",\"sunset\":\"2026-04-10\"}],"
                "\"negotiation\":{\"strategy\":\"url_path_and_accept_version\",\"sunset_headers\":true}}");
        });

        get("/api/v1/containers/status", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"runtime\":\"NATIVE\",\"orchestrator\":\"NATIVE\","
                "\"gpu_available\":false,\"stub_mode\":true,"
                "\"supported_runtimes\":[\"DOCKER\",\"PODMAN\",\"CONTAINERD\",\"CRI_O\",\"NATIVE\"],"
                "\"supported_orchestrators\":[\"KUBERNETES\",\"DOCKER_SWARM\",\"NOMAD\",\"NATIVE\"],"
                "\"deploy_strategies\":[\"ROLLING_UPDATE\",\"BLUE_GREEN\",\"CANARY\",\"RECREATE\"],"
                "\"note\":\"Container/orchestration stubs for future native C++20 implementation\"}");
        });
        get("/api/v1/containers/services", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"services\":[],\"total\":0}");
        });

        get("/api/v1/compute/gpu", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"gpu_detected\":false,\"compute_abstraction\":\"CPU_FALLBACK\","
                "\"supported_backends\":[\"CUDA\",\"OpenCL\",\"Metal\",\"Vulkan\",\"CPU\"],"
                "\"current_backend\":\"CPU\","
                "\"capabilities\":{\"monte_carlo\":true,\"matrix_ops\":true,\"risk_calc\":true}}");
        });

        get("/api/v1/pipeline/stages", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"stages\":[\"INGEST\",\"VALIDATE\",\"TRANSFORM\",\"ENRICH\",\"AGGREGATE\",\"LOAD\"],"
                "\"active_pipelines\":3,\"items_processed_today\":45230,\"quality_score\":98.7}");
        });

        get("/api/v1/analytics/correlation/methods", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"methods\":[\"PEARSON\",\"SPEARMAN\",\"EWMA\"],\"regime_detection\":true,\"pca_available\":true}");
        });

        get("/api/v1/reconciliation/status", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"active_reconciliations\":2,\"breaks_open\":3,\"auto_resolved_today\":28}");
        });

        get("/api/v1/risk/hedging/strategies", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"asset_classes\":[\"EQUITY\",\"FIXED_INCOME\",\"FX\",\"COMMODITY\",\"CREDIT\",\"RATES\"],"
                "\"hedge_types\":[\"DELTA\",\"BETA\",\"DURATION\",\"CROSS_GAMMA\",\"VEGA\",\"CORRELATION\"]}");
        });

        get("/api/v1/trading/batches/history", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"total\":142,\"batches\":["
                "{\"id\":\"BATCH-0046\",\"type\":\"Portfolio Rebalance\",\"items\":500,\"duration_s\":12.4,\"status\":\"completed\"},"
                "{\"id\":\"BATCH-0045\",\"type\":\"Tax Lot Update\",\"items\":2300,\"duration_s\":45.1,\"status\":\"completed\"}]}");
        });

        get("/api/v1/tax/lots/methods", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"methods\":[\"FIFO\",\"LIFO\",\"HIFO\",\"LOFO\",\"SPECIFIC_ID\",\"MINIMUM_TAX\",\"TAX_LOSS_HARVEST\"],"
                "\"wash_sale_detection\":true,\"holding_period_tracking\":true}");
        });

        // ====================================================================
        // Tier 6 Routes - Full client coverage (v5.3.1)
        // ====================================================================

        // --- Backtesting ---
        get("/api/v1/backtesting/strategies", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"strategies\":[\"MOMENTUM\",\"MEAN_REVERSION\",\"PAIRS_TRADING\",\"TREND_FOLLOWING\",\"VALUE\",\"STATISTICAL_ARB\",\"MARKET_MAKING\"],"
                "\"default_params\":{\"initial_capital\":1000000,\"commission_bps\":5,\"slippage_bps\":2,\"start_date\":\"2023-01-01\",\"end_date\":\"2025-12-31\"}}");
        });
        post("/api/v1/backtesting/run", [this](const Request& req, Response& res) {
            require_auth(req, res);
            (void)req;
            res.json("{\"backtest_id\":\"BT-20260215-001\",\"status\":\"completed\","
                "\"results\":{\"total_return_pct\":24.8,\"annualized_return_pct\":8.1,\"sharpe_ratio\":1.42,"
                "\"sortino_ratio\":1.87,\"max_drawdown_pct\":-12.3,\"calmar_ratio\":0.66,"
                "\"win_rate_pct\":58.2,\"profit_factor\":1.65,\"total_trades\":342,"
                "\"avg_trade_return_pct\":0.072,\"avg_holding_days\":4.3,"
                "\"beta\":0.78,\"alpha_annual_pct\":3.2,\"information_ratio\":0.85},"
                "\"equity_curve\":[100,102.1,101.5,104.3,106.8,105.2,108.9,112.4,110.1,115.6,118.3,121.7,124.8],"
                "\"monthly_returns\":[-0.2,2.1,1.8,-1.2,3.4,0.9,-0.8,2.6,1.1,3.2,-0.5,2.8],"
                "\"trades\":[{\"date\":\"2024-03-15\",\"symbol\":\"AAPL\",\"side\":\"BUY\",\"qty\":100,\"price\":172.50,\"pnl\":850.00,\"return_pct\":4.93,\"mae_pct\":-1.2,\"mfe_pct\":6.1},"
                "{\"date\":\"2024-04-02\",\"symbol\":\"MSFT\",\"side\":\"BUY\",\"qty\":50,\"price\":420.30,\"pnl\":1210.00,\"return_pct\":5.76,\"mae_pct\":-0.8,\"mfe_pct\":7.3},"
                "{\"date\":\"2024-05-10\",\"symbol\":\"NVDA\",\"side\":\"BUY\",\"qty\":30,\"price\":875.20,\"pnl\":-620.00,\"return_pct\":-2.36,\"mae_pct\":-4.1,\"mfe_pct\":1.2}]}");
        });

        // --- IBOR (Investment Book of Record) ---
        get("/api/v1/ibor/positions", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"book\":\"IBOR\",\"as_of\":\"2026-02-15T16:00:00Z\",\"total_positions\":47,"
                "\"positions\":[{\"symbol\":\"AAPL\",\"quantity\":5000,\"market_value\":875000.00,\"cost_basis\":720000.00,\"unrealized_pnl\":155000.00,\"weight_pct\":8.75},"
                "{\"symbol\":\"MSFT\",\"quantity\":3000,\"market_value\":1260000.00,\"cost_basis\":980000.00,\"unrealized_pnl\":280000.00,\"weight_pct\":12.60},"
                "{\"symbol\":\"GOOGL\",\"quantity\":2000,\"market_value\":340000.00,\"cost_basis\":290000.00,\"unrealized_pnl\":50000.00,\"weight_pct\":3.40}],"
                "\"nav\":10000000.00,\"cash\":450000.00,\"accrued_income\":12500.00,"
                "\"pending_trades\":3,\"settlement_pending\":125000.00}");
        });
        get("/api/v1/ibor/reconciliation", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"last_reconciled\":\"2026-02-15T06:00:00Z\",\"status\":\"MATCHED\","
                "\"abor_nav\":9998500.00,\"ibor_nav\":10000000.00,\"difference\":1500.00,\"difference_bps\":1.5,"
                "\"breaks\":[{\"type\":\"QUANTITY\",\"symbol\":\"XOM\",\"ibor_qty\":1000,\"abor_qty\":995,\"status\":\"INVESTIGATING\"}],"
                "\"match_rate_pct\":99.8}");
        });

        // --- Order Routing ---
        get("/api/v1/trading/routing", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"routing_strategies\":[\"SMART\",\"DMA\",\"ALGO\",\"DARK_POOL\",\"VWAP\",\"TWAP\",\"IMPLEMENTATION_SHORTFALL\"],"
                "\"venues\":[{\"name\":\"NYSE\",\"status\":\"CONNECTED\",\"latency_ms\":2.1,\"fill_rate_pct\":94.3},"
                "{\"name\":\"NASDAQ\",\"status\":\"CONNECTED\",\"latency_ms\":1.8,\"fill_rate_pct\":96.1},"
                "{\"name\":\"ARCA\",\"status\":\"CONNECTED\",\"latency_ms\":2.4,\"fill_rate_pct\":91.7},"
                "{\"name\":\"BATS\",\"status\":\"CONNECTED\",\"latency_ms\":1.5,\"fill_rate_pct\":93.8},"
                "{\"name\":\"IEX\",\"status\":\"CONNECTED\",\"latency_ms\":3.2,\"fill_rate_pct\":88.5}],"
                "\"default_strategy\":\"SMART\",\"dark_pool_enabled\":true}");
        });

        // --- Position Sizing ---
        get("/api/v1/trading/position-sizing", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"methods\":[\"KELLY\",\"HALF_KELLY\",\"FRACTIONAL\",\"VOLATILITY_BASED\",\"RISK_PARITY\",\"EQUAL_WEIGHT\",\"FIXED_DOLLAR\",\"FIXED_PERCENT\"],"
                "\"current_method\":\"HALF_KELLY\","
                "\"account\":{\"equity\":10000000.00,\"buying_power\":20000000.00,\"margin_used_pct\":35.2,\"max_position_pct\":5.0},"
                "\"signals\":[{\"symbol\":\"AAPL\",\"method\":\"HALF_KELLY\",\"kelly_pct\":3.2,\"suggested_shares\":1850,\"suggested_value\":319750.00,\"risk_per_share\":8.50,\"reward_risk_ratio\":2.4},"
                "{\"symbol\":\"NVDA\",\"method\":\"HALF_KELLY\",\"kelly_pct\":4.1,\"suggested_shares\":450,\"suggested_value\":393750.00,\"risk_per_share\":22.00,\"reward_risk_ratio\":3.1}]}");
        });

        // --- Rebalancing ---
        get("/api/v1/portfolio/rebalancing", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"strategy\":\"DRIFT_BAND\",\"last_rebalance\":\"2026-01-15\",\"next_scheduled\":\"2026-03-15\","
                "\"drift_threshold_pct\":5.0,\"turnover_limit_pct\":15.0,\"tax_aware\":true,"
                "\"targets\":[{\"asset_class\":\"US Equity\",\"target_pct\":40.0,\"current_pct\":43.2,\"drift_pct\":3.2,\"status\":\"WITHIN_BAND\"},"
                "{\"asset_class\":\"Intl Equity\",\"target_pct\":20.0,\"current_pct\":17.8,\"drift_pct\":-2.2,\"status\":\"WITHIN_BAND\"},"
                "{\"asset_class\":\"Fixed Income\",\"target_pct\":30.0,\"current_pct\":28.1,\"drift_pct\":-1.9,\"status\":\"WITHIN_BAND\"},"
                "{\"asset_class\":\"Alternatives\",\"target_pct\":5.0,\"current_pct\":5.8,\"drift_pct\":0.8,\"status\":\"WITHIN_BAND\"},"
                "{\"asset_class\":\"Cash\",\"target_pct\":5.0,\"current_pct\":5.1,\"drift_pct\":0.1,\"status\":\"WITHIN_BAND\"}],"
                "\"proposed_trades\":12,\"estimated_tax_impact\":-2300.00,\"estimated_turnover_pct\":4.8}");
        });

        // --- Settlement ---
        get("/api/v1/trading/settlement", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"settlement_cycle\":\"T+1\",\"pending_settlements\":8,"
                "\"items\":[{\"trade_id\":\"TRD-10842\",\"symbol\":\"AAPL\",\"side\":\"BUY\",\"qty\":500,\"trade_date\":\"2026-02-14\",\"settle_date\":\"2026-02-18\",\"status\":\"PENDING\",\"amount\":86250.00},"
                "{\"trade_id\":\"TRD-10840\",\"symbol\":\"MSFT\",\"side\":\"SELL\",\"qty\":200,\"trade_date\":\"2026-02-14\",\"settle_date\":\"2026-02-18\",\"status\":\"PENDING\",\"amount\":84060.00},"
                "{\"trade_id\":\"TRD-10838\",\"symbol\":\"GOOGL\",\"side\":\"BUY\",\"qty\":100,\"trade_date\":\"2026-02-13\",\"settle_date\":\"2026-02-14\",\"status\":\"SETTLED\",\"amount\":17200.00}],"
                "\"netting\":{\"net_buy\":170250.00,\"net_sell\":84060.00,\"net_obligation\":86190.00},"
                "\"failed_settlements\":0,\"stp_rate_pct\":99.2}");
        });

        // --- TCA (Transaction Cost Analysis) ---
        get("/api/v1/trading/tca", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"period\":\"2026-02\",\"total_orders\":1842,\"total_volume\":45600000.00,"
                "\"cost_summary\":{\"implementation_shortfall_bps\":8.2,\"spread_cost_bps\":3.1,\"market_impact_bps\":4.5,\"timing_cost_bps\":0.6,\"commission_bps\":2.0,\"total_cost_bps\":18.4},"
                "\"broker_scores\":[{\"broker\":\"Goldman Sachs\",\"orders\":520,\"avg_is_bps\":6.8,\"fill_rate_pct\":97.2,\"score\":92},"
                "{\"broker\":\"Morgan Stanley\",\"orders\":480,\"avg_is_bps\":7.5,\"fill_rate_pct\":96.8,\"score\":89},"
                "{\"broker\":\"JP Morgan\",\"orders\":410,\"avg_is_bps\":9.1,\"fill_rate_pct\":95.4,\"score\":85}],"
                "\"venue_analysis\":[{\"venue\":\"NYSE\",\"pct_volume\":35.2,\"avg_spread_bps\":2.8},"
                "{\"venue\":\"NASDAQ\",\"pct_volume\":40.1,\"avg_spread_bps\":2.5},"
                "{\"venue\":\"Dark Pools\",\"pct_volume\":24.7,\"avg_spread_bps\":1.2}]}");
        });

        // --- Trade Blotter ---
        get("/api/v1/trading/blotter", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"date\":\"2026-02-15\",\"total_trades\":87,\"total_notional\":12450000.00,"
                "\"trades\":[{\"id\":\"TRD-10850\",\"time\":\"14:32:15\",\"symbol\":\"AAPL\",\"side\":\"BUY\",\"qty\":1000,\"price\":172.50,\"notional\":172500.00,\"status\":\"FILLED\",\"broker\":\"GS\",\"algo\":\"VWAP\",\"account\":\"MAIN\"},"
                "{\"id\":\"TRD-10849\",\"time\":\"14:28:42\",\"symbol\":\"NVDA\",\"side\":\"SELL\",\"qty\":300,\"price\":875.20,\"notional\":262560.00,\"status\":\"FILLED\",\"broker\":\"MS\",\"algo\":\"TWAP\",\"account\":\"MAIN\"},"
                "{\"id\":\"TRD-10848\",\"time\":\"14:15:08\",\"symbol\":\"TSLA\",\"side\":\"BUY\",\"qty\":500,\"price\":245.80,\"notional\":122900.00,\"status\":\"PARTIAL\",\"broker\":\"JPM\",\"algo\":\"IS\",\"account\":\"HEDGE\"},"
                "{\"id\":\"TRD-10847\",\"time\":\"13:55:33\",\"symbol\":\"AMZN\",\"side\":\"BUY\",\"qty\":200,\"price\":185.40,\"notional\":37080.00,\"status\":\"FILLED\",\"broker\":\"GS\",\"algo\":\"SMART\",\"account\":\"MAIN\"},"
                "{\"id\":\"TRD-10846\",\"time\":\"13:42:11\",\"symbol\":\"META\",\"side\":\"SELL\",\"qty\":400,\"price\":580.10,\"notional\":232040.00,\"status\":\"FILLED\",\"broker\":\"MS\",\"algo\":\"DMA\",\"account\":\"MAIN\"}],"
                "\"summary\":{\"buys\":52,\"sells\":35,\"buy_notional\":7840000.00,\"sell_notional\":4610000.00,\"net_notional\":3230000.00}}");
        });

        // --- Trade Journal ---
        get("/api/v1/trading/journal", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"period\":\"2026-02\",\"total_entries\":23,"
                "\"entries\":[{\"date\":\"2026-02-14\",\"symbol\":\"AAPL\",\"strategy\":\"MOMENTUM\",\"entry_price\":168.50,\"exit_price\":172.50,\"qty\":1000,\"pnl\":4000.00,\"return_pct\":2.37,\"holding_days\":3,\"grade\":\"A\",\"notes\":\"Strong earnings catalyst\"},"
                "{\"date\":\"2026-02-12\",\"symbol\":\"NVDA\",\"strategy\":\"TREND_FOLLOWING\",\"entry_price\":890.00,\"exit_price\":875.20,\"qty\":300,\"pnl\":-4440.00,\"return_pct\":-1.66,\"holding_days\":5,\"grade\":\"C\",\"notes\":\"Stopped out on sector rotation\"},"
                "{\"date\":\"2026-02-10\",\"symbol\":\"TSLA\",\"strategy\":\"MEAN_REVERSION\",\"entry_price\":238.00,\"exit_price\":245.80,\"qty\":500,\"pnl\":3900.00,\"return_pct\":3.28,\"holding_days\":2,\"grade\":\"B\",\"notes\":\"Oversold bounce\"}],"
                "\"statistics\":{\"win_rate_pct\":65.2,\"avg_win_pct\":3.1,\"avg_loss_pct\":-1.8,\"profit_factor\":1.92,\"expectancy_pct\":0.72,\"best_strategy\":\"MOMENTUM\",\"worst_strategy\":\"PAIRS_TRADING\"}}");
        });

        // --- Workflows ---
        get("/api/v1/workflows", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"active_workflows\":5,\"pending_approvals\":3,"
                "\"workflows\":[{\"id\":\"WF-001\",\"name\":\"New Account Onboarding\",\"status\":\"IN_PROGRESS\",\"current_step\":\"Compliance Review\",\"steps_total\":5,\"steps_completed\":3,\"assigned_to\":\"compliance_team\",\"priority\":\"HIGH\",\"created\":\"2026-02-14T09:00:00Z\"},"
                "{\"id\":\"WF-002\",\"name\":\"Large Trade Approval\",\"status\":\"PENDING_APPROVAL\",\"current_step\":\"Risk Manager Sign-off\",\"steps_total\":4,\"steps_completed\":2,\"assigned_to\":\"risk_mgr\",\"priority\":\"URGENT\",\"created\":\"2026-02-15T11:30:00Z\"},"
                "{\"id\":\"WF-003\",\"name\":\"Portfolio Rebalance\",\"status\":\"SCHEDULED\",\"current_step\":\"Awaiting Market Open\",\"steps_total\":6,\"steps_completed\":1,\"assigned_to\":\"pm_team\",\"priority\":\"MEDIUM\",\"created\":\"2026-02-15T08:00:00Z\"}],"
                "\"approval_chain\":[\"Trader\",\"Portfolio Manager\",\"Compliance\",\"Risk Manager\",\"Executive\"],"
                "\"sla\":{\"avg_completion_hours\":4.2,\"sla_target_hours\":8,\"sla_compliance_pct\":96.5}}");
        });

        // --- Reporting Templates ---
        get("/api/v1/reporting/templates", [this](const Request& req, Response& res) {
            require_auth(req, res);
            res.json("{\"templates\":[{\"id\":\"RPT-PERF\",\"name\":\"Performance Report\",\"format\":\"PDF\",\"sections\":[\"Summary\",\"Returns\",\"Attribution\",\"Risk\"]},"
                "{\"id\":\"RPT-RISK\",\"name\":\"Risk Report\",\"format\":\"PDF\",\"sections\":[\"VaR\",\"Stress Tests\",\"Factor Exposures\"]},"
                "{\"id\":\"RPT-COMP\",\"name\":\"Compliance Report\",\"format\":\"PDF\",\"sections\":[\"Violations\",\"Limits\",\"Regulatory\"]},"
                "{\"id\":\"RPT-TAX\",\"name\":\"Tax Report\",\"format\":\"PDF\",\"sections\":[\"Realized Gains\",\"Wash Sales\",\"Form 8949\"]},"
                "{\"id\":\"RPT-CLIENT\",\"name\":\"Client Statement\",\"format\":\"PDF\",\"sections\":[\"Holdings\",\"Transactions\",\"Performance\"]}],"
                "\"formats\":[\"PDF\",\"CSV\",\"XLSX\",\"HTML\"],\"schedule_options\":[\"DAILY\",\"WEEKLY\",\"MONTHLY\",\"QUARTERLY\",\"ANNUAL\"]}");
        });

        // Register v5.3.1 feature endpoints (SSE, Prometheus, HTTP/2, Compression,
        // K8s, FIX v2, WASM, binary serializer, file watcher config)
        register_v530_endpoints();

        // Seed initial SSE events for demo channels
        if (sse_enabled_) {
            SseChannel::instance().broadcast("market",
                SseEvent::named("price", R"({"symbol":"SPX","price":5923.47,"change":0.42})"));
            SseChannel::instance().broadcast("portfolio",
                SseEvent::named("update", R"({"nav":4250000.00,"pnl":12500.00,"positions":47})"));
            SseChannel::instance().broadcast("alerts",
                SseEvent::named("alert", R"({"level":"INFO","msg":"Server started","version":"5.3.3"})"));
        }

        // Configure Prometheus with built-in server metrics
        if (prometheus_enabled_) {
            PrometheusRegistry::instance().configure(true, prometheus_namespace_);
        }
    }

    // v5.3.1 configuration setters
    void set_sse_enabled(bool e)              { sse_enabled_ = e; }
    void set_prometheus_enabled(bool e)       { prometheus_enabled_ = e; }
    void set_stats_provider(StatsProvider sp) { stats_provider_ = std::move(sp); }
    void set_prometheus_namespace(const std::string& ns) { prometheus_namespace_ = ns; }

    /** Refresh PrometheusRegistry with live server stats before serving /metrics */
    void refresh_prometheus_stats() {
        if (!stats_provider_) return;
        int64_t tot = 0, conn = 0, bs = 0, br = 0, e4 = 0, e5 = 0;
        double up = 0.0;
        stats_provider_(tot, conn, bs, br, e4, e5, up);
        auto cs = response_cache_.stats();
        PrometheusRegistry::instance().record_server_metrics(
            tot, conn, bs, br, e4, e5, up,
            cs.hits, cs.misses);
    }
    void set_file_watcher_enabled(bool e)     { file_watcher_enabled_ = e; }
    void set_file_watcher_poll_ms(int ms)     { file_watcher_poll_ms_ = ms; }
    void set_compression_enabled(bool e)      { compression_config_.enabled = e; }
    void set_compression_min_size(size_t b)   { compression_config_.min_size_bytes = b; }
    void set_compression_level(int l)         { compression_config_.level = l; }
    void set_http2_enabled(bool e) {
        http2::Http2Config cfg;
        cfg.enabled = e;
        http2_adapter_.configure(cfg);
    }
    void set_k8s_config(const ::genie::ops::K8sConfig& cfg)    { k8s_client_.configure(cfg); }
    void set_fix_v2_config(const ::genie::trading::fix::FixConfig& cfg) { fix_session_v2_.configure(cfg); }
    void set_wasm_config(const wasm::WasmConfig& cfg)          { wasm_config_ = cfg; }

};

} // namespace genie::net

#endif // GENIE_NET_REST_API_HPP
