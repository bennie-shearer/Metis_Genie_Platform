/**
 * @file backup_manager.hpp
 * @brief Automatic backups and system status dashboard
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Operations - Implement automatic backups, create system status dashboard
 */

#ifndef GENIE_OPS_BACKUP_MANAGER_HPP
#define GENIE_OPS_BACKUP_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <queue>

namespace genie {
namespace ops {

/**
 * @brief Backup type
 */
enum class BackupType {
    FULL,
    INCREMENTAL,
    DIFFERENTIAL
};

/**
 * @brief Backup status
 */
enum class BackupStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

/**
 * @brief Backup metadata
 */
struct BackupInfo {
    std::string id;
    std::string name;
    BackupType type{BackupType::FULL};
    BackupStatus status{BackupStatus::PENDING};
    std::string path;
    size_t size_bytes{0};
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::chrono::seconds duration{0};
    std::string error_message;
    std::vector<std::string> included_items;
    std::string checksum;
    bool compressed{false};
    bool encrypted{false};
    
    std::string status_string() const {
        switch (status) {
            case BackupStatus::PENDING: return "pending";
            case BackupStatus::RUNNING: return "running";
            case BackupStatus::COMPLETED: return "completed";
            case BackupStatus::FAILED: return "failed";
            case BackupStatus::CANCELLED: return "cancelled";
            default: return "unknown";
        }
    }
    
    std::string type_string() const {
        switch (type) {
            case BackupType::FULL: return "full";
            case BackupType::INCREMENTAL: return "incremental";
            case BackupType::DIFFERENTIAL: return "differential";
            default: return "unknown";
        }
    }
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"id\":\"" << id << "\",";
        json << "\"name\":\"" << name << "\",";
        json << "\"type\":\"" << type_string() << "\",";
        json << "\"status\":\"" << status_string() << "\",";
        json << "\"path\":\"" << path << "\",";
        json << "\"size_bytes\":" << size_bytes << ",";
        json << "\"duration_seconds\":" << duration.count() << ",";
        json << "\"compressed\":" << (compressed ? "true" : "false") << ",";
        json << "\"encrypted\":" << (encrypted ? "true" : "false");
        
        if (!error_message.empty()) {
            json << ",\"error\":\"" << error_message << "\"";
        }
        
        json << "}";
        return json.str();
    }
};

/**
 * @brief Backup schedule
 */
struct BackupSchedule {
    std::string id;
    std::string name;
    BackupType type{BackupType::FULL};
    std::string cron_expression;  // Simplified cron
    bool enabled{true};
    int retention_days{30};
    int max_backups{10};
    std::vector<std::string> targets;
    bool compress{true};
    bool encrypt{false};
    std::chrono::system_clock::time_point last_run;
    std::chrono::system_clock::time_point next_run;
};

/**
 * @brief Backup manager
 */
class BackupManager {
public:
    struct Config {
        std::string backup_directory{"./backups"};
        int default_retention_days{30};
        bool compress_by_default{true};
        bool encrypt_by_default{false};
        std::string encryption_key;
        size_t max_concurrent_backups{2};
        bool enabled{true};              // Master enable/disable from config.json
        int interval_hours{24};           // Auto-backup interval in hours
    };
    
    using BackupCallback = std::function<void(const BackupInfo&)>;
    using DataSource = std::function<std::vector<uint8_t>(const std::string&)>;
    
    BackupManager() : config_(), running_(false) {}
    
    explicit BackupManager(const Config& config)
        : config_(config), running_(false) {
        // Ensure backup directory exists
        std::filesystem::create_directories(config_.backup_directory);
    }
    
    ~BackupManager() {
        stop();
    }
    
    /**
     * @brief Register data source for backup
     */
    void register_source(const std::string& name, DataSource source) {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_[name] = std::move(source);
    }
    
    /**
     * @brief Create manual backup
     */
    std::string create_backup(const std::string& name,
                               const std::vector<std::string>& targets,
                               BackupType type = BackupType::FULL) {
        BackupInfo info;
        info.id = generate_backup_id();
        info.name = name;
        info.type = type;
        info.included_items = targets;
        info.compressed = config_.compress_by_default;
        info.encrypted = config_.encrypt_by_default;
        
        // Queue for execution
        {
            std::lock_guard<std::mutex> lock(mutex_);
            backups_[info.id] = info;
            backup_queue_.push(info.id);
        }
        
        // Start if not running
        if (!running_) {
            process_queue();
        }
        
        return info.id;
    }
    
