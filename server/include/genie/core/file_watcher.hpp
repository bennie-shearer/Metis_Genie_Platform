/**
 * @file file_watcher.hpp
 * @brief Cross-platform file watcher for config hot-reload
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Uses native OS APIs for efficient file change detection:
 *   Linux:   inotify (IN_CLOSE_WRITE | IN_MOVED_TO)
 *   Windows: ReadDirectoryChangesW (FILE_NOTIFY_CHANGE_LAST_WRITE)
 *   macOS:   kqueue / kevent (EVFILT_VNODE + NOTE_WRITE)
 *
 * Falls back to polling (std::filesystem::last_write_time) on any platform
 * if the native API is unavailable (e.g., network filesystems).
 *
 * Usage:
 *   FileWatcher watcher;
 *   watcher.watch("config.pson", [](const std::string& path) {
 *       config().load_from_file(path);
 *   });
 *   watcher.start();   // background thread
 *   // ...
 *   watcher.stop();
 *
 * config.pson:
 *   "file_watcher": { "enabled": true, "poll_fallback_ms": 2000 }
 *
 * Zero external dependencies. Cross-platform: Windows/Linux/macOS.
 */
#pragma once
#ifndef GENIE_CORE_FILE_WATCHER_HPP
#define GENIE_CORE_FILE_WATCHER_HPP

#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>

// Platform native includes
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#elif defined(__linux__)
  #include <sys/inotify.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <cerrno>
#elif defined(__APPLE__)
  #include <sys/event.h>
  #include <sys/time.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace genie::core {

using WatchCallback = std::function<void(const std::string& path)>;

// ============================================================================
// File hash (FNV-1a 64-bit) for content-change detection
// ============================================================================

[[nodiscard]] inline uint64_t file_hash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 14695981039346656037ULL;
    char c;
    while (f.get(c)) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

// ============================================================================
// FileWatcher
// ============================================================================

class FileWatcher {
public:
    explicit FileWatcher(int poll_fallback_ms = 2000)
        : poll_ms_(poll_fallback_ms) {}

    ~FileWatcher() { stop(); }
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    /** Register a file to watch with a callback on change */
    void watch(const std::string& path, WatchCallback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        watches_[path] = {std::move(cb), file_hash(path),
                          std::filesystem::exists(path) ?
                              std::filesystem::last_write_time(path) :
                              std::filesystem::file_time_type{}};
    }

    /** Remove a watch */
    void unwatch(const std::string& path) {
        std::lock_guard<std::mutex> lk(mtx_);
        watches_.erase(path);
    }

    /** Start the background watcher thread */
    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { run(); });
    }

    /** Stop the background watcher thread */
    void stop() {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }

    bool is_running() const { return running_.load(); }

    /** How many files are being watched */
    size_t watch_count() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return watches_.size();
    }

    /** Total change events fired */
    uint64_t change_count() const {
        return change_count_.load(std::memory_order_relaxed);
    }

private:
    struct WatchEntry {
        WatchCallback    callback;
        uint64_t         last_hash;
        std::filesystem::file_time_type last_mtime;
    };

    int                  poll_ms_;
    std::atomic<bool>    running_{false};
    std::thread          thread_;
    mutable std::mutex   mtx_;
    std::map<std::string, WatchEntry> watches_;
    std::atomic<uint64_t> change_count_{0};

    void run() {
#if defined(_WIN32)
        run_windows();
#elif defined(__linux__)
        run_linux();
#elif defined(__APPLE__)
        run_macos();
#else
        run_poll();
#endif
    }

    // ------------------------------------------------------------------
    // Shared poll fallback (called by all paths; always available)
    // ------------------------------------------------------------------
    void poll_once() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [path, entry] : watches_) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            auto mtime = std::filesystem::last_write_time(path, ec);
            if (ec) continue;
            if (mtime == entry.last_mtime) continue;
            entry.last_mtime = mtime;
            // Double-check with content hash to avoid false positives
            uint64_t h = file_hash(path);
            if (h == entry.last_hash) continue;
            entry.last_hash = h;
            change_count_.fetch_add(1, std::memory_order_relaxed);
            try { entry.callback(path); } catch (...) {}
        }
    }

    void run_poll() {
        while (running_.load()) {
            poll_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
        }
    }

    // ------------------------------------------------------------------
    // Windows: ReadDirectoryChangesW
    // ------------------------------------------------------------------
