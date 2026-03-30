/**
 * @file db_migrations.hpp
 * @brief Database schema migration framework
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements enterprise-grade database migrations:
 * - Version-tracked schema migrations with up/down support
 * - Migration dependency resolution
 * - Dry-run and rollback capabilities
 * - Migration history and audit trail
 * - Seed data management
 * - Schema snapshots and comparison
 * - Transaction-safe migration execution
 * - Cross-platform file-based storage
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERSISTENCE_DB_MIGRATIONS_HPP
#define GENIE_PERSISTENCE_DB_MIGRATIONS_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <memory>
#include <optional>
#include <fstream>
#include <set>
#include <filesystem>

namespace genie {
namespace persistence {
namespace migrations {

// ============================================================================
// Enumerations
// ============================================================================

enum class MigrationStatus {
    Pending,
    Running,
    Completed,
    Failed,
    RolledBack,
    Skipped
};

enum class MigrationDirection {
    Up,
    Down
};

enum class SchemaObjectType {
    Table,
    Index,
    View,
    Trigger,
    Constraint,
    Sequence,
    Function
};

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] inline std::string migration_status_string(MigrationStatus s) {
    switch (s) {
        case MigrationStatus::Pending:    return "pending";
        case MigrationStatus::Running:    return "running";
        case MigrationStatus::Completed:  return "completed";
        case MigrationStatus::Failed:     return "failed";
        case MigrationStatus::RolledBack: return "rolled_back";
        case MigrationStatus::Skipped:    return "skipped";
    }
    return "unknown";
}

[[nodiscard]] inline std::string schema_object_type_string(SchemaObjectType t) {
    switch (t) {
        case SchemaObjectType::Table:      return "table";
        case SchemaObjectType::Index:      return "index";
        case SchemaObjectType::View:       return "view";
        case SchemaObjectType::Trigger:    return "trigger";
        case SchemaObjectType::Constraint: return "constraint";
        case SchemaObjectType::Sequence:   return "sequence";
        case SchemaObjectType::Function:   return "function";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Column definition for schema
 */
struct ColumnDef {
    std::string name;
    std::string type;              // "TEXT", "INTEGER", "REAL", "BLOB", "TIMESTAMP"
    bool nullable{true};
    bool primary_key{false};
    bool unique{false};
    bool auto_increment{false};
    std::string default_value;
    std::string check_constraint;
    std::string foreign_key_table;
    std::string foreign_key_column;
    
    [[nodiscard]] std::string to_sql() const {
        std::ostringstream oss;
        oss << name << " " << type;
        if (primary_key) oss << " PRIMARY KEY";
        if (auto_increment) oss << " AUTOINCREMENT";
        if (!nullable && !primary_key) oss << " NOT NULL";
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
    std::string where_clause;
    
    [[nodiscard]] std::string to_sql() const {
        std::ostringstream oss;
        oss << "CREATE ";
        if (unique) oss << "UNIQUE ";
        oss << "INDEX IF NOT EXISTS " << name << " ON " << table << "(";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << columns[i];
        }
        oss << ")";
        if (!where_clause.empty()) oss << " WHERE " << where_clause;
        return oss.str();
    }
};

/**
 * @brief Table definition for schema snapshot
 */
struct TableDef {
    std::string name;
    std::vector<ColumnDef> columns;
    std::vector<IndexDef> indices;
    std::vector<std::string> constraints;
    
    [[nodiscard]] std::string create_sql() const {
        std::ostringstream oss;
        oss << "CREATE TABLE IF NOT EXISTS " << name << " (\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            oss << "  " << columns[i].to_sql();
            if (i + 1 < columns.size() || !constraints.empty()) oss << ",";
            oss << "\n";
        }
        for (size_t i = 0; i < constraints.size(); ++i) {
            oss << "  " << constraints[i];
            if (i + 1 < constraints.size()) oss << ",";
            oss << "\n";
        }
        oss << ")";
        return oss.str();
    }
    
    [[nodiscard]] std::string drop_sql() const {
        return "DROP TABLE IF EXISTS " + name;
    }
};

