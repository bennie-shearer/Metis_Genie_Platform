/**
 * @file ip_whitelist.hpp
 * @brief IP whitelist for API access and enhanced audit logging
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Security - IP whitelist for API access, audit logging for all actions
 */

#ifndef GENIE_SECURITY_IP_WHITELIST_HPP
#define GENIE_SECURITY_IP_WHITELIST_HPP

#include "../core/logging.hpp"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <mutex>
#include <regex>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace genie {
namespace security {

/**
 * @brief IP address or CIDR range
 */
struct IPRange {
    std::string address;
    int prefix_length{32};
    std::string description;
    bool enabled{true};
    std::chrono::system_clock::time_point added_at;
    std::chrono::system_clock::time_point expires_at;
    
    /**
     * @brief Check if IP matches this range
     */
    bool matches(const std::string& ip) const {
        if (!enabled) return false;
        
        // Check expiry
        if (expires_at != std::chrono::system_clock::time_point{} &&
            std::chrono::system_clock::now() > expires_at) {
            return false;
        }
        
        // Parse both IPs
        uint32_t range_ip = parse_ipv4(address);
        uint32_t check_ip = parse_ipv4(ip);
        
        if (range_ip == 0 || check_ip == 0) return false;
        
        // Create mask
        uint32_t mask = prefix_length == 0 ? 0 : ~((1U << (32 - prefix_length)) - 1);
        
        return (range_ip & mask) == (check_ip & mask);
    }
    
    static uint32_t parse_ipv4(const std::string& ip) {
        uint32_t result = 0;
        int octet = 0;
        int value = 0;
        
        for (char c : ip) {
            if (c == '.') {
                if (value > 255 || octet > 3) return 0;
                result = (result << 8) | static_cast<uint32_t>(value);
                value = 0;
                octet++;
            } else if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
            } else {
                return 0;
            }
        }
        
        if (value > 255 || octet != 3) return 0;
        result = (result << 8) | static_cast<uint32_t>(value);
        
        return result;
    }
    
    static std::string uint32_to_ip(uint32_t ip) {
        std::ostringstream ss;
        ss << ((ip >> 24) & 0xFF) << "."
           << ((ip >> 16) & 0xFF) << "."
           << ((ip >> 8) & 0xFF) << "."
           << (ip & 0xFF);
        return ss.str();
    }
};

/**
 * @brief IP Whitelist manager
 */
class IPWhitelist {
public:
    struct Config {
        bool enabled{true};
        bool allow_localhost{true};
        bool allow_private_ranges{false};
        int max_entries{1000};
        std::string persistence_path;
    };
    
    explicit IPWhitelist(const Config& config) : config_(config) {
        // Add localhost if allowed
        if (config_.allow_localhost) {
            add_range("127.0.0.1", 32, "Localhost", false);
            add_range("::1", 128, "Localhost IPv6", false);
        }
        
        // Add private ranges if allowed
        if (config_.allow_private_ranges) {
            add_range("10.0.0.0", 8, "Private Class A", false);
            add_range("172.16.0.0", 12, "Private Class B", false);
            add_range("192.168.0.0", 16, "Private Class C", false);
        }
    }
    
    /**
     * @brief Add IP range to whitelist
     */
    bool add_range(const std::string& address, int prefix_length = 32,
                   const std::string& description = "",
                   bool user_added = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (user_added && static_cast<int>(ranges_.size()) >= config_.max_entries) {
            return false;
        }
        
        IPRange range;
        range.address = address;
        range.prefix_length = prefix_length;
        range.description = description;
        range.enabled = true;
        range.added_at = std::chrono::system_clock::now();
        
        ranges_.push_back(range);
        return true;
    }
    
    /**
     * @brief Add IP range with expiry
     */
    bool add_temporary_range(const std::string& address, int prefix_length,
                             const std::string& description,
                             std::chrono::hours duration) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (static_cast<int>(ranges_.size()) >= config_.max_entries) {
            return false;
        }
        
        IPRange range;
        range.address = address;
        range.prefix_length = prefix_length;
        range.description = description;
        range.enabled = true;
        range.added_at = std::chrono::system_clock::now();
        range.expires_at = range.added_at + duration;
        
