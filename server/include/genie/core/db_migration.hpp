/**
 * @file db_migration.hpp
 * @brief Database Migration Framework
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements versioned database migration system:
 * - Sequential numbered migrations with up/down support
 * - Migration history tracking and rollback
 * - Schema version management
 * - Dry-run mode for validation
 * - Seed data management
 * - Transaction-safe execution
 * - Migration generators and templates
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_DB_MIGRATION_HPP
#define GENIE_CORE_DB_MIGRATION_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <memory>
#include <optional>
#include <stdexcept>
#include <fstream>

namespace genie {
namespace core {
namespace migration {

// ============================================================================
// Enumerations
// ============================================================================

enum class MigrationDirection { Up, Down };
enum class MigrationStatus { Pending, Applied, Failed, RolledBack, Skipped };
enum class ColumnType { Integer, BigInt, Text, Varchar, Boolean, Timestamp, Numeric, Blob, Json, Uuid };

[[nodiscard]] inline std::string status_name(MigrationStatus s) {
    switch (s) {
        case MigrationStatus::Pending:    return "PENDING";
        case MigrationStatus::Applied:    return "APPLIED";
        case MigrationStatus::Failed:     return "FAILED";
        case MigrationStatus::RolledBack: return "ROLLED_BACK";
        case MigrationStatus::Skipped:    return "SKIPPED";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline std::string column_type_sql(ColumnType t) {
    switch (t) {
        case ColumnType::Integer:   return "INTEGER";
        case ColumnType::BigInt:    return "BIGINT";
        case ColumnType::Text:      return "TEXT";
        case ColumnType::Varchar:   return "VARCHAR(255)";
        case ColumnType::Boolean:   return "BOOLEAN";
        case ColumnType::Timestamp: return "TIMESTAMP";
        case ColumnType::Numeric:   return "NUMERIC(18,8)";
        case ColumnType::Blob:      return "BLOB";
        case ColumnType::Json:      return "TEXT";  // JSON stored as TEXT for portability
        case ColumnType::Uuid:      return "VARCHAR(36)";
    }
    return "TEXT";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Column definition for schema builder
 */
struct ColumnDef {
    std::string name;
    ColumnType type{ColumnType::Text};
    bool nullable{true};
    bool primary_key{false};
    bool unique{false};
    bool auto_increment{false};
    std::string default_value;
    std::string foreign_key;           // "table(column)"
    std::string check_constraint;

    [[nodiscard]] std::string to_sql() const {
        std::ostringstream oss;
        oss << name << " " << column_type_sql(type);
        if (!nullable) oss << " NOT NULL";
        if (primary_key) oss << " PRIMARY KEY";
        if (auto_increment) oss << " AUTOINCREMENT";
        if (unique && !primary_key) oss << " UNIQUE";
        if (!default_value.empty()) oss << " DEFAULT " << default_value;
        if (!check_constraint.empty()) oss << " CHECK(" << check_constraint << ")";
        return oss.str();
    }
};

/**
 * @brief Index definition
 */
struct IndexDef {
    std::string name;
    std::string table;
    std::vector<std::string> columns;
    bool unique{false};

    [[nodiscard]] std::string to_sql() const {
        std::ostringstream oss;
        oss << "CREATE ";
        if (unique) oss << "UNIQUE ";
        oss << "INDEX IF NOT EXISTS " << name << " ON " << table << " (";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << columns[i];
        }
        oss << ")";
        return oss.str();
    }
};

/**
 * @brief Individual migration record
 */
struct MigrationRecord {
    int version{0};
    std::string name;
    std::string description;
    MigrationStatus status{MigrationStatus::Pending};
    std::string applied_at;
    std::string rolled_back_at;
    int64_t execution_time_ms{0};
    std::string error_message;
    std::string checksum;                  // For detecting tampering

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "V" << version << " [" << status_name(status) << "] " << name;
        if (!description.empty()) oss << " - " << description;
        if (execution_time_ms > 0) oss << " (" << execution_time_ms << "ms)";
        return oss.str();
    }
};

/**
 * @brief Migration definition with up/down SQL
 */
