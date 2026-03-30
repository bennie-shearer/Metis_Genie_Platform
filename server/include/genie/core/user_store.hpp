/**
 * @file user_store.hpp
 * @brief Persistent user storage with SQLite and password hashing
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides secure user management with:
 * - SHA-256 password hashing with salt
 * - SQLite persistent storage
 * - Thread-safe operations
 */

#ifndef GENIE_CORE_USER_STORE_HPP
#define GENIE_CORE_USER_STORE_HPP

#include "crypto.hpp"
#include <sqlite3.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <functional>

namespace genie {

// =========================================================================
// User Data Structures
// =========================================================================

/** User profile data */
struct User {
    int64_t id{0};
    std::string username;
    std::string password_hash;
    std::string salt;
    std::string role;           // Administrator, Trader, Analyst, Viewer
    std::string display_name;
    std::string email;
    bool active{true};
    std::string created_at;     // ISO 8601 format
    std::string last_login;     // ISO 8601 format
};

// =========================================================================
// Persistent User Store
// =========================================================================

/**
 * @brief SQLite-backed user store with password hashing
 */
class UserStore {
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
    std::function<void(const std::string&)> log_fn_;

    void log(const std::string& msg) const {
        if (log_fn_) log_fn_(msg);
    }

    static std::string now_iso8601() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }

