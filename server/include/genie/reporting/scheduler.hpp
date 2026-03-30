/**
 * @file scheduler.hpp
 * @brief Scheduled Report Generation
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements scheduled report execution:
 * - Daily, weekly, monthly schedules
 * - Cron-style expressions (simplified)
 * - Background execution
 */

#pragma once
#ifndef GENIE_REPORTING_SCHEDULER_HPP
#define GENIE_REPORTING_SCHEDULER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace genie::reporting {

/**
 * @brief Schedule definition
 */
class Schedule {
public:
    enum class Type {
        ONCE,
        DAILY,
        WEEKLY,
        MONTHLY,
        CUSTOM
    };
    
    Type type{Type::DAILY};
    int hour{9};         // Hour of day (0-23)
    int minute{0};       // Minute (0-59)
    int day_of_week{1};  // 0=Sunday, 1=Monday, ... 6=Saturday
    int day_of_month{1}; // 1-31
    
    /**
     * @brief Create daily schedule
     */
    static Schedule daily(int hour = 9, int minute = 0) {
        Schedule s;
        s.type = Type::DAILY;
        s.hour = hour;
        s.minute = minute;
        return s;
    }
    
    /**
     * @brief Create weekly schedule
     */
    static Schedule weekly(int day_of_week = 1, int hour = 9, int minute = 0) {
        Schedule s;
        s.type = Type::WEEKLY;
        s.day_of_week = day_of_week;
        s.hour = hour;
        s.minute = minute;
        return s;
    }
    
    /**
     * @brief Create monthly schedule
     */
    static Schedule monthly(int day_of_month = 1, int hour = 9, int minute = 0) {
        Schedule s;
        s.type = Type::MONTHLY;
        s.day_of_month = day_of_month;
        s.hour = hour;
        s.minute = minute;
        return s;
    }
    
    /**
     * @brief Calculate next run time
     */
    [[nodiscard]] std::chrono::system_clock::time_point next_run() const {
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time);
        
        std::tm next_tm = *now_tm;
        next_tm.tm_hour = hour;
        next_tm.tm_min = minute;
        next_tm.tm_sec = 0;
        
        switch (type) {
            case Type::DAILY: {
                // If time has passed today, schedule for tomorrow
                if (now_tm->tm_hour > hour || 
                    (now_tm->tm_hour == hour && now_tm->tm_min >= minute)) {
                    next_tm.tm_mday += 1;
                }
                break;
            }
            
            case Type::WEEKLY: {
                int current_dow = now_tm->tm_wday;
                int days_until = (day_of_week - current_dow + 7) % 7;
                
                if (days_until == 0 && 
                    (now_tm->tm_hour > hour || 
                     (now_tm->tm_hour == hour && now_tm->tm_min >= minute))) {
                    days_until = 7;
                }
                
                next_tm.tm_mday += days_until;
                break;
            }
            
            case Type::MONTHLY: {
                if (now_tm->tm_mday > day_of_month ||
                    (now_tm->tm_mday == day_of_month &&
                     (now_tm->tm_hour > hour ||
                      (now_tm->tm_hour == hour && now_tm->tm_min >= minute)))) {
                    next_tm.tm_mon += 1;
                }
                next_tm.tm_mday = day_of_month;
                break;
            }
            
            default:
                break;
        }
        
        std::time_t next_time = std::mktime(&next_tm);
        return std::chrono::system_clock::from_time_t(next_time);
    }
    
    /**
     * @brief Get human-readable description
     */
    [[nodiscard]] std::string description() const {
        std::ostringstream oss;
        
        switch (type) {
            case Type::ONCE:
                oss << "Once";
                break;
            case Type::DAILY:
                oss << "Daily at " << hour << ":" 
                    << (minute < 10 ? "0" : "") << minute;
                break;
            case Type::WEEKLY:
                oss << "Weekly on ";
                switch (day_of_week) {
                    case 0: oss << "Sunday"; break;
                    case 1: oss << "Monday"; break;
                    case 2: oss << "Tuesday"; break;
                    case 3: oss << "Wednesday"; break;
                    case 4: oss << "Thursday"; break;
                    case 5: oss << "Friday"; break;
                    case 6: oss << "Saturday"; break;
                }
                oss << " at " << hour << ":" << (minute < 10 ? "0" : "") << minute;
                break;
            case Type::MONTHLY:
                oss << "Monthly on day " << day_of_month 
                    << " at " << hour << ":" << (minute < 10 ? "0" : "") << minute;
                break;
            default:
                oss << "Custom";
        }
        
        return oss.str();
    }
};