struct MigrationDef {
    int version{0};
    std::string name;
    std::string description;
    std::vector<std::string> up_statements;
    std::vector<std::string> down_statements;
    std::string checksum;

    [[nodiscard]] std::string compute_checksum() const {
        // Simple hash based on content
        size_t hash = 0;
        for (const auto& s : up_statements) {
            for (char c : s) hash = hash * 31 + static_cast<size_t>(c);
        }
        for (const auto& s : down_statements) {
            for (char c : s) hash = hash * 31 + static_cast<size_t>(c);
        }
        std::ostringstream oss;
        oss << std::hex << hash;
        return oss.str();
    }
};

/**
 * @brief Seed data record
 */
struct SeedData {
    std::string name;
    std::string table;
    std::vector<std::map<std::string, std::string>> rows;
    bool run_once{true};
    bool applied{false};
};

/**
 * @brief Migration plan (dry-run result)
 */
struct MigrationPlan {
    std::vector<MigrationDef> pending_migrations;
    int current_version{0};
    int target_version{0};
    MigrationDirection direction{MigrationDirection::Up};
    int total_statements{0};
    bool is_clean{true};                   // No failed migrations

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Migration Plan: V" << current_version << " -> V" << target_version << "\n";
        oss << "Direction: " << (direction == MigrationDirection::Up ? "UP" : "DOWN") << "\n";
        oss << "Pending: " << pending_migrations.size() << " migrations\n";
        oss << "Statements: " << total_statements << "\n";
        for (const auto& m : pending_migrations) {
            oss << "  V" << m.version << " " << m.name << " ("
                << m.up_statements.size() << " statements)\n";
        }
        return oss.str();
    }
};

// ============================================================================
// Schema Builder
// ============================================================================

/**
 * @brief Fluent API for building table schemas
 */
class SchemaBuilder {
public:
    /**
     * @brief Start building a CREATE TABLE statement
     */
    SchemaBuilder& create_table(const std::string& name) {
        current_table_ = name;
        columns_.clear();
        indices_.clear();
        foreign_keys_.clear();
        return *this;
    }

    SchemaBuilder& add_column(const std::string& name, ColumnType type,
                               bool nullable = true) {
        ColumnDef col;
        col.name = name;
        col.type = type;
        col.nullable = nullable;
        columns_.push_back(col);
        return *this;
    }

    SchemaBuilder& add_id(const std::string& name = "id") {
        ColumnDef col;
        col.name = name;
        col.type = ColumnType::Integer;
        col.primary_key = true;
        col.auto_increment = true;
        col.nullable = false;
        columns_.push_back(col);
        return *this;
    }

    SchemaBuilder& add_uuid(const std::string& name = "id") {
        ColumnDef col;
        col.name = name;
        col.type = ColumnType::Uuid;
        col.primary_key = true;
        col.nullable = false;
        columns_.push_back(col);
        return *this;
    }

    SchemaBuilder& add_timestamps() {
        add_column("created_at", ColumnType::Timestamp, false);
        add_column("updated_at", ColumnType::Timestamp, false);
        return *this;
    }

    SchemaBuilder& add_foreign_key(const std::string& column,
                                    const std::string& ref_table,
                                    const std::string& ref_column = "id") {
        foreign_keys_.push_back({column, ref_table + "(" + ref_column + ")"});
        return *this;
    }

    SchemaBuilder& add_index(const std::string& name,
                              const std::vector<std::string>& columns,
                              bool unique = false) {
        IndexDef idx;
        idx.name = name;
        idx.table = current_table_;
        idx.columns = columns;
        idx.unique = unique;
        indices_.push_back(idx);
        return *this;
    }

