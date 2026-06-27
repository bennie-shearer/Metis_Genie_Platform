/**
 * @file price_store.hpp
 * @brief SQLite-based price storage with OHLCV schema
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides persistent storage for market data:
 * - Daily/intraday OHLCV price bars
 * - Adjusted prices with split/dividend factors
 * - Dividend and split history
 * - Corporate actions tracking
 * - Data quality metadata
 * 
 * Schema optimized for:
 * - Fast date-range queries
 * - Efficient bulk inserts
 * - Storage compression via WAL mode
 */
#pragma once
#ifndef GENIE_MARKET_PRICE_STORE_HPP
#define GENIE_MARKET_PRICE_STORE_HPP

#include "../core/database.hpp"
#include "alpha_vantage.hpp"  // For PriceBar, Quote structs
#include "yahoo_finance.hpp"  // For Dividend, Split structs
#include "trading_calendar.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief Price data source identifier
 */
enum class DataSource {
    Manual,
    AlphaVantage,
    YahooFinance,
    IEXCloud,
    Polygon,
    Broker,
    Calculated
};

inline std::string data_source_to_string(DataSource source) {
    switch (source) {
        case DataSource::Manual: return "manual";
        case DataSource::AlphaVantage: return "alpha_vantage";
        case DataSource::YahooFinance: return "yahoo_finance";
        case DataSource::IEXCloud: return "iex_cloud";
        case DataSource::Polygon: return "polygon";
        case DataSource::Broker: return "broker";
        case DataSource::Calculated: return "calculated";
    }
    return "unknown";
}

inline DataSource string_to_data_source(const std::string& s) {
    if (s == "manual") return DataSource::Manual;
    if (s == "alpha_vantage") return DataSource::AlphaVantage;
    if (s == "yahoo_finance") return DataSource::YahooFinance;
    if (s == "iex_cloud") return DataSource::IEXCloud;
    if (s == "polygon") return DataSource::Polygon;
    if (s == "broker") return DataSource::Broker;
    if (s == "calculated") return DataSource::Calculated;
    return DataSource::Manual;
}

/**
 * @brief Extended price bar with metadata
 */
struct StoredPriceBar {
    int64_t id{0};
    std::string symbol;
    std::string date;           // YYYY-MM-DD
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double adjusted_close{0};
    int64_t volume{0};
    double dividend{0};
    double split_factor{1.0};
    DataSource source{DataSource::Manual};
    std::string created_at;
    std::string updated_at;
    bool is_validated{false};
};

/**
 * @brief Symbol metadata
 */
struct SymbolInfo {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string asset_type;     // equity, etf, option, future, forex, crypto
    std::string currency;
    std::string sector;
    std::string industry;
    std::string cusip;
    std::string isin;
    std::string sedol;
    bool is_active{true};
    std::string first_trade_date;
    std::string last_update;
};

/**
 * @brief Price data quality statistics
 */
struct DataQualityStats {
    std::string symbol;
    int total_bars{0};
    int missing_days{0};
    int gaps_detected{0};
    int outliers_detected{0};
    double coverage_pct{0};
    std::string first_date;
    std::string last_date;
    std::string last_validated;
};

/**
 * @brief SQLite-based price storage
 */
class PriceStore {
public:
    explicit PriceStore(const std::string& db_path = "prices.db")
        : db_path_(db_path) {
        initialize_database();
    }
    
    // === Price Bar Operations ===
    
    /**
     * @brief Store a single price bar
     */
    bool store_price(const std::string& symbol, const PriceBar& bar,
                     DataSource source = DataSource::Manual) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = R"(
            INSERT OR REPLACE INTO daily_prices 
            (symbol, date, open, high, low, close, adjusted_close, volume, 
             dividend, split_factor, source, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        )";
        
