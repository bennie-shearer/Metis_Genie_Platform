/**
 * @file query_optimizer.hpp
 * @brief Database query optimization with indexes
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Performance - Database query optimization indexes
 */

#ifndef GENIE_PERFORMANCE_QUERY_OPTIMIZER_HPP
#define GENIE_PERFORMANCE_QUERY_OPTIMIZER_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <mutex>
#include <memory>
#include <sstream>
#include <functional>
#include <algorithm>

namespace genie {
namespace performance {

/**
 * @brief Index definition
 */
struct IndexDef {
    std::string name;
    std::string table;
    std::vector<std::string> columns;
    bool unique{false};
    bool partial{false};
    std::string where_clause;
    std::string index_type{"btree"};  // btree, hash, gin, gist
    
    std::string to_sql() const {
        std::ostringstream sql;
        sql << "CREATE ";
        if (unique) sql << "UNIQUE ";
        sql << "INDEX IF NOT EXISTS " << name;
        sql << " ON " << table << " (";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << columns[i];
        }
        sql << ")";
        if (partial && !where_clause.empty()) {
            sql << " WHERE " << where_clause;
        }
        return sql.str();
    }
};

/**
 * @brief Query statistics
 */
struct QueryStats {
    std::string query_hash;
    std::string query_pattern;
    int64_t execution_count{0};
    double total_time_ms{0.0};
    double avg_time_ms{0.0};
    double min_time_ms{0.0};
    double max_time_ms{0.0};
    int64_t rows_returned{0};
    bool uses_index{false};
    std::chrono::system_clock::time_point last_executed;
};

/**
 * @brief Query plan analysis
 */
struct QueryPlan {
    std::string operation;
    std::string table;
    std::string index_used;
    double estimated_cost{0.0};
    int64_t estimated_rows{0};
    std::vector<std::shared_ptr<QueryPlan>> children;
    std::map<std::string, std::string> details;
    
    bool is_seq_scan() const {
        return operation == "Seq Scan" || operation == "TABLE SCAN";
    }
    
    bool is_index_scan() const {
        return operation.find("Index") != std::string::npos;
    }
};

/**
 * @brief Database schema analyzer
 */
class SchemaAnalyzer {
public:
    /**
     * @brief Get recommended indexes for common queries
     */
    static std::vector<IndexDef> get_recommended_indexes() {
        std::vector<IndexDef> indexes;
        
        // Portfolio positions indexes
        indexes.push_back({"idx_positions_portfolio", "positions", 
            {"portfolio_id"}, false, false, ""});
        indexes.push_back({"idx_positions_symbol", "positions", 
            {"symbol"}, false, false, ""});
        indexes.push_back({"idx_positions_portfolio_symbol", "positions", 
            {"portfolio_id", "symbol"}, true, false, ""});
        
        // Orders indexes
        indexes.push_back({"idx_orders_user", "orders", 
            {"user_id"}, false, false, ""});
        indexes.push_back({"idx_orders_status", "orders", 
            {"status"}, false, false, ""});
        indexes.push_back({"idx_orders_symbol", "orders", 
            {"symbol"}, false, false, ""});
        indexes.push_back({"idx_orders_created", "orders", 
            {"created_at"}, false, false, ""});
        indexes.push_back({"idx_orders_user_status", "orders", 
            {"user_id", "status"}, false, false, ""});
        indexes.push_back({"idx_orders_pending", "orders", 
            {"user_id", "created_at"}, false, true, "status = 'pending'"});
        
        // Trades/executions indexes
        indexes.push_back({"idx_trades_order", "trades", 
            {"order_id"}, false, false, ""});
        indexes.push_back({"idx_trades_symbol", "trades", 
            {"symbol"}, false, false, ""});
        indexes.push_back({"idx_trades_timestamp", "trades", 
            {"executed_at"}, false, false, ""});
        indexes.push_back({"idx_trades_user_date", "trades", 
            {"user_id", "executed_at"}, false, false, ""});
        
        // Price history indexes
        indexes.push_back({"idx_prices_symbol", "price_history", 
            {"symbol"}, false, false, ""});
        indexes.push_back({"idx_prices_timestamp", "price_history", 
            {"timestamp"}, false, false, ""});
        indexes.push_back({"idx_prices_symbol_time", "price_history", 
            {"symbol", "timestamp"}, false, false, ""});
        
        // User/session indexes
        indexes.push_back({"idx_sessions_user", "sessions", 
            {"user_id"}, false, false, ""});
        indexes.push_back({"idx_sessions_expires", "sessions", 
            {"expires_at"}, false, false, ""});
        indexes.push_back({"idx_sessions_active", "sessions", 
            {"user_id", "expires_at"}, false, true, "is_active = 1"});
        
        // Audit log indexes
        indexes.push_back({"idx_audit_user", "audit_log", 
            {"user_id"}, false, false, ""});
        indexes.push_back({"idx_audit_type", "audit_log", 
            {"event_type"}, false, false, ""});
        indexes.push_back({"idx_audit_timestamp", "audit_log", 
            {"timestamp"}, false, false, ""});
        indexes.push_back({"idx_audit_user_time", "audit_log", 
            {"user_id", "timestamp"}, false, false, ""});
        
        // Alerts indexes
        indexes.push_back({"idx_alerts_user", "alerts", 
            {"user_id"}, false, false, ""});
        indexes.push_back({"idx_alerts_symbol", "alerts", 
            {"symbol"}, false, false, ""});
        indexes.push_back({"idx_alerts_active", "alerts", 
            {"user_id"}, false, true, "is_active = 1"});
        
        // Watchlist indexes
        indexes.push_back({"idx_watchlist_user", "watchlist", 
            {"user_id"}, false, false, ""});
        indexes.push_back({"idx_watchlist_user_symbol", "watchlist", 
            {"user_id", "symbol"}, true, false, ""});
        
        return indexes;
    }
    