/**
 * @brief Single migration step
 */
struct MigrationStep {
    std::string description;
    std::string up_sql;
    std::string down_sql;
};

/**
 * @brief Migration record
 */
struct Migration {
    std::string id;                // e.g., "20260101_001"
    std::string version;           // Semantic version this belongs to
    std::string name;              // Human-readable name
    std::string description;
    std::vector<std::string> dependencies;  // IDs of prerequisite migrations
    std::vector<MigrationStep> steps;
    
    MigrationStatus status{MigrationStatus::Pending};
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point executed_at;
    std::chrono::milliseconds duration{0};
    std::string error_message;
    std::string checksum;          // SHA256 of migration content
    bool reversible{true};
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "[" << id << "] " << name << " (" 
            << migration_status_string(status) << ")";
        if (!error_message.empty()) oss << " ERROR: " << error_message;
        return oss.str();
    }
};

/**
 * @brief Migration history entry
 */
struct MigrationHistoryEntry {
    std::string migration_id;
    MigrationDirection direction;
    MigrationStatus status;
    std::chrono::system_clock::time_point executed_at;
    std::chrono::milliseconds duration{0};
    std::string executed_by;
    std::string error_message;
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        auto t = std::chrono::system_clock::to_time_t(executed_at);
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        oss << " " << (direction == MigrationDirection::Up ? "UP" : "DOWN");
        oss << " " << migration_id;
        oss << " " << migration_status_string(status);
        oss << " (" << duration.count() << "ms)";
        return oss.str();
    }
};

/**
 * @brief Schema snapshot for comparison
 */
struct SchemaSnapshot {
    std::string version;
    std::chrono::system_clock::time_point captured_at;
    std::map<std::string, TableDef> tables;
    std::vector<IndexDef> standalone_indices;
    
    [[nodiscard]] int table_count() const { return static_cast<int>(tables.size()); }
    [[nodiscard]] int total_columns() const {
        int count = 0;
        for (const auto& [name, table] : tables) {
            count += static_cast<int>(table.columns.size());
        }
        return count;
    }
};

/**
 * @brief Seed data record
 */
struct SeedData {
    std::string id;
    std::string table;
    std::string description;
    std::vector<std::map<std::string, std::string>> rows;
    bool idempotent{true};         // Uses INSERT OR IGNORE
    
    [[nodiscard]] std::vector<std::string> to_sql() const {
        std::vector<std::string> statements;
        if (rows.empty()) return statements;
        
        for (const auto& row : rows) {
            std::ostringstream oss;
            if (idempotent) {
                oss << "INSERT OR IGNORE INTO " << table << " (";
            } else {
                oss << "INSERT INTO " << table << " (";
            }
            std::vector<std::string> cols, vals;
            for (const auto& [col, val] : row) {
                cols.push_back(col);
                vals.push_back("'" + val + "'");
            }
            for (size_t i = 0; i < cols.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << cols[i];
            }
            oss << ") VALUES (";
            for (size_t i = 0; i < vals.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << vals[i];
            }
            oss << ")";
            statements.push_back(oss.str());
        }
        return statements;
    }
};

// ============================================================================
// SQL Executor Interface
// ============================================================================

/**
 * @brief Abstract SQL executor for migration framework
 */
class SqlExecutor {
public:
    virtual ~SqlExecutor() = default;
    virtual bool execute(const std::string& sql) = 0;
    virtual bool execute_batch(const std::vector<std::string>& statements) = 0;
    virtual bool begin_transaction() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;
    [[nodiscard]] virtual std::string last_error() const = 0;
};

/**
 * @brief In-memory SQL executor for testing/dry-run
 */
class DryRunExecutor : public SqlExecutor {
public:
    bool execute(const std::string& sql) override {
        executed_.push_back(sql);
        return true;
    }
    
