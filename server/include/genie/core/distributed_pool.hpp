/**
 * @file distributed_pool.hpp
 * @brief Distributed compute framework for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Job serialization, worker node management, task dispatch,
 * and result aggregation for large-scale compute (MC, risk, revaluation).
 */
#pragma once
#ifndef GENIE_CORE_DISTRIBUTED_POOL_HPP
#define GENIE_CORE_DISTRIBUTED_POOL_HPP

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <optional>
#include <memory>
#include <sstream>

namespace genie::compute {

/** Job status */
enum class JobStatus { Queued, Running, Completed, Failed, Cancelled };

inline std::string status_name(JobStatus s) {
    switch (s) {
        case JobStatus::Queued: return "Queued"; case JobStatus::Running: return "Running";
        case JobStatus::Completed: return "Completed"; case JobStatus::Failed: return "Failed";
        case JobStatus::Cancelled: return "Cancelled"; default: return "Unknown";
    }
}

/** Job type for workload categorization */
enum class JobType { MonteCarlo, RiskCalc, Revaluation, Backtest, FactorModel, Custom };

/** A compute job */
struct Job {
    std::string id;
    JobType type{JobType::Custom};
    std::string payload;                // serialized parameters
    JobStatus status{JobStatus::Queued};
    std::string result;                 // serialized result
    std::string error;
    std::string assigned_worker;
    int priority{5};                    // 1=highest, 10=lowest
    std::chrono::system_clock::time_point submitted;
    std::chrono::system_clock::time_point started;
    std::chrono::system_clock::time_point completed;
    double progress{0};                 // 0.0 to 1.0

    [[nodiscard]] double elapsed_ms() const {
        auto end = (status == JobStatus::Completed || status == JobStatus::Failed)
                   ? completed : std::chrono::system_clock::now();
        return std::chrono::duration<double, std::milli>(end - started).count();
    }
};

/** Worker node info */
struct WorkerNode {
    std::string id;
    std::string hostname;
    int cores{1};
    int available_cores{1};
    size_t memory_mb{1024};
    bool active{true};
    std::chrono::system_clock::time_point registered;
    std::chrono::system_clock::time_point last_heartbeat;
    int jobs_completed{0};
    int jobs_failed{0};
    std::string current_job_id;

    [[nodiscard]] double utilization() const {
        return (cores > 0) ? 1.0 - static_cast<double>(available_cores) / cores : 0;
    }
};

/** Dispatch policy */
enum class DispatchPolicy { RoundRobin, LeastLoaded, Random, Affinity };

/** Distributed compute pool */
class DistributedPool {
    std::map<std::string, Job> jobs_;
    std::map<std::string, WorkerNode> workers_;
    std::queue<std::string> job_queue_;  // job IDs in priority order
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<int> job_counter_{0};
    DispatchPolicy policy_{DispatchPolicy::LeastLoaded};

    // Local execution fallback
    std::map<JobType, std::function<std::string(const std::string&)>> executors_;

public:
    DistributedPool() = default;

    void set_policy(DispatchPolicy p) { policy_ = p; }

    /** Register a local executor for a job type (for single-machine fallback) */
    void register_executor(JobType type,
                           std::function<std::string(const std::string&)> executor) {
        std::lock_guard lock(mutex_);
        executors_[type] = std::move(executor);
    }

    /** Register a worker node */
    std::string register_worker(const std::string& hostname, int cores = 1, size_t memory_mb = 1024) {
        std::lock_guard lock(mutex_);
        std::string id = "worker-" + std::to_string(workers_.size() + 1);
        WorkerNode w;
        w.id = id; w.hostname = hostname; w.cores = cores;
        w.available_cores = cores; w.memory_mb = memory_mb; w.active = true;
        w.registered = std::chrono::system_clock::now();
        w.last_heartbeat = w.registered;
        workers_[id] = w;
        return id;
    }

    /** Deregister a worker */
    void deregister_worker(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = workers_.find(id);
        if (it != workers_.end()) it->second.active = false;
    }

    /** Worker heartbeat */
    void heartbeat(const std::string& worker_id) {
        std::lock_guard lock(mutex_);
        auto it = workers_.find(worker_id);
        if (it != workers_.end())
            it->second.last_heartbeat = std::chrono::system_clock::now();
    }

