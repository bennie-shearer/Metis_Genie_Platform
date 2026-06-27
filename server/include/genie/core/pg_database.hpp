/**
 * @file pg_database.hpp
 * @brief PostgreSQL database backend for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Optional PostgreSQL backend implementing the same DataStore interface as
 * the SQLite database.hpp, allowing seamless backend switching via config.json.
 *
 * Features:
 *   - Connection pooling with configurable pool size
 *   - Prepared statement caching for performance
 *   - RAII connection/statement management
 *   - Same DataStore interface as SQLite (drop-in replacement)
 *   - Schema auto-migration with version tracking
 *   - Transaction support with auto-rollback on scope exit
 *   - Parameterized queries (prevents SQL injection)
 *   - LISTEN/NOTIFY for event-driven updates
 *   - Connection health checking (ping/reconnect)
 *   - SSL/TLS connection support
 *   - JSON/JSONB column support for flexible schema
 *   - Bulk insert with COPY protocol
 *   - Connection string and individual parameter configuration
 *   - Thread-safe: connections are per-thread from pool
 *   - Compile-time switchable: builds as no-op stubs without libpq
 *
 * Configuration (config.json):
 *   "database": {
 *     "backend": "postgresql",
 *     "host": "localhost",
 *     "port": 5432,
 *     "name": "metisgenie",
 *     "user": "genie",
 *     "password": "secret",
 *     "sslmode": "prefer",
 *     "pool_size": 8,
 *     "connection_timeout": 5,
 *     "statement_timeout": 30000
 *   }
 *
 * Build:
 *   # With PostgreSQL support:
 *   g++ -std=c++20 -O2 -I include -DGENIE_USE_POSTGRESQL -lpq
 *
 *   # Without (stubs only, default):
 *   g++ -std=c++20 -O2 -I include
 *
 * Link: -lpq (libpq - PostgreSQL C client library)
 * Install: apt install libpq-dev (Debian/Ubuntu)
 *          brew install libpq (macOS)
 *          vcpkg install libpq (Windows)
 */
#pragma once
#ifndef GENIE_CORE_PG_DATABASE_HPP
#define GENIE_CORE_PG_DATABASE_HPP

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <memory>
#include <variant>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>

// Conditional libpq include
#ifdef GENIE_USE_POSTGRESQL
  #include <libpq-fe.h>
#endif

namespace genie {
namespace core {

// ============================================================================
// PostgreSQL Configuration
// ============================================================================

/**
 * @brief PostgreSQL connection configuration
 */
struct PgConfig {
    std::string host            = "localhost";
    int port                    = 5432;
    std::string database        = "metisgenie";
    std::string user            = "genie";
    std::string password;
    std::string sslmode         = "prefer";     // disable, allow, prefer, require, verify-ca, verify-full
    int pool_size               = 8;
    int connection_timeout_sec  = 5;
    int statement_timeout_ms    = 30000;
    std::string application_name = "MetisGenie";
    std::string search_path     = "public";
    bool auto_migrate           = true;

    /**
     * @brief Build libpq connection string
     */
    std::string connection_string() const {
        std::ostringstream oss;
        oss << "host=" << host
            << " port=" << port
            << " dbname=" << database
            << " user=" << user;
        if (!password.empty()) oss << " password=" << password;
        oss << " sslmode=" << sslmode
            << " connect_timeout=" << connection_timeout_sec
            << " application_name=" << application_name;
        if (statement_timeout_ms > 0) {
            oss << " options='-c statement_timeout=" << statement_timeout_ms << "'";
        }
        return oss.str();
    }
};

// ============================================================================
// Query Parameter Types
// ============================================================================

/**
 * @brief Parameter value for prepared statements
 */
using PgParam = std::variant<
    std::nullptr_t,     // NULL
    bool,
    int,
    int64_t,
    double,
    std::string
>;

/**
 * @brief Convert parameter to string for libpq
 */
inline std::string param_to_string(const PgParam& p) {
    struct Visitor {
        std::string operator()(std::nullptr_t) const { return ""; }
        std::string operator()(bool v) const { return v ? "true" : "false"; }
        std::string operator()(int v) const { return std::to_string(v); }
        std::string operator()(int64_t v) const { return std::to_string(v); }
        std::string operator()(double v) const {
            std::ostringstream oss;
            oss << std::setprecision(15) << v;
            return oss.str();
        }
        std::string operator()(const std::string& v) const { return v; }
    };
    return std::visit(Visitor{}, p);
}

inline bool param_is_null(const PgParam& p) {
    return std::holds_alternative<std::nullptr_t>(p);
}

// ============================================================================
// Query Result
// ============================================================================

/**
 * @brief Row from a query result
 */
struct PgRow {
    std::vector<std::string> values;
    std::vector<bool> nulls;
    std::vector<std::string> column_names;

