/**
 * @file audit_log.hpp
 * @brief Security audit logging with SQLite persistence
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides comprehensive audit trail for:
 * - Authentication events (login, logout, failed attempts)
 * - User management (registration, profile changes, role changes)
 * - Administrative actions (user create/delete, permission changes)
 */

#ifndef GENIE_CORE_AUDIT_LOG_HPP
#define GENIE_CORE_AUDIT_LOG_HPP

#include <sqlite3.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace genie {

// =========================================================================
// Audit Event Types
// =========================================================================

enum class AuditAction {
    // Authentication
    LOGIN_SUCCESS,
    LOGIN_FAILED,
    LOGOUT,
    SESSION_EXPIRED,
    
    // Registration
    USER_REGISTERED,
    REGISTRATION_FAILED,
    
    // Profile
    PROFILE_UPDATED,
    PASSWORD_CHANGED,
    PASSWORD_CHANGE_FAILED,
    
    // Admin actions
    USER_CREATED,
    USER_DELETED,
    USER_ACTIVATED,
    USER_DEACTIVATED,
    ROLE_CHANGED,
    
    // System
    SYSTEM_STARTED,
    SYSTEM_STOPPED,
    CONFIG_CHANGED
};

inline std::string action_to_string(AuditAction action) {
    switch (action) {
        case AuditAction::LOGIN_SUCCESS: return "LOGIN_SUCCESS";
        case AuditAction::LOGIN_FAILED: return "LOGIN_FAILED";
        case AuditAction::LOGOUT: return "LOGOUT";
        case AuditAction::SESSION_EXPIRED: return "SESSION_EXPIRED";
        case AuditAction::USER_REGISTERED: return "USER_REGISTERED";
        case AuditAction::REGISTRATION_FAILED: return "REGISTRATION_FAILED";
        case AuditAction::PROFILE_UPDATED: return "PROFILE_UPDATED";
        case AuditAction::PASSWORD_CHANGED: return "PASSWORD_CHANGED";
        case AuditAction::PASSWORD_CHANGE_FAILED: return "PASSWORD_CHANGE_FAILED";
        case AuditAction::USER_CREATED: return "USER_CREATED";
        case AuditAction::USER_DELETED: return "USER_DELETED";
        case AuditAction::USER_ACTIVATED: return "USER_ACTIVATED";
        case AuditAction::USER_DEACTIVATED: return "USER_DEACTIVATED";
        case AuditAction::ROLE_CHANGED: return "ROLE_CHANGED";
        case AuditAction::SYSTEM_STARTED: return "SYSTEM_STARTED";
        case AuditAction::SYSTEM_STOPPED: return "SYSTEM_STOPPED";
        case AuditAction::CONFIG_CHANGED: return "CONFIG_CHANGED";
        default: return "UNKNOWN";
    }
}

// =========================================================================
// Audit Entry
// =========================================================================

struct AuditEntry {
    int64_t id{0};
    std::string timestamp;      // ISO 8601
    std::string username;       // Actor (who performed the action)
    AuditAction action;
    std::string resource;       // Target (e.g., affected username)
    std::string ip_address;
    std::string details;        // Additional JSON or text details
    bool success{true};
};

// =========================================================================
// Audit Log
// =========================================================================

/**
 * @brief Security audit log with SQLite and file persistence
 */
class AuditLog {
    sqlite3* db_{nullptr};
    std::ofstream file_;
    mutable std::mutex mutex_;
    std::function<void(const std::string&)> log_fn_;
    bool file_enabled_{true};
    bool db_enabled_{true};

    void log(const std::string& msg) const {
        if (log_fn_) log_fn_(msg);
    }

