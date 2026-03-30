/**
 * @file database.hpp
 * @brief SQLite database abstraction layer for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * RAII wrapper around sqlite3 C API providing:
 *   - Connection: auto-close, WAL mode, busy timeout, foreign keys
 *   - Statement:  prepare, bind, step, reset, column accessors
 *   - Transaction: auto-rollback on scope exit unless committed
 *   - Schema:     version-tracked migrations with auto-upgrade
 *
 * Link with -lsqlite3
 * Cross-platform: Windows (MSVC/MinGW), Linux (GCC/Clang), macOS (Clang)
 */
#pragma once
#ifndef GENIE_CORE_DATABASE_HPP
#define GENIE_CORE_DATABASE_HPP

#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <memory>
#include <functional>
#include <optional>
#include <sstream>
#include <mutex>

namespace genie::db {

// =========================================================================
// Exceptions
// =========================================================================

class DatabaseError : public std::runtime_error {
public:
    int code;
    DatabaseError(int code, const std::string& msg)
        : std::runtime_error("SQLite error " + std::to_string(code) + ": " + msg)
        , code(code) {}
};

// =========================================================================
// Statement - Prepared statement with RAII
// =========================================================================

class Statement {
    sqlite3_stmt* stmt_{nullptr};
    sqlite3* db_{nullptr};

public:
    Statement() = default;
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr);
        if (rc != SQLITE_OK)
            throw DatabaseError(rc, sqlite3_errmsg(db));
    }

    ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }

    // Move only
    Statement(Statement&& o) noexcept : stmt_(o.stmt_), db_(o.db_) { o.stmt_ = nullptr; }
    Statement& operator=(Statement&& o) noexcept {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = o.stmt_; db_ = o.db_; o.stmt_ = nullptr;
        return *this;
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // ---- Bind ----

    Statement& bind(int idx, int value) {
        sqlite3_bind_int(stmt_, idx, value); return *this;
    }
    Statement& bind(int idx, int64_t value) {
        sqlite3_bind_int64(stmt_, idx, value); return *this;
    }
    Statement& bind(int idx, double value) {
        sqlite3_bind_double(stmt_, idx, value); return *this;
    }
    Statement& bind(int idx, const std::string& value) {
        sqlite3_bind_text(stmt_, idx, value.c_str(), -1, SQLITE_TRANSIENT); return *this;
    }
    Statement& bind(int idx, std::nullptr_t) {
        sqlite3_bind_null(stmt_, idx); return *this;
    }

    // Named parameter binding
    Statement& bind(const std::string& name, int value) {
        return bind(sqlite3_bind_parameter_index(stmt_, name.c_str()), value);
    }
    Statement& bind(const std::string& name, int64_t value) {
        return bind(sqlite3_bind_parameter_index(stmt_, name.c_str()), value);
    }
    Statement& bind(const std::string& name, double value) {
        return bind(sqlite3_bind_parameter_index(stmt_, name.c_str()), value);
    }
    Statement& bind(const std::string& name, const std::string& value) {
        return bind(sqlite3_bind_parameter_index(stmt_, name.c_str()), value);
    }

    // ---- Execute ----

    /** Step one row. Returns true if a row is available (SQLITE_ROW). */
    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw DatabaseError(rc, sqlite3_errmsg(db_));
    }

    /** Execute (step until done), return rows affected. */
    int execute() {
        while (step()) {}
        return sqlite3_changes(db_);
    }

    /** Reset for re-use with new bindings. */
    Statement& reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
        return *this;
    }

    // ---- Column accessors ----

    [[nodiscard]] int column_int(int col) const { return sqlite3_column_int(stmt_, col); }
    [[nodiscard]] int64_t column_int64(int col) const { return sqlite3_column_int64(stmt_, col); }
    [[nodiscard]] double column_double(int col) const { return sqlite3_column_double(stmt_, col); }

    [[nodiscard]] std::string column_text(int col) const {
        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, col));
        return t ? std::string(t) : std::string();
    }

    [[nodiscard]] bool column_is_null(int col) const {
        return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
    }

    [[nodiscard]] int column_count() const { return sqlite3_column_count(stmt_); }

    [[nodiscard]] std::string column_name(int col) const {
        return sqlite3_column_name(stmt_, col);
    }

    /** Read current row as string map. */
    [[nodiscard]] std::map<std::string, std::string> row_map() const {
        std::map<std::string, std::string> row;
        int n = column_count();
        for (int i = 0; i < n; ++i) {
            row[column_name(i)] = column_is_null(i) ? "" : column_text(i);
        }
        return row;
    }

    [[nodiscard]] sqlite3_stmt* raw() const { return stmt_; }
};

// =========================================================================
// Connection - Database handle with RAII
// =========================================================================

class Connection {
    sqlite3* db_{nullptr};
    mutable std::recursive_mutex mutex_;

public:
    /** Database connection configuration */
    struct Config {
        bool wal_mode{true};
        int busy_timeout_ms{5000};
    };

