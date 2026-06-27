/**
 * @file job_processor.hpp
 * @brief Background job processing system
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Performance - Create background job processing
 */

#ifndef GENIE_PERFORMANCE_JOB_PROCESSOR_HPP
#define GENIE_PERFORMANCE_JOB_PROCESSOR_HPP

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <future>
#include <optional>
#include <sstream>

namespace genie {
namespace performance {

/**
 * @brief Job priority levels
 */
enum class JobPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

/**
 * @brief Job status
 */
enum class JobStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED,
    RETRY_PENDING
};

/**
 * @brief Job definition
 */
struct Job {
    std::string id;
    std::string type;
    std::string payload;
    JobPriority priority{JobPriority::NORMAL};
    JobStatus status{JobStatus::PENDING};
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point scheduled_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    int retry_count{0};
    int max_retries{3};
    std::chrono::seconds retry_delay{60};
    std::string result;
    std::string error;
    std::map<std::string, std::string> metadata;
    
    bool operator<(const Job& other) const {
        // Higher priority first, then earlier scheduled time
        if (priority != other.priority) {
            return priority < other.priority;
        }
        return scheduled_at > other.scheduled_at;
    }
    
    std::string status_string() const {
        switch (status) {
            case JobStatus::PENDING: return "PENDING";
            case JobStatus::RUNNING: return "RUNNING";
            case JobStatus::COMPLETED: return "COMPLETED";
            case JobStatus::FAILED: return "FAILED";
            case JobStatus::CANCELLED: return "CANCELLED";
            case JobStatus::RETRY_PENDING: return "RETRY_PENDING";
            default: return "UNKNOWN";
        }
    }
};

/**
 * @brief Job handler function type
 */
using JobHandler = std::function<std::string(const Job&)>;

/**
 * @brief Job completion callback
 */
using JobCallback = std::function<void(const Job&)>;

/**
 * @brief Background job processor
 */
class JobProcessor {
public:
    struct Config {
        int worker_threads{4};
        int max_queue_size{10000};
        std::chrono::seconds poll_interval{1};
        bool persist_jobs{false};
        std::string persistence_path;
        bool enable_dead_letter{true};
        int dead_letter_max{1000};
    };
    
    JobProcessor() : config_(), running_(false), jobs_processed_(0), jobs_failed_(0) {}
    
    explicit JobProcessor(const Config& config)
        : config_(config), running_(false), jobs_processed_(0), jobs_failed_(0) {}
    
    ~JobProcessor() {
        stop();
    }
    
    /**
     * @brief Register job handler
     */
    void register_handler(const std::string& job_type, JobHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[job_type] = std::move(handler);
    }
    
    /**
     * @brief Submit job for processing
     */
    std::string submit(const std::string& type, const std::string& payload,
                       JobPriority priority = JobPriority::NORMAL,
                       const std::map<std::string, std::string>& metadata = {}) {
        Job job;
        job.id = generate_job_id();
        job.type = type;
        job.payload = payload;
        job.priority = priority;
        job.status = JobStatus::PENDING;
        job.created_at = std::chrono::system_clock::now();
        job.scheduled_at = job.created_at;
        job.metadata = metadata;
        
        return enqueue(job);
    }
    
    /**
     * @brief Schedule job for future execution
     */
    std::string schedule(const std::string& type, const std::string& payload,
                         std::chrono::system_clock::time_point run_at,
                         JobPriority priority = JobPriority::NORMAL) {
        Job job;
        job.id = generate_job_id();
        job.type = type;
        job.payload = payload;
        job.priority = priority;
        job.status = JobStatus::PENDING;
        job.created_at = std::chrono::system_clock::now();
        job.scheduled_at = run_at;
        
        return enqueue(job);
    }
    
    /**
     * @brief Schedule recurring job
     */
    std::string schedule_recurring(const std::string& type,
                                    const std::string& payload,
                                    std::chrono::seconds interval,
                                    JobPriority priority = JobPriority::NORMAL) {
        std::string recurring_id = "recurring:" + generate_job_id();
        
        RecurringJob rj;
        rj.id = recurring_id;
        rj.type = type;
        rj.payload = payload;
        rj.interval = interval;
        rj.priority = priority;
        rj.next_run = std::chrono::system_clock::now();
        rj.enabled = true;
        
        {
            std::lock_guard<std::mutex> lock(recurring_mutex_);
            recurring_jobs_[recurring_id] = rj;
        }
        
        return recurring_id;
    }
    
    /**
     * @brief Cancel recurring job
     */
    bool cancel_recurring(const std::string& recurring_id) {
        std::lock_guard<std::mutex> lock(recurring_mutex_);
        auto it = recurring_jobs_.find(recurring_id);
        if (it != recurring_jobs_.end()) {
            it->second.enabled = false;
            return true;
        }
        return false;
    }
    