    /** Submit a job */
    std::string submit(JobType type, const std::string& payload, int priority = 5) {
        std::lock_guard lock(mutex_);
        std::string id = "job-" + std::to_string(++job_counter_);
        Job job;
        job.id = id; job.type = type; job.payload = payload;
        job.priority = priority; job.status = JobStatus::Queued;
        job.submitted = std::chrono::system_clock::now();
        jobs_[id] = job;
        job_queue_.push(id);
        cv_.notify_one();
        return id;
    }

    /** Dispatch queued jobs to available workers */
    size_t dispatch() {
        std::lock_guard lock(mutex_);
        size_t dispatched = 0;

        while (!job_queue_.empty()) {
            auto worker = select_worker();
            if (!worker) break;

            std::string job_id = job_queue_.front();
            job_queue_.pop();

            auto& job = jobs_[job_id];
            job.status = JobStatus::Running;
            job.assigned_worker = worker->id;
            job.started = std::chrono::system_clock::now();

            worker->available_cores--;
            worker->current_job_id = job_id;

            // If local executor available, run it
            auto exec_it = executors_.find(job.type);
            if (exec_it != executors_.end()) {
                try {
                    job.result = exec_it->second(job.payload);
                    job.status = JobStatus::Completed;
                    job.progress = 1.0;
                    worker->jobs_completed++;
                } catch (const std::exception& e) {
                    job.status = JobStatus::Failed;
                    job.error = e.what();
                    worker->jobs_failed++;
                }
                job.completed = std::chrono::system_clock::now();
                worker->available_cores++;
                worker->current_job_id.clear();
            }
            dispatched++;
        }
        return dispatched;
    }

    /** Execute locally (no workers needed - uses registered executors) */
    std::string execute_local(JobType type, const std::string& payload) {
        auto id = submit(type, payload);
        // Register self as worker if no workers exist
        if (workers_.empty()) register_worker("localhost", 4, 8192);
        dispatch();
        auto job = get_job(id);
        if (job && job->status == JobStatus::Completed) return job->result;
        if (job && job->status == JobStatus::Failed) throw std::runtime_error(job->error);
        return "";
    }

    /** Get job status */
    [[nodiscard]] std::optional<Job> get_job(const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return std::nullopt;
        return it->second;
    }

    /** Cancel a job */
    bool cancel(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return false;
        if (it->second.status == JobStatus::Queued) {
            it->second.status = JobStatus::Cancelled;
            return true;
        }
        return false;
    }

    // ---- Statistics ----

    [[nodiscard]] size_t worker_count() const { std::lock_guard lock(mutex_); return workers_.size(); }
    [[nodiscard]] size_t active_workers() const {
        std::lock_guard lock(mutex_);
        size_t c = 0; for (const auto& [k, w] : workers_) if (w.active) c++;
        return c;
    }
    [[nodiscard]] size_t job_count() const { std::lock_guard lock(mutex_); return jobs_.size(); }
    [[nodiscard]] size_t queued_count() const {
        std::lock_guard lock(mutex_);
        size_t c = 0; for (const auto& [k, j] : jobs_) if (j.status == JobStatus::Queued) c++;
        return c;
    }
    [[nodiscard]] size_t completed_count() const {
        std::lock_guard lock(mutex_);
        size_t c = 0; for (const auto& [k, j] : jobs_) if (j.status == JobStatus::Completed) c++;
        return c;
    }

    [[nodiscard]] int total_cores() const {
        std::lock_guard lock(mutex_);
        int c = 0; for (const auto& [k, w] : workers_) if (w.active) c += w.cores;
        return c;
    }

private:
    WorkerNode* select_worker() {
        WorkerNode* best = nullptr;
        for (auto& [id, w] : workers_) {
            if (!w.active || w.available_cores <= 0) continue;
            if (policy_ == DispatchPolicy::LeastLoaded) {
                if (!best || w.available_cores > best->available_cores) best = &w;
            } else {
                best = &w; break; // RoundRobin/Random: take first available
            }
        }
        return best;
    }
};

} // namespace genie::compute

#endif // GENIE_CORE_DISTRIBUTED_POOL_HPP