    /**
     * @brief Generate SQL for all indexes
     */
    static std::string generate_index_sql() {
        std::ostringstream sql;
        auto indexes = get_recommended_indexes();
        
        for (const auto& idx : indexes) {
            sql << idx.to_sql() << ";\n";
        }
        
        return sql.str();
    }
};

/**
 * @brief Query optimizer
 */
class QueryOptimizer {
public:
    /**
     * @brief Analyze query and suggest optimizations
     */
    static std::vector<std::string> analyze_query(const std::string& query) {
        std::vector<std::string> suggestions;
        std::string upper_query = to_upper(query);
        
        // Check for SELECT *
        if (upper_query.find("SELECT *") != std::string::npos) {
            suggestions.push_back("Avoid SELECT * - specify only needed columns");
        }
        
        // Check for missing WHERE clause
        if (upper_query.find("SELECT") != std::string::npos &&
            upper_query.find("WHERE") == std::string::npos &&
            upper_query.find("LIMIT") == std::string::npos) {
            suggestions.push_back("Query lacks WHERE clause - may return large result set");
        }
        
        // Check for LIKE with leading wildcard
        if (upper_query.find("LIKE '%") != std::string::npos) {
            suggestions.push_back("LIKE with leading wildcard prevents index usage");
        }
        
        // Check for OR conditions
        size_t or_count = 0;
        size_t pos = 0;
        while ((pos = upper_query.find(" OR ", pos)) != std::string::npos) {
            or_count++;
            pos += 4;
        }
        if (or_count > 2) {
            suggestions.push_back("Multiple OR conditions may prevent index usage - consider UNION");
        }
        
        // Check for functions on indexed columns
        if (upper_query.find("WHERE") != std::string::npos) {
            std::vector<std::string> functions = {"UPPER(", "LOWER(", "DATE(", "YEAR(", "MONTH("};
            for (const auto& func : functions) {
                if (upper_query.find(func) != std::string::npos) {
                    suggestions.push_back("Function on column in WHERE clause prevents index usage");
                    break;
                }
            }
        }
        
        // Check for ORDER BY without index
        if (upper_query.find("ORDER BY") != std::string::npos &&
            upper_query.find("LIMIT") == std::string::npos) {
            suggestions.push_back("ORDER BY without LIMIT may cause full table sort");
        }
        
        // Check for subqueries that could be joins
        if (upper_query.find("WHERE") != std::string::npos &&
            upper_query.find("SELECT", upper_query.find("WHERE")) != std::string::npos) {
            suggestions.push_back("Consider converting subquery to JOIN for better performance");
        }
        
        // Check for missing LIMIT
        if (upper_query.find("SELECT") != std::string::npos &&
            upper_query.find("LIMIT") == std::string::npos &&
            upper_query.find("COUNT(") == std::string::npos) {
            suggestions.push_back("Consider adding LIMIT to prevent unbounded result sets");
        }
        
        return suggestions;
    }
    