    /**
     * @brief Cancel pending job
     */
    bool cancel(const std::string& job_id) {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        auto it = all_jobs_.find(job_id);
        if (it != all_jobs_.end() && it->second.status == JobStatus::PENDING) {
            it->second.status = JobStatus::CANCELLED;
            return true;
        }
        return false;
    }
    
    /**
     * @brief Get job status
     */
    std::optional<Job> get_job(const std::string& job_id) const {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        auto it = all_jobs_.find(job_id);
        if (it != all_jobs_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief Set completion callback
     */
    void on_completion(JobCallback callback) {
        completion_callback_ = std::move(callback);
    }
    
    /**
     * @brief Set failure callback
     */
    void on_failure(JobCallback callback) {
        failure_callback_ = std::move(callback);
    }
    
    /**
     * @brief Start processing
     */
    void start() {
        if (running_.exchange(true)) {
            return;
        }
        
        // Start worker threads
        for (int i = 0; i < config_.worker_threads; ++i) {
            workers_.emplace_back([this, i]() { worker_loop(i); });
        }
        
        // Start scheduler thread
        scheduler_thread_ = std::thread([this]() { scheduler_loop(); });
    }
    
    /**
     * @brief Stop processing
     */
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        
        cv_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
        
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.join();
        }
    }
    
    /**
     * @brief Wait for all pending jobs
     */
    void wait_all(std::chrono::seconds timeout = std::chrono::seconds{60}) {
        auto deadline = std::chrono::system_clock::now() + timeout;
        
        while (std::chrono::system_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (pending_queue_.empty() && running_count_ == 0) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    /**
     * @brief Get statistics
     */
    std::map<std::string, int64_t> get_stats() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        return {
            {"pending", static_cast<int64_t>(pending_queue_.size())},
            {"running", running_count_},
            {"processed", jobs_processed_.load()},
            {"failed", jobs_failed_.load()},
            {"dead_letter", static_cast<int64_t>(dead_letter_.size())}
        };
    }
    
    /**
     * @brief Get queue size
     */
    size_t queue_size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return pending_queue_.size();
    }
    
    /**
     * @brief Get dead letter jobs
     */
    std::vector<Job> get_dead_letter() const {
        std::lock_guard<std::mutex> lock(dead_letter_mutex_);
        return std::vector<Job>(dead_letter_.begin(), dead_letter_.end());
    }
    
    /**
     * @brief Retry dead letter job
     */
    bool retry_dead_letter(const std::string& job_id) {
        std::lock_guard<std::mutex> lock(dead_letter_mutex_);
        
        for (auto it = dead_letter_.begin(); it != dead_letter_.end(); ++it) {
            if (it->id == job_id) {
                Job job = *it;
                job.status = JobStatus::PENDING;
                job.retry_count = 0;
                job.error.clear();
                dead_letter_.erase(it);
                enqueue(job);
                return true;
            }
        }
        return false;
    }

private:
    struct RecurringJob {
        std::string id;
        std::string type;
        std::string payload;
        std::chrono::seconds interval;
        JobPriority priority;
        std::chrono::system_clock::time_point next_run;
        bool enabled{true};
    };
    
    Config config_;
    std::atomic<bool> running_;
    std::atomic<int64_t> jobs_processed_;
    std::atomic<int64_t> jobs_failed_;
    
    std::priority_queue<Job> pending_queue_;
    std::map<std::string, Job> all_jobs_;
    std::deque<Job> dead_letter_;
    std::map<std::string, RecurringJob> recurring_jobs_;
    std::map<std::string, JobHandler> handlers_;
    
    std::vector<std::thread> workers_;
    std::thread scheduler_thread_;
    
    mutable std::mutex queue_mutex_;
    mutable std::mutex jobs_mutex_;
    mutable std::mutex dead_letter_mutex_;
    std::mutex handlers_mutex_;
    std::mutex recurring_mutex_;
    std::condition_variable cv_;
    
    int running_count_{0};
    
    JobCallback completion_callback_;
    JobCallback failure_callback_;
    
    std::string enqueue(Job job) {
        std::string job_id = job.id;
        
        {
            std::lock_guard<std::mutex> lock(jobs_mutex_);
            all_jobs_[job_id] = job;
        }
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (pending_queue_.size() >= static_cast<size_t>(config_.max_queue_size)) {
                throw std::runtime_error("Job queue is full");
            }
            pending_queue_.push(job);
        }
        
        cv_.notify_one();
        return job_id;
    }
    