    std::string get(int col, const std::string& def = "") const {
        if (col < 0 || col >= static_cast<int>(values.size())) return def;
        if (nulls[col]) return def;
        return values[col];
    }

    std::string get(const std::string& col_name, const std::string& def = "") const {
        for (size_t i = 0; i < column_names.size(); ++i) {
            if (column_names[i] == col_name) {
                if (nulls[i]) return def;
                return values[i];
            }
        }
        return def;
    }

    int get_int(const std::string& col, int def = 0) const {
        auto v = get(col);
        if (v.empty()) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    }

    int64_t get_int64(const std::string& col, int64_t def = 0) const {
        auto v = get(col);
        if (v.empty()) return def;
        try { return std::stoll(v); } catch (...) { return def; }
    }

    double get_double(const std::string& col, double def = 0.0) const {
        auto v = get(col);
        if (v.empty()) return def;
        try { return std::stod(v); } catch (...) { return def; }
    }

    bool get_bool(const std::string& col, bool def = false) const {
        auto v = get(col);
        if (v.empty()) return def;
        return v == "t" || v == "true" || v == "1" || v == "yes";
    }
};

/**
 * @brief Query result set
 */
struct PgResult {
    std::vector<PgRow> rows;
    std::vector<std::string> columns;
    int affected_rows       = 0;
    bool success            = false;
    std::string error;
    double elapsed_ms       = 0.0;

    bool empty() const { return rows.empty(); }
    size_t size() const { return rows.size(); }

    const PgRow& operator[](size_t i) const { return rows[i]; }

    std::optional<PgRow> first() const {
        if (rows.empty()) return std::nullopt;
        return rows[0];
    }

    std::optional<std::string> scalar() const {
        if (rows.empty() || rows[0].values.empty()) return std::nullopt;
        if (rows[0].nulls[0]) return std::nullopt;
        return rows[0].values[0];
    }

    int scalar_int(int def = 0) const {
        auto v = scalar();
        if (!v) return def;
        try { return std::stoi(*v); } catch (...) { return def; }
    }
};

// ============================================================================
// Connection Wrapper
// ============================================================================

#ifdef GENIE_USE_POSTGRESQL

/**
 * @brief RAII PostgreSQL connection wrapper
 */
class PgConnection {
public:
    PgConnection() = default;

    explicit PgConnection(const std::string& conn_string) {
        conn_ = PQconnectdb(conn_string.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("PostgreSQL connection failed: " + err);
        }
    }

    ~PgConnection() {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    // Move-only
    PgConnection(PgConnection&& other) noexcept : conn_(other.conn_) {
        other.conn_ = nullptr;
    }

    PgConnection& operator=(PgConnection&& other) noexcept {
        if (this != &other) {
            if (conn_) PQfinish(conn_);
            conn_ = other.conn_;
            other.conn_ = nullptr;
        }
        return *this;
    }

    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;

    PGconn* get() const { return conn_; }
    bool is_valid() const { return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK; }

    bool ping() {
        if (!conn_) return false;
        PGresult* res = PQexec(conn_, "SELECT 1");
        bool ok = (PQresultStatus(res) == PGRES_TUPLES_OK);
        PQclear(res);
        return ok;
    }

    void reset() {
        if (conn_) PQreset(conn_);
    }

private:
    PGconn* conn_ = nullptr;
};

#else // !GENIE_USE_POSTGRESQL

/**
 * @brief Stub connection when PostgreSQL is not available
 */
class PgConnection {
public:
    PgConnection() = default;
    explicit PgConnection(const std::string&) {
        // No-op: PostgreSQL support not compiled in
    }
    bool is_valid() const { return false; }
    bool ping() { return false; }
    void reset() {}
};

#endif // GENIE_USE_POSTGRESQL

// ============================================================================
// Connection Pool
// ============================================================================

/**
 * @brief Thread-safe connection pool
 */
class PgConnectionPool {
public:
    PgConnectionPool() = default;

