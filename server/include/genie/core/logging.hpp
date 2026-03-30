/**
 * @file logging.hpp
 * @brief Logging framework for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_LOGGING_HPP
#define GENIE_LOGGING_HPP

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include <filesystem>

namespace genie {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

inline std::string log_level_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "?????";
    }
}

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string component;
    std::string message;
    
    std::string format() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()) % 1000;
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count()
           << " [" << log_level_string(level) << "] "
           << "[" << component << "] " << message;
        return ss.str();
    }
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
};

class ConsoleSink : public LogSink {
    std::mutex mutex_;
public:
    void write(const LogEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& out = (entry.level >= LogLevel::ERROR) ? std::cerr : std::cout;
        out << entry.format() << "\n";
    }
};

class FileSink : public LogSink {
    std::string base_path_;
    std::ofstream file_;
    std::mutex mutex_;
    size_t max_file_bytes_{0};       // 0 = no rotation
    int max_files_{10};
    size_t current_size_{0};

    void rotate() {
        if (max_file_bytes_ == 0 || base_path_.empty()) return;
        file_.close();
        // Remove oldest, shift existing: .9 -> .10 (deleted), .8 -> .9, ... .1 -> .2, base -> .1
        for (int i = max_files_ - 1; i >= 1; --i) {
            std::string src = base_path_ + "." + std::to_string(i);
            std::string dst = base_path_ + "." + std::to_string(i + 1);
            std::error_code ec;
            std::filesystem::rename(src, dst, ec);
        }
        {
            std::error_code ec;
            std::filesystem::rename(base_path_, base_path_ + ".1", ec);
        }
        file_.open(base_path_, std::ios::app);
        current_size_ = 0;
    }

public:
    explicit FileSink(const std::string& filename) : base_path_(filename), file_(filename, std::ios::app) {
        if (file_.is_open()) {
            file_.seekp(0, std::ios::end);
            current_size_ = static_cast<size_t>(file_.tellp());
        }
    }

    /** Enable log rotation: max size in MB, max number of rotated files */
    void set_rotation(int max_file_size_mb, int max_files) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_file_bytes_ = static_cast<size_t>(std::max(1, max_file_size_mb)) * 1024UL * 1024UL;
        max_files_ = std::max(1, max_files);
    }

    void write(const LogEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            std::string line = entry.format() + "\n";
            file_ << line;
            file_.flush();
            current_size_ += line.size();
            if (max_file_bytes_ > 0 && current_size_ >= max_file_bytes_) {
                rotate();
            }
        }
    }
};

// ---------------------------------------------------------------------------
// MemorySink — in-memory ring buffer, last N entries (default 500)
// Thread-safe. Accessible via Logger::memory_sink() for the /api/v1/logs endpoint.
// ---------------------------------------------------------------------------
class MemorySink : public LogSink {
    static constexpr size_t DEFAULT_CAPACITY = 500;
    std::vector<LogEntry> ring_;
    size_t head_{0};
    size_t count_{0};
    size_t capacity_;
    mutable std::mutex mutex_;

public:
    explicit MemorySink(size_t capacity = DEFAULT_CAPACITY) : capacity_(capacity) {
        ring_.resize(capacity);
    }

    void write(const LogEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ring_[head_] = entry;
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
    }

    /** Return up to n most recent entries at or above min_level, newest last */
    std::vector<LogEntry> recent(size_t n = 100,
                                 LogLevel min_level = LogLevel::DEBUG) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LogEntry> result;
        result.reserve(std::min(n, count_));
        // Walk ring from oldest to newest
        size_t start = (count_ == capacity_) ? head_ : 0;
        for (size_t i = 0; i < count_; ++i) {
            const auto& e = ring_[(start + i) % capacity_];
            if (e.level >= min_level) result.push_back(e);
        }
        // Trim to last n
        if (result.size() > n)
            result.erase(result.begin(), result.begin() + static_cast<long>(result.size() - n));
        return result;
    }

    /** Serialize recent entries to a JSON array string */
    std::string to_json(size_t n = 100, LogLevel min_level = LogLevel::DEBUG) const {
        auto entries = recent(n, min_level);
        std::string out = "[";
        bool first = true;
        for (const auto& e : entries) {
            if (!first) out += ",";
            first = false;
            auto t = std::chrono::system_clock::to_time_t(e.timestamp);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                e.timestamp.time_since_epoch()) % 1000;
            std::ostringstream ts;
            ts << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
               << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
            // Escape message for JSON
            std::string msg;
            for (char c : e.message) {
                if      (c == '"')  msg += "\\\"";
                else if (c == '\\') msg += "\\\\";
                else if (c == '\n') msg += "\\n";
                else if (c == '\r') msg += "\\r";
                else                msg += c;
            }
            out += "{\"ts\":\"" + ts.str() + "\""
                 + ",\"level\":\"" + log_level_string(e.level) + "\""
                 + ",\"component\":\"" + e.component + "\""
                 + ",\"message\":\"" + msg + "\"}";
        }
        out += "]";
        return out;
    }
};