        return execute_sql(sql, {
            symbol, bar.date,
            std::to_string(bar.open), std::to_string(bar.high),
            std::to_string(bar.low), std::to_string(bar.close),
            std::to_string(bar.adjusted_close), std::to_string(bar.volume),
            std::to_string(bar.dividend_amount), std::to_string(bar.split_coefficient),
            data_source_to_string(source)
        });
    }
    
    /**
     * @brief Store multiple price bars (bulk insert)
     */
    int store_prices(const std::string& symbol, const std::vector<PriceBar>& bars,
                     DataSource source = DataSource::Manual) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int count = 0;
        
        execute_sql("BEGIN TRANSACTION");
        
        for (const auto& bar : bars) {
            std::string sql = R"(
                INSERT OR REPLACE INTO daily_prices 
                (symbol, date, open, high, low, close, adjusted_close, volume, 
                 dividend, split_factor, source, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
            )";
            
            if (execute_sql(sql, {
                symbol, bar.date,
                std::to_string(bar.open), std::to_string(bar.high),
                std::to_string(bar.low), std::to_string(bar.close),
                std::to_string(bar.adjusted_close), std::to_string(bar.volume),
                std::to_string(bar.dividend_amount), std::to_string(bar.split_coefficient),
                data_source_to_string(source)
            })) {
                count++;
            }
        }
        
        execute_sql("COMMIT");
        
        return count;
    }
    
    /**
     * @brief Get price bars for date range
     */
    std::vector<StoredPriceBar> get_prices(const std::string& symbol,
                                           const std::string& start_date = "",
                                           const std::string& end_date = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StoredPriceBar> result;
        
        std::string sql = R"(
            SELECT id, symbol, date, open, high, low, close, adjusted_close,
                   volume, dividend, split_factor, source, created_at, updated_at, is_validated
            FROM daily_prices 
            WHERE symbol = ?
        )";
        
        std::vector<std::string> params = {symbol};
        
        if (!start_date.empty()) {
            sql += " AND date >= ?";
            params.push_back(start_date);
        }
        if (!end_date.empty()) {
            sql += " AND date <= ?";
            params.push_back(end_date);
        }
        
        sql += " ORDER BY date DESC";
        
        auto rows = query_sql(sql, params);
        for (const auto& row : rows) {
            StoredPriceBar bar;
            bar.id = std::stoll(row.at("id"));
            bar.symbol = row.at("symbol");
            bar.date = row.at("date");
            bar.open = std::stod(row.at("open"));
            bar.high = std::stod(row.at("high"));
            bar.low = std::stod(row.at("low"));
            bar.close = std::stod(row.at("close"));
            bar.adjusted_close = std::stod(row.at("adjusted_close"));
            bar.volume = std::stoll(row.at("volume"));
            bar.dividend = std::stod(row.at("dividend"));
            bar.split_factor = std::stod(row.at("split_factor"));
            bar.source = string_to_data_source(row.at("source"));
            bar.created_at = row.at("created_at");
            bar.updated_at = row.at("updated_at");
            bar.is_validated = row.at("is_validated") == "1";
            result.push_back(bar);
        }
        
        return result;
    }
    
    /**
     * @brief Get latest price for symbol
     */
    std::optional<StoredPriceBar> get_latest_price(const std::string& symbol) {
        auto prices = get_prices(symbol, "", "");
        if (prices.empty()) return std::nullopt;
        return prices.front();
    }
    
    /**
     * @brief Get closing prices only (for analytics)
     */
    std::vector<double> get_close_prices(const std::string& symbol,
                                         const std::string& start_date = "",
                                         const std::string& end_date = "",
                                         bool adjusted = true) {
        auto bars = get_prices(symbol, start_date, end_date);
        std::vector<double> prices;
        prices.reserve(bars.size());
        
        for (auto it = bars.rbegin(); it != bars.rend(); ++it) {
            prices.push_back(adjusted ? it->adjusted_close : it->close);
        }
        
        return prices;
    }
    
    /**
     * @brief Get returns from prices
     */
    std::vector<double> get_returns(const std::string& symbol,
                                    const std::string& start_date = "",
                                    const std::string& end_date = "",
                                    bool adjusted = true) {
        auto prices = get_close_prices(symbol, start_date, end_date, adjusted);
        std::vector<double> returns;
        
        if (prices.size() < 2) return returns;
        
        returns.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) {
            if (prices[i-1] > 0) {
                returns.push_back((prices[i] - prices[i-1]) / prices[i-1]);
            }
        }
        
        return returns;
    }
    
    /**
     * @brief Delete prices for symbol
     */
    bool delete_prices(const std::string& symbol,
                       const std::string& start_date = "",
                       const std::string& end_date = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = "DELETE FROM daily_prices WHERE symbol = ?";
        std::vector<std::string> params = {symbol};
        
        if (!start_date.empty()) {
            sql += " AND date >= ?";
            params.push_back(start_date);
        }
        if (!end_date.empty()) {
            sql += " AND date <= ?";
            params.push_back(end_date);
        }
        
        return execute_sql(sql, params);
    }
    
    // === Symbol Operations ===
    
    /**
     * @brief Store symbol info
     */
    bool store_symbol(const SymbolInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = R"(
            INSERT OR REPLACE INTO symbols 
            (symbol, name, exchange, asset_type, currency, sector, industry,
             cusip, isin, sedol, is_active, first_trade_date, last_update)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        )";
        
        return execute_sql(sql, {
            info.symbol, info.name, info.exchange, info.asset_type,
            info.currency, info.sector, info.industry,
            info.cusip, info.isin, info.sedol,
            info.is_active ? "1" : "0", info.first_trade_date
        });
    }
    
    /**
     * @brief Get symbol info
     */
    std::optional<SymbolInfo> get_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT * FROM symbols WHERE symbol = ?", {symbol});
        
        if (rows.empty()) return std::nullopt;
        
        const auto& row = rows[0];
        SymbolInfo info;
        info.symbol = row.at("symbol");
        info.name = row.at("name");
        info.exchange = row.at("exchange");
        info.asset_type = row.at("asset_type");
        info.currency = row.at("currency");
        info.sector = row.at("sector");
        info.industry = row.at("industry");
        info.cusip = row.at("cusip");
        info.isin = row.at("isin");
        info.sedol = row.at("sedol");
        info.is_active = row.at("is_active") == "1";
        info.first_trade_date = row.at("first_trade_date");
        info.last_update = row.at("last_update");
        
        return info;
    }
    
    /**
     * @brief Search symbols
     */
    std::vector<SymbolInfo> search_symbols(const std::string& query) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string search = "%" + query + "%";
        auto rows = query_sql(
            "SELECT * FROM symbols WHERE symbol LIKE ? OR name LIKE ? LIMIT 50",
            {search, search});
        
        std::vector<SymbolInfo> result;
        for (const auto& row : rows) {
            SymbolInfo info;
            info.symbol = row.at("symbol");
            info.name = row.at("name");
            info.exchange = row.at("exchange");
            info.asset_type = row.at("asset_type");
            info.currency = row.at("currency");
            info.is_active = row.at("is_active") == "1";
            result.push_back(info);
        }
        
        return result;
    }
    
    /**
     * @brief List all symbols with prices
     */
    std::vector<std::string> list_symbols() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT DISTINCT symbol FROM daily_prices ORDER BY symbol", {});
        
        std::vector<std::string> symbols;
        for (const auto& row : rows) {
            symbols.push_back(row.at("symbol"));
        }
        
        return symbols;
    }
    
    // === Dividend Operations ===
    
    /**
     * @brief Store dividend
     */
    bool store_dividend(const std::string& symbol, const Dividend& div) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return execute_sql(
            "INSERT OR REPLACE INTO dividends (symbol, ex_date, amount) VALUES (?, ?, ?)",
            {symbol, div.date, std::to_string(div.amount)});
    }
    
    /**
     * @brief Store multiple dividends
     */
    int store_dividends(const std::string& symbol, const std::vector<Dividend>& divs) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int count = 0;
        execute_sql("BEGIN TRANSACTION");
        
        for (const auto& div : divs) {
            if (execute_sql(
                "INSERT OR REPLACE INTO dividends (symbol, ex_date, amount) VALUES (?, ?, ?)",
                {symbol, div.date, std::to_string(div.amount)})) {
                count++;
            }
        }
        
        execute_sql("COMMIT");
        return count;
    }
    
    /**
     * @brief Get dividends
     */
    std::vector<Dividend> get_dividends(const std::string& symbol,
                                        const std::string& start_date = "",
                                        const std::string& end_date = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = "SELECT ex_date, amount FROM dividends WHERE symbol = ?";
        std::vector<std::string> params = {symbol};
        
        if (!start_date.empty()) {
            sql += " AND ex_date >= ?";
            params.push_back(start_date);
        }
        if (!end_date.empty()) {
            sql += " AND ex_date <= ?";
            params.push_back(end_date);
        }
        
        sql += " ORDER BY ex_date DESC";
        
        auto rows = query_sql(sql, params);
        std::vector<Dividend> result;
        
        for (const auto& row : rows) {
            Dividend div;
            div.date = row.at("ex_date");
            div.amount = std::stod(row.at("amount"));
            result.push_back(div);
        }
        
        return result;
    }
    
    // === Split Operations ===
    
    /**
     * @brief Store split
     */
    bool store_split(const std::string& symbol, const Split& split) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return execute_sql(
            "INSERT OR REPLACE INTO splits (symbol, date, ratio, factor) VALUES (?, ?, ?, ?)",
            {symbol, split.date, split.ratio, std::to_string(split.factor)});
    }
    
    /**
     * @brief Store multiple splits
     */
    int store_splits(const std::string& symbol, const std::vector<Split>& splits) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int count = 0;
        execute_sql("BEGIN TRANSACTION");
        
        for (const auto& split : splits) {
            if (execute_sql(
                "INSERT OR REPLACE INTO splits (symbol, date, ratio, factor) VALUES (?, ?, ?, ?)",
                {symbol, split.date, split.ratio, std::to_string(split.factor)})) {
                count++;
            }
        }
        
        execute_sql("COMMIT");
        return count;
    }
    
    /**
     * @brief Get splits
     */
    std::vector<Split> get_splits(const std::string& symbol,
                                  const std::string& start_date = "",
                                  const std::string& end_date = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string sql = "SELECT date, ratio, factor FROM splits WHERE symbol = ?";
        std::vector<std::string> params = {symbol};
        
        if (!start_date.empty()) {
            sql += " AND date >= ?";
            params.push_back(start_date);
        }
        if (!end_date.empty()) {
            sql += " AND date <= ?";
            params.push_back(end_date);
        }
        
        sql += " ORDER BY date DESC";
        
        auto rows = query_sql(sql, params);
        std::vector<Split> result;
        
        for (const auto& row : rows) {
            Split split;
            split.date = row.at("date");
            split.ratio = row.at("ratio");
            split.factor = std::stod(row.at("factor"));
            result.push_back(split);
        }
        
        return result;
    }
    
    // === Data Quality ===
    
    /**
     * @brief Get data quality statistics for symbol
     */
    DataQualityStats get_data_quality(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        DataQualityStats stats;
        stats.symbol = symbol;
        
        auto rows = query_sql(R"(
            SELECT 
                COUNT(*) as total,
                MIN(date) as first_date,
                MAX(date) as last_date,
                SUM(is_validated) as validated
            FROM daily_prices 
            WHERE symbol = ?
        )", {symbol});
        
        if (!rows.empty()) {
            stats.total_bars = std::stoi(rows[0].at("total"));
            stats.first_date = rows[0].at("first_date");
            stats.last_date = rows[0].at("last_date");
            
            // Calculate expected trading days using actual trading calendar
            if (!stats.first_date.empty() && !stats.last_date.empty()) {
                int expected = genie::market::trading_calendar().trading_days_between(
                    stats.first_date, stats.last_date);
                if (expected > 0) {
                    stats.coverage_pct = (stats.total_bars * 100.0) / expected;
                    stats.missing_days = expected - stats.total_bars;
                }
            }
        }
        
        return stats;
    }
    
    /**
     * @brief Mark price as validated
     */
    bool validate_price(const std::string& symbol, const std::string& date) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return execute_sql(
            "UPDATE daily_prices SET is_validated = 1 WHERE symbol = ? AND date = ?",
            {symbol, date});
    }
    
    // === Database Maintenance ===
    
    /**
     * @brief Vacuum database to reclaim space
     */
    void vacuum() {
        std::lock_guard<std::mutex> lock(mutex_);
        execute_sql("VACUUM");
    }
    
    /**
     * @brief Get database statistics
     */
    std::map<std::string, int64_t> get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int64_t> stats;
        
        auto rows = query_sql("SELECT COUNT(*) as cnt FROM daily_prices", {});
        stats["total_price_bars"] = rows.empty() ? 0 : std::stoll(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(DISTINCT symbol) as cnt FROM daily_prices", {});
        stats["total_symbols"] = rows.empty() ? 0 : std::stoll(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(*) as cnt FROM dividends", {});
        stats["total_dividends"] = rows.empty() ? 0 : std::stoll(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(*) as cnt FROM splits", {});
        stats["total_splits"] = rows.empty() ? 0 : std::stoll(rows[0].at("cnt"));
        
        return stats;
    }

private:
    std::string db_path_;
    mutable std::mutex mutex_;
    
    void initialize_database() {
        // Create tables
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS daily_prices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                symbol TEXT NOT NULL,
                date TEXT NOT NULL,
                open REAL NOT NULL,
                high REAL NOT NULL,
                low REAL NOT NULL,
                close REAL NOT NULL,
                adjusted_close REAL NOT NULL,
                volume INTEGER NOT NULL,
                dividend REAL DEFAULT 0,
                split_factor REAL DEFAULT 1.0,
                source TEXT DEFAULT 'manual',
                created_at TEXT DEFAULT (datetime('now')),
                updated_at TEXT DEFAULT (datetime('now')),
                is_validated INTEGER DEFAULT 0,
                UNIQUE(symbol, date)
            )
        )");
        
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS symbols (
                symbol TEXT PRIMARY KEY,
                name TEXT,
                exchange TEXT,
                asset_type TEXT DEFAULT 'equity',
                currency TEXT DEFAULT 'USD',
                sector TEXT,
                industry TEXT,
                cusip TEXT,
                isin TEXT,
                sedol TEXT,
                is_active INTEGER DEFAULT 1,
                first_trade_date TEXT,
                last_update TEXT
            )
        )");
        
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS dividends (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                symbol TEXT NOT NULL,
                ex_date TEXT NOT NULL,
                amount REAL NOT NULL,
                pay_date TEXT,
                record_date TEXT,
                declared_date TEXT,
                UNIQUE(symbol, ex_date)
            )
        )");
        
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS splits (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                symbol TEXT NOT NULL,
                date TEXT NOT NULL,
                ratio TEXT NOT NULL,
                factor REAL NOT NULL,
                UNIQUE(symbol, date)
            )
        )");
        
        // Create indexes for performance
        execute_sql("CREATE INDEX IF NOT EXISTS idx_prices_symbol_date ON daily_prices(symbol, date)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_prices_date ON daily_prices(date)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_dividends_symbol ON dividends(symbol)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_splits_symbol ON splits(symbol)");
        
        // Enable WAL mode for better concurrency
        execute_sql("PRAGMA journal_mode=WAL");
        execute_sql("PRAGMA synchronous=NORMAL");
    }
    
    bool execute_sql(const std::string& sql,
                     const std::vector<std::string>& params = {}) {
        // Using core::Database would be ideal, but for simplicity
        // we use sqlite3 directly here
        sqlite3* db = nullptr;
        int rc = sqlite3_open(db_path_.c_str(), &db);
        if (rc != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return false;
        }
        
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close(db);
            return false;
        }
        
        for (size_t i = 0; i < params.size(); ++i) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        
        return (rc == SQLITE_DONE || rc == SQLITE_ROW);
    }
    
    std::vector<std::map<std::string, std::string>> query_sql(
        const std::string& sql,
        const std::vector<std::string>& params) const {
        
        std::vector<std::map<std::string, std::string>> result;
        
        sqlite3* db = nullptr;
        int rc = sqlite3_open(db_path_.c_str(), &db);
        if (rc != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return result;
        }
        
        sqlite3_stmt* stmt = nullptr;
        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_close(db);
            return result;
        }
        
        for (size_t i = 0; i < params.size(); ++i) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        
        int cols = sqlite3_column_count(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::map<std::string, std::string> row;
            for (int i = 0; i < cols; ++i) {
                const char* name = sqlite3_column_name(stmt, i);
                const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row[name] = val ? val : "";
            }
            result.push_back(row);
        }
        
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        
        return result;
    }
};

} // namespace genie::market

#endif // GENIE_MARKET_PRICE_STORE_HPP
