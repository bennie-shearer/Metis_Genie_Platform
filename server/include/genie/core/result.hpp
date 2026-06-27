/**
 * @file result.hpp
 * @brief Typed Result<T,E> error handling framework
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides a monadic Result type for structured error propagation:
 * - Result<T, E> for success/failure with typed errors
 * - Error categories and error chains
 * - Monadic map/flatMap/recover operations
 * - Exception-free error handling path
 * - Composable error transforms
 * - JSON-serializable error details
 * - Stack-friendly (no heap allocation for errors)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_RESULT_HPP
#define GENIE_CORE_RESULT_HPP

#include <string>
#include <variant>
#include <optional>
#include <functional>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>

namespace genie {
namespace core {

// ============================================================================
// Error Categories
// ============================================================================

enum class ErrorCategory {
    None,
    Validation,
    NotFound,
    Unauthorized,
    Forbidden,
    Timeout,
    NetworkError,
    DatabaseError,
    ParseError,
    ConfigError,
    BrokerError,
    MarketDataError,
    ComplianceError,
    RiskLimitError,
    InternalError,
    RateLimited,
    ServiceUnavailable,
    Cancelled,
    Unknown
};

[[nodiscard]] inline std::string error_category_string(ErrorCategory c) {
    switch (c) {
        case ErrorCategory::None:               return "none";
        case ErrorCategory::Validation:         return "validation";
        case ErrorCategory::NotFound:           return "not_found";
        case ErrorCategory::Unauthorized:       return "unauthorized";
        case ErrorCategory::Forbidden:          return "forbidden";
        case ErrorCategory::Timeout:            return "timeout";
        case ErrorCategory::NetworkError:       return "network_error";
        case ErrorCategory::DatabaseError:      return "database_error";
        case ErrorCategory::ParseError:         return "parse_error";
        case ErrorCategory::ConfigError:        return "config_error";
        case ErrorCategory::BrokerError:        return "broker_error";
        case ErrorCategory::MarketDataError:    return "market_data_error";
        case ErrorCategory::ComplianceError:    return "compliance_error";
        case ErrorCategory::RiskLimitError:     return "risk_limit_error";
        case ErrorCategory::InternalError:      return "internal_error";
        case ErrorCategory::RateLimited:        return "rate_limited";
        case ErrorCategory::ServiceUnavailable: return "service_unavailable";
        case ErrorCategory::Cancelled:          return "cancelled";
        case ErrorCategory::Unknown:            return "unknown";
    }
    return "unknown";
}

[[nodiscard]] inline int error_category_http_status(ErrorCategory c) {
    switch (c) {
        case ErrorCategory::Validation:         return 400;
        case ErrorCategory::NotFound:           return 404;
        case ErrorCategory::Unauthorized:       return 401;
        case ErrorCategory::Forbidden:          return 403;
        case ErrorCategory::Timeout:            return 408;
        case ErrorCategory::RateLimited:        return 429;
        case ErrorCategory::ServiceUnavailable: return 503;
        case ErrorCategory::Cancelled:          return 499;
        default:                                return 500;
    }
}

// ============================================================================
// Error Type
// ============================================================================

/**
 * @brief Structured error with category, message, and chain
 */
struct Error {
    ErrorCategory category{ErrorCategory::Unknown};
    std::string message;
    std::string code;              // Machine-readable error code
    std::string source;            // Module/component that produced error
    std::string detail;            // Extended detail (stack, context)
    std::chrono::system_clock::time_point timestamp;
    std::optional<Error*> cause;   // Raw pointer to avoid circular shared_ptr

    Error() : timestamp(std::chrono::system_clock::now()) {}

    Error(ErrorCategory cat, const std::string& msg)
        : category(cat), message(msg), timestamp(std::chrono::system_clock::now()) {}

    Error(ErrorCategory cat, const std::string& msg, const std::string& src)
        : category(cat), message(msg), source(src),
          timestamp(std::chrono::system_clock::now()) {}

    [[nodiscard]] int http_status() const {
        return error_category_http_status(category);
    }

    [[nodiscard]] bool is(ErrorCategory cat) const { return category == cat; }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "[" << error_category_string(category) << "]";
        if (!source.empty()) oss << " " << source << ":";
        oss << " " << message;
        if (!code.empty()) oss << " (code=" << code << ")";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{\"category\":\"" << error_category_string(category)
            << "\",\"message\":\"" << message << "\"";
        if (!code.empty()) oss << ",\"code\":\"" << code << "\"";
        if (!source.empty()) oss << ",\"source\":\"" << source << "\"";
        oss << ",\"http_status\":" << http_status() << "}";
        return oss.str();
    }
};

// Convenience error constructors
inline Error validation_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::Validation, msg, src);
}
inline Error not_found_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::NotFound, msg, src);
}
inline Error unauthorized_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::Unauthorized, msg, src);
}
inline Error timeout_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::Timeout, msg, src);
}
inline Error internal_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::InternalError, msg, src);
}
inline Error broker_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::BrokerError, msg, src);
}
inline Error market_error(const std::string& msg, const std::string& src = "") {
    return Error(ErrorCategory::MarketDataError, msg, src);
}

// ============================================================================
// Result<T, E>
// ============================================================================