    /**
     * @brief Rewrite query for optimization
     */
    static std::string optimize_query(const std::string& query) {
        std::string optimized = query;
        
        // Add LIMIT if missing and it's a SELECT
        std::string upper = to_upper(query);
        if (upper.find("SELECT") != std::string::npos &&
            upper.find("LIMIT") == std::string::npos &&
            upper.find("COUNT(") == std::string::npos &&
            upper.find("INSERT") == std::string::npos &&
            upper.find("UPDATE") == std::string::npos &&
            upper.find("DELETE") == std::string::npos) {
            // Don't add LIMIT to queries with aggregates
            if (upper.find("GROUP BY") == std::string::npos) {
                optimized += " LIMIT 1000";
            }
        }
        
        return optimized;
    }
    
    /**
     * @brief Generate parameterized query
     */
    static std::pair<std::string, std::vector<std::string>> parameterize(
            const std::string& query) {
        std::string parameterized;
        std::vector<std::string> params;
        
        bool in_string = false;
        char string_char = 0;
        std::string current_param;
        int param_index = 1;
        
        for (size_t i = 0; i < query.size(); ++i) {
            char c = query[i];
            
            if (!in_string && (c == '\'' || c == '"')) {
                in_string = true;
                string_char = c;
            } else if (in_string && c == string_char) {
                // Check for escaped quote
                if (i + 1 < query.size() && query[i + 1] == string_char) {
                    current_param += c;
                    current_param += query[++i];
                    continue;
                }
                in_string = false;
                params.push_back(current_param);
                current_param.clear();
                parameterized += "?";
                param_index++;
                continue;
            }
            
            if (in_string) {
                current_param += c;
            } else {
                parameterized += c;
            }
        }
        
        return {parameterized, params};
    }

private:
    static std::string to_upper(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return result;
    }
};

/**
 * @brief Query statistics collector
 */
class QueryStatsCollector {
public:
    /**
     * @brief Record query execution
     */
    void record_query(const std::string& query, double execution_time_ms,
                      int64_t rows_returned, bool used_index = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string hash = hash_query(query);
        auto& stats = query_stats_[hash];
        
        stats.query_hash = hash;
        stats.query_pattern = normalize_query(query);
        stats.execution_count++;
        stats.total_time_ms += execution_time_ms;
        stats.avg_time_ms = stats.total_time_ms / stats.execution_count;
        stats.rows_returned += rows_returned;
        stats.uses_index = used_index;
        stats.last_executed = std::chrono::system_clock::now();
        
        if (stats.execution_count == 1 || execution_time_ms < stats.min_time_ms) {
            stats.min_time_ms = execution_time_ms;
        }
        if (execution_time_ms > stats.max_time_ms) {
            stats.max_time_ms = execution_time_ms;
        }
    }
    
    /**
     * @brief Get slow queries
     */
    std::vector<QueryStats> get_slow_queries(double threshold_ms = 100.0,
                                              int limit = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<QueryStats> slow;
        for (const auto& [hash, stats] : query_stats_) {
            if (stats.avg_time_ms >= threshold_ms) {
                slow.push_back(stats);
            }
        }
        
        std::sort(slow.begin(), slow.end(),
                  [](const QueryStats& a, const QueryStats& b) {
                      return a.total_time_ms > b.total_time_ms;
                  });
        
        if (slow.size() > static_cast<size_t>(limit)) {
            slow.resize(limit);
        }
        
        return slow;
    }
    
    /**
     * @brief Get most frequent queries
     */
    std::vector<QueryStats> get_frequent_queries(int limit = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<QueryStats> frequent;
        for (const auto& [hash, stats] : query_stats_) {
            frequent.push_back(stats);
        }
        
        std::sort(frequent.begin(), frequent.end(),
                  [](const QueryStats& a, const QueryStats& b) {
                      return a.execution_count > b.execution_count;
                  });
        
        if (frequent.size() > static_cast<size_t>(limit)) {
            frequent.resize(limit);
        }
        
        return frequent;
    }
    
    /**
     * @brief Get queries not using indexes
     */
    std::vector<QueryStats> get_unindexed_queries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<QueryStats> unindexed;
        for (const auto& [hash, stats] : query_stats_) {
            if (!stats.uses_index && stats.execution_count > 10) {
                unindexed.push_back(stats);
            }
        }
        
