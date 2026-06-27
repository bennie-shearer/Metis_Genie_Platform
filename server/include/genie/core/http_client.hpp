/**
 * @file http_client.hpp
 * @brief Async HTTP client for external API integrations
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides HTTP client functionality for:
 * - REST API calls to data providers
 * - Broker API integration
 * - Rate limiting and retry logic
 * - Response caching
 */
#pragma once
#ifndef GENIE_CORE_HTTP_CLIENT_HPP
#define GENIE_CORE_HTTP_CLIENT_HPP

#include "platform_http.hpp"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <optional>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace genie::core {

/**
 * @brief HTTP methods
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH
};

/**
 * @brief HTTP response
 */
struct HttpResponse {
    int status_code{0};
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
    std::chrono::milliseconds latency{0};
    bool success{false};
    
    bool ok() const { return status_code >= 200 && status_code < 300; }
    bool is_rate_limited() const { return status_code == 429; }
    bool is_unauthorized() const { return status_code == 401; }
    bool is_not_found() const { return status_code == 404; }
};

/**
 * @brief HTTP request configuration
 */
struct HttpRequest {
    HttpMethod method{HttpMethod::GET};
    std::string url;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::string body;
    int timeout_ms{30000};
    bool follow_redirects{true};
};

/**
 * @brief Rate limiter for API calls
 */
class RateLimiter {
public:
    RateLimiter(int max_requests, int window_seconds)
        : max_requests_(max_requests)
        , window_ms_(window_seconds * 1000) {}
    
    bool try_acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        // Remove old timestamps
        while (!timestamps_.empty()) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - timestamps_.front()).count();
            if (age > window_ms_) {
                timestamps_.pop();
            } else {
                break;
            }
        }
        
        if (static_cast<int>(timestamps_.size()) >= max_requests_) {
            return false;
        }
        
        timestamps_.push(now);
        return true;
    }
    
    void wait_for_slot() {
        while (!try_acquire()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    int remaining() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_requests_ - static_cast<int>(timestamps_.size());
    }

private:
    int max_requests_;
    int window_ms_;
    mutable std::mutex mutex_;
    std::queue<std::chrono::steady_clock::time_point> timestamps_;
};

/**
 * @brief Response cache with TTL
 */
class ResponseCache {
public:
    struct CacheEntry {
        HttpResponse response;
        std::chrono::steady_clock::time_point expires_at;
    };
    
    void put(const std::string& key, const HttpResponse& response, 
             std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = {
            response,
            std::chrono::steady_clock::now() + ttl
        };
    }
    
    std::optional<HttpResponse> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }
        
        if (std::chrono::steady_clock::now() > it->second.expires_at) {
            cache_.erase(it);
            return std::nullopt;
        }
        
        return it->second.response;
    }
    
    void invalidate(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.erase(key);
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, CacheEntry> cache_;
};

/**
 * @brief URL encoding utilities
 */
inline std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

inline std::string build_query_string(const std::map<std::string, std::string>& params) {
    if (params.empty()) return "";
    
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) oss << "&";
        oss << url_encode(key) << "=" << url_encode(value);
        first = false;
    }
    return oss.str();
}