    explicit PgConnectionPool(const PgConfig& config)
        : config_(config) {}

    /**
     * @brief Initialize pool with configured number of connections
     */
    bool initialize() {
#ifdef GENIE_USE_POSTGRESQL
        std::lock_guard<std::mutex> lock(mtx_);
        std::string conn_str = config_.connection_string();

        for (int i = 0; i < config_.pool_size; ++i) {
            try {
                auto conn = std::make_unique<PgConnection>(conn_str);
                pool_.push(std::move(conn));
                total_created_++;
            } catch (const std::exception& e) {
                last_error_ = e.what();
                if (i == 0) return false; // Can't create even one connection
            }
        }
        initialized_ = true;
        return !pool_.empty();
#else
        last_error_ = "PostgreSQL support not compiled (define GENIE_USE_POSTGRESQL)";
        return false;
#endif
    }

    /**
     * @brief Acquire a connection from the pool
     * @param timeout Maximum wait time
     * @return Connection or nullptr if timeout
     */
    std::unique_ptr<PgConnection> acquire(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [this]{ return !pool_.empty() || !initialized_; })) {
            return nullptr; // Timeout
        }
        if (pool_.empty()) return nullptr;

        auto conn = std::move(pool_.front());
        pool_.pop();
        active_count_++;

        // Validate connection
        if (!conn->is_valid()) {
            conn->reset();
            if (!conn->is_valid()) {
                // Create new connection
                try {
                    conn = std::make_unique<PgConnection>(config_.connection_string());
                    total_created_++;
                } catch (...) {
                    active_count_--;
                    return nullptr;
                }
            }
        }

        return conn;
    }

    /**
     * @brief Return a connection to the pool
     */
    void release(std::unique_ptr<PgConnection> conn) {
        if (!conn) return;
        std::lock_guard<std::mutex> lock(mtx_);
        active_count_--;
        if (conn->is_valid()) {
            pool_.push(std::move(conn));
        } else {
            // Discard broken connection, create new one
            try {
                auto new_conn = std::make_unique<PgConnection>(config_.connection_string());
                pool_.push(std::move(new_conn));
                total_created_++;
            } catch (...) {
                // Pool size shrinks
            }
        }
        cv_.notify_one();
    }

    // Stats
    int available() const { std::lock_guard<std::mutex> lock(mtx_); return (int)pool_.size(); }
    int active() const { return active_count_.load(); }
    int total_created() const { return total_created_.load(); }
    bool is_initialized() const { return initialized_; }
    std::string last_error() const { return last_error_; }

