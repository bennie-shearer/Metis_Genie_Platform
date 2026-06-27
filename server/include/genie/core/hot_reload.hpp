/**
 * @file hot_reload.hpp
 * @brief Configuration hot-reload manager for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Watches configuration files for changes and applies updates without restart.
 * Uses filesystem polling for cross-platform compatibility (Windows/Linux/macOS).
 * Thread-safe with atomic swaps of configuration state.
 *
 * Features:
 *  - Filesystem polling at configurable interval (default 2s)
 *  - Content-hash comparison to avoid false positives on metadata changes
 *  - Debouncing to prevent rapid-fire reloads during multi-file saves
 *  - Validation hooks to reject invalid configurations before applying
 *  - Automatic rollback on validation failure or callback exception
 *  - Change history with diff tracking and configurable depth
 *  - Environment variable override merging
 *  - Graceful shutdown with thread join
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_HOT_RELOAD_HPP
#define GENIE_HOT_RELOAD_HPP

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <optional>
#include <deque>
#include <algorithm>

namespace genie::core {

// ============================================================================
// Data Structures
// ============================================================================

/** @brief File change detection entry with snapshot state */
struct WatchedFile {
    std::string path;
    std::filesystem::file_time_type last_modified{};
    std::size_t last_size{0};
    std::string last_hash;
    std::string last_content;
    bool exists{false};
};

/** @brief Record of a configuration change event */
struct ReloadEvent {
    std::string path;
    std::string timestamp;
    std::string old_hash;
    std::string new_hash;
    std::size_t old_size{0};
    std::size_t new_size{0};
    bool validation_passed{true};
    bool rollback_triggered{false};
    double detection_latency_ms{0.0};
    std::string error_message;
};

/** @brief Hot reload statistics */
struct HotReloadStats {
    uint64_t total_reloads{0};
    uint64_t successful_reloads{0};
    uint64_t failed_validations{0};
    uint64_t rollbacks{0};
    uint64_t file_not_found{0};
    uint64_t poll_cycles{0};
    std::size_t watched_files{0};
    double avg_detection_latency_ms{0.0};
    bool is_running{false};
    std::string last_reload_time;
    std::vector<ReloadEvent> recent_events;
};

/** @brief Environment variable override entry */
struct EnvOverride {
    std::string env_var;
    std::string config_key;
    std::string default_value;
    std::string current_value;
    bool applied{false};
};

/** @brief Callback signatures */
using ReloadCallback = std::function<void(const std::string&, const std::string&)>;
using ValidationFunc = std::function<bool(const std::string&, const std::string&)>;

/** @brief Debounce configuration */
struct DebounceConfig {
    std::chrono::milliseconds window{std::chrono::milliseconds(500)};
    bool enabled{true};
};

// ============================================================================
// HotReloadManager
// ============================================================================

/**
 * @class HotReloadManager
 * @brief Monitors files for changes and triggers reload callbacks
 *
 * Design:
 *  - Filesystem polling at configurable interval (default 2s)
 *  - DJB2a content-hash comparison to avoid false positives
 *  - Debouncing to coalesce rapid changes within a time window
 *  - Pre-reload validation with automatic rollback on failure
 *  - Change history with configurable depth (default 100 events)
 *  - Environment variable overlay support for 12-factor config
 *  - Thread-safe callback dispatch with exception handling
 *  - Graceful shutdown with thread join
 */
class HotReloadManager {
public:
    explicit HotReloadManager(
        std::chrono::milliseconds poll_interval = std::chrono::milliseconds(2000),
        std::size_t max_history = 100
    ) : poll_interval_(poll_interval), max_history_(max_history) {}

    ~HotReloadManager() { stop(); }

    HotReloadManager(const HotReloadManager&) = delete;
    HotReloadManager& operator=(const HotReloadManager&) = delete;

    // ---- Validation Configuration ----

    /** @brief Set global validation function applied before any reload */
    void set_validator(ValidationFunc validator) {
        std::lock_guard lock(mutex_);
        global_validator_ = std::move(validator);
    }

    /** @brief Set per-file validation function */
    void set_file_validator(const std::string& path, ValidationFunc validator) {
        std::lock_guard lock(mutex_);
        per_file_validators_[path] = std::move(validator);
    }

    /** @brief Configure debouncing behavior */
    void set_debounce(DebounceConfig config) {
        std::lock_guard lock(mutex_);
        debounce_ = config;
    }

    // ---- Environment Overrides ----

    /** @brief Register an environment variable override for a config key */
    void add_env_override(const std::string& env_var, const std::string& config_key,
                          const std::string& default_value = "") {
        std::lock_guard lock(mutex_);
        EnvOverride ov;
        ov.env_var = env_var;
        ov.config_key = config_key;
        ov.default_value = default_value;
        const char* val = std::getenv(env_var.c_str());
        ov.current_value = val ? val : default_value;
        ov.applied = (val != nullptr);
        env_overrides_.push_back(std::move(ov));
    }

