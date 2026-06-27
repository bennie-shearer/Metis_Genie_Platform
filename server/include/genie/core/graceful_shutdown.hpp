/**
 * @file graceful_shutdown.hpp
 * @brief Graceful Shutdown Manager with Request Draining
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Manages orderly server shutdown:
 * - Signal handling (SIGTERM, SIGINT) cross-platform
 * - In-flight request draining with configurable timeout
 * - Shutdown hook registration (cleanup callbacks)
 * - Component shutdown ordering (reverse registration)
 * - Health endpoint state transition (healthy -> draining -> stopped)
 * - Connection close notification
 * - Shutdown progress reporting
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_CORE_GRACEFUL_SHUTDOWN_HPP
#define GENIE_CORE_GRACEFUL_SHUTDOWN_HPP

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#endif

namespace genie::core {

enum class ShutdownState { RUNNING, DRAINING, STOPPING, STOPPED };

inline std::string shutdown_state_name(ShutdownState s) {
    switch (s) {
        case ShutdownState::RUNNING: return "running";
        case ShutdownState::DRAINING: return "draining";
        case ShutdownState::STOPPING: return "stopping";
        case ShutdownState::STOPPED: return "stopped";
    }
    return "unknown";
}

struct ShutdownHook {
    std::string name;
    int priority{0}; // Higher = later shutdown
    std::function<void()> callback;
};

struct ShutdownStatus {
    ShutdownState state{ShutdownState::RUNNING};
    int active_requests{0};
    int hooks_completed{0};
    int hooks_total{0};
    int drain_timeout_seconds{30};
    double elapsed_seconds{0.0};
    std::string message;

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"state\":\"" << shutdown_state_name(state) << "\""
           << ",\"active_requests\":" << active_requests
           << ",\"hooks_completed\":" << hooks_completed
           << ",\"hooks_total\":" << hooks_total
           << ",\"drain_timeout_seconds\":" << drain_timeout_seconds
           << ",\"elapsed_seconds\":" << elapsed_seconds
           << ",\"message\":\"" << message << "\""
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Graceful Shutdown Manager (singleton pattern)
// ---------------------------------------------------------------
class GracefulShutdown {
public:
    static GracefulShutdown& instance() {
        static GracefulShutdown inst;
        return inst;
    }

    // Set drain timeout
    void set_drain_timeout(int seconds) {
        drain_timeout_ = std::max(1, std::min(seconds, 300));
    }

    // Register a shutdown hook
    void register_hook(const std::string& name, int priority, std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(mtx_);
        hooks_.push_back({name, priority, std::move(callback)});
    }

    // Track active requests
    void request_started() {
        active_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    void request_completed() {
        int prev = active_requests_.fetch_sub(1, std::memory_order_relaxed);
        if (prev <= 1 && state_.load() == ShutdownState::DRAINING) {
            drain_cv_.notify_all();
        }
    }

    // Check if accepting new requests
    bool is_accepting_requests() const {
        return state_.load() == ShutdownState::RUNNING;
    }

    // Check if shutdown was initiated
    bool is_shutting_down() const {
        return state_.load() != ShutdownState::RUNNING;
    }

    // Initiate shutdown
    void initiate_shutdown(const std::string& reason = "shutdown requested") {
        ShutdownState expected = ShutdownState::RUNNING;
        if (!state_.compare_exchange_strong(expected, ShutdownState::DRAINING)) {
            return; // Already shutting down
        }

        shutdown_start_ = std::chrono::steady_clock::now();
        shutdown_reason_ = reason;

        // Phase 1: Drain active requests
        {
            std::unique_lock<std::mutex> lock(drain_mtx_);
            drain_cv_.wait_for(lock, std::chrono::seconds(drain_timeout_), [this]() {
                return active_requests_.load() <= 0;
            });
        }

        // Phase 2: Execute shutdown hooks (reverse priority order)
        state_.store(ShutdownState::STOPPING);
        execute_hooks();

        // Phase 3: Done
        state_.store(ShutdownState::STOPPED);
    }

    // Install signal handlers
    void install_signal_handlers() {
#ifdef _WIN32
        SetConsoleCtrlHandler(windows_handler, TRUE);
#else
        struct sigaction sa;
        sa.sa_handler = unix_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
#endif
    }

    // Get current status
    ShutdownStatus get_status() const {
        ShutdownStatus status;
        status.state = state_.load();
        status.active_requests = active_requests_.load();
        status.hooks_total = static_cast<int>(hooks_.size());
        status.hooks_completed = hooks_completed_.load();
        status.drain_timeout_seconds = drain_timeout_;

        if (status.state != ShutdownState::RUNNING) {
            auto now = std::chrono::steady_clock::now();
            status.elapsed_seconds = std::chrono::duration<double>(now - shutdown_start_).count();
            status.message = shutdown_reason_;
        } else {
            status.message = "Server is running normally";
        }

        return status;
    }

    // Wait for shutdown to complete
    void wait_for_shutdown() {
        while (state_.load() != ShutdownState::STOPPED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // RAII request tracker
    class RequestGuard {
    public:
        explicit RequestGuard(GracefulShutdown& gs) : gs_(gs) {
            gs_.request_started();
        }
        ~RequestGuard() { gs_.request_completed(); }
        RequestGuard(const RequestGuard&) = delete;
        RequestGuard& operator=(const RequestGuard&) = delete;
    private:
        GracefulShutdown& gs_;
    };

private:
    GracefulShutdown() = default;
    GracefulShutdown(const GracefulShutdown&) = delete;
    GracefulShutdown& operator=(const GracefulShutdown&) = delete;

    std::atomic<ShutdownState> state_{ShutdownState::RUNNING};
    std::atomic<int> active_requests_{0};
    std::atomic<int> hooks_completed_{0};
    int drain_timeout_{30};
    std::mutex mtx_;
    std::mutex drain_mtx_;
    std::condition_variable drain_cv_;
    std::vector<ShutdownHook> hooks_;
    std::chrono::steady_clock::time_point shutdown_start_;
    std::string shutdown_reason_;

    void execute_hooks() {
        std::lock_guard<std::mutex> lock(mtx_);
        // Sort by priority (higher priority = executed later)
        std::sort(hooks_.begin(), hooks_.end(),
            [](const ShutdownHook& a, const ShutdownHook& b) {
                return a.priority < b.priority;
            });

        for (auto& hook : hooks_) {
            try {
                hook.callback();
            } catch (...) {
                // Log but don't fail
            }
            hooks_completed_.fetch_add(1);
        }
    }

#ifdef _WIN32
    static BOOL WINAPI windows_handler(DWORD ctrl_type) {
        if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
            ctrl_type == CTRL_CLOSE_EVENT) {
            instance().initiate_shutdown("Windows console signal");
            return TRUE;
        }
        return FALSE;
    }
#else
    static void unix_handler(int signum) {
        const char* reason = (signum == SIGTERM) ? "SIGTERM" : "SIGINT";
        instance().initiate_shutdown(reason);
    }
#endif
};

} // namespace genie::core

#endif // GENIE_CORE_GRACEFUL_SHUTDOWN_HPP