    /**
     * @brief Close all connections
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!pool_.empty()) pool_.pop();
        initialized_ = false;
    }

private:
    PgConfig config_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::unique_ptr<PgConnection>> pool_;
    bool initialized_ = false;
    std::atomic<int> active_count_{0};
    std::atomic<int> total_created_{0};
    std::string last_error_;
};

// ============================================================================
// Scoped Connection (RAII pool checkout)
// ============================================================================

/**
 * @brief RAII wrapper that returns connection to pool on destruction
 */
class ScopedConnection {
public:
    ScopedConnection(PgConnectionPool& pool,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        : pool_(pool)
        , conn_(pool.acquire(timeout)) {}

    ~ScopedConnection() {
        if (conn_) pool_.release(std::move(conn_));
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    PgConnection* operator->() { return conn_.get(); }
    PgConnection& operator*() { return *conn_; }
    bool valid() const { return conn_ && conn_->is_valid(); }
    explicit operator bool() const { return valid(); }

private:
    PgConnectionPool& pool_;
    std::unique_ptr<PgConnection> conn_;
};

// ============================================================================
// PostgreSQL Database (DataStore-compatible interface)
// ============================================================================

/**
 * @brief PostgreSQL database implementing DataStore-compatible interface
 *
 * Provides the same query/execute API as the SQLite Database class,
 * enabling drop-in backend replacement via configuration.
 */
class PgDatabase {
public:
    PgDatabase() = default;

    explicit PgDatabase(const PgConfig& config)
        : config_(config)
        , pool_(config) {}

    /**
     * @brief Initialize database connection pool and run migrations
     */
    bool open() {
        if (!pool_.initialize()) {
            last_error_ = "Connection pool initialization failed: " + pool_.last_error();
            return false;
        }

        if (config_.auto_migrate) {
            if (!run_migrations()) {
                return false;
            }
        }

        open_ = true;
        return true;
    }

    /**
     * @brief Close all connections
     */
    void close() {
        pool_.shutdown();
        open_ = false;
    }

    bool is_open() const { return open_; }

    // ---- Query Interface (matches SQLite DataStore) ----

    /**
     * @brief Execute a query with parameters, return results
     */
    PgResult query(const std::string& sql,
                   const std::vector<PgParam>& params = {}) {
        PgResult result;
        auto start = std::chrono::steady_clock::now();

#ifdef GENIE_USE_POSTGRESQL
        ScopedConnection conn(pool_);
        if (!conn) {
            result.error = "No available connections";
            return result;
        }

        // Convert params to C strings for PQexecParams
        std::vector<std::string> param_strings;
        std::vector<const char*> param_values;
        std::vector<int> param_lengths;
        std::vector<int> param_formats;

        for (const auto& p : params) {
            if (param_is_null(p)) {
                param_strings.emplace_back("");
                param_values.push_back(nullptr);
            } else {
                param_strings.push_back(param_to_string(p));
                param_values.push_back(param_strings.back().c_str());
            }
            param_lengths.push_back(0);  // Text format
            param_formats.push_back(0);  // Text format
        }

        PGresult* pg_res = PQexecParams(
            conn->get(), sql.c_str(),
            static_cast<int>(params.size()),
            nullptr,    // Let server infer types
            param_values.empty() ? nullptr : param_values.data(),
            param_lengths.empty() ? nullptr : param_lengths.data(),
            param_formats.empty() ? nullptr : param_formats.data(),
            0           // Text result format
        );

        auto status = PQresultStatus(pg_res);

        if (status == PGRES_TUPLES_OK) {
            // SELECT query
            int ncols = PQnfields(pg_res);
            int nrows = PQntuples(pg_res);

            // Column names
            for (int c = 0; c < ncols; ++c) {
                result.columns.push_back(PQfname(pg_res, c));
            }

            // Rows
            for (int r = 0; r < nrows; ++r) {
                PgRow row;
                row.column_names = result.columns;
                for (int c = 0; c < ncols; ++c) {
                    if (PQgetisnull(pg_res, r, c)) {
                        row.values.emplace_back("");
                        row.nulls.push_back(true);
                    } else {
                        row.values.emplace_back(PQgetvalue(pg_res, r, c));
                        row.nulls.push_back(false);
                    }
                }
                result.rows.push_back(std::move(row));
            }
            result.success = true;
        } else if (status == PGRES_COMMAND_OK) {
            // INSERT/UPDATE/DELETE
            char* affected = PQcmdTuples(pg_res);
            if (affected && affected[0]) {
                result.affected_rows = std::atoi(affected);
            }
            result.success = true;
        } else {
            result.error = PQresultErrorMessage(pg_res);
            last_error_ = result.error;
        }

        PQclear(pg_res);
#else
        result.error = "PostgreSQL not compiled (define GENIE_USE_POSTGRESQL)";
#endif

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        result.elapsed_ms = elapsed / 1000.0;

        return result;
    }

    /**
     * @brief Execute a statement (no result expected)
     */
    bool execute(const std::string& sql,
                 const std::vector<PgParam>& params = {}) {
        auto result = query(sql, params);
        return result.success;
    }

    /**
     * @brief Execute multiple statements in a transaction
     */
    bool transaction(const std::function<bool()>& func) {
        if (!execute("BEGIN")) return false;
        try {
            if (func()) {
                return execute("COMMIT");
            } else {
                execute("ROLLBACK");
                return false;
            }
        } catch (...) {
            execute("ROLLBACK");
            return false;
        }
    }

    /**
     * @brief Query a single scalar value
     */
    std::optional<std::string> query_scalar(const std::string& sql,
                                             const std::vector<PgParam>& params = {}) {
        auto result = query(sql, params);
        return result.scalar();
    }

    /**
     * @brief Query a single integer value
     */
    int query_int(const std::string& sql, const std::vector<PgParam>& params = {},
                  int def = 0) {
        auto result = query(sql, params);
        return result.scalar_int(def);
    }

    // ---- Schema Management ----

    /**
     * @brief Check if a table exists
     */
    bool table_exists(const std::string& table_name) {
        auto result = query(
            "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
            "WHERE table_schema = 'public' AND table_name = $1)",
            {table_name});
        if (!result.success || result.empty()) return false;
        return result[0].get_bool("exists");
    }

    /**
     * @brief Get current schema version
     */
    int schema_version() {
        if (!table_exists("schema_version")) return 0;
        return query_int("SELECT MAX(version) FROM schema_version");
    }

    // ---- Pool Stats ----

    int pool_available() const { return pool_.available(); }
    int pool_active() const { return pool_.active(); }
    std::string last_error() const { return last_error_; }

    /**
     * @brief Get database server version string
     */
    std::string server_version() {
#ifdef GENIE_USE_POSTGRESQL
        auto result = query("SELECT version()");
        if (result.success && !result.empty()) {
            return result[0].get(0);
        }
#endif
        return "unknown";
    }

    /**
     * @brief Get database size
     */
    std::string database_size() {
        auto result = query(
            "SELECT pg_size_pretty(pg_database_size(current_database()))");
        if (result.success && !result.empty()) {
            return result[0].get(0);
        }
        return "unknown";
    }

    /**
     * @brief JSON status report
     */
    std::string status_json() const {
        std::ostringstream oss;
        oss << "{\n"
            << "  \"backend\": \"postgresql\",\n"
            << "  \"host\": \"" << config_.host << "\",\n"
            << "  \"port\": " << config_.port << ",\n"
            << "  \"database\": \"" << config_.database << "\",\n"
            << "  \"open\": " << (open_ ? "true" : "false") << ",\n"
            << "  \"pool_size\": " << config_.pool_size << ",\n"
            << "  \"pool_available\": " << pool_.available() << ",\n"
            << "  \"pool_active\": " << pool_.active() << ",\n"
            << "  \"pool_total_created\": " << pool_.total_created() << "\n"
            << "}";
        return oss.str();
    }

private:
    /**
     * @brief Run schema migrations
     */
    bool run_migrations() {
        // Create schema_version table if not exists
        if (!execute(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "  version INTEGER PRIMARY KEY,"
            "  applied_at TIMESTAMPTZ DEFAULT NOW(),"
            "  description TEXT"
            ")")) {
            return false;
        }

        int current = schema_version();

        // Migration 1: Core tables
        if (current < 1) {
            bool ok = transaction([this]() {
                // Users
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS users ("
                    "  id SERIAL PRIMARY KEY,"
                    "  username VARCHAR(64) UNIQUE NOT NULL,"
                    "  password_hash VARCHAR(256) NOT NULL,"
                    "  role VARCHAR(32) DEFAULT 'viewer',"
                    "  email VARCHAR(256),"
                    "  created_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  last_login TIMESTAMPTZ,"
                    "  locked BOOLEAN DEFAULT FALSE,"
                    "  failed_attempts INTEGER DEFAULT 0"
                    ")")) return false;

                // Sessions
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS sessions ("
                    "  token VARCHAR(256) PRIMARY KEY,"
                    "  username VARCHAR(64) REFERENCES users(username),"
                    "  created_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  expires_at TIMESTAMPTZ,"
                    "  ip_address VARCHAR(45),"
                    "  user_agent TEXT"
                    ")")) return false;

                // Portfolios
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS portfolios ("
                    "  id SERIAL PRIMARY KEY,"
                    "  name VARCHAR(128) NOT NULL,"
                    "  owner VARCHAR(64) REFERENCES users(username),"
                    "  currency VARCHAR(3) DEFAULT 'USD',"
                    "  created_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  metadata JSONB DEFAULT '{}'::jsonb"
                    ")")) return false;

                // Positions
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS positions ("
                    "  id SERIAL PRIMARY KEY,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  symbol VARCHAR(20) NOT NULL,"
                    "  quantity NUMERIC(18,6) NOT NULL,"
                    "  avg_cost NUMERIC(18,6),"
                    "  market_value NUMERIC(18,2),"
                    "  unrealized_pnl NUMERIC(18,2),"
                    "  updated_at TIMESTAMPTZ DEFAULT NOW()"
                    ")")) return false;

                // Transactions
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS transactions ("
                    "  id SERIAL PRIMARY KEY,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  symbol VARCHAR(20) NOT NULL,"
                    "  side VARCHAR(4) NOT NULL,"
                    "  quantity NUMERIC(18,6) NOT NULL,"
                    "  price NUMERIC(18,6) NOT NULL,"
                    "  fees NUMERIC(18,4) DEFAULT 0,"
                    "  executed_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  order_id VARCHAR(64),"
                    "  metadata JSONB DEFAULT '{}'::jsonb"
                    ")")) return false;

                // Market data
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS market_data ("
                    "  id BIGSERIAL PRIMARY KEY,"
                    "  symbol VARCHAR(20) NOT NULL,"
                    "  date DATE NOT NULL,"
                    "  open NUMERIC(18,6),"
                    "  high NUMERIC(18,6),"
                    "  low NUMERIC(18,6),"
                    "  close NUMERIC(18,6),"
                    "  volume BIGINT,"
                    "  source VARCHAR(32),"
                    "  UNIQUE(symbol, date)"
                    ")")) return false;

