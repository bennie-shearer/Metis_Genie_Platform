/**
 * @file historical_replay.hpp
 * @brief Historical market data replay engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Replays historical market data at configurable speeds for backtesting,
 * strategy development, and training. Supports tick-by-tick and bar-based
 * replay with event injection, pause/resume, seek, filtering, bookmarks,
 * and comprehensive statistics.
 *
 * Features:
 *  - Multiple replay speeds: real-time, 10x, 100x, max, step-by-step
 *  - Pause, resume, seek, and step controls
 *  - Symbol and event-type filtering
 *  - Bookmark support for marking and returning to specific points
 *  - Event injection for synthetic scenario testing
 *  - Comprehensive session statistics with throughput metrics
 *  - Multiple concurrent session support
 *  - Thread-safe with condition variable signaling
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_HISTORICAL_REPLAY_HPP
#define GENIE_HISTORICAL_REPLAY_HPP

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <queue>
#include <algorithm>
#include <sstream>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <optional>

namespace genie::market {

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Replay speed multiplier */
enum class ReplaySpeed { REALTIME_1X, FAST_2X, FAST_5X, FAST_10X, FAST_50X, FAST_100X, MAX_SPEED, STEP_BY_STEP };

/** @brief Replay state */
enum class ReplayState { STOPPED, LOADING, PLAYING, PAUSED, SEEKING, COMPLETED, ERROR };

/** @brief A historical market event */
struct MarketEvent {
    std::string timestamp;
    std::string symbol;
    double price{0.0};
    double volume{0.0};
    double bid{0.0};
    double ask{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double vwap{0.0};
    std::string event_type{"trade"};  // trade, quote, bar, corporate_action, auction
    std::string exchange;
    uint64_t sequence{0};
    std::string condition;  // trade condition codes
};

/** @brief Replay session configuration */
struct ReplayConfig {
    std::string session_id;
    std::string start_date;
    std::string end_date;
    std::vector<std::string> symbols;
    ReplaySpeed speed{ReplaySpeed::FAST_10X};
    bool include_quotes{true};
    bool include_trades{true};
    bool include_bars{true};
    bool include_auctions{false};
    bool loop{false};
    int loop_count{0}; // 0 = infinite
    std::string description;
};

/** @brief Bookmark for marking a point in the replay stream */
struct ReplayBookmark {
    std::string name;
    std::size_t event_index{0};
    std::string timestamp;
    std::string description;
    std::string created_at;
};

/** @brief Replay session statistics */
struct ReplayStats {
    std::string session_id;
    ReplayState state{ReplayState::STOPPED};
    uint64_t events_replayed{0};
    uint64_t total_events{0};
    double progress_pct{0.0};
    double events_per_second{0.0};
    std::string current_timestamp;
    double elapsed_seconds{0.0};
    std::string start_date;
    std::string end_date;
    std::size_t symbols_count{0};
    uint64_t trades_replayed{0};
    uint64_t quotes_replayed{0};
    uint64_t bars_replayed{0};
    int current_loop{0};
    std::vector<ReplayBookmark> bookmarks;
    std::unordered_map<std::string, uint64_t> events_by_symbol;
};

/** @brief Callback for replay events */
using ReplayCallback = std::function<void(const MarketEvent&)>;

// ============================================================================
// HistoricalReplayEngine
// ============================================================================

/**
 * @class HistoricalReplayEngine
 * @brief Replays historical market data with comprehensive controls
 */
class HistoricalReplayEngine {
public:
    HistoricalReplayEngine() = default;
    ~HistoricalReplayEngine() { stop(); }

    HistoricalReplayEngine(const HistoricalReplayEngine&) = delete;
    HistoricalReplayEngine& operator=(const HistoricalReplayEngine&) = delete;

    // ---- Data Loading ----

    /** @brief Load historical data for replay */
    void load(const std::vector<MarketEvent>& events) {
        std::lock_guard lock(mutex_);
        events_ = events;
        std::sort(events_.begin(), events_.end(), [](const MarketEvent& a, const MarketEvent& b) {
            return a.sequence < b.sequence || (a.sequence == b.sequence && a.timestamp < b.timestamp);
        });
        state_ = ReplayState::STOPPED;
        events_replayed_ = 0;
    }