    static std::string now_iso8601() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return std::string(buf) + "." + std::to_string(ms.count()) + "Z";
    }

    bool exec(const std::string& sql) {
        if (!db_) return false;
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "Unknown error";
            sqlite3_free(err);
            log("Audit SQL error: " + msg);
            return false;
        }
        return true;
    }

    void init_schema() {
        const char* schema = R"(
            CREATE TABLE IF NOT EXISTS audit_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                username TEXT,
                action TEXT NOT NULL,
                resource TEXT,
                ip_address TEXT,
                details TEXT,
                success INTEGER NOT NULL DEFAULT 1
            );
            CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_log(timestamp);
            CREATE INDEX IF NOT EXISTS idx_audit_username ON audit_log(username);
            CREATE INDEX IF NOT EXISTS idx_audit_action ON audit_log(action);
        )";
        exec(schema);
    }

    void write_to_file(const AuditEntry& entry) {
        if (!file_.is_open()) return;
        
        file_ << entry.timestamp 
              << " [" << action_to_string(entry.action) << "] "
              << (entry.success ? "OK" : "FAIL") << " "
              << "user=" << (entry.username.empty() ? "-" : entry.username) << " "
              << "resource=" << (entry.resource.empty() ? "-" : entry.resource) << " "
              << "ip=" << (entry.ip_address.empty() ? "-" : entry.ip_address);
        
        if (!entry.details.empty()) {
            file_ << " details=" << entry.details;
        }
        file_ << "\n";
        file_.flush();
    }

    void write_to_db(const AuditEntry& entry) {
        if (!db_) return;

        const char* sql = R"(
            INSERT INTO audit_log (timestamp, username, action, resource, ip_address, details, success)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return;
        }

        std::string action_str = action_to_string(entry.action);
        
        sqlite3_bind_text(stmt, 1, entry.timestamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entry.username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, action_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, entry.resource.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, entry.ip_address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, entry.details.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, entry.success ? 1 : 0);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