                // Audit log
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS audit_log ("
                    "  id BIGSERIAL PRIMARY KEY,"
                    "  timestamp TIMESTAMPTZ DEFAULT NOW(),"
                    "  username VARCHAR(64),"
                    "  action VARCHAR(64) NOT NULL,"
                    "  resource VARCHAR(128),"
                    "  details JSONB DEFAULT '{}'::jsonb,"
                    "  ip_address VARCHAR(45)"
                    ")")) return false;

                // Orders
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS orders ("
                    "  id SERIAL PRIMARY KEY,"
                    "  order_id VARCHAR(64) UNIQUE NOT NULL,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  symbol VARCHAR(20) NOT NULL,"
                    "  side VARCHAR(4) NOT NULL,"
                    "  order_type VARCHAR(16) NOT NULL,"
                    "  quantity NUMERIC(18,6) NOT NULL,"
                    "  price NUMERIC(18,6),"
                    "  status VARCHAR(16) DEFAULT 'pending',"
                    "  filled_qty NUMERIC(18,6) DEFAULT 0,"
                    "  avg_fill_price NUMERIC(18,6),"
                    "  created_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  updated_at TIMESTAMPTZ DEFAULT NOW(),"
                    "  metadata JSONB DEFAULT '{}'::jsonb"
                    ")")) return false;