/**
 * @brief Scheduled report configuration
 */
struct ScheduledReport {
    std::string id;
    std::string name;
    std::string template_name;
    Schedule schedule;
    std::string output_path;
    bool enabled{true};
    std::function<void()> on_execute;
    
    // Status
    std::chrono::system_clock::time_point last_run;
    std::chrono::system_clock::time_point next_run;
    int run_count{0};
    std::string last_error;
};

/**
 * @brief Report scheduler
 */
class ReportScheduler {
    std::map<std::string, ScheduledReport> reports_;
    std::thread scheduler_thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    int next_id_{1};

public:
    ~ReportScheduler() {
        stop();
    }

    /**
     * @brief Start the scheduler
     */
    void start() {
        if (running_) return;
        
        running_ = true;
        scheduler_thread_ = std::thread([this]() {
            scheduler_loop();
        });
    }

    /**
     * @brief Stop the scheduler
     */
    void stop() {
        if (!running_) return;
        
        running_ = false;
        cv_.notify_all();
        
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.join();
        }
    }

    /**
     * @brief Schedule a new report
     * @return Report ID
     */
    std::string schedule(ScheduledReport report) {
        std::lock_guard lock(mutex_);
        
        if (report.id.empty()) {
            report.id = "report_" + std::to_string(next_id_++);
        }
        
        report.next_run = report.schedule.next_run();
        reports_[report.id] = std::move(report);
        
        cv_.notify_one();
        
        return reports_[report.id].id;
    }

    /**
     * @brief Cancel a scheduled report
     */
    void cancel(const std::string& report_id) {
        std::lock_guard lock(mutex_);
        reports_.erase(report_id);
    }

    /**
     * @brief Run a report immediately
     */
    void run_now(const std::string& report_id) {
        std::lock_guard lock(mutex_);
        
        auto it = reports_.find(report_id);
        if (it != reports_.end() && it->second.on_execute) {
            execute_report(it->second);
        }
    }

    /**
     * @brief Enable/disable a report
     */
    void set_enabled(const std::string& report_id, bool enabled) {
        std::lock_guard lock(mutex_);
        
        auto it = reports_.find(report_id);
        if (it != reports_.end()) {
            it->second.enabled = enabled;
            if (enabled) {
                it->second.next_run = it->second.schedule.next_run();
            }
        }
    }

    /**
     * @brief Get all scheduled reports
     */
    [[nodiscard]] std::vector<ScheduledReport> list_scheduled() const {
        std::lock_guard lock(mutex_);
        
        std::vector<ScheduledReport> result;
        result.reserve(reports_.size());
        
        for (const auto& [id, report] : reports_) {
            result.push_back(report);
        }
        
        return result;
    }

    /**
     * @brief Get a specific report
     */
    [[nodiscard]] std::optional<ScheduledReport> get(const std::string& id) const {
        std::lock_guard lock(mutex_);
        
        auto it = reports_.find(id);
        if (it != reports_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Check if scheduler is running
     */
    [[nodiscard]] bool is_running() const {
        return running_;
    }

private:
    void scheduler_loop() {
        while (running_) {
            std::unique_lock lock(mutex_);
            
            // Find next report to run
            auto next_time = std::chrono::system_clock::time_point::max();
            ScheduledReport* next_report = nullptr;
            
            for (auto& [id, report] : reports_) {
                if (report.enabled && report.next_run < next_time) {
                    next_time = report.next_run;
                    next_report = &report;
                }
            }
            
            if (!next_report) {
                // No reports, wait for new ones
                cv_.wait_for(lock, std::chrono::minutes(1));
                continue;
            }
            
            auto now = std::chrono::system_clock::now();
            
            if (next_time <= now) {
                // Time to run
                execute_report(*next_report);
                next_report->next_run = next_report->schedule.next_run();
            } else {
                // Wait until next run time or new report
                cv_.wait_until(lock, next_time);
            }
        }
    }

    void execute_report(ScheduledReport& report) {
        try {
            if (report.on_execute) {
                report.on_execute();
            }
            
            report.last_run = std::chrono::system_clock::now();
            ++report.run_count;
            report.last_error.clear();
        } catch (const std::exception& e) {
            report.last_error = e.what();
        } catch (...) {
            report.last_error = "Unknown error";
        }
    }
};

} // namespace genie::reporting

#endif // GENIE_REPORTING_SCHEDULER_HPP