#ifdef _WIN32
    void run_windows() {
        // Collect unique directories from watched files
        std::map<std::wstring, HANDLE> dir_handles;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& [path, _] : watches_) {
                namespace fs = std::filesystem;
                fs::path p(path);
                std::wstring dir = p.parent_path().wstring();
                if (dir.empty()) dir = L".";
                if (!dir_handles.count(dir)) {
                    HANDLE h = CreateFileW(dir.c_str(),
                        FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                    if (h != INVALID_HANDLE_VALUE) dir_handles[dir] = h;
                }
            }
        }

        if (dir_handles.empty()) { run_poll(); return; }

        alignas(DWORD) char buf[8192];
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        while (running_.load()) {
            for (auto& [dir, h] : dir_handles) {
                DWORD bytes = 0;
                ResetEvent(ov.hEvent);
                ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE,
                    FILE_NOTIFY_CHANGE_LAST_WRITE, &bytes, &ov, nullptr);
                DWORD wait = WaitForSingleObject(ov.hEvent,
                    static_cast<DWORD>(poll_ms_));
                if (wait == WAIT_OBJECT_0) {
                    GetOverlappedResult(h, &ov, &bytes, FALSE);
                    poll_once(); // re-check all watched files in this dir
                }
            }
        }

        CloseHandle(ov.hEvent);
        for (auto& [_, h] : dir_handles) CloseHandle(h);
    }
#endif

    // ------------------------------------------------------------------
    // Linux: inotify
    // ------------------------------------------------------------------
#if defined(__linux__)
    void run_linux() {
        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) { run_poll(); return; }

        std::map<int, std::string> wd_to_path;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& [path, _] : watches_) {
                int wd = inotify_add_watch(fd, path.c_str(),
                    IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY);
                if (wd >= 0) wd_to_path[wd] = path;
            }
        }

        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        while (running_.load()) {
            fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
            struct timeval tv{0, static_cast<suseconds_t>(poll_ms_) * 1000};
            if (select(fd + 1, &fds, nullptr, nullptr, &tv) > 0) {
                ssize_t len = read(fd, buf, sizeof(buf));
                if (len > 0) poll_once();
            }
        }
        close(fd);
    }
#endif

    // ------------------------------------------------------------------
    // macOS: kqueue
    // ------------------------------------------------------------------
#if defined(__APPLE__)
    void run_macos() {
        int kq = kqueue();
        if (kq < 0) { run_poll(); return; }

        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& [path, _] : watches_) {
                int fd = open(path.c_str(), O_RDONLY | O_EVTONLY);
                if (fd >= 0) {
                    struct kevent ke{};
                    EV_SET(&ke, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
                           NOTE_WRITE | NOTE_RENAME | NOTE_DELETE, 0, nullptr);
                    kevent(kq, &ke, 1, nullptr, 0, nullptr);
                    fds.push_back(fd);
                }
            }
        }

        while (running_.load()) {
            struct kevent ev{};
            struct timespec ts{0, static_cast<long>(poll_ms_) * 1000000L};
            if (kevent(kq, nullptr, 0, &ev, 1, &ts) > 0) poll_once();
        }

        for (int fd : fds) close(fd);
        close(kq);
    }
#endif

    // ------------------------------------------------------------------
    // Unknown platform: pure polling
    // ------------------------------------------------------------------
#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)
    void run_windows() { run_poll(); }
    void run_linux()   { run_poll(); }
    void run_macos()   { run_poll(); }
#endif
};

} // namespace genie::core

#endif // GENIE_CORE_FILE_WATCHER_HPP