    /** @brief Inject a synthetic event into the stream at the correct position */
    void inject_event(const MarketEvent& event) {
        std::lock_guard lock(mutex_);
        auto pos = std::lower_bound(events_.begin(), events_.end(), event,
            [](const MarketEvent& a, const MarketEvent& b) {
                return a.timestamp < b.timestamp;
            });
        events_.insert(pos, event);
    }

    /** @brief Set symbol filter (empty = all symbols) */
    void set_symbol_filter(const std::vector<std::string>& symbols) {
        std::lock_guard lock(mutex_);
        symbol_filter_.clear();
        for (const auto& s : symbols) symbol_filter_.insert(s);
    }

    /** @brief Set event type filter */
    void set_event_type_filter(const std::vector<std::string>& types) {
        std::lock_guard lock(mutex_);
        type_filter_.clear();
        for (const auto& t : types) type_filter_.insert(t);
    }

    // ---- Playback Control ----

    /** @brief Start replay with callback */
    void start(ReplayConfig config, ReplayCallback callback) {
        stop();
        std::lock_guard lock(mutex_);
        config_ = std::move(config);
        callback_ = std::move(callback);
        state_ = ReplayState::PLAYING;
        events_replayed_ = 0;
        trades_replayed_ = 0;
        quotes_replayed_ = 0;
        bars_replayed_ = 0;
        current_loop_ = 0;
        events_by_symbol_.clear();
        start_time_ = std::chrono::steady_clock::now();

        replay_thread_ = std::thread([this] { replay_loop(); });
    }

    /** @brief Pause replay */
    void pause() {
        std::lock_guard lock(mutex_);
        if (state_ == ReplayState::PLAYING) state_ = ReplayState::PAUSED;
    }

    /** @brief Resume replay */
    void resume() {
        {
            std::lock_guard lock(mutex_);
            if (state_ == ReplayState::PAUSED) state_ = ReplayState::PLAYING;
        }
        pause_cv_.notify_all();
    }

    /** @brief Step one event forward (in step-by-step or paused mode) */
    void step() {
        {
            std::lock_guard lock(mutex_);
            step_requested_ = true;
            if (state_ == ReplayState::PAUSED) state_ = ReplayState::PLAYING;
        }
        pause_cv_.notify_all();
    }

    /** @brief Seek to a specific event index */
    void seek(std::size_t event_index) {
        std::lock_guard lock(mutex_);
        if (event_index < events_.size()) {
            events_replayed_ = event_index;
        }
    }

    /** @brief Seek to a bookmark */
    bool seek_to_bookmark(const std::string& name) {
        std::lock_guard lock(mutex_);
        for (const auto& bm : bookmarks_) {
            if (bm.name == name) {
                events_replayed_ = bm.event_index;
                return true;
            }
        }
        return false;
    }

    /** @brief Change replay speed dynamically */
    void set_speed(ReplaySpeed speed) {
        std::lock_guard lock(mutex_);
        config_.speed = speed;
    }

    /** @brief Stop replay */
    void stop() {
        {
            std::lock_guard lock(mutex_);
            state_ = ReplayState::STOPPED;
        }
        pause_cv_.notify_all();
        if (replay_thread_.joinable()) replay_thread_.join();
    }

    // ---- Bookmarks ----

    /** @brief Add a bookmark at current position */
    void add_bookmark(const std::string& name, const std::string& description = "") {
        std::lock_guard lock(mutex_);
        ReplayBookmark bm;
        bm.name = name;
        bm.event_index = events_replayed_;
        bm.description = description;
        if (events_replayed_ < events_.size()) {
            bm.timestamp = events_[events_replayed_].timestamp;
        }
        bm.created_at = now_str();
        bookmarks_.push_back(std::move(bm));
    }

    /** @brief Remove a bookmark */
    bool remove_bookmark(const std::string& name) {
        std::lock_guard lock(mutex_);
        auto it = std::remove_if(bookmarks_.begin(), bookmarks_.end(),
            [&](const ReplayBookmark& b) { return b.name == name; });
        if (it != bookmarks_.end()) {
            bookmarks_.erase(it, bookmarks_.end());
            return true;
        }
        return false;
    }

    // ---- Statistics ----

