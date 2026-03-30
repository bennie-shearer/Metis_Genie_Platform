/**
 * @file thread_pool.hpp
 * @brief Dynamic elastic thread pool with parallel execution primitives
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Dynamic/elastic thread pool that grows under load and shrinks when idle:
 *
 *   Core threads:    Always alive, never expire (default: 2)
 *   Max threads:     Upper bound on total workers (default: hardware_concurrency)
 *   Elastic threads: Spawned on demand when queue backs up, expire after idle timeout
 *   Idle timeout:    Elastic threads die if no work arrives (default: 5 seconds)
 *
 * Growth policy:
 *   On submit(), if pending tasks > 0 AND all workers are busy AND
 *   live_threads < max_threads, spawn one elastic worker immediately.
 *
 * Shrink policy:
 *   Elastic workers that receive no task within idle_timeout exit their
 *   loop and are joined by a reaper. Core workers never expire.
 *
 * Primitives:
 *   - submit(fn)             Async task returning std::future
 *   - parallel_for()         Index-based work splitting
 *   - parallel_for_chunked() Chunk-based work splitting
 *   - parallel_map()         Transform collection in parallel
 *   - parallel_reduce()      Split-reduce-combine
 *
 * Zero external dependencies. Pure C++20.
 * Cross-platform: Windows (MSVC/MinGW), Linux (GCC/Clang), macOS (Clang)
 */
#pragma once
#ifndef GENIE_CORE_THREAD_POOL_HPP
#define GENIE_CORE_THREAD_POOL_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <vector>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <cstddef>

namespace genie {

// =========================================================================
// ThreadPool Configuration
// =========================================================================

struct ThreadPoolConfig {
    size_t core_threads = 2;                                         // Always alive
    size_t max_threads = 0;                                          // 0 = hardware_concurrency
    std::chrono::milliseconds idle_timeout = std::chrono::milliseconds{5000};  // Elastic lifetime
};

// =========================================================================
// ThreadPool - Dynamic elastic worker pool
// =========================================================================

class ThreadPool {
public:
    using Config = ThreadPoolConfig;

private:
    // ---- Worker tracking ----
    struct WorkerState {
        std::thread thread;
        bool is_core{false};
        bool finished{false};
    };

    std::vector<std::unique_ptr<WorkerState>> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex mutex_;
    std::condition_variable task_cv_;         // Wakes workers
    std::condition_variable idle_cv_;         // Wakes wait_idle callers

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> live_threads_{0};

    size_t core_threads_;
    size_t max_threads_;
    std::chrono::milliseconds idle_timeout_;

public:
    /**
     * Create a dynamic thread pool.
     * @param cfg Configuration (core/max threads, idle timeout)
     */
    explicit ThreadPool(Config cfg = {})
        : core_threads_(cfg.core_threads)
        , max_threads_(cfg.max_threads == 0 ? optimal_thread_count() : cfg.max_threads)
        , idle_timeout_(cfg.idle_timeout) {
        // Sanity
        if (core_threads_ < 1) core_threads_ = 1;
        if (max_threads_ < core_threads_) max_threads_ = core_threads_;

        // Launch core threads
        std::lock_guard lock(mutex_);
        for (size_t i = 0; i < core_threads_; ++i) {
            spawn_worker(true);
        }
    }

    /**
     * Convenience: create with explicit thread counts.
     * @param core  Minimum always-alive threads
     * @param max   Maximum threads under load (0 = hardware_concurrency)
     */
    ThreadPool(size_t core, size_t max = 0)
        : ThreadPool(Config{core, max, std::chrono::milliseconds{5000}}) {}

    ~ThreadPool() {
        shutdown();
    }

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // ---- Task Submission ----