        ranges_.push_back(range);
        return true;
    }
    
    /**
     * @brief Remove IP range
     */
    bool remove_range(const std::string& address, int prefix_length = 32) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::remove_if(ranges_.begin(), ranges_.end(),
            [&](const IPRange& r) {
                return r.address == address && r.prefix_length == prefix_length;
            });
        
        if (it != ranges_.end()) {
            ranges_.erase(it, ranges_.end());
            return true;
        }
        return false;
    }
    
    /**
     * @brief Check if IP is allowed
     */
    bool is_allowed(const std::string& ip) const {
        if (!config_.enabled) return true;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& range : ranges_) {
            if (range.matches(ip)) {
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * @brief Get all whitelisted ranges
     */
    std::vector<IPRange> get_ranges() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ranges_;
    }
    
    /**
     * @brief Enable/disable whitelist
     */
    void set_enabled(bool enabled) {
        config_.enabled = enabled;
    }
    
    /**
     * @brief Cleanup expired entries
     */
    int cleanup_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::system_clock::now();
        size_t before = ranges_.size();
        
        ranges_.erase(
            std::remove_if(ranges_.begin(), ranges_.end(),
                [&now](const IPRange& r) {
                    return r.expires_at != std::chrono::system_clock::time_point{} &&
                           now > r.expires_at;
                }),
            ranges_.end());
        
        return static_cast<int>(before - ranges_.size());
    }

private:
    Config config_;
    std::vector<IPRange> ranges_;
    mutable std::mutex mutex_;
};

/**
 * @brief Audit event types
 */
enum class AuditEventType {
    // Authentication
    LOGIN_SUCCESS,
    LOGIN_FAILURE,
    LOGOUT,
    SESSION_CREATED,
    SESSION_EXPIRED,
    TWO_FA_ENABLED,
    TWO_FA_DISABLED,
    TWO_FA_VERIFIED,
    TWO_FA_FAILED,
    PASSWORD_CHANGED,
    PASSWORD_RESET_REQUESTED,
    
    // Authorization
    ACCESS_DENIED,
    IP_BLOCKED,
    RATE_LIMITED,
    
    // Trading
    ORDER_SUBMITTED,
    ORDER_FILLED,
    ORDER_CANCELLED,
    ORDER_REJECTED,
    POSITION_OPENED,
    POSITION_CLOSED,
    
    // Account
    ACCOUNT_CREATED,
    ACCOUNT_MODIFIED,
    ACCOUNT_DELETED,
    API_KEY_CREATED,
    API_KEY_REVOKED,
    
    // Data access
    DATA_EXPORTED,
    REPORT_GENERATED,
    
    // System
    SETTINGS_CHANGED,
    BACKUP_CREATED,
    SYSTEM_ERROR,
    
    // Security
    SUSPICIOUS_ACTIVITY,
    BRUTE_FORCE_DETECTED,
    
    CUSTOM
};

/**
 * @brief Audit event
 */
struct AuditEvent {
    std::string event_id;
    AuditEventType type{AuditEventType::CUSTOM};
    std::string user_id;
    std::string session_id;
    std::string ip_address;
    std::string user_agent;
    std::string action;
    std::string resource;
    std::string details;
    std::map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point timestamp;
    bool success{true};
    std::string error_message;
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"event_id\":\"" << event_id << "\",";
        json << "\"type\":\"" << event_type_to_string(type) << "\",";
        json << "\"user_id\":\"" << user_id << "\",";
        json << "\"session_id\":\"" << session_id << "\",";
        json << "\"ip_address\":\"" << ip_address << "\",";
        json << "\"action\":\"" << action << "\",";
        json << "\"resource\":\"" << resource << "\",";
        json << "\"success\":" << (success ? "true" : "false") << ",";
        
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        json << "\"timestamp\":\"" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ") << "\"";
        
        if (!error_message.empty()) {
            json << ",\"error\":\"" << error_message << "\"";
        }
        
        if (!metadata.empty()) {
            json << ",\"metadata\":{";
            bool first = true;
            for (const auto& [k, v] : metadata) {
                if (!first) json << ",";
                json << "\"" << k << "\":\"" << v << "\"";
                first = false;
            }
            json << "}";
        }
        