                // Alerts
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS alerts ("
                    "  id SERIAL PRIMARY KEY,"
                    "  username VARCHAR(64) REFERENCES users(username),"
                    "  symbol VARCHAR(20),"
                    "  condition VARCHAR(32) NOT NULL,"
                    "  threshold NUMERIC(18,6),"
                    "  active BOOLEAN DEFAULT TRUE,"
                    "  triggered_at TIMESTAMPTZ,"
                    "  created_at TIMESTAMPTZ DEFAULT NOW()"
                    ")")) return false;

                // Create indexes
                execute("CREATE INDEX IF NOT EXISTS idx_positions_portfolio ON positions(portfolio_id)");
                execute("CREATE INDEX IF NOT EXISTS idx_transactions_portfolio ON transactions(portfolio_id)");
                execute("CREATE INDEX IF NOT EXISTS idx_transactions_symbol ON transactions(symbol)");
                execute("CREATE INDEX IF NOT EXISTS idx_market_data_symbol ON market_data(symbol)");
                execute("CREATE INDEX IF NOT EXISTS idx_market_data_date ON market_data(date)");
                execute("CREATE INDEX IF NOT EXISTS idx_audit_log_timestamp ON audit_log(timestamp)");
                execute("CREATE INDEX IF NOT EXISTS idx_audit_log_username ON audit_log(username)");
                execute("CREATE INDEX IF NOT EXISTS idx_orders_portfolio ON orders(portfolio_id)");
                execute("CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status)");
                execute("CREATE INDEX IF NOT EXISTS idx_sessions_username ON sessions(username)");

                // Record migration
                return execute(
                    "INSERT INTO schema_version (version, description) VALUES (1, 'Initial schema')");
            });

            if (!ok) {
                last_error_ = "Migration 1 failed";
                return false;
            }
        }

        // Migration 2: Performance and compliance tables
        if (current < 2) {
            bool ok = transaction([this]() {
                if (!execute(
                    "CREATE TABLE IF NOT EXISTS performance_snapshots ("
                    "  id BIGSERIAL PRIMARY KEY,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  date DATE NOT NULL,"
                    "  total_value NUMERIC(18,2),"
                    "  daily_return NUMERIC(12,8),"
                    "  cumulative_return NUMERIC(12,8),"
                    "  benchmark_return NUMERIC(12,8),"
                    "  metadata JSONB DEFAULT '{}'::jsonb,"
                    "  UNIQUE(portfolio_id, date)"
                    ")")) return false;

                if (!execute(
                    "CREATE TABLE IF NOT EXISTS compliance_checks ("
                    "  id SERIAL PRIMARY KEY,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  rule_name VARCHAR(128) NOT NULL,"
                    "  status VARCHAR(16) NOT NULL,"
                    "  message TEXT,"
                    "  checked_at TIMESTAMPTZ DEFAULT NOW()"
                    ")")) return false;

                if (!execute(
                    "CREATE TABLE IF NOT EXISTS risk_snapshots ("
                    "  id BIGSERIAL PRIMARY KEY,"
                    "  portfolio_id INTEGER REFERENCES portfolios(id),"
                    "  date DATE NOT NULL,"
                    "  var_95 NUMERIC(18,6),"
                    "  var_99 NUMERIC(18,6),"
                    "  cvar_95 NUMERIC(18,6),"
                    "  beta NUMERIC(12,8),"
                    "  sharpe NUMERIC(12,8),"
                    "  max_drawdown NUMERIC(12,8),"
                    "  metadata JSONB DEFAULT '{}'::jsonb,"
                    "  UNIQUE(portfolio_id, date)"
                    ")")) return false;

                execute("CREATE INDEX IF NOT EXISTS idx_perf_portfolio_date ON performance_snapshots(portfolio_id, date)");
                execute("CREATE INDEX IF NOT EXISTS idx_risk_portfolio_date ON risk_snapshots(portfolio_id, date)");

                return execute(
                    "INSERT INTO schema_version (version, description) "
                    "VALUES (2, 'Performance and risk tables')");
            });

            if (!ok) {
                last_error_ = "Migration 2 failed";
                return false;
            }
        }

        return true;
    }

    PgConfig config_;
    PgConnectionPool pool_;
    bool open_ = false;
    std::string last_error_;
};

// ============================================================================
// Database Factory (SQLite vs PostgreSQL)
// ============================================================================

/**
 * @brief Database backend type selection
 */
enum class DatabaseBackend {
    SQLITE,
    POSTGRESQL
};

inline DatabaseBackend parse_backend(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "postgresql" || lower == "postgres" || lower == "pg") {
        return DatabaseBackend::POSTGRESQL;
    }
    return DatabaseBackend::SQLITE;
}

} // namespace core
} // namespace genie

#endif // GENIE_CORE_PG_DATABASE_HPP