/**
 * @brief Simple JSON parser for API responses
 */
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    
    JsonValue() : type_(Type::Null) {}
    explicit JsonValue(bool b) : type_(Type::Bool), bool_val_(b) {}
    explicit JsonValue(double d) : type_(Type::Number), num_val_(d) {}
    explicit JsonValue(const std::string& s) : type_(Type::String), str_val_(s) {}
    explicit JsonValue(std::vector<JsonValue> arr) : type_(Type::Array), arr_val_(std::move(arr)) {}
    explicit JsonValue(std::map<std::string, JsonValue> obj) : type_(Type::Object), obj_val_(std::move(obj)) {}
    
    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }
    
    bool as_bool() const { return bool_val_; }
    double as_number() const { return num_val_; }
    int as_int() const { return static_cast<int>(num_val_); }
    const std::string& as_string() const { return str_val_; }
    const std::vector<JsonValue>& as_array() const { return arr_val_; }
    const std::map<std::string, JsonValue>& as_object() const { return obj_val_; }
    
    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_val_.count(key) > 0;
    }
    
    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null_val;
        if (type_ != Type::Object) return null_val;
        auto it = obj_val_.find(key);
        return (it != obj_val_.end()) ? it->second : null_val;
    }
    
    const JsonValue& operator[](size_t index) const {
        static JsonValue null_val;
        if (type_ != Type::Array || index >= arr_val_.size()) return null_val;
        return arr_val_[index];
    }
    
    size_t size() const {
        if (type_ == Type::Array) return arr_val_.size();
        if (type_ == Type::Object) return obj_val_.size();
        return 0;
    }
    
    bool empty() const {
        if (type_ == Type::Array) return arr_val_.empty();
        if (type_ == Type::Object) return obj_val_.empty();
        if (type_ == Type::String) return str_val_.empty();
        return type_ == Type::Null;
    }
    
    // Iterator support for arrays
    auto begin() const { return arr_val_.begin(); }
    auto end() const { return arr_val_.end(); }
    
    // Helper methods with defaults
    std::string value(const std::string& key, const std::string& def) const {
        if (type_ != Type::Object) return def;
        auto it = obj_val_.find(key);
        if (it == obj_val_.end()) return def;
        if (it->second.is_string()) return it->second.as_string();
        if (it->second.is_number()) return std::to_string(it->second.as_number());
        return def;
    }
    
    // Overload for const char* to prevent implicit conversion to bool
    std::string value(const std::string& key, const char* def) const {
        return value(key, std::string(def));
    }
    
    double value(const std::string& key, double def) const {
        if (type_ != Type::Object) return def;
        auto it = obj_val_.find(key);
        if (it == obj_val_.end()) return def;
        if (it->second.is_number()) return it->second.as_number();
        if (it->second.is_string()) {
            try { return std::stod(it->second.as_string()); }
            catch (...) { return def; }
        }
        return def;
    }
    
    int value(const std::string& key, int def) const {
        return static_cast<int>(value(key, static_cast<double>(def)));
    }
    
    int64_t value(const std::string& key, int64_t def) const {
        return static_cast<int64_t>(value(key, static_cast<double>(def)));
    }
    
    bool value(const std::string& key, bool def) const {
        if (type_ != Type::Object) return def;
        auto it = obj_val_.find(key);
        if (it == obj_val_.end()) return def;
        if (it->second.is_bool()) return it->second.as_bool();
        if (it->second.is_number()) return it->second.as_number() != 0;
        return def;
    }
    
    bool contains(const std::string& key) const {
        return has(key);
    }
    
    const std::vector<JsonValue>& array() const { return arr_val_; }
    const std::map<std::string, JsonValue>& object() const { return obj_val_; }
    
    // Get string with default (aliases for value())
    std::string get_string(const std::string& key, const std::string& def = "") const {
        return value(key, def);
    }
    
    double get_double(const std::string& key, double def = 0) const {
        return value(key, def);
    }
    
    int get_int(const std::string& key, int def = 0) const {
        return value(key, def);
    }
    
    // Overload for direct value conversion (used on array elements)
    int get_int(int def = 0) const {
        if (is_number()) return static_cast<int>(as_number());
        if (is_string()) {
            try { return std::stoi(as_string()); }
            catch (...) { return def; }
        }
        return def;
    }
    
    bool get_bool(const std::string& key, bool def = false) const {
        return value(key, def);
    }
    
    // Overload for direct value conversion (used on array elements)
    bool get_bool(bool def = false) const {
        if (is_bool()) return as_bool();
        if (is_number()) return as_number() != 0;
        if (is_string()) {
            const auto& s = as_string();
            if (s == "true" || s == "1") return true;
            if (s == "false" || s == "0") return false;
        }
        return def;
    }
    
    // Direct string conversion for array elements
    std::string to_string(const std::string& def = "") const {
        if (is_string()) return as_string();
        if (is_number()) return std::to_string(as_number());
        return def;
    }

private:
    Type type_;
    bool bool_val_{false};
    double num_val_{0};
    std::string str_val_;
    std::vector<JsonValue> arr_val_;
    std::map<std::string, JsonValue> obj_val_;
};

