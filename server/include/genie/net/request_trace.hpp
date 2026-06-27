/**
 * @file request_trace.hpp
 * @brief Request ID generation and tracing for end-to-end request tracking
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Generates a unique request ID for each API call, included in response
 * headers (X-Request-ID) and all associated log entries. Enables
 * correlation of client requests with server-side processing.
 *
 * Platforms: Windows, Linux, macOS (C++20, zero dependencies)
 */

#pragma once

#include <string>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::net {

/**
 * @brief Generates unique, sortable request IDs
 *
 * Format: "req-{timestamp_hex}-{counter_hex}"
 * Example: "req-18d5a3b2c-00042f"
 */
class RequestIdGenerator {
public:
    /** Generate a unique request ID */
    [[nodiscard]] static std::string generate() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        auto seq = counter_.fetch_add(1, std::memory_order_relaxed);

        std::ostringstream oss;
        oss << "req-" << std::hex << ms << "-" << std::setw(6) << std::setfill('0') << (seq & 0xFFFFFF);
        return oss.str();
    }

    /** Reset counter (for testing) */
    static void reset() { counter_.store(0, std::memory_order_relaxed); }

    /** Get total requests generated */
    [[nodiscard]] static uint64_t total_requests() {
        return counter_.load(std::memory_order_relaxed);
    }

private:
    static inline std::atomic<uint64_t> counter_{0};
};

/**
 * @brief Request trace context carried through a single request lifecycle
 */
struct RequestTrace {
    std::string request_id;
    std::string method;
    std::string path;
    std::string client_ip;
    std::chrono::steady_clock::time_point start_time;

    RequestTrace()
        : request_id(RequestIdGenerator::generate())
        , start_time(std::chrono::steady_clock::now()) {}

    /** Elapsed time in milliseconds */
    [[nodiscard]] double elapsed_ms() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_time).count();
    }
};

} // namespace genie::net