        std::sort(unindexed.begin(), unindexed.end(),
                  [](const QueryStats& a, const QueryStats& b) {
                      return a.total_time_ms > b.total_time_ms;
                  });
        
        return unindexed;
    }
    
    /**
     * @brief Get statistics summary
     */
    std::map<std::string, double> get_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, double> summary;
        
        int64_t total_queries = 0;
        double total_time = 0.0;
        int64_t indexed_queries = 0;
        
        for (const auto& [hash, stats] : query_stats_) {
            total_queries += stats.execution_count;
            total_time += stats.total_time_ms;
            if (stats.uses_index) {
                indexed_queries += stats.execution_count;
            }
        }
        
        summary["total_queries"] = static_cast<double>(total_queries);
        summary["total_time_ms"] = total_time;
        summary["unique_queries"] = static_cast<double>(query_stats_.size());
        summary["avg_time_ms"] = total_queries > 0 ? total_time / total_queries : 0;
        summary["index_usage_pct"] = total_queries > 0 ? 
            100.0 * indexed_queries / total_queries : 0;
        
        return summary;
    }
    
    /**
     * @brief Clear statistics
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        query_stats_.clear();
    }

private:
    std::map<std::string, QueryStats> query_stats_;
    mutable std::mutex mutex_;
    
    static std::string hash_query(const std::string& query) {
        // Simple hash - replace literals with placeholders
        std::string normalized = normalize_query(query);
        
        uint64_t hash = 0xcbf29ce484222325ULL;
        for (char c : normalized) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x100000001b3ULL;
        }
        
        std::ostringstream ss;
        ss << std::hex << hash;
        return ss.str();
    }
    
    static std::string normalize_query(const std::string& query) {
        std::string result;
        bool in_string = false;
        bool in_number = false;
        char string_char = 0;
        
        for (size_t i = 0; i < query.size(); ++i) {
            char c = query[i];
            
            if (!in_string && (c == '\'' || c == '"')) {
                in_string = true;
                string_char = c;
                result += "?";
            } else if (in_string && c == string_char) {
                in_string = false;
            } else if (!in_string && !in_number && std::isdigit(static_cast<unsigned char>(c))) {
                in_number = true;
                result += "?";
            } else if (in_number && !std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
                in_number = false;
                result += c;
            } else if (!in_string && !in_number) {
                result += std::toupper(static_cast<unsigned char>(c));
            }
        }
        
        return result;
    }
};

/**
 * @brief Connection pool for database
 */
class ConnectionPool {
public:
    struct Config {
        std::string connection_string;
        int min_connections{2};
        int max_connections{10};
        std::chrono::seconds idle_timeout{300};
        std::chrono::seconds connection_timeout{30};
        bool validate_on_borrow{true};
    };
    
    explicit ConnectionPool(const Config& config) 
        : config_(config), active_count_(0), total_created_(0) {}
    
    /**
     * @brief Get connection from pool
     */
    template<typename Connection>
    std::shared_ptr<Connection> get_connection() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait for available connection
        if (!cv_.wait_for(lock, config_.connection_timeout,
                [this]() { return !available_.empty() || 
                           active_count_ < config_.max_connections; })) {
            throw std::runtime_error("Connection pool timeout");
        }
        
        std::shared_ptr<Connection> conn;
        
        if (!available_.empty()) {
            // Reuse existing connection
            conn = std::static_pointer_cast<Connection>(available_.front());
            available_.pop_front();
        } else {
            // Create new connection
            conn = std::make_shared<Connection>();
            total_created_++;
        }
        
        active_count_++;
        return conn;
    }
    
    /**
     * @brief Return connection to pool
     */
    template<typename Connection>
    void return_connection(std::shared_ptr<Connection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        active_count_--;
        
        if (static_cast<int>(available_.size()) < config_.max_connections) {
            available_.push_back(conn);
        }
        
        cv_.notify_one();
    }
    
    /**
     * @brief Get pool statistics
     */
    std::map<std::string, int> get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            {"active", active_count_},
            {"available", static_cast<int>(available_.size())},
            {"total_created", total_created_},
            {"max_connections", config_.max_connections}
        };
    }

private:
    Config config_;
    std::list<std::shared_ptr<void>> available_;
    int active_count_;
    int total_created_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_QUERY_OPTIMIZER_HPP