    bool exec(const std::string& sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "Unknown error";
            sqlite3_free(err);
            log("SQL error: " + msg);
            return false;
        }
        return true;
    }

    void init_schema() {
        const char* schema = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT UNIQUE NOT NULL,
                password_hash TEXT NOT NULL,
                salt TEXT NOT NULL,
                role TEXT NOT NULL DEFAULT 'Viewer',
                display_name TEXT,
                email TEXT,
                active INTEGER NOT NULL DEFAULT 1,
                created_at TEXT NOT NULL,
                last_login TEXT
            );
            CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
            CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
        )";
        exec(schema);
    }

    void seed_defaults() {
        // Check if admin exists (use internal - mutex already held by open())
        auto admin = get_by_username_internal("admin");
        if (!admin) {
            // Seed default users directly (avoid register_user which would deadlock)
            seed_user("admin", "demo", "Administrator", "System Admin");
            seed_user("trader", "trade", "Trader", "Demo Trader");
            seed_user("user", "user", "Viewer", "Demo User");
            log("Seeded default users");
        }
    }
    
    // Direct user insert (called from seed_defaults, mutex already held)
    void seed_user(const std::string& username, const std::string& password,
                   const std::string& role, const std::string& display_name) {
        auto [salt, hash] = crypto::create_password_hash(password);
        std::string now = now_iso8601();

        const char* sql = R"(
            INSERT INTO users (username, password_hash, salt, role, display_name, email, active, created_at, last_login)
            VALUES (?, ?, ?, ?, ?, '', 1, ?, ?)
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

public:
    UserStore() = default;
    ~UserStore() { close(); }

    // Non-copyable
    UserStore(const UserStore&) = delete;
    UserStore& operator=(const UserStore&) = delete;

    /** Set logger callback */
    void set_logger(std::function<void(const std::string&)> fn) {
        log_fn_ = std::move(fn);
    }

    /**
     * @brief Open or create user database
     * @param db_path Path to SQLite database file
     * @return true on success
     */
    bool open(const std::string& db_path = "users.db") {
        std::lock_guard lock(mutex_);
        
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }

        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            log("Failed to open database: " + db_path);
            return false;
        }

        // Enable foreign keys and WAL mode
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
        
        init_schema();
        seed_defaults();
        
        log("UserStore opened: " + db_path);
        return true;
    }

    /** Close database connection */
    void close() {
        std::lock_guard lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
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
        log("Connection configured: WAL=" + std::string(wal_mode ? "yes" : "no") +
            ", busy_timeout=" + std::to_string(busy_timeout_ms) + "ms" +
            ", sql_trace=" + std::string(sql_trace ? "yes" : "no"));
    }

    /** Check if database is open */
    [[nodiscard]] bool is_open() const {
        std::lock_guard lock(mutex_);
        return db_ != nullptr;
    }

    // =====================================================================
    // Validation
    // =====================================================================

    static bool is_valid_username(const std::string& name) {
        if (name.length() < 3 || name.length() > 32) return false;
        for (char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') 
                return false;
        }
        return true;
    }

    static bool is_valid_password(const std::string& pass) {
        return pass.length() >= 4;
    }

    static bool is_valid_email(const std::string& email) {
        if (email.empty()) return true;
        auto at = email.find('@');
        auto dot = email.rfind('.');
        return at != std::string::npos && dot != std::string::npos && at < dot && at > 0;
    }

    static bool is_valid_role(const std::string& role) {
        static const std::set<std::string> valid = {
            "Administrator", "Trader", "Analyst", "Viewer"
        };
        return valid.count(role) > 0;
    }

    // =====================================================================
    // User Operations
    // =====================================================================

    /**
     * @brief Register a new user
     * @return Error message or empty string on success
     */
    [[nodiscard]] std::string register_user(const std::string& username,
                                            const std::string& password,
                                            const std::string& role = "Viewer",
                                            const std::string& email = "",
                                            const std::string& display_name = "") {
        if (!is_valid_username(username)) {
            return "Username must be 3-32 alphanumeric characters";
        }
        if (!is_valid_password(password)) {
            return "Password must be at least 4 characters";
        }
        if (!is_valid_email(email)) {
            return "Invalid email format";
        }
        if (!is_valid_role(role)) {
            return "Invalid role";
        }

        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        // Check if username exists
        if (get_by_username_internal(username)) {
            return "Username already exists";
        }

        // Hash password
        auto [salt, hash] = crypto::create_password_hash(password);
        std::string now = now_iso8601();
        std::string dname = display_name.empty() ? username : display_name;

        const char* sql = R"(
            INSERT INTO users (username, password_hash, salt, role, display_name, email, active, created_at, last_login)
            VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?)
        )";

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return "Database error";

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, dname.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, now.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            return "Failed to create user";
        }

        log("User registered: " + username);
        return "";
    }

    /**
     * @brief Authenticate user
     * @return Role on success, nullopt on failure
     */
    [[nodiscard]] std::optional<std::string> authenticate(const std::string& username,
                                                          const std::string& password) {
        std::lock_guard lock(mutex_);
        if (!db_) return std::nullopt;

        auto user = get_by_username_internal(username);
        if (!user || !user->active) {
            return std::nullopt;
        }

        if (!crypto::verify_password(password, user->salt, user->password_hash)) {
            return std::nullopt;
        }

        // Update last login
        update_last_login_internal(username);
        
        log("User authenticated: " + username);
        return user->role;
    }

    /**
     * @brief Get user by username
     */
    [[nodiscard]] std::optional<User> get_by_username(const std::string& username) const {
        std::lock_guard lock(mutex_);
        return get_by_username_internal(username);
    }

    /**
     * @brief Get all users
     */
    [[nodiscard]] std::vector<User> get_all_users() const {
        std::lock_guard lock(mutex_);
        std::vector<User> users;
        if (!db_) return users;

        const char* sql = "SELECT id, username, password_hash, salt, role, display_name, email, active, created_at, last_login FROM users ORDER BY username";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return users;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            users.push_back(row_to_user(stmt));
        }
        sqlite3_finalize(stmt);
        return users;
    }

    /**
     * @brief Update user profile
     * @return Error message or empty on success
     */
    [[nodiscard]] std::string update_profile(const std::string& username,
                                             const std::string& display_name = "",
                                             const std::string& email = "") {
        if (!display_name.empty() && display_name.length() > 64) {
            return "Display name too long";
        }
        if (!email.empty() && !is_valid_email(email)) {
            return "Invalid email format";
        }

        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        std::string sql = "UPDATE users SET ";
        std::vector<std::string> updates;
        
        if (!display_name.empty()) updates.push_back("display_name = ?");
        if (!email.empty()) updates.push_back("email = ?");
        
        if (updates.empty()) return "";

        sql += updates[0];
        for (size_t i = 1; i < updates.size(); ++i) {
            sql += ", " + updates[i];
        }
        sql += " WHERE username = ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return "Database error";
        }

        int idx = 1;
        if (!display_name.empty()) sqlite3_bind_text(stmt, idx++, display_name.c_str(), -1, SQLITE_TRANSIENT);
        if (!email.empty()) sqlite3_bind_text(stmt, idx++, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, idx, username.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return (rc == SQLITE_DONE) ? "" : "Failed to update profile";
    }

    /**
     * @brief Change user password
     * @return Error message or empty on success
     */
    [[nodiscard]] std::string change_password(const std::string& username,
                                              const std::string& old_password,
                                              const std::string& new_password) {
        if (!is_valid_password(new_password)) {
            return "New password must be at least 4 characters";
        }

        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        auto user = get_by_username_internal(username);
        if (!user) return "User not found";

        if (!crypto::verify_password(old_password, user->salt, user->password_hash)) {
            return "Current password incorrect";
        }

        auto [salt, hash] = crypto::create_password_hash(new_password);

        const char* sql = "UPDATE users SET password_hash = ?, salt = ? WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return "Database error";
        }

        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, username.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            log("Password changed: " + username);
            return "";
        }
        return "Failed to change password";
    }

    /**
     * @brief Set user role (admin operation)
     */
    [[nodiscard]] std::string set_role(const std::string& username, const std::string& role) {
        if (!is_valid_role(role)) {
            return "Invalid role";
        }

        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        const char* sql = "UPDATE users SET role = ? WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return "Database error";
        }

        sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            log("Role changed: " + username + " -> " + role);
            return "";
        }
        return "Failed to set role";
    }

    /**
     * @brief Set user active status
     */
    [[nodiscard]] std::string set_active(const std::string& username, bool active) {
        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        const char* sql = "UPDATE users SET active = ? WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return "Database error";
        }

        sqlite3_bind_int(stmt, 1, active ? 1 : 0);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            log("User " + std::string(active ? "activated" : "deactivated") + ": " + username);
            return "";
        }
        return "Failed to update status";
    }

    /**
     * @brief Delete user
     */
    [[nodiscard]] std::string delete_user(const std::string& username) {
        std::lock_guard lock(mutex_);
        if (!db_) return "Database not open";

        const char* sql = "DELETE FROM users WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return "Database error";
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
            log("User deleted: " + username);
            return "";
        }
        return "User not found";
    }

    /**
     * @brief Check if username exists
     */
    [[nodiscard]] bool exists(const std::string& username) const {
        std::lock_guard lock(mutex_);
        return get_by_username_internal(username).has_value();
    }

    /**
     * @brief Get user count
     */
    [[nodiscard]] size_t count() const {
        std::lock_guard lock(mutex_);
        if (!db_) return 0;

        const char* sql = "SELECT COUNT(*) FROM users";
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

private:
    std::optional<User> get_by_username_internal(const std::string& username) const {
        if (!db_) return std::nullopt;

        const char* sql = "SELECT id, username, password_hash, salt, role, display_name, email, active, created_at, last_login FROM users WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::nullopt;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<User> user;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user = row_to_user(stmt);
        }
        sqlite3_finalize(stmt);
        return user;
    }

    void update_last_login_internal(const std::string& username) {
        const char* sql = "UPDATE users SET last_login = ? WHERE username = ?";
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return;
        }

        std::string now = now_iso8601();
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    static User row_to_user(sqlite3_stmt* stmt) {
        User u;
        u.id = sqlite3_column_int64(stmt, 0);
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u.salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        u.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        
        auto dn = sqlite3_column_text(stmt, 5);
        u.display_name = dn ? reinterpret_cast<const char*>(dn) : "";
        
        auto em = sqlite3_column_text(stmt, 6);
        u.email = em ? reinterpret_cast<const char*>(em) : "";
        
        u.active = sqlite3_column_int(stmt, 7) != 0;
        
        auto ca = sqlite3_column_text(stmt, 8);
        u.created_at = ca ? reinterpret_cast<const char*>(ca) : "";
        
        auto ll = sqlite3_column_text(stmt, 9);
        u.last_login = ll ? reinterpret_cast<const char*>(ll) : "";
        
        return u;
    }
};

} // namespace genie

#endif // GENIE_CORE_USER_STORE_HPP