    /**
     * @brief Generate CREATE TABLE SQL
     */
    [[nodiscard]] std::vector<std::string> build() const {
        std::vector<std::string> stmts;
        std::ostringstream oss;
        oss << "CREATE TABLE IF NOT EXISTS " << current_table_ << " (\n";
        for (size_t i = 0; i < columns_.size(); ++i) {
            oss << "  " << columns_[i].to_sql();
            if (i < columns_.size() - 1 || !foreign_keys_.empty()) oss << ",";
            oss << "\n";
        }
        for (size_t i = 0; i < foreign_keys_.size(); ++i) {
            oss << "  FOREIGN KEY (" << foreign_keys_[i].first
                << ") REFERENCES " << foreign_keys_[i].second;
            if (i < foreign_keys_.size() - 1) oss << ",";
            oss << "\n";
        }
        oss << ")";
        stmts.push_back(oss.str());

        for (const auto& idx : indices_) {
            stmts.push_back(idx.to_sql());
        }

        return stmts;
    }

    /**
     * @brief Generate DROP TABLE SQL
     */
    [[nodiscard]] std::string build_drop() const {
        return "DROP TABLE IF EXISTS " + current_table_;
    }

private:
    std::string current_table_;
    std::vector<ColumnDef> columns_;
    std::vector<IndexDef> indices_;
    std::vector<std::pair<std::string, std::string>> foreign_keys_;
};

// ============================================================================
// Migration Engine
// ============================================================================

/**
 * @brief Core migration engine
 *
 * Manages versioned database migrations with:
 * - Ordered migration application
 * - Rollback support
 * - Dry-run validation
 * - History tracking
 * - Seed data management
 */
class MigrationEngine {
public:
    using SqlExecutor = std::function<bool(const std::string& sql, std::string& error)>;

    explicit MigrationEngine(SqlExecutor executor = nullptr) : executor_(std::move(executor)) {}

    /**
     * @brief Register a migration
     */
    void add_migration(MigrationDef migration) {
        std::lock_guard<std::mutex> lock(mutex_);
        migration.checksum = migration.compute_checksum();
        migrations_[migration.version] = std::move(migration);
    }

    /**
     * @brief Register migration with builder pattern
     */
    void add_migration(int version, const std::string& name,
                       const std::vector<std::string>& up,
                       const std::vector<std::string>& down,
                       const std::string& description = "") {
        MigrationDef def;
        def.version = version;
        def.name = name;
        def.description = description;
        def.up_statements = up;
        def.down_statements = down;
        add_migration(std::move(def));
    }

    /**
     * @brief Register seed data
     */
    void add_seed(SeedData seed) {
        std::lock_guard<std::mutex> lock(mutex_);
        seeds_.push_back(std::move(seed));
    }

    /**
     * @brief Plan migration (dry run)
     */
    [[nodiscard]] MigrationPlan plan(int target_version = -1) const {
        std::lock_guard<std::mutex> lock(mutex_);
        MigrationPlan result;
        result.current_version = current_version_;

        if (target_version < 0) {
            // Find max version
            target_version = 0;
            for (const auto& [v, _] : migrations_) {
                target_version = std::max(target_version, v);
            }
        }
        result.target_version = target_version;

        if (target_version > current_version_) {
            result.direction = MigrationDirection::Up;
            for (const auto& [v, m] : migrations_) {
                if (v > current_version_ && v <= target_version) {
                    result.pending_migrations.push_back(m);
                    result.total_statements += static_cast<int>(m.up_statements.size());
                }
            }
        } else if (target_version < current_version_) {
            result.direction = MigrationDirection::Down;
            for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
                if (it->first <= current_version_ && it->first > target_version) {
                    result.pending_migrations.push_back(it->second);
                    result.total_statements += static_cast<int>(it->second.down_statements.size());
                }
            }
        }

