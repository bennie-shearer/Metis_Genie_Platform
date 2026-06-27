/**
 * @file json_logger.hpp
 * @brief Structured JSON logging for machine-parseable log output
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Produces JSON-formatted log entries with timestamp, level, component,
 * message, and optional key-value context. Compatible with log aggregation
 * tools (ELK, Splunk, CloudWatch) without custom parsers.
 *
 * Platforms: Windows, Linux, macOS (C++20, zero dependencies)
 */

#pragma once

#include <string>
#include <string_view>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <utility>
#include <mutex>
#include <fstream>
#include <iostream>

namespace genie::core {

/**
 * @brief JSON-structured log entry builder and writer
 */
class JsonLogger {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR, FATAL };

    struct Config {
        bool enabled = false;                  ///< JSON logging mode
        std::string file_path;                 ///< Output file (empty = stdout)
        Level min_level = Level::INFO;         ///< Minimum log level
        bool include_request_id = true;        ///< Include X-Request-ID
    };

    JsonLogger() : config_{} {}
    explicit JsonLogger(const Config& cfg) : config_(cfg) {
        if (!cfg.file_path.empty()) {
            file_.open(cfg.file_path, std::ios::app);
        }
    }

    /** Log a structured JSON entry */
    void log(Level level, std::string_view component, std::string_view message,
             const std::vector<std::pair<std::string, std::string>>& context = {}) {
        if (!config_.enabled || level < config_.min_level) return;

        std::ostringstream oss;
        oss << "{\"timestamp\":\"" << timestamp() << "\""
            << ",\"level\":\"" << level_str(level) << "\""
            << ",\"component\":\"" << escape(component) << "\""
            << ",\"message\":\"" << escape(message) << "\"";

        for (const auto& [key, value] : context) {
            oss << ",\"" << escape(key) << "\":\"" << escape(value) << "\"";
        }

        oss << "}";

        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_ << oss.str() << "\n";
            file_.flush();
        } else {
            std::cout << oss.str() << "\n";
        }
    }

    void set_enabled(bool enabled) { config_.enabled = enabled; }
    [[nodiscard]] bool is_enabled() const { return config_.enabled; }
    [[nodiscard]] const Config& config() const { return config_; }

private:
    Config config_;
    std::ofstream file_;
    std::mutex mutex_;

    [[nodiscard]] static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm_buf{};
#ifdef _WIN32
        gmtime_s(&tm_buf, &time);
#else
        gmtime_r(&time, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count() << "Z";
        return oss.str();
    }

    [[nodiscard]] static std::string_view level_str(Level l) {
        switch (l) {
            case Level::DEBUG: return "DEBUG";
            case Level::INFO:  return "INFO";
            case Level::WARN:  return "WARN";
            case Level::ERROR: return "ERROR";
            case Level::FATAL: return "FATAL";
        }
        return "UNKNOWN";
    }

    [[nodiscard]] static std::string escape(std::string_view s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }
};

} // namespace genie::core