    /**
     * @brief Get backup status
     */
    std::optional<BackupInfo> get_backup(const std::string& backup_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backups_.find(backup_id);
        if (it != backups_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief List all backups
     */
    std::vector<BackupInfo> list_backups() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BackupInfo> result;
        for (const auto& [id, info] : backups_) {
            result.push_back(info);
        }
        // Sort by date descending
        std::sort(result.begin(), result.end(),
                  [](const BackupInfo& a, const BackupInfo& b) {
                      return a.started_at > b.started_at;
                  });
        return result;
    }
    
    /**
     * @brief Delete backup
     */
    bool delete_backup(const std::string& backup_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backups_.find(backup_id);
        if (it == backups_.end()) {
            return false;
        }
        
        // Delete file
        if (!it->second.path.empty()) {
            std::filesystem::remove(it->second.path);
        }
        
        backups_.erase(it);
        return true;
    }
    
    /**
     * @brief Restore from backup
     */
    bool restore(const std::string& backup_id,
                 const std::vector<std::string>& /*targets*/ = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = backups_.find(backup_id);
        if (it == backups_.end() || it->second.status != BackupStatus::COMPLETED) {
            return false;
        }
        
        // Restore logic would go here
        // For now, just verify backup exists
        return std::filesystem::exists(it->second.path);
    }
    
    /**
     * @brief Add backup schedule
     */
    void add_schedule(const BackupSchedule& schedule) {
        std::lock_guard<std::mutex> lock(mutex_);
        schedules_[schedule.id] = schedule;
        calculate_next_run(schedules_[schedule.id]);
    }
    
    /**
     * @brief Remove schedule
     */
    bool remove_schedule(const std::string& schedule_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return schedules_.erase(schedule_id) > 0;
    }
    
    /**
     * @brief List schedules
     */
    std::vector<BackupSchedule> list_schedules() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BackupSchedule> result;
        for (const auto& [id, schedule] : schedules_) {
            result.push_back(schedule);
        }
        return result;
    }
    
    /**
     * @brief Set completion callback
     */
    void on_complete(BackupCallback callback) {
        completion_callback_ = std::move(callback);
    }
    
    /**
     * @brief Start scheduler
     */
    void start() {
        if (running_.exchange(true)) return;
        scheduler_thread_ = std::thread([this]() { scheduler_loop(); });
    }
    
    /**
     * @brief Stop scheduler
     */
    void stop() {
        running_ = false;
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.join();
        }
    }
    
    /**
     * @brief Cleanup old backups
     */
    int cleanup_old_backups(int retention_days = -1) {
        if (retention_days < 0) {
            retention_days = config_.default_retention_days;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto cutoff = std::chrono::system_clock::now() - 
                      std::chrono::hours(24 * retention_days);
        
        std::vector<std::string> to_delete;
        for (const auto& [id, info] : backups_) {
            if (info.completed_at < cutoff && info.status == BackupStatus::COMPLETED) {
                to_delete.push_back(id);
            }
        }
        
        for (const auto& id : to_delete) {
            auto it = backups_.find(id);
            if (it != backups_.end()) {
                if (!it->second.path.empty()) {
                    std::filesystem::remove(it->second.path);
                }
                backups_.erase(it);
            }
        }
        
        return static_cast<int>(to_delete.size());
    }

private:
    Config config_;
    std::map<std::string, BackupInfo> backups_;
    std::map<std::string, BackupSchedule> schedules_;
    std::map<std::string, DataSource> sources_;
    std::queue<std::string> backup_queue_;
    std::atomic<bool> running_;
    std::thread scheduler_thread_;
    BackupCallback completion_callback_;
    mutable std::mutex mutex_;
    
    void process_queue() {
        while (true) {
            std::string backup_id;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (backup_queue_.empty()) return;
                backup_id = backup_queue_.front();
                backup_queue_.pop();
            }
            
            execute_backup(backup_id);
        }
    }
    