/**
 * @brief Simple JSON parser
 */
class JsonParser {
public:
    static JsonValue parse(const std::string& json) {
        size_t pos = 0;
        return parse_value(json, pos);
    }

private:
    static void skip_whitespace(const std::string& json, size_t& pos) {
        while (pos < json.size() && std::isspace(json[pos])) pos++;
    }
    
    static JsonValue parse_value(const std::string& json, size_t& pos) {
        skip_whitespace(json, pos);
        if (pos >= json.size()) return JsonValue();
        
        char c = json[pos];
        if (c == 'n') return parse_null(json, pos);
        if (c == 't' || c == 'f') return parse_bool(json, pos);
        if (c == '"') return parse_string(json, pos);
        if (c == '[') return parse_array(json, pos);
        if (c == '{') return parse_object(json, pos);
        if (c == '-' || std::isdigit(c)) return parse_number(json, pos);
        return JsonValue();
    }
    
    static JsonValue parse_null(const std::string& json, size_t& pos) {
        if (json.substr(pos, 4) == "null") { pos += 4; return JsonValue(); }
        return JsonValue();
    }
    
    static JsonValue parse_bool(const std::string& json, size_t& pos) {
        if (json.substr(pos, 4) == "true") { pos += 4; return JsonValue(true); }
        if (json.substr(pos, 5) == "false") { pos += 5; return JsonValue(false); }
        return JsonValue();
    }
    
    static JsonValue parse_number(const std::string& json, size_t& pos) {
        size_t start = pos;
        if (json[pos] == '-') pos++;
        while (pos < json.size() && std::isdigit(json[pos])) pos++;
        if (pos < json.size() && json[pos] == '.') {
            pos++;
            while (pos < json.size() && std::isdigit(json[pos])) pos++;
        }
        if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E')) {
            pos++;
            if (pos < json.size() && (json[pos] == '+' || json[pos] == '-')) pos++;
            while (pos < json.size() && std::isdigit(json[pos])) pos++;
        }
        return JsonValue(std::stod(json.substr(start, pos - start)));
    }
    
    static JsonValue parse_string(const std::string& json, size_t& pos) {
        pos++; // skip opening quote
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                switch (json[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += json[pos]; break;
                }
            } else {
                result += json[pos];
            }
            pos++;
        }
        if (pos < json.size()) pos++; // skip closing quote
        return JsonValue(result);
    }
    
    static JsonValue parse_array(const std::string& json, size_t& pos) {
        pos++; // skip '['
        std::vector<JsonValue> arr;
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') { pos++; return JsonValue(arr); }
        
        while (pos < json.size()) {
            arr.push_back(parse_value(json, pos));
            skip_whitespace(json, pos);
            if (pos >= json.size() || json[pos] == ']') break;
            if (json[pos] == ',') pos++;
        }
        if (pos < json.size()) pos++; // skip ']'
        return JsonValue(arr);
    }
    
    static JsonValue parse_object(const std::string& json, size_t& pos) {
        pos++; // skip '{'
        std::map<std::string, JsonValue> obj;
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == '}') { pos++; return JsonValue(obj); }
        
        while (pos < json.size()) {
            skip_whitespace(json, pos);
            auto key = parse_string(json, pos);
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') pos++;
            obj[key.as_string()] = parse_value(json, pos);
            skip_whitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}') break;
            if (json[pos] == ',') pos++;
        }
        if (pos < json.size()) pos++; // skip '}'
        return JsonValue(obj);
    }
    