        json << "}";
        return json.str();
    }
    
    static std::string event_type_to_string(AuditEventType type) {
        switch (type) {
            case AuditEventType::LOGIN_SUCCESS: return "LOGIN_SUCCESS";
            case AuditEventType::LOGIN_FAILURE: return "LOGIN_FAILURE";
            case AuditEventType::LOGOUT: return "LOGOUT";
            case AuditEventType::SESSION_CREATED: return "SESSION_CREATED";
            case AuditEventType::SESSION_EXPIRED: return "SESSION_EXPIRED";
            case AuditEventType::TWO_FA_ENABLED: return "TWO_FA_ENABLED";
            case AuditEventType::TWO_FA_DISABLED: return "TWO_FA_DISABLED";
            case AuditEventType::TWO_FA_VERIFIED: return "TWO_FA_VERIFIED";
            case AuditEventType::TWO_FA_FAILED: return "TWO_FA_FAILED";
            case AuditEventType::PASSWORD_CHANGED: return "PASSWORD_CHANGED";
            case AuditEventType::PASSWORD_RESET_REQUESTED: return "PASSWORD_RESET_REQUESTED";
            case AuditEventType::ACCESS_DENIED: return "ACCESS_DENIED";
            case AuditEventType::IP_BLOCKED: return "IP_BLOCKED";
            case AuditEventType::RATE_LIMITED: return "RATE_LIMITED";
            case AuditEventType::ORDER_SUBMITTED: return "ORDER_SUBMITTED";
            case AuditEventType::ORDER_FILLED: return "ORDER_FILLED";
            case AuditEventType::ORDER_CANCELLED: return "ORDER_CANCELLED";
            case AuditEventType::ORDER_REJECTED: return "ORDER_REJECTED";
            case AuditEventType::POSITION_OPENED: return "POSITION_OPENED";
            case AuditEventType::POSITION_CLOSED: return "POSITION_CLOSED";
            case AuditEventType::ACCOUNT_CREATED: return "ACCOUNT_CREATED";
            case AuditEventType::ACCOUNT_MODIFIED: return "ACCOUNT_MODIFIED";
            case AuditEventType::ACCOUNT_DELETED: return "ACCOUNT_DELETED";
            case AuditEventType::API_KEY_CREATED: return "API_KEY_CREATED";
            case AuditEventType::API_KEY_REVOKED: return "API_KEY_REVOKED";
            case AuditEventType::DATA_EXPORTED: return "DATA_EXPORTED";
            case AuditEventType::REPORT_GENERATED: return "REPORT_GENERATED";
            case AuditEventType::SETTINGS_CHANGED: return "SETTINGS_CHANGED";
            case AuditEventType::BACKUP_CREATED: return "BACKUP_CREATED";
            case AuditEventType::SYSTEM_ERROR: return "SYSTEM_ERROR";
            case AuditEventType::SUSPICIOUS_ACTIVITY: return "SUSPICIOUS_ACTIVITY";
            case AuditEventType::BRUTE_FORCE_DETECTED: return "BRUTE_FORCE_DETECTED";
            default: return "CUSTOM";
        }
    }
};

/**
 * @brief Enhanced audit logger
 */
class AuditLogger {
public:
    struct Config {
        std::string log_path{"audit.log"};
        std::string db_path{"audit.db"};
        bool log_to_file{true};
        bool log_to_db{true};
        bool log_to_console{false};
        int retention_days{365};
        size_t max_file_size_mb{100};
        bool rotate_files{true};
    };
    
    explicit AuditLogger(const Config& config) : config_(config) {
        if (config_.log_to_file) {
            log_file_.open(config_.log_path, std::ios::app);
        }
    }
    
    ~AuditLogger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    /**
     * @brief Log audit event
     */
    void log(const AuditEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        AuditEvent e = event;
        e.event_id = generate_event_id();
        e.timestamp = std::chrono::system_clock::now();
        
        // Store in memory buffer
        events_.push_back(e);
        if (events_.size() > 10000) {
            events_.erase(events_.begin());
        }
        
        std::string json = e.to_json();
        
        // Write to file
        if (config_.log_to_file && log_file_.is_open()) {
            log_file_ << json << std::endl;
            log_file_.flush();
            
            // Check rotation
            if (config_.rotate_files) {
                check_rotation();
            }
        }
        
        // Output to console via logger
        if (config_.log_to_console) {
            ::genie::logger().log(::genie::LogLevel::INFO, "AUDIT", json);
        }
    }
    
    /**
     * @brief Quick log helper for authentication events
     */
    void log_auth(AuditEventType type, const std::string& user_id,
                  const std::string& ip_address, bool success,
                  const std::string& error = "") {
        AuditEvent event;
        event.type = type;
        event.user_id = user_id;
        event.ip_address = ip_address;
        event.success = success;
        event.error_message = error;
        event.action = AuditEvent::event_type_to_string(type);
        log(event);
    }
    