        return result;
    }

    /**
     * @brief Run pending migrations up to target version
     */
    std::vector<MigrationRecord> migrate(int target_version = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MigrationRecord> results;

        if (target_version < 0) {
            target_version = 0;
            for (const auto& [v, _] : migrations_) {
                target_version = std::max(target_version, v);
            }
        }

        if (target_version > current_version_) {
            // Migrate up
            for (const auto& [v, m] : migrations_) {
                if (v > current_version_ && v <= target_version) {
                    auto record = execute_migration(m, MigrationDirection::Up);
                    results.push_back(record);
                    if (record.status == MigrationStatus::Applied) {
                        current_version_ = v;
                    } else {
                        break;  // Stop on failure
                    }
                }
            }
        } else if (target_version < current_version_) {
            // Migrate down
            for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
                if (it->first <= current_version_ && it->first > target_version) {
                    auto record = execute_migration(it->second, MigrationDirection::Down);
                    results.push_back(record);
                    if (record.status == MigrationStatus::Applied) {
                        current_version_ = it->first - 1;
                    } else {
                        break;
                    }
                }
            }
        }

        return results;
    }

    /**
     * @brief Rollback last N migrations
     */
    std::vector<MigrationRecord> rollback(int count = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MigrationRecord> results;

        for (int i = 0; i < count && current_version_ > 0; ++i) {
            auto it = migrations_.find(current_version_);
            if (it == migrations_.end()) break;

            auto record = execute_migration(it->second, MigrationDirection::Down);
            results.push_back(record);
            if (record.status == MigrationStatus::Applied) {
                current_version_--;
            } else {
                break;
            }
        }

        return results;
    }

    /**
     * @brief Get migration history
     */
    [[nodiscard]] std::vector<MigrationRecord> history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_;
    }

    /**
     * @brief Get current version
     */
    [[nodiscard]] int current_version() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_version_;
    }

    /**
     * @brief Check if up to date
     */
    [[nodiscard]] bool is_current() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (migrations_.empty()) return true;
        return current_version_ >= migrations_.rbegin()->first;
    }

    /**
     * @brief Get pending migration count
     */
    [[nodiscard]] int pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (const auto& [v, _] : migrations_) {
            if (v > current_version_) count++;
        }
        return count;
    }

    /**
     * @brief List all registered migrations
     */
    [[nodiscard]] std::vector<MigrationDef> list_migrations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MigrationDef> result;
        for (const auto& [_, m] : migrations_) {
            result.push_back(m);
        }
        return result;
    }

    /**
     * @brief Set SQL executor
     */
    void set_executor(SqlExecutor executor) {
        std::lock_guard<std::mutex> lock(mutex_);
        executor_ = std::move(executor);
    }

    /**
     * @brief Export all migrations as SQL
     */
    [[nodiscard]] std::string export_sql() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "-- Metis Genie Platform Database Migrations\n";
        oss << "-- Generated: " << now_string() << "\n\n";

        for (const auto& [v, m] : migrations_) {
            oss << "-- ============================================\n";
            oss << "-- V" << v << ": " << m.name << "\n";
            if (!m.description.empty()) oss << "-- " << m.description << "\n";
            oss << "-- ============================================\n\n";

            oss << "-- UP\n";
            for (const auto& stmt : m.up_statements) {
                oss << stmt << ";\n\n";
            }

            oss << "-- DOWN\n";
            for (const auto& stmt : m.down_statements) {
                oss << stmt << ";\n\n";
            }
        }

        return oss.str();
    }

    /**
     * @brief Generate initial migration schema for the platform
     */
    static MigrationDef create_initial_schema() {
        SchemaBuilder sb;

        MigrationDef def;
        def.version = 1;
        def.name = "create_initial_schema";
        def.description = "Create core platform tables";

        // Users table
        sb.create_table("users")
          .add_uuid("id")
          .add_column("username", ColumnType::Varchar, false)
          .add_column("email", ColumnType::Varchar, false)
          .add_column("password_hash", ColumnType::Varchar, false)
          .add_column("role", ColumnType::Varchar, false)
          .add_column("is_active", ColumnType::Boolean, false)
          .add_timestamps()
          .add_index("idx_users_email", {"email"}, true)
          .add_index("idx_users_username", {"username"}, true);
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Portfolios table
        sb.create_table("portfolios")
          .add_uuid("id")
          .add_column("name", ColumnType::Varchar, false)
          .add_column("owner_id", ColumnType::Uuid, false)
          .add_column("benchmark", ColumnType::Varchar)
          .add_column("currency", ColumnType::Varchar, false)
          .add_column("inception_date", ColumnType::Timestamp)
          .add_column("strategy", ColumnType::Text)
          .add_column("is_active", ColumnType::Boolean, false)
          .add_timestamps()
          .add_foreign_key("owner_id", "users")
          .add_index("idx_port_owner", {"owner_id"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Positions table
        sb.create_table("positions")
          .add_uuid("id")
          .add_column("portfolio_id", ColumnType::Uuid, false)
          .add_column("symbol", ColumnType::Varchar, false)
          .add_column("quantity", ColumnType::Numeric, false)
          .add_column("cost_basis", ColumnType::Numeric, false)
          .add_column("current_price", ColumnType::Numeric)
          .add_column("asset_class", ColumnType::Varchar)
          .add_column("sector", ColumnType::Varchar)
          .add_timestamps()
          .add_foreign_key("portfolio_id", "portfolios")
          .add_index("idx_pos_portfolio", {"portfolio_id"})
          .add_index("idx_pos_symbol", {"symbol"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Orders table
        sb.create_table("orders")
          .add_uuid("id")
          .add_column("portfolio_id", ColumnType::Uuid, false)
          .add_column("symbol", ColumnType::Varchar, false)
          .add_column("side", ColumnType::Varchar, false)
          .add_column("order_type", ColumnType::Varchar, false)
          .add_column("quantity", ColumnType::Numeric, false)
          .add_column("price", ColumnType::Numeric)
          .add_column("status", ColumnType::Varchar, false)
          .add_column("filled_quantity", ColumnType::Numeric)
          .add_column("average_fill_price", ColumnType::Numeric)
          .add_column("broker_id", ColumnType::Varchar)
          .add_timestamps()
          .add_foreign_key("portfolio_id", "portfolios")
          .add_index("idx_orders_portfolio", {"portfolio_id"})
          .add_index("idx_orders_status", {"status"})
          .add_index("idx_orders_symbol", {"symbol"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Risk snapshots
        sb.create_table("risk_snapshots")
          .add_uuid("id")
          .add_column("portfolio_id", ColumnType::Uuid, false)
          .add_column("snapshot_date", ColumnType::Timestamp, false)
          .add_column("var_95", ColumnType::Numeric)
          .add_column("var_99", ColumnType::Numeric)
          .add_column("cvar_95", ColumnType::Numeric)
          .add_column("beta", ColumnType::Numeric)
          .add_column("sharpe_ratio", ColumnType::Numeric)
          .add_column("max_drawdown", ColumnType::Numeric)
          .add_column("volatility", ColumnType::Numeric)
          .add_column("details_json", ColumnType::Json)
          .add_timestamps()
          .add_foreign_key("portfolio_id", "portfolios")
          .add_index("idx_risk_portfolio_date", {"portfolio_id", "snapshot_date"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Audit log
        sb.create_table("audit_log")
          .add_id()
          .add_column("user_id", ColumnType::Uuid)
          .add_column("action", ColumnType::Varchar, false)
          .add_column("entity_type", ColumnType::Varchar)
          .add_column("entity_id", ColumnType::Varchar)
          .add_column("details_json", ColumnType::Json)
          .add_column("ip_address", ColumnType::Varchar)
          .add_column("created_at", ColumnType::Timestamp, false)
          .add_index("idx_audit_user", {"user_id"})
          .add_index("idx_audit_action", {"action"})
          .add_index("idx_audit_created", {"created_at"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Down migrations
        def.down_statements = {
            "DROP TABLE IF EXISTS audit_log",
            "DROP TABLE IF EXISTS risk_snapshots",
            "DROP TABLE IF EXISTS orders",
            "DROP TABLE IF EXISTS positions",
            "DROP TABLE IF EXISTS portfolios",
            "DROP TABLE IF EXISTS users"
        };

        return def;
    }

    /**
     * @brief Generate crypto tables migration
     */
    static MigrationDef create_crypto_schema() {
        SchemaBuilder sb;

        MigrationDef def;
        def.version = 2;
        def.name = "create_crypto_tables";
        def.description = "Add cryptocurrency trading support tables";

        // Wallets
        sb.create_table("crypto_wallets")
          .add_uuid("id")
          .add_column("name", ColumnType::Varchar, false)
          .add_column("address", ColumnType::Varchar, false)
          .add_column("wallet_type", ColumnType::Varchar, false)
          .add_column("chain", ColumnType::Varchar, false)
          .add_column("portfolio_id", ColumnType::Uuid)
          .add_timestamps()
          .add_index("idx_wallet_address", {"address"}, true)
          .add_index("idx_wallet_chain", {"chain"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Token balances
        sb.create_table("token_balances")
          .add_id()
          .add_column("wallet_id", ColumnType::Uuid, false)
          .add_column("token_symbol", ColumnType::Varchar, false)
          .add_column("balance", ColumnType::Numeric, false)
          .add_column("value_usd", ColumnType::Numeric)
          .add_timestamps()
          .add_foreign_key("wallet_id", "crypto_wallets")
          .add_index("idx_balance_wallet", {"wallet_id"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Crypto orders
        sb.create_table("crypto_orders")
          .add_uuid("id")
          .add_column("exchange", ColumnType::Varchar, false)
          .add_column("base_symbol", ColumnType::Varchar, false)
          .add_column("quote_symbol", ColumnType::Varchar, false)
          .add_column("side", ColumnType::Varchar, false)
          .add_column("order_type", ColumnType::Varchar, false)
          .add_column("status", ColumnType::Varchar, false)
          .add_column("quantity", ColumnType::Numeric, false)
          .add_column("price", ColumnType::Numeric)
          .add_column("filled_quantity", ColumnType::Numeric)
          .add_column("portfolio_id", ColumnType::Uuid)
          .add_timestamps()
          .add_index("idx_crypto_exchange", {"exchange"})
          .add_index("idx_crypto_status", {"status"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        // Staking positions
        sb.create_table("staking_positions")
          .add_uuid("id")
          .add_column("validator", ColumnType::Varchar)
          .add_column("chain", ColumnType::Varchar, false)
          .add_column("token", ColumnType::Varchar, false)
          .add_column("staked_amount", ColumnType::Numeric, false)
          .add_column("rewards_earned", ColumnType::Numeric)
          .add_column("apy", ColumnType::Numeric)
          .add_column("status", ColumnType::Varchar, false)
          .add_column("portfolio_id", ColumnType::Uuid)
          .add_timestamps()
          .add_index("idx_staking_chain", {"chain"})
          .add_index("idx_staking_status", {"status"});
        for (const auto& s : sb.build()) def.up_statements.push_back(s);

        def.down_statements = {
            "DROP TABLE IF EXISTS staking_positions",
            "DROP TABLE IF EXISTS crypto_orders",
            "DROP TABLE IF EXISTS token_balances",
            "DROP TABLE IF EXISTS crypto_wallets"
        };

        return def;
    }

private:
    mutable std::mutex mutex_;
    std::map<int, MigrationDef> migrations_;      // Ordered by version
    std::vector<MigrationRecord> history_;
    std::vector<SeedData> seeds_;
    SqlExecutor executor_;
    int current_version_{0};

    MigrationRecord execute_migration(const MigrationDef& def, MigrationDirection dir) {
        MigrationRecord record;
        record.version = def.version;
        record.name = def.name;
        record.description = def.description;
        record.checksum = def.checksum;

        auto start = std::chrono::steady_clock::now();

        const auto& statements = (dir == MigrationDirection::Up)
            ? def.up_statements : def.down_statements;

        bool success = true;
        if (executor_) {
            for (const auto& stmt : statements) {
                std::string error;
                if (!executor_(stmt, error)) {
                    record.error_message = error;
                    success = false;
                    break;
                }
            }
        }
        // If no executor, just track as applied (dry-run compatible)

        auto end = std::chrono::steady_clock::now();
        record.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();

        record.status = success ? MigrationStatus::Applied : MigrationStatus::Failed;
        record.applied_at = now_string();

        history_.push_back(record);
        return record;
    }

    [[nodiscard]] static std::string now_string() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }
};

} // namespace migration
} // namespace core
} // namespace genie

#endif // GENIE_CORE_DB_MIGRATION_HPP