    void execute_backup(const std::string& backup_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = backups_.find(backup_id);
        if (it == backups_.end()) return;
        
        auto& info = it->second;
        info.status = BackupStatus::RUNNING;
        info.started_at = std::chrono::system_clock::now();
        
        try {
            // Generate backup filename
            auto time_t = std::chrono::system_clock::to_time_t(info.started_at);
            std::ostringstream filename;
            filename << info.name << "_" 
                     << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
            if (info.compressed) filename << ".gz";
            filename << ".bak";
            
            info.path = config_.backup_directory + "/" + filename.str();
            
            // Collect data from sources
            std::vector<uint8_t> backup_data;
            for (const auto& target : info.included_items) {
                auto source_it = sources_.find(target);
                if (source_it != sources_.end()) {
                    auto data = source_it->second(target);
                    backup_data.insert(backup_data.end(), data.begin(), data.end());
                }
            }
            
            // Write backup file
            std::ofstream file(info.path, std::ios::binary);
            if (file) {
                file.write(reinterpret_cast<const char*>(backup_data.data()),
                          backup_data.size());
                file.close();
                
                info.size_bytes = backup_data.size();
                info.status = BackupStatus::COMPLETED;
                info.checksum = calculate_checksum(backup_data);
            } else {
                throw std::runtime_error("Failed to create backup file");
            }
            
        } catch (const std::exception& e) {
            info.status = BackupStatus::FAILED;
            info.error_message = e.what();
        }
        
        info.completed_at = std::chrono::system_clock::now();
        info.duration = std::chrono::duration_cast<std::chrono::seconds>(
            info.completed_at - info.started_at);
        
        if (completion_callback_) {
            completion_callback_(info);
        }
    }
    
    void scheduler_loop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            
            if (!running_) break;
            
            auto now = std::chrono::system_clock::now();
            
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, schedule] : schedules_) {
                if (schedule.enabled && now >= schedule.next_run) {
                    // Create scheduled backup
                    BackupInfo info;
                    info.id = generate_backup_id();
                    info.name = schedule.name;
                    info.type = schedule.type;
                    info.included_items = schedule.targets;
                    info.compressed = schedule.compress;
                    info.encrypted = schedule.encrypt;
                    
                    backups_[info.id] = info;
                    backup_queue_.push(info.id);
                    
                    schedule.last_run = now;
                    calculate_next_run(schedule);
                }
            }
        }
    }
    
    void calculate_next_run(BackupSchedule& schedule) {
        // Simplified: just add 24 hours for daily backups
        schedule.next_run = std::chrono::system_clock::now() + std::chrono::hours(24);
    }
    
    static std::string generate_backup_id() {
        static std::atomic<int> counter{0};
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream ss;
        ss << std::hex << now << "-" << counter++;
        return "bak-" + ss.str();
    }
    
    static std::string calculate_checksum(const std::vector<uint8_t>& data) {
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (uint8_t b : data) {
            hash ^= b;
            hash *= 0x100000001b3ULL;
        }
        std::ostringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
};

/**
 * @brief System status dashboard data
 */
struct SystemStatus {
    // System info
    std::string version;
    std::chrono::seconds uptime{0};
    std::chrono::system_clock::time_point server_time;
    
    // Resource usage
    double cpu_usage_pct{0.0};
    size_t memory_used_mb{0};
    size_t memory_total_mb{0};
    size_t disk_used_mb{0};
    size_t disk_total_mb{0};
    
    // Application metrics
    int64_t active_sessions{0};
    int64_t total_requests{0};
    double requests_per_second{0.0};
    double avg_response_time_ms{0.0};
    double error_rate_pct{0.0};
    
    // Component status
    std::map<std::string, std::string> component_status;
    
    // Recent activity
    std::vector<std::string> recent_events;
    
    // Alerts
    std::vector<std::string> active_alerts;
    
    std::string to_json() const {
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);
        json << "{";
        
        json << "\"version\":\"" << version << "\",";
        json << "\"uptime_seconds\":" << uptime.count() << ",";
        
        auto time_t = std::chrono::system_clock::to_time_t(server_time);
        json << "\"server_time\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\",";
        
        json << "\"resources\":{";
        json << "\"cpu_usage_pct\":" << cpu_usage_pct << ",";
        json << "\"memory_used_mb\":" << memory_used_mb << ",";
        json << "\"memory_total_mb\":" << memory_total_mb << ",";
        json << "\"disk_used_mb\":" << disk_used_mb << ",";
        json << "\"disk_total_mb\":" << disk_total_mb;
        json << "},";
        
        json << "\"metrics\":{";
        json << "\"active_sessions\":" << active_sessions << ",";
        json << "\"total_requests\":" << total_requests << ",";
        json << "\"requests_per_second\":" << requests_per_second << ",";
        json << "\"avg_response_time_ms\":" << avg_response_time_ms << ",";
        json << "\"error_rate_pct\":" << error_rate_pct;
        json << "},";
        