    /**
     * Submit a callable and return a future for its result.
     * If all workers are busy and we're below max_threads, spawns an elastic worker.
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        auto future = task->get_future();
        {
            std::lock_guard lock(mutex_);
            if (stop_) throw std::runtime_error("ThreadPool is shut down");
            tasks_.emplace([task]() { (*task)(); });
            maybe_grow();
        }
        task_cv_.notify_one();
        return future;
    }

    // ---- Parallel Primitives ----

    /**
     * Parallel for-loop over index range [begin, end).
     * Chunks work across current live thread count.
     */
    void parallel_for(size_t begin, size_t end, const std::function<void(size_t)>& body) {
        if (begin >= end) return;
        size_t total = end - begin;
        size_t workers = effective_parallelism();

        if (total <= workers || workers <= 1) {
            for (size_t i = begin; i < end; ++i) body(i);
            return;
        }

        size_t chunk = (total + workers - 1) / workers;
        std::vector<std::future<void>> futures;
        futures.reserve(workers);

        for (size_t t = 0; t < workers; ++t) {
            size_t lo = begin + t * chunk;
            size_t hi = std::min(lo + chunk, end);
            if (lo >= end) break;
            futures.push_back(submit([lo, hi, &body]() {
                for (size_t i = lo; i < hi; ++i) body(i);
            }));
        }

        for (auto& f : futures) f.get();
    }

    /**
     * Parallel for with chunk-based callback.
     * body(chunk_begin, chunk_end) is called once per chunk.
     */
    void parallel_for_chunked(size_t begin, size_t end,
                               const std::function<void(size_t, size_t)>& body) {
        if (begin >= end) return;
        size_t total = end - begin;
        size_t workers = effective_parallelism();
        size_t chunk = (total + workers - 1) / workers;
        std::vector<std::future<void>> futures;

        for (size_t t = 0; t < workers; ++t) {
            size_t lo = begin + t * chunk;
            size_t hi = std::min(lo + chunk, end);
            if (lo >= end) break;
            futures.push_back(submit([lo, hi, &body]() { body(lo, hi); }));
        }

        for (auto& f : futures) f.get();
    }

    /**
     * Transform a collection in parallel, returning results.
     */
    template<typename T, typename F>
    std::vector<std::invoke_result_t<F, const T&>>
    parallel_map(const std::vector<T>& input, F&& func) {
        using R = std::invoke_result_t<F, const T&>;
        std::vector<R> results(input.size());

        parallel_for(0, input.size(), [&](size_t i) {
            results[i] = func(input[i]);
        });

        return results;
    }

    /**
     * Parallel reduce: split work, reduce per-chunk, then combine.
     */
    template<typename T, typename MapFn, typename ReduceFn>
    T parallel_reduce(size_t begin, size_t end, T init,
                      MapFn&& map_fn, ReduceFn&& reduce_fn) {
        if (begin >= end) return init;
        size_t total = end - begin;
        size_t workers = effective_parallelism();
        size_t chunk = (total + workers - 1) / workers;

        std::vector<std::future<T>> futures;
        for (size_t t = 0; t < workers; ++t) {
            size_t lo = begin + t * chunk;
            size_t hi = std::min(lo + chunk, end);
            if (lo >= end) break;
            futures.push_back(submit([lo, hi, init, &map_fn, &reduce_fn]() {
                T local = init;
                for (size_t i = lo; i < hi; ++i) local = reduce_fn(local, map_fn(i));
                return local;
            }));
        }

        T result = init;
        for (auto& f : futures) result = reduce_fn(result, f.get());
        return result;
    }

    // ---- Status ----

    /** Number of currently live worker threads (core + elastic). */
    [[nodiscard]] size_t thread_count() const { return live_threads_.load(); }

    /** Configured core (minimum) threads. */
    [[nodiscard]] size_t core_thread_count() const { return core_threads_; }

    /** Configured maximum threads. */
    [[nodiscard]] size_t max_thread_count() const { return max_threads_; }