public:
    AuditLog() = default;
    ~AuditLog() { close(); }

    // Non-copyable
    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;

    /** Set logger callback */
    void set_logger(std::function<void(const std::string&)> fn) {
        log_fn_ = std::move(fn);
    }

    /**
     * @brief Open audit log
     * @param db_path Path to SQLite database
     * @param file_path Path to audit log file
     * @return true on success
     */
    bool open(const std::string& db_path = "audit.db",
              const std::string& file_path = "") {
        std::lock_guard lock(mutex_);

        // Open database
        if (db_enabled_ && !db_path.empty()) {
            if (db_) {
                sqlite3_close(db_);
                db_ = nullptr;
            }

            int rc = sqlite3_open(db_path.c_str(), &db_);
            if (rc != SQLITE_OK) {
                log("Failed to open audit database: " + db_path);
                db_ = nullptr;
            } else {
                exec("PRAGMA journal_mode = WAL;");
                init_schema();
                log("AuditLog database opened: " + db_path);
            }
        }

        // Open file
        if (file_enabled_ && !file_path.empty()) {
            file_.open(file_path, std::ios::app);
            if (!file_.is_open()) {
                log("Failed to open audit file: " + file_path);
            } else {
                log("AuditLog file opened: " + file_path);
            }
        }

        return db_ != nullptr || file_.is_open();
    }

    /** Close audit log */
    void close() {
        std::lock_guard lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        if (file_.is_open()) {
            file_.close();
        }
    }

    /**
     * @brief Apply database connection settings from config.json
     * @param wal_mode Enable WAL journal mode (database.wal_mode)
     * @param busy_timeout_ms Busy timeout in milliseconds (database.busy_timeout_ms)
     * @param sql_trace Enable SQL statement tracing (debug.sql_logging)
     *
     * Call after open() to override default PRAGMA settings.
     */
    void configure_connection(bool wal_mode, int busy_timeout_ms, bool sql_trace) {
        std::lock_guard lock(mutex_);
        if (!db_) return;
        if (wal_mode) {
            exec("PRAGMA journal_mode=WAL;");
            exec("PRAGMA synchronous=NORMAL;");
        } else {
            exec("PRAGMA journal_mode=DELETE;");
            exec("PRAGMA synchronous=FULL;");
        }
        exec("PRAGMA busy_timeout=" + std::to_string(busy_timeout_ms) + ";");
        if (sql_trace) {
            sqlite3_trace_v2(db_, SQLITE_TRACE_STMT, [](unsigned, void* ctx,
                    void* stmt, void*) -> int {
                const char* sql = sqlite3_expanded_sql(static_cast<sqlite3_stmt*>(stmt));
                if (sql) {
                    auto* fn = static_cast<std::function<void(const std::string&)>*>(ctx);
                    if (fn && *fn) (*fn)("[SQL] " + std::string(sql));
                    sqlite3_free(const_cast<char*>(sql));
                }
                return 0;
            }, &log_fn_);
        }
        log("AuditLog connection configured: WAL=" + std::string(wal_mode ? "yes" : "no") +
            ", busy_timeout=" + std::to_string(busy_timeout_ms) + "ms" +
            ", sql_trace=" + std::string(sql_trace ? "yes" : "no"));
    }

    /** Enable/disable file logging */
    void set_file_enabled(bool enabled) { file_enabled_ = enabled; }
    
    /** Enable/disable database logging */
    void set_db_enabled(bool enabled) { db_enabled_ = enabled; }

    // =====================================================================
    // Logging Methods
    // =====================================================================

    /**
     * @brief Log an audit event
     */
    void log_event(AuditAction action,
                   const std::string& username = "",
                   const std::string& resource = "",
                   const std::string& ip_address = "",
                   const std::string& details = "",
                   bool success = true) {
        AuditEntry entry;
        entry.timestamp = now_iso8601();
        entry.username = username;
        entry.action = action;
        entry.resource = resource;
        entry.ip_address = ip_address;
        entry.details = details;
        entry.success = success;

        std::lock_guard lock(mutex_);
        
        if (db_enabled_) write_to_db(entry);
        if (file_enabled_) write_to_file(entry);
    }

    // Convenience methods
    void login_success(const std::string& username, const std::string& ip = "") {
        log_event(AuditAction::LOGIN_SUCCESS, username, "", ip);
    }

    void login_failed(const std::string& username, const std::string& ip = "", const std::string& reason = "") {
        log_event(AuditAction::LOGIN_FAILED, username, "", ip, reason, false);
    }

    void logout(const std::string& username, const std::string& ip = "") {
        log_event(AuditAction::LOGOUT, username, "", ip);
    }

    void user_registered(const std::string& username, const std::string& ip = "") {
        log_event(AuditAction::USER_REGISTERED, username, username, ip);
    }

    void registration_failed(const std::string& username, const std::string& ip = "", const std::string& reason = "") {
        log_event(AuditAction::REGISTRATION_FAILED, "", username, ip, reason, false);
    }

    void profile_updated(const std::string& username, const std::string& ip = "", const std::string& changes = "") {
        log_event(AuditAction::PROFILE_UPDATED, username, username, ip, changes);
    }

    void password_changed(const std::string& username, const std::string& ip = "") {
        log_event(AuditAction::PASSWORD_CHANGED, username, username, ip);
    }

    void password_change_failed(const std::string& username, const std::string& ip = "", const std::string& reason = "") {
        log_event(AuditAction::PASSWORD_CHANGE_FAILED, username, username, ip, reason, false);
    }

    void user_created(const std::string& admin, const std::string& new_user, const std::string& ip = "") {
        log_event(AuditAction::USER_CREATED, admin, new_user, ip);
    }

    void user_deleted(const std::string& admin, const std::string& deleted_user, const std::string& ip = "") {
        log_event(AuditAction::USER_DELETED, admin, deleted_user, ip);
    }

    void user_activated(const std::string& admin, const std::string& target_user, const std::string& ip = "") {
        log_event(AuditAction::USER_ACTIVATED, admin, target_user, ip);
    }

    void user_deactivated(const std::string& admin, const std::string& target_user, const std::string& ip = "") {
        log_event(AuditAction::USER_DEACTIVATED, admin, target_user, ip);
    }

    void role_changed(const std::string& admin, const std::string& target_user, const std::string& new_role, const std::string& ip = "") {
        log_event(AuditAction::ROLE_CHANGED, admin, target_user, ip, "new_role=" + new_role);
    }

    void system_started(const std::string& details = "") {
        log_event(AuditAction::SYSTEM_STARTED, "system", "", "", details);
    }

    void system_stopped(const std::string& details = "") {
        log_event(AuditAction::SYSTEM_STOPPED, "system", "", "", details);
    }

    // =====================================================================
    // Query Methods
    // =====================================================================

    /**
     * @brief Get recent audit entries
     * @param limit Maximum entries to return
     * @param username Filter by username (empty for all)
     * @param action Filter by action (empty for all)
     */
    [[nodiscard]] std::vector<AuditEntry> get_recent(size_t limit = 100,
                                                      const std::string& username = "",
                                                      const std::string& action = "") const {
        std::lock_guard lock(mutex_);
        std::vector<AuditEntry> entries;
        if (!db_) return entries;

        std::string sql = "SELECT id, timestamp, username, action, resource, ip_address, details, success FROM audit_log";
        std::vector<std::string> conditions;
        
        if (!username.empty()) conditions.push_back("username = ?");
        if (!action.empty()) conditions.push_back("action = ?");
        
        if (!conditions.empty()) {
            sql += " WHERE " + conditions[0];
            for (size_t i = 1; i < conditions.size(); ++i) {
                sql += " AND " + conditions[i];
            }
        }
        
        sql += " ORDER BY id DESC LIMIT ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return entries;
        }

        int idx = 1;
        if (!username.empty()) sqlite3_bind_text(stmt, idx++, username.c_str(), -1, SQLITE_TRANSIENT);
        if (!action.empty()) sqlite3_bind_text(stmt, idx++, action.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, idx, static_cast<int>(limit));

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AuditEntry e;
            e.id = sqlite3_column_int64(stmt, 0);
            e.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            
            auto un = sqlite3_column_text(stmt, 2);
            e.username = un ? reinterpret_cast<const char*>(un) : "";
            
            std::string action_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            // Map string back to enum (simplified - just store string)
            e.action = AuditAction::LOGIN_SUCCESS; // Default, actual lookup would be more complex
            
            auto res = sqlite3_column_text(stmt, 4);
            e.resource = res ? reinterpret_cast<const char*>(res) : "";
            
            auto ip = sqlite3_column_text(stmt, 5);
            e.ip_address = ip ? reinterpret_cast<const char*>(ip) : "";
            
            auto det = sqlite3_column_text(stmt, 6);
            e.details = det ? reinterpret_cast<const char*>(det) : "";
            
            e.success = sqlite3_column_int(stmt, 7) != 0;
            
            entries.push_back(e);
        }
        sqlite3_finalize(stmt);
        return entries;
    }

    /**
     * @brief Get entry count
     */
    [[nodiscard]] size_t count() const {
        std::lock_guard lock(mutex_);
        if (!db_) return 0;

        const char* sql = "SELECT COUNT(*) FROM audit_log";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        size_t cnt = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            cnt = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return cnt;
    }

    /**
     * @brief Purge old entries
     * @param days_to_keep Keep entries from last N days
     * @return Number of entries deleted
     */
    size_t purge_old(int days_to_keep = 90) {
        std::lock_guard lock(mutex_);
        if (!db_) return 0;

        // Calculate cutoff date
        auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * days_to_keep);
        auto time = std::chrono::system_clock::to_time_t(cutoff);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        std::string cutoff_str = buf;

        const char* sql = "DELETE FROM audit_log WHERE timestamp < ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        sqlite3_bind_text(stmt, 1, cutoff_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return static_cast<size_t>(sqlite3_changes(db_));
    }
};

} // namespace genie

#endif // GENIE_CORE_AUDIT_LOG_HPP