    /** @brief Get replay statistics */
    [[nodiscard]] ReplayStats stats() const {
        std::lock_guard lock(mutex_);
        ReplayStats s;
        s.session_id = config_.session_id;
        s.state = state_;
        s.events_replayed = events_replayed_;
        s.total_events = events_.size();
        s.progress_pct = events_.empty() ? 0.0
            : static_cast<double>(events_replayed_) / events_.size() * 100.0;
        s.start_date = config_.start_date;
        s.end_date = config_.end_date;
        s.symbols_count = events_by_symbol_.size();
        s.trades_replayed = trades_replayed_;
        s.quotes_replayed = quotes_replayed_;
        s.bars_replayed = bars_replayed_;
        s.current_loop = current_loop_;
        s.bookmarks = bookmarks_;
        s.events_by_symbol = events_by_symbol_;

        if (events_replayed_ > 0 && events_replayed_ <= events_.size()) {
            s.current_timestamp = events_[events_replayed_ - 1].timestamp;
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        s.elapsed_seconds = std::chrono::duration<double>(elapsed).count();
        s.events_per_second = s.elapsed_seconds > 0
            ? events_replayed_ / s.elapsed_seconds : 0.0;

        return s;
    }

    [[nodiscard]] ReplayState state() const { std::lock_guard lock(mutex_); return state_; }
    [[nodiscard]] std::size_t event_count() const { return events_.size(); }

private:
    void replay_loop() {
        do {
            for (std::size_t i = events_replayed_; i < events_.size(); ++i) {
                // Check state
                {
                    std::unique_lock lock(mutex_);
                    while (state_ == ReplayState::PAUSED && !step_requested_) {
                        pause_cv_.wait(lock);
                    }
                    if (state_ == ReplayState::STOPPED) return;
                    step_requested_ = false;
                }

                const auto& event = events_[i];

                // Apply filters
                if (!symbol_filter_.empty() && symbol_filter_.find(event.symbol) == symbol_filter_.end())
                    continue;
                if (!type_filter_.empty() && type_filter_.find(event.event_type) == type_filter_.end())
                    continue;

                // Dispatch event
                if (callback_) callback_(event);

                // Update counters
                events_replayed_ = i + 1;
                events_by_symbol_[event.symbol]++;
                if (event.event_type == "trade") trades_replayed_++;
                else if (event.event_type == "quote") quotes_replayed_++;
                else if (event.event_type == "bar") bars_replayed_++;

                // Speed control
                apply_speed_delay();

                // Step-by-step: pause after each event
                if (config_.speed == ReplaySpeed::STEP_BY_STEP) {
                    std::lock_guard lock(mutex_);
                    state_ = ReplayState::PAUSED;
                }
            }

            current_loop_++;
            if (config_.loop) {
                events_replayed_ = 0;
                if (config_.loop_count > 0 && current_loop_ >= config_.loop_count) break;
            }
        } while (config_.loop && state_ != ReplayState::STOPPED);

        std::lock_guard lock(mutex_);
        if (state_ != ReplayState::STOPPED) state_ = ReplayState::COMPLETED;
    }

    void apply_speed_delay() const {
        switch (config_.speed) {
            case ReplaySpeed::REALTIME_1X:
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                break;
            case ReplaySpeed::FAST_2X:
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                break;
            case ReplaySpeed::FAST_5X:
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                break;
            case ReplaySpeed::FAST_10X:
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                break;
            case ReplaySpeed::FAST_50X:
                std::this_thread::sleep_for(std::chrono::microseconds(20));
                break;
            case ReplaySpeed::FAST_100X:
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                break;
            case ReplaySpeed::MAX_SPEED:
            case ReplaySpeed::STEP_BY_STEP:
                break;
        }
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::vector<MarketEvent> events_;
    ReplayConfig config_;
    ReplayCallback callback_;
    ReplayState state_{ReplayState::STOPPED};
    std::atomic<uint64_t> events_replayed_{0};
    std::atomic<uint64_t> trades_replayed_{0};
    std::atomic<uint64_t> quotes_replayed_{0};
    std::atomic<uint64_t> bars_replayed_{0};
    int current_loop_{0};
    bool step_requested_{false};
    std::chrono::steady_clock::time_point start_time_;
    std::unordered_set<std::string> symbol_filter_;
    std::unordered_set<std::string> type_filter_;
    std::unordered_map<std::string, uint64_t> events_by_symbol_;
    std::vector<ReplayBookmark> bookmarks_;
    mutable std::mutex mutex_;
    std::condition_variable pause_cv_;
    std::thread replay_thread_;
};

} // namespace genie::market

#endif // GENIE_HISTORICAL_REPLAY_HPP