    /** Tasks waiting in queue. */
    [[nodiscard]] size_t pending_tasks() const {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

    /** Tasks currently being executed. */
    [[nodiscard]] size_t active_task_count() const {
        return active_tasks_.load();
    }

    /** True if no tasks pending or running. */
    [[nodiscard]] bool is_idle() const {
        std::lock_guard lock(mutex_);
        return tasks_.empty() && active_tasks_ == 0;
    }

    /** Idle timeout for elastic threads. */
    [[nodiscard]] std::chrono::milliseconds idle_timeout() const { return idle_timeout_; }

    /** Wait until all submitted tasks are complete. */
    void wait_idle() {
        std::unique_lock lock(mutex_);
        idle_cv_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
    }

    /** Gracefully shut down: finish pending tasks, join all threads. */
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            if (stop_) return;
            stop_ = true;
        }
        task_cv_.notify_all();
        // Join all workers
        for (auto& ws : workers_) {
            if (ws && ws->thread.joinable()) ws->thread.join();
        }
        workers_.clear();
    }

    // ---- Static Helpers ----

    static size_t optimal_thread_count() {
        size_t hw = std::thread::hardware_concurrency();
        return hw > 0 ? hw : 4;
    }

private:
    // ---- Worker lifecycle ----

    /** Spawn a new worker thread. Caller must hold mutex_. */
    void spawn_worker(bool is_core) {
        auto ws = std::make_unique<WorkerState>();
        ws->is_core = is_core;
        ws->finished = false;
        auto* ptr = ws.get();
        ws->thread = std::thread([this, ptr, is_core] { worker_loop(ptr, is_core); });
        workers_.push_back(std::move(ws));
        ++live_threads_;
    }

    /** Check if we should grow the pool. Caller must hold mutex_. */
    void maybe_grow() {
        if (stop_) return;
        size_t live = live_threads_.load();
        // Grow if: tasks queued, all threads busy, below max
        if (!tasks_.empty() && active_tasks_ >= live && live < max_threads_) {
            spawn_worker(false);
            reap_finished();
        }
    }

    /** Remove finished elastic workers. Caller must hold mutex_. */
    void reap_finished() {
        for (auto it = workers_.begin(); it != workers_.end(); ) {
            if ((*it)->finished) {
                if ((*it)->thread.joinable()) (*it)->thread.join();
                it = workers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /** Worker thread main loop. */
    void worker_loop(WorkerState* state, bool is_core) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);

                if (is_core) {
                    // Core thread: wait indefinitely
                    task_cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                } else {
                    // Elastic thread: wait with timeout
                    bool got_work = task_cv_.wait_for(lock, idle_timeout_,
                        [this] { return stop_ || !tasks_.empty(); });

                    if (!got_work || (stop_ && tasks_.empty())) {
                        // Timed out with no work, or shutting down - exit
                        state->finished = true;
                        --live_threads_;
                        return;
                    }
                }

                if (tasks_.empty()) continue;
                task = std::move(tasks_.front());
                tasks_.pop();
                ++active_tasks_;
            }
            task();
            --active_tasks_;
            idle_cv_.notify_all();
        }
    }

    /** How many workers to split parallel work across. */
    [[nodiscard]] size_t effective_parallelism() const {
        size_t live = live_threads_.load();
        return live > 0 ? live : core_threads_;
    }
};

// =========================================================================
// Global thread pool singleton
// =========================================================================

/**
 * Get the global thread pool. Created on first call.
 * Core: 2 threads (always alive)
 * Max:  hardware_concurrency (elastic growth under load)
 * Idle: 5 second timeout for elastic threads
 */
inline ThreadPool& thread_pool() {
    static ThreadPool pool(ThreadPool::Config{
        2,                                        // core_threads
        ThreadPool::optimal_thread_count(),       // max_threads
        std::chrono::milliseconds{5000}           // idle_timeout
    });
    return pool;
}

} // namespace genie

#endif // GENIE_CORE_THREAD_POOL_HPP
