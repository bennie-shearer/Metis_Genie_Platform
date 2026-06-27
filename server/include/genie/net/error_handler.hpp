/**
 * @file error_handler.hpp
 * @brief Structured Error Handling with Trace IDs for REST API
 * @version 5.5.11
 *
 * Provides consistent error response formatting across all API endpoints
 * with unique trace IDs for debugging and log correlation.
 *
 * @note Cross-platform: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (AppleClang)
 *
 * Copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once

#include <string>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace genie::net {

/**
 * @brief Standardized error codes for REST API responses
 */
enum class ErrorCode {
    VALIDATION_ERROR     = 1001,
    AUTHENTICATION_ERROR = 1002,
    AUTHORIZATION_ERROR  = 1003,
    NOT_FOUND           = 1004,
    RATE_LIMITED        = 1005,
    INTERNAL_ERROR      = 1006,
    SERVICE_UNAVAILABLE = 1007,
    BAD_REQUEST         = 1008,
    CONFLICT            = 1009,
    TIMEOUT             = 1010
};

/**
 * @brief Generates structured error responses with trace IDs
 *
 * Response format:
 * @code
 * {
 *   "error": "Human-readable message",
 *   "code": 1001,
 *   "trace_id": "genie-20260208-153045-000042",
 *   "timestamp": "2026-02-08T15:30:45Z",
 *   "path": "/api/v1/orders",
 *   "details": "Optional details"
 * }
 * @endcode
 */
class ErrorHandler {
public:
    /**
     * @brief Build a structured error JSON response
     * @param code Error code enum
     * @param message Human-readable error message
     * @param path Request path that caused the error
     * @param details Optional additional details
     * @return JSON string
     */
    static std::string build(ErrorCode code, const std::string& message,
                             const std::string& path = "",
                             const std::string& details = "") {
        std::string trace_id = generate_trace_id();

        std::ostringstream json;
        json << "{\"error\":\"" << json_escape(message) << "\""
             << ",\"code\":" << static_cast<int>(code)
             << ",\"trace_id\":\"" << trace_id << "\""
             << ",\"timestamp\":\"" << iso_timestamp() << "\"";

        if (!path.empty())
            json << ",\"path\":\"" << json_escape(path) << "\"";
        if (!details.empty())
            json << ",\"details\":\"" << json_escape(details) << "\"";

        json << "}";
        return json.str();
    }

    /**
     * @brief Get HTTP status code for an error code
     */
    static int http_status(ErrorCode code) {
        static const std::unordered_map<int, int> mapping = {
            {static_cast<int>(ErrorCode::VALIDATION_ERROR), 400},
            {static_cast<int>(ErrorCode::BAD_REQUEST), 400},
            {static_cast<int>(ErrorCode::AUTHENTICATION_ERROR), 401},
            {static_cast<int>(ErrorCode::AUTHORIZATION_ERROR), 403},
            {static_cast<int>(ErrorCode::NOT_FOUND), 404},
            {static_cast<int>(ErrorCode::CONFLICT), 409},
            {static_cast<int>(ErrorCode::RATE_LIMITED), 429},
            {static_cast<int>(ErrorCode::INTERNAL_ERROR), 500},
            {static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE), 503},
            {static_cast<int>(ErrorCode::TIMEOUT), 504},
        };
        auto it = mapping.find(static_cast<int>(code));
        return (it != mapping.end()) ? it->second : 500;
    }

    /**
     * @brief Get error code name as string
     */
    static std::string code_name(ErrorCode code) {
        static const std::unordered_map<int, std::string> names = {
            {static_cast<int>(ErrorCode::VALIDATION_ERROR), "VALIDATION_ERROR"},
            {static_cast<int>(ErrorCode::AUTHENTICATION_ERROR), "AUTHENTICATION_ERROR"},
            {static_cast<int>(ErrorCode::AUTHORIZATION_ERROR), "AUTHORIZATION_ERROR"},
            {static_cast<int>(ErrorCode::NOT_FOUND), "NOT_FOUND"},
            {static_cast<int>(ErrorCode::RATE_LIMITED), "RATE_LIMITED"},
            {static_cast<int>(ErrorCode::INTERNAL_ERROR), "INTERNAL_ERROR"},
            {static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE), "SERVICE_UNAVAILABLE"},
            {static_cast<int>(ErrorCode::BAD_REQUEST), "BAD_REQUEST"},
            {static_cast<int>(ErrorCode::CONFLICT), "CONFLICT"},
            {static_cast<int>(ErrorCode::TIMEOUT), "TIMEOUT"},
        };
        auto it = names.find(static_cast<int>(code));
        return (it != names.end()) ? it->second : "UNKNOWN";
    }

private:
    static inline std::atomic<uint64_t> counter_{0};

    static std::string generate_trace_id() {
        uint64_t seq = counter_.fetch_add(1, std::memory_order_relaxed);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        std::ostringstream ss;
        ss << "genie-"
           << std::put_time(&tm, "%Y%m%d-%H%M%S")
           << "-" << std::setfill('0') << std::setw(6) << seq;
        return ss.str();
    }

    static std::string iso_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    static std::string json_escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    }
};

}  // namespace genie::net