public:
    /**
     * @brief Convert a map to JSON string
     */
    static std::string stringify(const std::map<std::string, std::string>& obj) {
        std::ostringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& [key, val] : obj) {
            if (!first) ss << ",";
            first = false;
            ss << "\"" << key << "\":\"" << val << "\"";
        }
        ss << "}";
        return ss.str();
    }
    
    /**
     * @brief Convert a JsonValue to JSON string
     */
    static std::string stringify(const JsonValue& val) {
        if (val.is_null()) return "null";
        if (val.is_bool()) return val.as_bool() ? "true" : "false";
        if (val.is_number()) {
            std::ostringstream ss;
            ss << val.as_number();
            return ss.str();
        }
        if (val.is_string()) {
            std::ostringstream ss;
            ss << "\"" << val.as_string() << "\"";
            return ss.str();
        }
        if (val.is_array()) {
            std::ostringstream ss;
            ss << "[";
            bool first = true;
            for (const auto& item : val.as_array()) {
                if (!first) ss << ",";
                first = false;
                ss << stringify(item);
            }
            ss << "]";
            return ss.str();
        }
        if (val.is_object()) {
            std::ostringstream ss;
            ss << "{";
            bool first = true;
            for (const auto& [key, v] : val.as_object()) {
                if (!first) ss << ",";
                first = false;
                ss << "\"" << key << "\":" << stringify(v);
            }
            ss << "}";
            return ss.str();
        }
        return "null";
    }
};

// Type alias for JSON object building
using JsonObject = std::map<std::string, std::string>;

/**
 * @brief HTTP client with rate limiting, caching, and retry
 * 
 * Uses platform-native TLS via platform_http.hpp:
 *   Windows: WinHTTP, macOS: SecureTransport, Linux: OpenSSL
 */
class HttpClient {
public:
    struct Config {
        int default_timeout_ms{30000};
        int max_retries{3};
        int retry_delay_ms{1000};
        bool enable_cache{true};
        std::chrono::seconds default_cache_ttl{60};
        
        Config() = default;
    };
    
    HttpClient() : config_() {}
    
    explicit HttpClient(const Config& config)
        : config_(config) {}
    
    explicit HttpClient(const std::string& base_url)
        : config_(), base_url_(base_url) {}
    
    void set_base_url(const std::string& url) {
        base_url_ = url;
    }
    
    void set_timeout(int timeout_ms) {
        config_.default_timeout_ms = timeout_ms;
    }
    
    void set_header(const std::string& key, const std::string& value) {
        default_headers_[key] = value;
    }
    
    void set_default_header(const std::string& key, const std::string& value) {
        default_headers_[key] = value;
    }
    
    void set_rate_limiter(std::shared_ptr<RateLimiter> limiter) {
        rate_limiter_ = limiter;
    }
    
    static std::string url_encode(const std::string& value) {
        return genie::core::url_encode(value);
    }
    
    /**
     * @brief Execute HTTP request
     */
    HttpResponse execute(const HttpRequest& request) {
        auto start = std::chrono::steady_clock::now();
        
        // Build full URL
        std::string full_url = base_url_ + request.url;
        if (!request.query_params.empty()) {
            full_url += "?" + build_query_string(request.query_params);
        }
        
        // Check cache for GET requests
        if (request.method == HttpMethod::GET && config_.enable_cache) {
            auto cached = cache_.get(full_url);
            if (cached) {
                return *cached;
            }
        }
        
        // Apply rate limiting
        if (rate_limiter_) {
            rate_limiter_->wait_for_slot();
        }
        
        // Merge headers
        auto headers = default_headers_;
        for (const auto& [k, v] : request.headers) {
            headers[k] = v;
        }
        
        HttpResponse response;
        int attempts = 0;
        
        while (attempts < config_.max_retries) {
            attempts++;
            
            // Execute request via platform-native TLS
            response = execute_request(full_url, request.method, headers, request.body);
            
            if (response.ok() || response.is_not_found() || response.is_unauthorized()) {
                break;
            }
            
            if (response.is_rate_limited()) {
                // Exponential backoff for rate limits
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.retry_delay_ms * attempts));
            } else if (attempts < config_.max_retries) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.retry_delay_ms));
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Cache successful GET responses
        if (request.method == HttpMethod::GET && response.ok() && config_.enable_cache) {
            cache_.put(full_url, response, config_.default_cache_ttl);
        }
        
        return response;
    }
    
    // Convenience methods
    HttpResponse get(const std::string& path, 
                     const std::map<std::string, std::string>& params = {}) {
        HttpRequest req;
        req.method = HttpMethod::GET;
        req.url = path;
        req.query_params = params;
        return execute(req);
    }
    
    HttpResponse post(const std::string& path, const std::string& body) {
        HttpRequest req;
        req.method = HttpMethod::POST;
        req.url = path;
        req.body = body;
        return execute(req);
    }
    
    HttpResponse post(const std::string& path, const std::string& body, 
                      const std::string& content_type) {
        HttpRequest req;
        req.method = HttpMethod::POST;
        req.url = path;
        req.body = body;
        req.headers["Content-Type"] = content_type;
        return execute(req);
    }
    
    HttpResponse post(const std::string& path, const std::string& body,
                      const std::map<std::string, std::string>& headers) {
        HttpRequest req;
        req.method = HttpMethod::POST;
        req.url = path;
        req.body = body;
        req.headers = headers;
        return execute(req);
    }
    
    HttpResponse put(const std::string& path, const std::string& body) {
        HttpRequest req;
        req.method = HttpMethod::PUT;
        req.url = path;
        req.body = body;
        return execute(req);
    }
    
    HttpResponse patch(const std::string& path, const std::string& body) {
        HttpRequest req;
        req.method = HttpMethod::PATCH;
        req.url = path;
        req.body = body;
        return execute(req);
    }
    
    HttpResponse del(const std::string& path) {
        HttpRequest req;
        req.method = HttpMethod::DELETE;
        req.url = path;
        return execute(req);
    }
    
    ResponseCache& cache() { return cache_; }
    const Config& config() const { return config_; }