/**
 * @brief Monadic Result type for error handling without exceptions
 *
 * Usage:
 *   Result<double> calc = divide(10.0, 0.0);
 *   if (calc) { use(*calc); }
 *   else { handle(calc.error()); }
 *
 *   auto result = fetch_price("AAPL")
 *       .map([](double p) { return p * 1.1; })
 *       .map_error([](Error e) { log(e); return e; });
 */
template<typename T, typename E = Error>
class Result {
public:
    // Success constructors
    Result(const T& value) : data_(value) {}
    Result(T&& value) : data_(std::move(value)) {}

    // Error constructors
    Result(const E& error) : data_(error) {}
    Result(E&& error) : data_(std::move(error)) {}

    // Check success
    [[nodiscard]] bool ok() const { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool is_error() const { return std::holds_alternative<E>(data_); }
    explicit operator bool() const { return ok(); }

    // Access value (undefined if error)
    [[nodiscard]] const T& value() const { return std::get<T>(data_); }
    [[nodiscard]] T& value() { return std::get<T>(data_); }
    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] T& operator*() { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }
    [[nodiscard]] T* operator->() { return &value(); }

    // Access error (undefined if success)
    [[nodiscard]] const E& error() const { return std::get<E>(data_); }
    [[nodiscard]] E& error() { return std::get<E>(data_); }

    // Value with default
    [[nodiscard]] T value_or(const T& default_val) const {
        return ok() ? value() : default_val;
    }

    // Monadic map: transform success value
    template<typename F>
    auto map(F&& f) const -> Result<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (ok()) return Result<U, E>(f(value()));
        return Result<U, E>(error());
    }

    // Monadic flatMap: chain Result-returning operations
    template<typename F>
    auto flat_map(F&& f) const -> decltype(f(std::declval<T>())) {
        if (ok()) return f(value());
        using RetType = decltype(f(std::declval<T>()));
        return RetType(error());
    }

    // Transform error
    template<typename F>
    auto map_error(F&& f) const -> Result<T, decltype(f(std::declval<E>()))> {
        using E2 = decltype(f(std::declval<E>()));
        if (ok()) return Result<T, E2>(value());
        return Result<T, E2>(f(error()));
    }

    // Recover from error
    template<typename F>
    Result<T, E> recover(F&& f) const {
        if (ok()) return *this;
        return f(error());
    }

    // Execute side effect on success
    template<typename F>
    const Result& on_success(F&& f) const {
        if (ok()) f(value());
        return *this;
    }

    // Execute side effect on error
    template<typename F>
    const Result& on_error(F&& f) const {
        if (is_error()) f(error());
        return *this;
    }

private:
    std::variant<T, E> data_;
};

// ============================================================================
// Result<void, E> specialization
// ============================================================================

template<typename E>
class Result<void, E> {
public:
    Result() : error_() {}  // Success
    Result(const E& error) : error_(error) {}
    Result(E&& error) : error_(std::move(error)) {}

    [[nodiscard]] bool ok() const { return !error_.has_value(); }
    [[nodiscard]] bool is_error() const { return error_.has_value(); }
    explicit operator bool() const { return ok(); }

    [[nodiscard]] const E& error() const { return *error_; }

    template<typename F>
    const Result& on_success(F&& f) const {
        if (ok()) f();
        return *this;
    }

    template<typename F>
    const Result& on_error(F&& f) const {
        if (is_error()) f(error());
        return *this;
    }

private:
    std::optional<E> error_;
};

// ============================================================================
// Factory Helpers
// ============================================================================

template<typename T>
Result<T, Error> Ok(T&& value) {
    return Result<T, Error>(std::forward<T>(value));
}

template<typename T>
Result<T, Error> Ok(const T& value) {
    return Result<T, Error>(value);
}

inline Result<void, Error> OkVoid() {
    return Result<void, Error>();
}

template<typename T = void>
Result<T, Error> Err(ErrorCategory cat, const std::string& msg, const std::string& src = "") {
    return Result<T, Error>(Error(cat, msg, src));
}

template<typename T = void>
Result<T, Error> Err(Error error) {
    return Result<T, Error>(std::move(error));
}

// ============================================================================
// Result Collection Utilities
// ============================================================================

/**
 * @brief Collect multiple Results, returning first error or all values
 */
template<typename T>
Result<std::vector<T>, Error> collect(const std::vector<Result<T, Error>>& results) {
    std::vector<T> values;
    values.reserve(results.size());
    for (const auto& r : results) {
        if (!r) return Result<std::vector<T>, Error>(r.error());
        values.push_back(r.value());
    }
    return Result<std::vector<T>, Error>(std::move(values));
}

/**
 * @brief Collect results, gathering all errors
 */
struct MultiError {
    std::vector<Error> errors;
    [[nodiscard]] bool empty() const { return errors.empty(); }
    [[nodiscard]] int count() const { return static_cast<int>(errors.size()); }
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << count() << " errors:";
        for (const auto& e : errors) oss << "\n  - " << e.format();
        return oss.str();
    }
};

template<typename T>
Result<std::vector<T>, MultiError> collect_all(const std::vector<Result<T, Error>>& results) {
    std::vector<T> values;
    MultiError errors;
    for (const auto& r : results) {
        if (r) values.push_back(r.value());
        else errors.errors.push_back(r.error());
    }
    if (!errors.empty()) return Result<std::vector<T>, MultiError>(std::move(errors));
    return Result<std::vector<T>, MultiError>(std::move(values));
}

} // namespace core
} // namespace genie

#endif // GENIE_CORE_RESULT_HPP