    explicit Connection(const std::string& path = ":memory:",
                        bool wal_mode = true,
                        int busy_timeout_ms = 5000) {
        int rc = sqlite3_open(path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::string err = db_ ? sqlite3_errmsg(db_) : "Cannot open database";
            if (db_) sqlite3_close(db_);
            throw DatabaseError(rc, err);
        }
        // Configure for production use
        if (wal_mode) {
            exec("PRAGMA journal_mode=WAL");
            exec("PRAGMA synchronous=NORMAL");
        }
        exec("PRAGMA foreign_keys=ON");
        exec("PRAGMA busy_timeout=" + std::to_string(busy_timeout_ms));
        exec("PRAGMA cache_size=-8000"); // 8MB cache
        // Log open (uses genie::logger if available via genie.hpp include order)
    }

    ~Connection() { if (db_) sqlite3_close(db_); }

    // Move only
    Connection(Connection&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
    Connection& operator=(Connection&& o) noexcept {
        if (db_) sqlite3_close(db_);
        db_ = o.db_; o.db_ = nullptr;
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // ---- Execution ----

    /** Execute raw SQL (no results). */
    void exec(const std::string& sql) {
        std::lock_guard lock(mutex_);
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "SQL error";
            sqlite3_free(err);
            throw DatabaseError(rc, msg + " [SQL: " + sql.substr(0, 100) + "]");
        }
    }

    /** Prepare a statement. */
    Statement prepare(const std::string& sql) {
        std::lock_guard lock(mutex_);
        return Statement(db_, sql);
    }

    /** Execute and return all rows as string maps. */
    std::vector<std::map<std::string, std::string>> query(const std::string& sql) {
        std::lock_guard lock(mutex_);
        auto stmt = prepare(sql);
        std::vector<std::map<std::string, std::string>> rows;
        while (stmt.step()) rows.push_back(stmt.row_map());
        return rows;
    }

    /** Execute and return single value. */
    template<typename T>
    std::optional<T> scalar(const std::string& sql);

    /** Last insert rowid. */
    [[nodiscard]] int64_t last_insert_rowid() const {
        return sqlite3_last_insert_rowid(db_);
    }

    /** Total rows changed by last statement. */
    [[nodiscard]] int changes() const {
        return sqlite3_changes(db_);
    }

    /** Check if a table exists. */
    [[nodiscard]] bool table_exists(const std::string& name) {
        auto stmt = prepare("SELECT count(*) FROM sqlite_master WHERE type='table' AND name=?");
        stmt.bind(1, name);
        stmt.step();
        return stmt.column_int(0) > 0;
    }

    [[nodiscard]] sqlite3* raw() const { return db_; }
    [[nodiscard]] std::recursive_mutex& mutex() { return mutex_; }
};

// Template specializations for scalar()
template<> inline std::optional<int> Connection::scalar(const std::string& sql) {
    auto stmt = prepare(sql); if (!stmt.step()) return std::nullopt;
    return stmt.column_int(0);
}
template<> inline std::optional<int64_t> Connection::scalar(const std::string& sql) {
    auto stmt = prepare(sql); if (!stmt.step()) return std::nullopt;
    return stmt.column_int64(0);
}
template<> inline std::optional<double> Connection::scalar(const std::string& sql) {
    auto stmt = prepare(sql); if (!stmt.step()) return std::nullopt;
    return stmt.column_double(0);
}
template<> inline std::optional<std::string> Connection::scalar(const std::string& sql) {
    auto stmt = prepare(sql); if (!stmt.step()) return std::nullopt;
    return stmt.column_text(0);
}

// =========================================================================
// Transaction - Auto-rollback RAII guard
// =========================================================================

class Transaction {
    Connection& conn_;
    bool committed_{false};

public:
    explicit Transaction(Connection& conn) : conn_(conn) {
        conn_.exec("BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (!committed_) {
            try { conn_.exec("ROLLBACK"); } catch (...) {}
        }
    }

    void commit() {
        conn_.exec("COMMIT");
        committed_ = true;
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
};

// =========================================================================
// Schema Manager - Version-tracked migrations
// =========================================================================

class SchemaManager {
    Connection& conn_;

public:
    explicit SchemaManager(Connection& conn) : conn_(conn) {}

    /** Get current schema version. */
    [[nodiscard]] int version() {
        if (!conn_.table_exists("schema_version")) return 0;
        auto v = conn_.scalar<int>("SELECT version FROM schema_version ORDER BY version DESC LIMIT 1");
        return v.value_or(0);
    }

    /** Apply migrations up to target version. */
    void migrate(int target_version, const std::vector<std::string>& migrations) {
        int current = version();
        if (current >= target_version) return;

        // Create version tracking table if needed
        if (current == 0) {
            conn_.exec(
                "CREATE TABLE IF NOT EXISTS schema_version ("
                "  version INTEGER PRIMARY KEY,"
                "  applied_at TEXT NOT NULL DEFAULT (datetime('now')),"
                "  description TEXT"
                ")");
        }

        Transaction txn(conn_);
        for (int v = current; v < target_version && v < static_cast<int>(migrations.size()); ++v) {
            conn_.exec(migrations[v]);
            auto stmt = conn_.prepare(
                "INSERT INTO schema_version (version, description) VALUES (?, ?)");
            stmt.bind(1, v + 1);
            stmt.bind(2, std::string("Migration v") + std::to_string(v + 1));
            stmt.execute();
        }
        txn.commit();
    }
};

} // namespace genie::db

#endif // GENIE_CORE_DATABASE_HPP