    void worker_loop([[maybe_unused]] int worker_id) {
        while (running_) {
            Job job;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait_for(lock, config_.poll_interval, [this]() {
                    return !running_ || !pending_queue_.empty();
                });
                
                if (!running_) break;
                if (pending_queue_.empty()) continue;
                
                job = pending_queue_.top();
                
                // Check if scheduled time has arrived
                if (job.scheduled_at > std::chrono::system_clock::now()) {
                    continue;
                }
                
                // Check if cancelled
                if (job.status == JobStatus::CANCELLED) {
                    pending_queue_.pop();
                    continue;
                }
                
                pending_queue_.pop();
                running_count_++;
            }
            
            process_job(job);
            
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                running_count_--;
            }
        }
    }
    
    void process_job(Job& job) {
        JobHandler handler;
        
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            auto it = handlers_.find(job.type);
            if (it == handlers_.end()) {
                job.status = JobStatus::FAILED;
                job.error = "No handler registered for job type: " + job.type;
                update_job(job);
                handle_failure(job);
                return;
            }
            handler = it->second;
        }
        
        job.status = JobStatus::RUNNING;
        job.started_at = std::chrono::system_clock::now();
        update_job(job);
        
        try {
            job.result = handler(job);
            job.status = JobStatus::COMPLETED;
            job.completed_at = std::chrono::system_clock::now();
            jobs_processed_++;
            
            if (completion_callback_) {
                completion_callback_(job);
            }
        } catch (const std::exception& e) {
            job.status = JobStatus::FAILED;
            job.error = e.what();
            job.completed_at = std::chrono::system_clock::now();
            jobs_failed_++;
            
            handle_failure(job);
        }
        
        update_job(job);
    }
    
    void handle_failure(Job& job) {
        if (job.retry_count < job.max_retries) {
            job.retry_count++;
            job.status = JobStatus::RETRY_PENDING;
            job.scheduled_at = std::chrono::system_clock::now() + 
                               job.retry_delay * job.retry_count;
            
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                pending_queue_.push(job);
            }
            cv_.notify_one();
        } else {
            // Move to dead letter queue
            if (config_.enable_dead_letter) {
                std::lock_guard<std::mutex> lock(dead_letter_mutex_);
                dead_letter_.push_back(job);
                if (dead_letter_.size() > static_cast<size_t>(config_.dead_letter_max)) {
                    dead_letter_.pop_front();
                }
            }
            
            if (failure_callback_) {
                failure_callback_(job);
            }
        }
    }
    
    void update_job(const Job& job) {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        all_jobs_[job.id] = job;
    }
    
    void scheduler_loop() {
        while (running_) {
            std::this_thread::sleep_for(config_.poll_interval);
            
            if (!running_) break;
            
            auto now = std::chrono::system_clock::now();
            
            std::lock_guard<std::mutex> lock(recurring_mutex_);
            for (auto& [id, rj] : recurring_jobs_) {
                if (rj.enabled && now >= rj.next_run) {
                    // Submit job
                    submit(rj.type, rj.payload, rj.priority, {{"recurring_id", id}});
                    rj.next_run = now + rj.interval;
                }
            }
        }
    }
    
    static std::string generate_job_id() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        
        std::ostringstream ss;
        ss << std::hex << now << "-" << counter++;
        return ss.str();
    }
};

/**
 * @brief Pre-built job types
 */
namespace jobs {

/**
 * @brief Email sending job
 */
struct EmailJob {
    std::string to;
    std::string subject;
    std::string body;
    std::string template_id;
    std::map<std::string, std::string> variables;
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << "{\"to\":\"" << to << "\",\"subject\":\"" << subject << "\"}";
        return ss.str();
    }
};

/**
 * @brief Report generation job
 */
struct ReportJob {
    std::string report_type;
    std::string user_id;
    std::string portfolio_id;
    std::string start_date;
    std::string end_date;
    std::string format;  // pdf, csv, html
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << "{\"type\":\"" << report_type << "\",\"portfolio\":\"" << portfolio_id << "\"}";
        return ss.str();
    }
};

/**
 * @brief Data sync job
 */
struct DataSyncJob {
    std::string source;
    std::string destination;
    std::vector<std::string> symbols;
    bool full_sync{false};
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << "{\"source\":\"" << source << "\",\"dest\":\"" << destination << "\"}";
        return ss.str();
    }
};

/**
 * @brief Cleanup job
 */
struct CleanupJob {
    std::string target;  // sessions, cache, logs, audit
    int retention_days{30};
    
    std::string serialize() const {
        std::ostringstream ss;
        ss << "{\"target\":\"" << target << "\",\"retention_days\":" << retention_days << "}";
        return ss.str();
    }
};

} // namespace jobs

} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_JOB_PROCESSOR_HPP
