/**
 * @file request_context.hpp
 * @brief Correlation ID tracing for distributed request tracking
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides request context propagation with correlation IDs for
 * tracing requests across service boundaries. Supports thread-local
 * context storage, nested spans, and structured logging integration.
 *
 * Zero external dependencies. Thread-safe. Cross-platform.
 */
#pragma once
#ifndef GENIE_REQUEST_CONTEXT_HPP
#define GENIE_REQUEST_CONTEXT_HPP

#include <string>
#include <chrono>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace genie {

// ============================================================================
// Correlation ID Generator
// ============================================================================

class CorrelationIdGenerator {
public:
    // Generate a unique correlation ID (UUID v4 format)
    static std::string generate() {
        static thread_local std::mt19937_64 rng(
            std::chrono::steady_clock::now().time_since_epoch().count() ^
            reinterpret_cast<uintptr_t>(&rng));

        std::uniform_int_distribution<uint64_t> dist;
        uint64_t a = dist(rng);
        uint64_t b = dist(rng);

        // UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL; // version 4
        b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL; // variant 1

        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << "-"
            << std::setw(4) << ((a >> 16) & 0xFFFF) << "-"
            << std::setw(4) << (a & 0xFFFF) << "-"
            << std::setw(4) << ((b >> 48) & 0xFFFF) << "-"
            << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
        return oss.str();
    }

    // Generate a short ID (for spans within a trace)
    static std::string generate_short() {
        static thread_local std::mt19937_64 rng(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<uint64_t> dist;
        uint64_t val = dist(rng);
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << val;
        return oss.str();
    }
};

// ============================================================================
// Span - a unit of work within a trace
// ============================================================================

struct Span {
    std::string                                 span_id;
    std::string                                 parent_span_id;
    std::string                                 operation_name;
    std::chrono::steady_clock::time_point       start_time;
    std::chrono::steady_clock::time_point       end_time;
    std::unordered_map<std::string, std::string> tags;
    std::vector<std::string>                    logs;
    bool                                        finished{false};

    double duration_ms() const {
        auto end = finished ? end_time : std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_time).count();
    }

    void finish() {
        if (!finished) {
            end_time = std::chrono::steady_clock::now();
            finished = true;
        }
    }

    void set_tag(const std::string& key, const std::string& value) {
        tags[key] = value;
    }

    void log(const std::string& message) {
        logs.push_back(message);
    }

    std::string to_json() const {
        std::ostringstream oss;
        oss << "{\"span_id\":\"" << span_id << "\""
            << ",\"parent_span_id\":\"" << parent_span_id << "\""
            << ",\"operation\":\"" << operation_name << "\""
            << ",\"duration_ms\":" << std::fixed << std::setprecision(3)
            << duration_ms()
            << ",\"finished\":" << (finished ? "true" : "false")
            << ",\"tags\":{";
        bool first = true;
        for (const auto& [k, v] : tags) {
            if (!first) oss << ",";
            oss << "\"" << k << "\":\"" << v << "\"";
            first = false;
        }
        oss << "},\"logs\":[";
        first = true;
        for (const auto& l : logs) {
            if (!first) oss << ",";
            oss << "\"" << l << "\"";
            first = false;
        }
        oss << "]}";
        return oss.str();
    }
};

// ============================================================================
// Request Context - holds trace and span information
// ============================================================================

class RequestContext {
public:
    RequestContext()
        : correlation_id_(CorrelationIdGenerator::generate())
        , created_at_(std::chrono::system_clock::now()) {}

    explicit RequestContext(const std::string& correlation_id)
        : correlation_id_(correlation_id)
        , created_at_(std::chrono::system_clock::now()) {}

    // Correlation ID
    const std::string& correlation_id() const { return correlation_id_; }

    // Request metadata
    void set_user_id(const std::string& user_id) { user_id_ = user_id; }
    void set_client_ip(const std::string& ip) { client_ip_ = ip; }
    void set_method(const std::string& method) { method_ = method; }
    void set_path(const std::string& path) { path_ = path; }
    void set_attribute(const std::string& key, const std::string& value) {
        attributes_[key] = value;
    }