class Logger {
    std::vector<std::shared_ptr<LogSink>> sinks_;
    std::shared_ptr<MemorySink> memory_sink_;
    LogLevel min_level_{LogLevel::INFO};
    std::string component_{"Genie"};
    mutable std::mutex mutex_;

    Logger() {
        sinks_.push_back(std::make_shared<ConsoleSink>());
        memory_sink_ = std::make_shared<MemorySink>(500);
        sinks_.push_back(memory_sink_);
    }

public:
    static Logger& instance() { static Logger inst; return inst; }

    /** Access the in-memory sink for log retrieval (e.g. /api/v1/logs endpoint) */
    MemorySink& memory() { return *memory_sink_; }

    void set_level(LogLevel level) { min_level_ = level; }
    LogLevel level() const { return min_level_; }
    void set_component(const std::string& comp) { component_ = comp; }
    
    void add_sink(std::shared_ptr<LogSink> sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.push_back(sink);
    }
    
    void add_file(const std::string& filename,
                  int max_file_size_mb = 0, int max_files = 10) {
        auto sink = std::make_shared<FileSink>(filename);
        if (max_file_size_mb > 0) {
            sink->set_rotation(max_file_size_mb, max_files);
        }
        add_sink(sink);
    }
    
    void clear_sinks() {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.clear();
    }
    
    void log(LogLevel level, const std::string& component, const std::string& message) {
        if (level < min_level_) return;
        
        LogEntry entry{std::chrono::system_clock::now(), level, component, message};
        
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& sink : sinks_) {
            sink->write(entry);
        }
    }
    
    void debug(const std::string& msg) { log(LogLevel::DEBUG, component_, msg); }
    void info(const std::string& msg) { log(LogLevel::INFO, component_, msg); }
    void warn(const std::string& msg) { log(LogLevel::WARN, component_, msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, component_, msg); }
    void fatal(const std::string& msg) { log(LogLevel::FATAL, component_, msg); }
    
    // Two-argument versions with explicit component
    void debug(const std::string& comp, const std::string& msg) { log(LogLevel::DEBUG, comp, msg); }
    void info(const std::string& comp, const std::string& msg) { log(LogLevel::INFO, comp, msg); }
    void warn(const std::string& comp, const std::string& msg) { log(LogLevel::WARN, comp, msg); }
    void error(const std::string& comp, const std::string& msg) { log(LogLevel::ERROR, comp, msg); }
    void fatal(const std::string& comp, const std::string& msg) { log(LogLevel::FATAL, comp, msg); }
};

inline Logger& logger() { return Logger::instance(); }

// Macro helpers for variadic argument handling
#define LOG_GET_MACRO(_1, _2, NAME, ...) NAME

// Single-argument macros (use function name as component)
#define LOG_DEBUG_1(msg) genie::logger().log(genie::LogLevel::DEBUG, __func__, msg)
#define LOG_INFO_1(msg)  genie::logger().log(genie::LogLevel::INFO, __func__, msg)
#define LOG_WARN_1(msg)  genie::logger().log(genie::LogLevel::WARN, __func__, msg)
#define LOG_ERROR_1(msg) genie::logger().log(genie::LogLevel::ERROR, __func__, msg)
#define LOG_FATAL_1(msg) genie::logger().log(genie::LogLevel::FATAL, __func__, msg)

// Two-argument macros (explicit component)
#define LOG_DEBUG_2(comp, msg) genie::logger().log(genie::LogLevel::DEBUG, comp, msg)
#define LOG_INFO_2(comp, msg)  genie::logger().log(genie::LogLevel::INFO, comp, msg)
#define LOG_WARN_2(comp, msg)  genie::logger().log(genie::LogLevel::WARN, comp, msg)
#define LOG_ERROR_2(comp, msg) genie::logger().log(genie::LogLevel::ERROR, comp, msg)
#define LOG_FATAL_2(comp, msg) genie::logger().log(genie::LogLevel::FATAL, comp, msg)

// Variadic macros that select between 1 and 2 argument versions
#define LOG_DEBUG(...) LOG_GET_MACRO(__VA_ARGS__, LOG_DEBUG_2, LOG_DEBUG_1)(__VA_ARGS__)
#define LOG_INFO(...)  LOG_GET_MACRO(__VA_ARGS__, LOG_INFO_2, LOG_INFO_1)(__VA_ARGS__)
#define LOG_WARN(...)  LOG_GET_MACRO(__VA_ARGS__, LOG_WARN_2, LOG_WARN_1)(__VA_ARGS__)
#define LOG_ERROR(...) LOG_GET_MACRO(__VA_ARGS__, LOG_ERROR_2, LOG_ERROR_1)(__VA_ARGS__)
#define LOG_FATAL(...) LOG_GET_MACRO(__VA_ARGS__, LOG_FATAL_2, LOG_FATAL_1)(__VA_ARGS__)

} // namespace genie
#endif // GENIE_LOGGING_HPP