    /** @brief Refresh all environment variable overrides */
    std::size_t refresh_env_overrides() {
        std::lock_guard lock(mutex_);
        std::size_t changed = 0;
        for (auto& ov : env_overrides_) {
            const char* val = std::getenv(ov.env_var.c_str());
            std::string new_val = val ? val : ov.default_value;
            if (new_val != ov.current_value) {
                ov.current_value = new_val;
                ov.applied = (val != nullptr);
                changed++;
            }
        }
        return changed;
    }

    /** @brief Get current environment overrides */
    [[nodiscard]] std::vector<EnvOverride> env_overrides() const {
        std::lock_guard lock(mutex_);
        return env_overrides_;
    }

    // ---- Watch Management ----

    /** @brief Register a file to watch with a change callback */
    void watch(const std::string& path, ReloadCallback callback) {
        std::lock_guard lock(mutex_);
        WatchedFile wf;
        wf.path = path;
        wf.exists = std::filesystem::exists(path);
        if (wf.exists) {
            wf.last_modified = std::filesystem::last_write_time(path);
            wf.last_size = std::filesystem::file_size(path);
            wf.last_hash = compute_hash(path);
            wf.last_content = read_file(path);
        }
        watches_.push_back({std::move(wf), std::move(callback), {}, false});
    }

    /** @brief Unwatch a file */
    bool unwatch(const std::string& path) {
        std::lock_guard lock(mutex_);
        auto it = std::remove_if(watches_.begin(), watches_.end(),
            [&](const WatchEntry& e) { return e.file.path == path; });
        if (it != watches_.end()) {
            watches_.erase(it, watches_.end());
            return true;
        }
        return false;
    }

    /** @brief List all watched file paths */
    [[nodiscard]] std::vector<std::string> watched_paths() const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> paths;
        paths.reserve(watches_.size());
        for (const auto& w : watches_) paths.push_back(w.file.path);
        return paths;
    }

    // ---- Lifecycle ----

    /** @brief Start the polling thread */
    void start() {
        if (running_.exchange(true)) return;
        poll_thread_ = std::thread([this] { poll_loop(); });
    }

    /** @brief Stop the polling thread */
    void stop() {
        running_ = false;
        if (poll_thread_.joinable()) poll_thread_.join();
    }

    /** @brief Check if manager is running */
    [[nodiscard]] bool is_running() const { return running_; }

    // ---- Manual Triggers ----

    /** @brief Force reload of a specific file, bypassing change detection */
    bool force_reload(const std::string& path) {
        std::lock_guard lock(mutex_);
        for (auto& entry : watches_) {
            if (entry.file.path == path) {
                return execute_reload(entry.file, entry.callback);
            }
        }
        return false;
    }

    /** @brief Force reload of all watched files */
    std::size_t force_reload_all() {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (auto& entry : watches_) {
            if (execute_reload(entry.file, entry.callback)) count++;
        }
        return count;
    }

    /** @brief Rollback a file to its previous content snapshot */
    bool rollback(const std::string& path) {
        std::lock_guard lock(mutex_);
        for (auto& entry : watches_) {
            if (entry.file.path == path && !entry.file.last_content.empty()) {
                entry.callback(path, entry.file.last_content);
                rollback_count_++;
                record_event(path, entry.file.last_hash, entry.file.last_hash,
                             entry.file.last_size, entry.file.last_size,
                             true, true, 0.0, "Manual rollback");
                return true;
            }
        }
        return false;
    }

    // ---- Statistics ----

    [[nodiscard]] std::size_t watch_count() const {
        std::lock_guard lock(mutex_);
        return watches_.size();
    }

    [[nodiscard]] uint64_t reload_count() const { return reload_count_; }
    [[nodiscard]] uint64_t validation_failure_count() const { return validation_failures_; }
    [[nodiscard]] uint64_t rollback_total() const { return rollback_count_; }

    /** @brief Get comprehensive statistics */
    [[nodiscard]] HotReloadStats stats() const {
        std::lock_guard lock(mutex_);
        HotReloadStats s;
        s.total_reloads = reload_count_;
        s.successful_reloads = reload_count_ - validation_failures_;
        s.failed_validations = validation_failures_;
        s.rollbacks = rollback_count_;
        s.file_not_found = file_not_found_count_;
        s.poll_cycles = poll_cycles_;
        s.watched_files = watches_.size();
        s.is_running = running_;
        s.avg_detection_latency_ms = (reload_count_ > 0)
            ? total_latency_ms_ / static_cast<double>(reload_count_) : 0.0;
        s.recent_events.assign(history_.begin(), history_.end());
        if (!history_.empty()) s.last_reload_time = history_.back().timestamp;
        return s;
    }

    /** @brief Get change history */
    [[nodiscard]] std::vector<ReloadEvent> history() const {
        std::lock_guard lock(mutex_);
        return {history_.begin(), history_.end()};
    }