    const std::string& user_id() const { return user_id_; }
    const std::string& client_ip() const { return client_ip_; }
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }

    std::string attribute(const std::string& key,
                         const std::string& default_val = "") const {
        auto it = attributes_.find(key);
        return it != attributes_.end() ? it->second : default_val;
    }

    // Span management
    Span& start_span(const std::string& operation_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        Span span;
        span.span_id = CorrelationIdGenerator::generate_short();
        span.parent_span_id = spans_.empty() ? "" : spans_.back().span_id;
        span.operation_name = operation_name;
        span.start_time = std::chrono::steady_clock::now();
        spans_.push_back(std::move(span));
        return spans_.back();
    }

    void finish_current_span() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!spans_.empty()) {
            spans_.back().finish();
        }
    }

    const std::vector<Span>& spans() const { return spans_; }

    // Elapsed time since context creation
    double elapsed_ms() const {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration<double, std::milli>(now - created_at_).count();
    }

    // HTTP header propagation
    struct TraceHeaders {
        std::string x_correlation_id;
        std::string x_request_id;
        std::string traceparent;    // W3C Trace Context format
    };

    TraceHeaders to_headers() const {
        TraceHeaders h;
        h.x_correlation_id = correlation_id_;
        h.x_request_id = correlation_id_;
        // W3C Trace Context: version-trace_id-parent_id-flags
        h.traceparent = "00-" + correlation_id_ + "-0000000000000000-01";
        return h;
    }

    // Parse incoming headers
    static RequestContext from_headers(const std::string& correlation_id_header,
                                      const std::string& traceparent_header = "") {
        if (!correlation_id_header.empty()) {
            return RequestContext(correlation_id_header);
        }
        if (!traceparent_header.empty()) {
            // Parse W3C traceparent: version-trace_id-parent_id-flags
            auto parts = split(traceparent_header, '-');
            if (parts.size() >= 2) {
                return RequestContext(parts[1]);
            }
        }
        return RequestContext(); // Generate new
    }

    // JSON representation
    std::string to_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\"correlation_id\":\"" << correlation_id_ << "\""
            << ",\"user_id\":\"" << user_id_ << "\""
            << ",\"client_ip\":\"" << client_ip_ << "\""
            << ",\"method\":\"" << method_ << "\""
            << ",\"path\":\"" << path_ << "\""
            << ",\"elapsed_ms\":" << std::fixed << std::setprecision(3)
            << elapsed_ms()
            << ",\"spans\":[";
        bool first = true;
        for (const auto& span : spans_) {
            if (!first) oss << ",";
            oss << span.to_json();
            first = false;
        }
        oss << "]}";
        return oss.str();
    }

private:
    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> parts;
        std::istringstream stream(s);
        std::string part;
        while (std::getline(stream, part, delim)) {
            parts.push_back(part);
        }
        return parts;
    }

    std::string                                         correlation_id_;
    std::chrono::system_clock::time_point               created_at_;
    std::string                                         user_id_;
    std::string                                         client_ip_;
    std::string                                         method_;
    std::string                                         path_;
    std::unordered_map<std::string, std::string>        attributes_;
    std::vector<Span>                                   spans_;
    mutable std::mutex                                  mutex_;
};

// ============================================================================
// Thread-local Context Storage
// ============================================================================

class ContextStorage {
public:
    static void set(std::shared_ptr<RequestContext> ctx) {
        current() = std::move(ctx);
    }

    static std::shared_ptr<RequestContext> get() {
        return current();
    }

    static void clear() {
        current().reset();
    }

    // RAII scope guard for automatic cleanup
    class ScopeGuard {
    public:
        explicit ScopeGuard(std::shared_ptr<RequestContext> ctx) {
            ContextStorage::set(std::move(ctx));
        }
        ~ScopeGuard() { ContextStorage::clear(); }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
    };

private:
    static std::shared_ptr<RequestContext>& current() {
        static thread_local std::shared_ptr<RequestContext> ctx;
        return ctx;
    }
};

// ============================================================================
// Span Scope Guard (RAII for span lifecycle)
// ============================================================================

class SpanGuard {
public:
    SpanGuard(RequestContext& ctx, const std::string& operation)
        : ctx_(ctx)
        , span_(ctx.start_span(operation)) {}

    ~SpanGuard() { span_.finish(); }

    Span& span() { return span_; }

    SpanGuard(const SpanGuard&) = delete;
    SpanGuard& operator=(const SpanGuard&) = delete;

private:
    RequestContext& ctx_;
    Span&           span_;
};

// ============================================================================
// Request Trace Log (collects completed traces)
// ============================================================================

class TraceLog {
public:
    static TraceLog& instance() {
        static TraceLog log;
        return log;
    }

    void record(const RequestContext& ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_traces_.push_back(ctx.to_json());
        if (completed_traces_.size() > max_traces_) {
            completed_traces_.erase(
                completed_traces_.begin(),
                completed_traces_.begin() +
                    static_cast<long>(completed_traces_.size() - max_traces_));
        }
    }

    std::vector<std::string> recent_traces(size_t count = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t n = std::min(count, completed_traces_.size());
        return std::vector<std::string>(
            completed_traces_.end() - static_cast<long>(n),
            completed_traces_.end());
    }

    size_t trace_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_traces_.size();
    }

    void set_max_traces(size_t max) { max_traces_ = max; }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_traces_.clear();
    }

private:
    TraceLog() = default;
    mutable std::mutex              mutex_;
    std::vector<std::string>        completed_traces_;
    size_t                          max_traces_{10000};
};

} // namespace genie

#endif // GENIE_REQUEST_CONTEXT_HPP