    /**
     * @brief Quick log helper for trading events
     */
    void log_trade(AuditEventType type, const std::string& user_id,
                   const std::string& order_id, const std::string& symbol,
                   const std::map<std::string, std::string>& details) {
        AuditEvent event;
        event.type = type;
        event.user_id = user_id;
        event.resource = order_id;
        event.action = AuditEvent::event_type_to_string(type);
        event.metadata = details;
        event.metadata["symbol"] = symbol;
        event.success = true;
        log(event);
    }
    
    /**
     * @brief Quick log helper for data access
     */
    void log_data_access(const std::string& user_id, const std::string& action,
                         const std::string& resource,
                         const std::string& ip_address = "") {
        AuditEvent event;
        event.type = AuditEventType::DATA_EXPORTED;
        event.user_id = user_id;
        event.action = action;
        event.resource = resource;
        event.ip_address = ip_address;
        event.success = true;
        log(event);
    }
    
    /**
     * @brief Query audit log
     */
    std::vector<AuditEvent> query(const std::string& user_id = "",
                                   AuditEventType type = AuditEventType::CUSTOM,
                                   std::chrono::system_clock::time_point from = {},
                                   std::chrono::system_clock::time_point to = {},
                                   int limit = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<AuditEvent> results;
        
        for (auto it = events_.rbegin(); it != events_.rend() && static_cast<int>(results.size()) < limit; ++it) {
            bool match = true;
            
            if (!user_id.empty() && it->user_id != user_id) match = false;
            if (type != AuditEventType::CUSTOM && it->type != type) match = false;
            if (from != std::chrono::system_clock::time_point{} && it->timestamp < from) match = false;
            if (to != std::chrono::system_clock::time_point{} && it->timestamp > to) match = false;
            
            if (match) {
                results.push_back(*it);
            }
        }
        
        return results;
    }
    
    /**
     * @brief Get event counts by type
     */
    std::map<std::string, int> get_event_counts(std::chrono::hours period = std::chrono::hours(24)) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int> counts;
        auto cutoff = std::chrono::system_clock::now() - period;
        
        for (const auto& event : events_) {
            if (event.timestamp >= cutoff) {
                counts[AuditEvent::event_type_to_string(event.type)]++;
            }
        }
        
        return counts;
    }
    
    /**
     * @brief Get failed login attempts by IP
     */
    std::map<std::string, int> get_failed_logins_by_ip(std::chrono::hours period = std::chrono::hours(1)) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int> counts;
        auto cutoff = std::chrono::system_clock::now() - period;
        
        for (const auto& event : events_) {
            if (event.type == AuditEventType::LOGIN_FAILURE &&
                event.timestamp >= cutoff &&
                !event.ip_address.empty()) {
                counts[event.ip_address]++;
            }
        }
        
        return counts;
    }
    
    /**
     * @brief Detect brute force attempts
     */
    std::vector<std::string> detect_brute_force(int threshold = 5,
                                                 std::chrono::minutes window = std::chrono::minutes(5)) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int> counts;
        auto cutoff = std::chrono::system_clock::now() - window;
        
        for (const auto& event : events_) {
            if (event.type == AuditEventType::LOGIN_FAILURE &&
                event.timestamp >= cutoff &&
                !event.ip_address.empty()) {
                counts[event.ip_address]++;
            }
        }
        
        std::vector<std::string> suspicious;
        for (const auto& [ip, count] : counts) {
            if (count >= threshold) {
                suspicious.push_back(ip);
            }
        }
        
        return suspicious;
    }

private:
    Config config_;
    std::vector<AuditEvent> events_;
    std::ofstream log_file_;
    mutable std::mutex mutex_;
    
    static std::string generate_event_id() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        
        std::ostringstream ss;
        ss << std::hex << now << "-" << counter++;
        return ss.str();
    }
    
    void check_rotation() {
        if (!log_file_.is_open()) return;
        
        // Check file size
        log_file_.seekp(0, std::ios::end);
        auto size = log_file_.tellp();
        
        if (size > static_cast<std::streamoff>(config_.max_file_size_mb * 1024 * 1024)) {
            log_file_.close();
            
            // Rename current file with timestamp
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            
            std::ostringstream new_name;
            new_name << config_.log_path << "."
                     << std::put_time(std::localtime(&time_t), "%Y%m%d%H%M%S");
            
            std::rename(config_.log_path.c_str(), new_name.str().c_str());
            
            // Open new file
            log_file_.open(config_.log_path, std::ios::app);
        }
    }
};

} // namespace security
} // namespace genie

#endif // GENIE_SECURITY_IP_WHITELIST_HPP