private:
    struct WatchEntry {
        WatchedFile file;
        ReloadCallback callback;
        std::chrono::steady_clock::time_point last_change_detected;
        bool change_pending{false};
    };

    void poll_loop() {
        while (running_) {
            check_changes();
            poll_cycles_++;
            std::this_thread::sleep_for(poll_interval_);
        }
    }

    void check_changes() {
        std::lock_guard lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        for (auto& entry : watches_) {
            auto& wf = entry.file;

            if (!std::filesystem::exists(wf.path)) {
                if (wf.exists) { wf.exists = false; file_not_found_count_++; }
                continue;
            }
            wf.exists = true;

            auto mod_time = std::filesystem::last_write_time(wf.path);
            auto file_size = std::filesystem::file_size(wf.path);

            if (mod_time != wf.last_modified || file_size != wf.last_size) {
                if (debounce_.enabled) {
                    entry.last_change_detected = now;
                    entry.change_pending = true;
                } else {
                    process_change(entry);
                }
            }

            if (entry.change_pending && debounce_.enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - entry.last_change_detected);
                if (elapsed >= debounce_.window) {
                    process_change(entry);
                    entry.change_pending = false;
                }
            }
        }
    }

    void process_change(WatchEntry& entry) {
        std::string new_hash = compute_hash(entry.file.path);
        if (new_hash != entry.file.last_hash) {
            auto start = std::chrono::steady_clock::now();
            execute_reload(entry.file, entry.callback);
            total_latency_ms_ += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    }

    bool execute_reload(WatchedFile& wf, const ReloadCallback& cb) {
        if (!std::filesystem::exists(wf.path)) {
            file_not_found_count_++;
            return false;
        }

        std::string content = read_file(wf.path);
        std::string new_hash = compute_hash(wf.path);
        auto new_mod = std::filesystem::last_write_time(wf.path);
        auto new_size = std::filesystem::file_size(wf.path);
        std::string old_hash = wf.last_hash;
        std::size_t old_size = wf.last_size;

        // Validate
        bool valid = true;
        std::string error_msg;

        auto pv = per_file_validators_.find(wf.path);
        if (pv != per_file_validators_.end()) {
            try { valid = pv->second(wf.path, content); }
            catch (const std::exception& e) { valid = false; error_msg = e.what(); }
            if (!valid && error_msg.empty()) error_msg = "Per-file validation failed";
        }

        if (valid && global_validator_) {
            try { valid = global_validator_(wf.path, content); }
            catch (const std::exception& e) { valid = false; error_msg = e.what(); }
            if (!valid && error_msg.empty()) error_msg = "Global validation failed";
        }

        if (!valid) {
            validation_failures_++;
            record_event(wf.path, old_hash, new_hash, old_size, new_size,
                         false, false, 0.0, error_msg);
            return false;
        }

        // Apply
        std::string previous = wf.last_content;
        wf.last_modified = new_mod;
        wf.last_size = new_size;
        wf.last_hash = new_hash;
        wf.last_content = content;

        try {
            cb(wf.path, content);
            reload_count_++;
            record_event(wf.path, old_hash, new_hash, old_size, new_size, true, false, 0.0, "");
            return true;
        } catch (const std::exception& e) {
            wf.last_content = previous;
            rollback_count_++;
            record_event(wf.path, old_hash, new_hash, old_size, new_size,
                         false, true, 0.0, std::string("Rollback: ") + e.what());
            return false;
        }
    }

    void record_event(const std::string& path, const std::string& old_hash,
                      const std::string& new_hash, std::size_t old_size,
                      std::size_t new_size, bool passed, bool rollback,
                      double latency, const std::string& error) {
        ReloadEvent evt;
        evt.path = path;
        evt.timestamp = now_str();
        evt.old_hash = old_hash;
        evt.new_hash = new_hash;
        evt.old_size = old_size;
        evt.new_size = new_size;
        evt.validation_passed = passed;
        evt.rollback_triggered = rollback;
        evt.detection_latency_ms = latency;
        evt.error_message = error;
        history_.push_back(std::move(evt));
        while (history_.size() > max_history_) history_.pop_front();
    }

    static std::string compute_hash(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return "";
        uint64_t hash = 5381;
        char c;
        while (file.get(c)) hash = ((hash << 5) + hash) ^ static_cast<unsigned char>(c);
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }

    static std::string read_file(const std::string& path) {
        std::ifstream file(path);
        if (!file) return "";
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::chrono::milliseconds poll_interval_;
    std::size_t max_history_;
    DebounceConfig debounce_;
    ValidationFunc global_validator_;
    std::unordered_map<std::string, ValidationFunc> per_file_validators_;
    std::vector<EnvOverride> env_overrides_;
    std::vector<WatchEntry> watches_;
    std::deque<ReloadEvent> history_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread poll_thread_;
    std::atomic<uint64_t> reload_count_{0};
    std::atomic<uint64_t> validation_failures_{0};
    std::atomic<uint64_t> rollback_count_{0};
    std::atomic<uint64_t> file_not_found_count_{0};
    std::atomic<uint64_t> poll_cycles_{0};
    double total_latency_ms_{0.0};
};

} // namespace genie::core

#endif // GENIE_HOT_RELOAD_HPP