    bool execute_batch(const std::vector<std::string>& statements) override {
        for (const auto& s : statements) executed_.push_back(s);
        return true;
    }
    
    bool begin_transaction() override { 
        executed_.push_back("BEGIN TRANSACTION"); 
        return true; 
    }
    
    bool commit() override { 
        executed_.push_back("COMMIT"); 
        return true; 
    }
    
    bool rollback() override { 
        executed_.push_back("ROLLBACK"); 
        return true; 
    }
    
    [[nodiscard]] std::string last_error() const override { return ""; }
    
    [[nodiscard]] const std::vector<std::string>& executed() const { return executed_; }
    
    void clear() { executed_.clear(); }

private:
    std::vector<std::string> executed_;
};

// ============================================================================
// Schema Builder (Fluent API)
// ============================================================================

/**
 * @brief Fluent API for building table schemas
 */
class SchemaBuilder {
public:
    SchemaBuilder& create_table(const std::string& name) {
        current_table_ = TableDef{};
        current_table_.name = name;
        return *this;
    }
    
    SchemaBuilder& id(const std::string& name = "id") {
        ColumnDef col;
        col.name = name;
        col.type = "INTEGER";
        col.primary_key = true;
        col.auto_increment = true;
        col.nullable = false;
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& text(const std::string& name, bool nullable = true) {
        ColumnDef col;
        col.name = name;
        col.type = "TEXT";
        col.nullable = nullable;
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& integer(const std::string& name, bool nullable = true) {
        ColumnDef col;
        col.name = name;
        col.type = "INTEGER";
        col.nullable = nullable;
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& real(const std::string& name, bool nullable = true) {
        ColumnDef col;
        col.name = name;
        col.type = "REAL";
        col.nullable = nullable;
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& timestamp(const std::string& name, 
                              const std::string& default_val = "CURRENT_TIMESTAMP") {
        ColumnDef col;
        col.name = name;
        col.type = "TIMESTAMP";
        col.default_value = default_val;
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& blob(const std::string& name) {
        ColumnDef col;
        col.name = name;
        col.type = "BLOB";
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& boolean(const std::string& name, bool default_val = false) {
        ColumnDef col;
        col.name = name;
        col.type = "INTEGER";
        col.default_value = default_val ? "1" : "0";
        col.check_constraint = name + " IN (0, 1)";
        current_table_.columns.push_back(col);
        return *this;
    }
    
    SchemaBuilder& unique(const std::string& col_name) {
        for (auto& col : current_table_.columns) {
            if (col.name == col_name) { col.unique = true; break; }
        }
        return *this;
    }
    
    SchemaBuilder& not_null(const std::string& col_name) {
        for (auto& col : current_table_.columns) {
            if (col.name == col_name) { col.nullable = false; break; }
        }
        return *this;
    }
    
    SchemaBuilder& foreign_key(const std::string& col, 
                                const std::string& ref_table,
                                const std::string& ref_col = "id") {
        for (auto& c : current_table_.columns) {
            if (c.name == col) {
                c.foreign_key_table = ref_table;
                c.foreign_key_column = ref_col;
                break;
            }
        }
        current_table_.constraints.push_back(
            "FOREIGN KEY(" + col + ") REFERENCES " + ref_table + "(" + ref_col + ")"
        );
        return *this;
    }
    
    SchemaBuilder& index(const std::string& name, 
                          const std::vector<std::string>& columns,
                          bool unique_idx = false) {
        IndexDef idx;
        idx.name = name;
        idx.table = current_table_.name;
        idx.columns = columns;
        idx.unique = unique_idx;
        current_table_.indices.push_back(idx);
        return *this;
    }
    
    [[nodiscard]] TableDef build() const { return current_table_; }
    
    [[nodiscard]] std::string build_up_sql() const {
        std::ostringstream oss;
        oss << current_table_.create_sql() << ";\n";
        for (const auto& idx : current_table_.indices) {
            oss << idx.to_sql() << ";\n";
        }
        return oss.str();
    }
    
    [[nodiscard]] std::string build_down_sql() const {
        return current_table_.drop_sql() + ";\n";
    }

private:
    TableDef current_table_;
};

// ============================================================================
// Migration Registry
// ============================================================================

/**
 * @brief Central migration registry and executor
 */
class MigrationManager {
public:
    MigrationManager() {
        register_core_migrations();
    }
    
    /**
     * @brief Register a migration
     */
    void register_migration(Migration migration) {
        std::lock_guard<std::mutex> lock(mutex_);
        migration.created_at = std::chrono::system_clock::now();
        migrations_[migration.id] = std::move(migration);
    }
    
    /**
     * @brief Get migration by ID
     */
    [[nodiscard]] std::optional<Migration> get_migration(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = migrations_.find(id);
        if (it == migrations_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief List all migrations
     */
    [[nodiscard]] std::vector<Migration> list_migrations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Migration> result;
        for (const auto& [id, m] : migrations_) result.push_back(m);
        std::sort(result.begin(), result.end(),
                  [](const Migration& a, const Migration& b) { return a.id < b.id; });
        return result;
    }
    
    /**
     * @brief Get pending migrations in dependency order
     */
    [[nodiscard]] std::vector<std::string> pending_migrations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> pending;
        for (const auto& [id, m] : migrations_) {
            if (m.status == MigrationStatus::Pending) {
                pending.push_back(id);
            }
        }
        // Topological sort by dependencies
        return topological_sort(pending);
    }
    
    /**
     * @brief Run all pending migrations
     */
    struct MigrationResult {
        int applied{0};
        int skipped{0};
        int failed{0};
        std::vector<std::string> applied_ids;
        std::vector<std::string> failed_ids;
        std::string error_message;
        
        [[nodiscard]] bool success() const { return failed == 0; }
    };
    
    MigrationResult migrate_up(SqlExecutor& executor, bool dry_run = false) {
        MigrationResult result;
        auto pending = pending_migrations();
        
        for (const auto& id : pending) {
            auto& migration = migrations_[id];
            
            // Check dependencies
            bool deps_met = true;
            for (const auto& dep : migration.dependencies) {
                auto it = migrations_.find(dep);
                if (it == migrations_.end() || it->second.status != MigrationStatus::Completed) {
                    deps_met = false;
                    break;
                }
            }
            
            if (!deps_met) {
                migration.status = MigrationStatus::Skipped;
                ++result.skipped;
                continue;
            }
            
            // Execute migration
            migration.status = MigrationStatus::Running;
            auto start = std::chrono::steady_clock::now();
            
            bool success = true;
            if (!dry_run) {
                executor.begin_transaction();
            }
            
            for (const auto& step : migration.steps) {
                if (!dry_run) {
                    if (!executor.execute(step.up_sql)) {
                        success = false;
                        migration.error_message = executor.last_error();
                        break;
                    }
                }
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start;
            migration.duration = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
            migration.executed_at = std::chrono::system_clock::now();
            
            if (success) {
                if (!dry_run) executor.commit();
                migration.status = MigrationStatus::Completed;
                ++result.applied;
                result.applied_ids.push_back(id);
                
                // Record history
                MigrationHistoryEntry entry;
                entry.migration_id = id;
                entry.direction = MigrationDirection::Up;
                entry.status = MigrationStatus::Completed;
                entry.executed_at = migration.executed_at;
                entry.duration = migration.duration;
                history_.push_back(entry);
            } else {
                if (!dry_run) executor.rollback();
                migration.status = MigrationStatus::Failed;
                ++result.failed;
                result.failed_ids.push_back(id);
                result.error_message = migration.error_message;
                break; // Stop on first failure
            }
        }
        
        return result;
    }
    
    /**
     * @brief Rollback last N migrations
     */
    MigrationResult migrate_down(SqlExecutor& executor, int count = 1) {
        MigrationResult result;
        
        // Get completed migrations in reverse order
        std::vector<std::string> completed;
        for (const auto& [id, m] : migrations_) {
            if (m.status == MigrationStatus::Completed) {
                completed.push_back(id);
            }
        }
        std::sort(completed.begin(), completed.end(), std::greater<>());
        
        int rolled = 0;
        for (const auto& id : completed) {
            if (rolled >= count) break;
            
            auto& migration = migrations_[id];
            if (!migration.reversible) {
                ++result.skipped;
                continue;
            }
            
            migration.status = MigrationStatus::Running;
            executor.begin_transaction();
            
            bool success = true;
            // Execute steps in reverse order
            for (auto it = migration.steps.rbegin(); it != migration.steps.rend(); ++it) {
                if (!it->down_sql.empty()) {
                    if (!executor.execute(it->down_sql)) {
                        success = false;
                        migration.error_message = executor.last_error();
                        break;
                    }
                }
            }
            
            if (success) {
                executor.commit();
                migration.status = MigrationStatus::RolledBack;
                ++result.applied;
                result.applied_ids.push_back(id);
                ++rolled;
                
                MigrationHistoryEntry entry;
                entry.migration_id = id;
                entry.direction = MigrationDirection::Down;
                entry.status = MigrationStatus::Completed;
                entry.executed_at = std::chrono::system_clock::now();
                history_.push_back(entry);
            } else {
                executor.rollback();
                migration.status = MigrationStatus::Failed;
                ++result.failed;
                result.failed_ids.push_back(id);
                result.error_message = migration.error_message;
                break;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Register seed data
     */
    void register_seed(SeedData seed) {
        std::lock_guard<std::mutex> lock(mutex_);
        seeds_[seed.id] = std::move(seed);
    }
    
    /**
     * @brief Run all seeds
     */
    int seed(SqlExecutor& executor) {
        int count = 0;
        for (const auto& [id, s] : seeds_) {
            auto statements = s.to_sql();
            for (const auto& sql : statements) {
                if (executor.execute(sql)) ++count;
            }
        }
        return count;
    }
    
    /**
     * @brief Get migration history
     */
    [[nodiscard]] std::vector<MigrationHistoryEntry> history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_;
    }
    
    /**
     * @brief Get current schema version
     */
    [[nodiscard]] std::string current_version() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string latest;
        for (const auto& [id, m] : migrations_) {
            if (m.status == MigrationStatus::Completed && m.version > latest) {
                latest = m.version;
            }
        }
        return latest.empty() ? "0.0.0" : latest;
    }
    
    /**
     * @brief Take schema snapshot
     */
    [[nodiscard]] SchemaSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        SchemaSnapshot snap;
        snap.version = current_version();
        snap.captured_at = std::chrono::system_clock::now();
        snap.tables = schema_tables_;
        return snap;
    }
    
    /**
     * @brief Compare two schema snapshots
     */
    struct SchemaDiff {
        std::vector<std::string> added_tables;
        std::vector<std::string> removed_tables;
        std::vector<std::string> modified_tables;
        int total_changes{0};
        
        [[nodiscard]] std::string format() const {
            std::ostringstream oss;
            oss << "Schema Diff: " << total_changes << " changes\n";
            for (const auto& t : added_tables) oss << "  + " << t << "\n";
            for (const auto& t : removed_tables) oss << "  - " << t << "\n";
            for (const auto& t : modified_tables) oss << "  ~ " << t << "\n";
            return oss.str();
        }
    };
    
    [[nodiscard]] static SchemaDiff compare(const SchemaSnapshot& before, 
                                              const SchemaSnapshot& after) {
        SchemaDiff diff;
        
        for (const auto& [name, table] : after.tables) {
            if (before.tables.find(name) == before.tables.end()) {
                diff.added_tables.push_back(name);
            }
        }
        
        for (const auto& [name, table] : before.tables) {
            if (after.tables.find(name) == after.tables.end()) {
                diff.removed_tables.push_back(name);
            } else {
                // Check column differences
                const auto& before_cols = table.columns;
                const auto& after_cols = after.tables.at(name).columns;
                if (before_cols.size() != after_cols.size()) {
                    diff.modified_tables.push_back(name);
                }
            }
        }
        
        diff.total_changes = static_cast<int>(
            diff.added_tables.size() + diff.removed_tables.size() + 
            diff.modified_tables.size());
        return diff;
    }
    
    /**
     * @brief Generate migration report
     */
    [[nodiscard]] std::string report() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "=== Migration Report ===\n";
        oss << "Current Version: " << current_version() << "\n";
        oss << "Total Migrations: " << migrations_.size() << "\n";
        
        int completed = 0, pending = 0, failed = 0;
        for (const auto& [id, m] : migrations_) {
            switch (m.status) {
                case MigrationStatus::Completed: ++completed; break;
                case MigrationStatus::Pending:   ++pending;   break;
                case MigrationStatus::Failed:    ++failed;    break;
                default: break;
            }
        }
        
        oss << "  Completed: " << completed << "\n";
        oss << "  Pending:   " << pending << "\n";
        oss << "  Failed:    " << failed << "\n\n";
        
        oss << "History (" << history_.size() << " entries):\n";
        for (const auto& entry : history_) {
            oss << "  " << entry.format() << "\n";
        }
        
        return oss.str();
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Migration> migrations_;
    std::map<std::string, SeedData> seeds_;
    std::vector<MigrationHistoryEntry> history_;
    std::map<std::string, TableDef> schema_tables_;
    
    /**
     * @brief Topological sort for dependency resolution
     */
    [[nodiscard]] std::vector<std::string> topological_sort(
        const std::vector<std::string>& ids) const {
        
        std::map<std::string, std::set<std::string>> deps;
        std::set<std::string> id_set(ids.begin(), ids.end());
        
        for (const auto& id : ids) {
            auto it = migrations_.find(id);
            if (it != migrations_.end()) {
                for (const auto& dep : it->second.dependencies) {
                    if (id_set.count(dep)) deps[id].insert(dep);
                }
            }
        }
        
        std::vector<std::string> sorted;
        std::set<std::string> visited;
        
        std::function<void(const std::string&)> visit = [&](const std::string& id) {
            if (visited.count(id)) return;
            visited.insert(id);
            for (const auto& dep : deps[id]) visit(dep);
            sorted.push_back(id);
        };
        
        for (const auto& id : ids) visit(id);
        return sorted;
    }
    
    /**
     * @brief Register core platform migrations
     */
    void register_core_migrations() {
        // Migration 001: Users table
        {
            Migration m;
            m.id = "20260101_001";
            m.version = "4.4.0";
            m.name = "create_users_table";
            m.description = "Core users table for authentication";
            
            SchemaBuilder sb;
            sb.create_table("users")
              .id()
              .text("username", false)
              .text("email", false)
              .text("password_hash", false)
              .text("role")
              .boolean("active", true)
              .timestamp("created_at")
              .timestamp("updated_at")
              .unique("username")
              .unique("email")
              .index("idx_users_email", {"email"}, true);
            
            MigrationStep step;
            step.description = "Create users table";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["users"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Migration 002: Portfolios table
        {
            Migration m;
            m.id = "20260101_002";
            m.version = "4.4.0";
            m.name = "create_portfolios_table";
            m.description = "Portfolio definitions";
            m.dependencies = {"20260101_001"};
            
            SchemaBuilder sb;
            sb.create_table("portfolios")
              .id()
              .text("name", false)
              .text("description")
              .integer("owner_id", false)
              .text("strategy")
              .text("benchmark")
              .real("inception_value")
              .boolean("active", true)
              .timestamp("created_at")
              .timestamp("updated_at")
              .foreign_key("owner_id", "users")
              .index("idx_portfolios_owner", {"owner_id"});
            
            MigrationStep step;
            step.description = "Create portfolios table";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["portfolios"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Migration 003: Positions table
        {
            Migration m;
            m.id = "20260101_003";
            m.version = "4.4.0";
            m.name = "create_positions_table";
            m.description = "Portfolio positions";
            m.dependencies = {"20260101_002"};
            
            SchemaBuilder sb;
            sb.create_table("positions")
              .id()
              .integer("portfolio_id", false)
              .text("symbol", false)
              .real("quantity", false)
              .real("cost_basis")
              .real("market_value")
              .text("asset_class")
              .timestamp("opened_at")
              .timestamp("updated_at")
              .foreign_key("portfolio_id", "portfolios")
              .index("idx_positions_portfolio", {"portfolio_id"})
              .index("idx_positions_symbol", {"symbol"});
            
            MigrationStep step;
            step.description = "Create positions table";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["positions"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Migration 004: Orders table
        {
            Migration m;
            m.id = "20260101_004";
            m.version = "4.4.0";
            m.name = "create_orders_table";
            m.description = "Trading orders";
            m.dependencies = {"20260101_002"};
            
            SchemaBuilder sb;
            sb.create_table("orders")
              .id()
              .integer("portfolio_id", false)
              .text("symbol", false)
              .text("side", false)
              .text("order_type", false)
              .real("quantity", false)
              .real("limit_price")
              .real("filled_quantity")
              .real("average_fill_price")
              .text("status")
              .text("broker")
              .text("broker_order_id")
              .timestamp("submitted_at")
              .timestamp("filled_at")
              .foreign_key("portfolio_id", "portfolios")
              .index("idx_orders_portfolio", {"portfolio_id"})
              .index("idx_orders_status", {"status"});
            
            MigrationStep step;
            step.description = "Create orders table";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["orders"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Migration 005: Audit log table
        {
            Migration m;
            m.id = "20260101_005";
            m.version = "4.4.0";
            m.name = "create_audit_log_table";
            m.description = "Audit trail for compliance";
            m.dependencies = {"20260101_001"};
            
            SchemaBuilder sb;
            sb.create_table("audit_log")
              .id()
              .integer("user_id")
              .text("action", false)
              .text("entity_type")
              .text("entity_id")
              .text("details")
              .text("ip_address")
              .timestamp("created_at")
              .index("idx_audit_user", {"user_id"})
              .index("idx_audit_action", {"action"})
              .index("idx_audit_entity", {"entity_type", "entity_id"});
            
            MigrationStep step;
            step.description = "Create audit log table";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["audit_log"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Migration 006: Market data cache table
        {
            Migration m;
            m.id = "20260101_006";
            m.version = "4.4.0";
            m.name = "create_market_data_cache";
            m.description = "Cached market data for offline use";
            
            SchemaBuilder sb;
            sb.create_table("market_data_cache")
              .id()
              .text("symbol", false)
              .text("data_type", false)
              .text("data_json")
              .real("last_price")
              .timestamp("data_date")
              .timestamp("cached_at")
              .index("idx_mdc_symbol_type", {"symbol", "data_type"}, true);
            
            MigrationStep step;
            step.description = "Create market data cache";
            step.up_sql = sb.build_up_sql();
            step.down_sql = sb.build_down_sql();
            m.steps.push_back(step);
            
            schema_tables_["market_data_cache"] = sb.build();
            migrations_[m.id] = m;
        }
        
        // Seed: Default admin user
        {
            SeedData seed;
            seed.id = "seed_admin_user";
            seed.table = "users";
            seed.description = "Default admin user";
            seed.rows.push_back({
                {"username", "admin"},
                {"email", "admin@genie.local"},
                {"password_hash", "CHANGE_ME"},
                {"role", "admin"},
                {"active", "1"}
            });
            seeds_[seed.id] = seed;
        }
    }
};

} // namespace migrations
} // namespace persistence
} // namespace genie

#endif // GENIE_PERSISTENCE_DB_MIGRATIONS_HPP