private:
    Config config_;
    std::string base_url_;
    std::map<std::string, std::string> default_headers_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    ResponseCache cache_;
    
    /**
     * @brief Execute actual HTTP request via platform-native TLS
     * 
     * Uses WinHTTP on Windows, SecureTransport on macOS, OpenSSL on Linux.
     * Supports HTTP and HTTPS, all methods, custom headers and body.
     */
    HttpResponse execute_request(const std::string& url,
                                 HttpMethod method,
                                 const std::map<std::string, std::string>& headers,
                                 const std::string& body) {
        HttpResponse response;
        
        // Store request details for inspection/testing
        last_request_url_ = url;
        last_request_method_ = method;
        last_request_headers_ = headers;
        last_request_body_ = body;
        
        // If mock response is set, return it (for testing)
        if (mock_response_) {
            response = *mock_response_;
            response.success = response.ok();
            return response;
        }
        
        // Execute real HTTP request via platform-native implementation
        auto platform_resp = platform_http_request(
            url,
            static_cast<int>(method),
            headers,
            body,
            config_.default_timeout_ms);
        
        response.status_code = platform_resp.status_code;
        response.body = std::move(platform_resp.body);
        response.headers = std::move(platform_resp.headers);
        response.error = std::move(platform_resp.error);
        response.success = platform_resp.success && 
                           (response.status_code >= 200 && response.status_code < 500);
        
        return response;
    }

public:
    // For testing/simulation
    void set_mock_response(const HttpResponse& response) {
        mock_response_ = response;
    }
    
    void clear_mock_response() {
        mock_response_.reset();
    }
    
    const std::string& last_request_url() const { return last_request_url_; }

private:
    std::optional<HttpResponse> mock_response_;
    std::string last_request_url_;
    HttpMethod last_request_method_;
    std::map<std::string, std::string> last_request_headers_;
    std::string last_request_body_;
};

/**
 * @brief API credentials vault
 */
class ApiCredentials {
public:
    void set(const std::string& provider, const std::string& key, 
             const std::string& secret = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        credentials_[provider] = {key, secret};
    }
    
    std::pair<std::string, std::string> get(const std::string& provider) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = credentials_.find(provider);
        if (it == credentials_.end()) {
            return {"", ""};
        }
        return it->second;
    }
    
    bool has(const std::string& provider) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return credentials_.count(provider) > 0;
    }
    
    void remove(const std::string& provider) {
        std::lock_guard<std::mutex> lock(mutex_);
        credentials_.erase(provider);
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::pair<std::string, std::string>> credentials_;
};

// Global credentials instance
inline ApiCredentials& api_credentials() {
    static ApiCredentials instance;
    return instance;
}

} // namespace genie::core

#endif // GENIE_CORE_HTTP_CLIENT_HPP