        json << "\"components\":{";
        bool first = true;
        for (const auto& [k, v] : component_status) {
            if (!first) json << ",";
            json << "\"" << k << "\":\"" << v << "\"";
            first = false;
        }
        json << "},";
        
        json << "\"active_alerts\":[";
        for (size_t i = 0; i < active_alerts.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << active_alerts[i] << "\"";
        }
        json << "]";
        
        json << "}";
        return json.str();
    }
};

/**
 * @brief System status collector
 */
class StatusDashboard {
public:
    using MetricProvider = std::function<double()>;
    using StatusProvider = std::function<std::string()>;
    
    explicit StatusDashboard(const std::string& version = "2.25.0")
        : version_(version), start_time_(std::chrono::system_clock::now()) {}
    
    /**
     * @brief Register metric provider
     */
    void register_metric(const std::string& name, MetricProvider provider) {
        std::lock_guard<std::mutex> lock(mutex_);
        metric_providers_[name] = std::move(provider);
    }
    
    /**
     * @brief Register component status provider
     */
    void register_component(const std::string& name, StatusProvider provider) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_providers_[name] = std::move(provider);
    }
    
    /**
     * @brief Record request
     */
    void record_request(double response_time_ms, bool success) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        total_requests_++;
        if (!success) error_count_++;
        
        total_response_time_ += response_time_ms;
        
        // Update per-second tracking
        auto now = std::chrono::system_clock::now();
        if (now - last_rate_calc_ >= std::chrono::seconds(1)) {
            requests_per_second_ = static_cast<double>(requests_in_window_);
            requests_in_window_ = 0;
            last_rate_calc_ = now;
        }
        requests_in_window_++;
    }
    
    /**
     * @brief Add alert
     */
    void add_alert(const std::string& alert) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_alerts_.push_back(alert);
        if (active_alerts_.size() > 100) {
            active_alerts_.erase(active_alerts_.begin());
        }
    }
    
    /**
     * @brief Clear alert
     */
    void clear_alert(const std::string& alert) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_alerts_.erase(
            std::remove(active_alerts_.begin(), active_alerts_.end(), alert),
            active_alerts_.end());
    }
    
    /**
     * @brief Set active sessions count
     */
    void set_active_sessions(int64_t count) {
        active_sessions_ = count;
    }
    
    /**
     * @brief Get current system status
     */
    SystemStatus get_status() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        SystemStatus status;
        status.version = version_;
        status.server_time = std::chrono::system_clock::now();
        status.uptime = std::chrono::duration_cast<std::chrono::seconds>(
            status.server_time - start_time_);
        
        // Get metrics from providers
        for (const auto& [name, provider] : metric_providers_) {
            double value = provider();
            if (name == "cpu_usage") status.cpu_usage_pct = value;
            else if (name == "memory_used") status.memory_used_mb = static_cast<size_t>(value);
            else if (name == "memory_total") status.memory_total_mb = static_cast<size_t>(value);
            else if (name == "disk_used") status.disk_used_mb = static_cast<size_t>(value);
            else if (name == "disk_total") status.disk_total_mb = static_cast<size_t>(value);
        }
        
        // Application metrics
        status.active_sessions = active_sessions_;
        status.total_requests = total_requests_;
        status.requests_per_second = requests_per_second_;
        status.avg_response_time_ms = total_requests_ > 0 ?
            total_response_time_ / total_requests_ : 0.0;
        status.error_rate_pct = total_requests_ > 0 ?
            100.0 * error_count_ / total_requests_ : 0.0;
        
        // Component status
        for (const auto& [name, provider] : status_providers_) {
            status.component_status[name] = provider();
        }
        
        // Alerts
        status.active_alerts = active_alerts_;
        
        return status;
    }
    
    /**
     * @brief Generate HTML dashboard
     */
    std::string generate_html_dashboard() {
        auto status = get_status();
        
        std::ostringstream html;
        html << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Metis Genie Platform - System Status</title>
    <style>
        body { font-family: -apple-system, sans-serif; margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 { color: #00d4aa; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
        .card { background: #16213e; border-radius: 8px; padding: 20px; }
        .card h2 { margin-top: 0; color: #00d4aa; font-size: 1.1em; }
        .metric { display: flex; justify-content: space-between; margin: 10px 0; }
        .metric-value { font-weight: bold; color: #00d4aa; }
        .status-healthy { color: #00d4aa; }
        .status-degraded { color: #ffc107; }
        .status-unhealthy { color: #dc3545; }
        .progress { background: #0f3460; border-radius: 4px; height: 8px; margin-top: 5px; }
        .progress-bar { background: #00d4aa; border-radius: 4px; height: 100%; }
        .alert { background: #dc3545; padding: 10px; border-radius: 4px; margin: 5px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Metis Genie Platform System Status</h1>
        <p>Version: )" << status.version << R"( | Uptime: )" << (status.uptime.count() / 3600) << R"(h )" << ((status.uptime.count() % 3600) / 60) << R"(m</p>
        
        <div class="grid">
            <div class="card">
                <h2>Resources</h2>
                <div class="metric">
                    <span>CPU Usage</span>
                    <span class="metric-value">)" << std::fixed << std::setprecision(1) << status.cpu_usage_pct << R"(%</span>
                </div>
                <div class="progress"><div class="progress-bar" style="width: )" << status.cpu_usage_pct << R"(%"></div></div>
                
                <div class="metric">
                    <span>Memory</span>
                    <span class="metric-value">)" << status.memory_used_mb << R"( / )" << status.memory_total_mb << R"( MB</span>
                </div>
                <div class="progress"><div class="progress-bar" style="width: )" << (status.memory_total_mb > 0 ? 100.0 * status.memory_used_mb / status.memory_total_mb : 0) << R"(%"></div></div>
                
                <div class="metric">
                    <span>Disk</span>
                    <span class="metric-value">)" << status.disk_used_mb << R"( / )" << status.disk_total_mb << R"( MB</span>
                </div>
                <div class="progress"><div class="progress-bar" style="width: )" << (status.disk_total_mb > 0 ? 100.0 * status.disk_used_mb / status.disk_total_mb : 0) << R"(%"></div></div>
            </div>
            
            <div class="card">
                <h2>Application Metrics</h2>
                <div class="metric">
                    <span>Active Sessions</span>
                    <span class="metric-value">)" << status.active_sessions << R"(</span>
                </div>
                <div class="metric">
                    <span>Total Requests</span>
                    <span class="metric-value">)" << status.total_requests << R"(</span>
                </div>
                <div class="metric">
                    <span>Requests/sec</span>
                    <span class="metric-value">)" << std::setprecision(1) << status.requests_per_second << R"(</span>
                </div>
                <div class="metric">
                    <span>Avg Response Time</span>
                    <span class="metric-value">)" << std::setprecision(2) << status.avg_response_time_ms << R"( ms</span>
                </div>
                <div class="metric">
                    <span>Error Rate</span>
                    <span class="metric-value">)" << std::setprecision(2) << status.error_rate_pct << R"(%</span>
                </div>
            </div>
            
            <div class="card">
                <h2>Components</h2>)";
        
        for (const auto& [name, comp_status] : status.component_status) {
            std::string status_class = "status-healthy";
            if (comp_status == "degraded") status_class = "status-degraded";
            else if (comp_status == "unhealthy") status_class = "status-unhealthy";
            
            html << R"(
                <div class="metric">
                    <span>)" << name << R"(</span>
                    <span class=")" << status_class << R"(">)" << comp_status << R"(</span>
                </div>)";
        }
        
        html << R"(
            </div>)";
        
        if (!status.active_alerts.empty()) {
            html << R"(
            <div class="card">
                <h2>Active Alerts</h2>)";
            for (const auto& alert : status.active_alerts) {
                html << R"(
                <div class="alert">)" << alert << R"(</div>)";
            }
            html << R"(
            </div>)";
        }
        
        html << R"(
        </div>
    </div>
    <script>
        setTimeout(() => location.reload(), 30000);
    </script>
</body>
</html>)";
        
        return html.str();
    }

private:
    std::string version_;
    std::chrono::system_clock::time_point start_time_;
    
    std::map<std::string, MetricProvider> metric_providers_;
    std::map<std::string, StatusProvider> status_providers_;
    
    std::atomic<int64_t> total_requests_{0};
    std::atomic<int64_t> error_count_{0};
    std::atomic<int64_t> active_sessions_{0};
    double total_response_time_{0.0};
    double requests_per_second_{0.0};
    int requests_in_window_{0};
    std::chrono::system_clock::time_point last_rate_calc_{std::chrono::system_clock::now()};
    
    std::vector<std::string> active_alerts_;
    
    mutable std::mutex mutex_;
};

} // namespace ops
} // namespace genie

#endif // GENIE_OPS_BACKUP_MANAGER_HPP
