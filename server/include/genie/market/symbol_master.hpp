/**
 * @file symbol_master.hpp
 * @brief Symbol master database with ticker/CUSIP/ISIN mapping
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides comprehensive security identification:
 * - Ticker symbol management
 * - CUSIP/ISIN/SEDOL identifiers
 * - Security classification
 * - Corporate action history (symbol changes)
 * - Exchange and market mapping
 */
#pragma once
#ifndef GENIE_MARKET_SYMBOL_MASTER_HPP
#define GENIE_MARKET_SYMBOL_MASTER_HPP

#include "../core/database.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <mutex>
#include <algorithm>
#include <sstream>

namespace genie::market {

/**
 * @brief Security/asset type
 */
enum class AssetType {
    Equity,         // Common stock
    ETF,            // Exchange-traded fund
    ETN,            // Exchange-traded note
    ADR,            // American Depositary Receipt
    REIT,           // Real Estate Investment Trust
    MLP,            // Master Limited Partnership
    PreferredStock, // Preferred shares
    Warrant,        // Warrants
    Right,          // Rights
    Unit,           // Units
    MutualFund,     // Mutual funds
    ClosedEndFund,  // Closed-end funds
    Option,         // Options
    Future,         // Futures
    Forex,          // Currency pairs
    Crypto,         // Cryptocurrency
    Bond,           // Fixed income
    Index,          // Market index
    Unknown
};

inline std::string asset_type_to_string(AssetType type) {
    switch (type) {
        case AssetType::Equity: return "equity";
        case AssetType::ETF: return "etf";
        case AssetType::ETN: return "etn";
        case AssetType::ADR: return "adr";
        case AssetType::REIT: return "reit";
        case AssetType::MLP: return "mlp";
        case AssetType::PreferredStock: return "preferred";
        case AssetType::Warrant: return "warrant";
        case AssetType::Right: return "right";
        case AssetType::Unit: return "unit";
        case AssetType::MutualFund: return "mutual_fund";
        case AssetType::ClosedEndFund: return "closed_end";
        case AssetType::Option: return "option";
        case AssetType::Future: return "future";
        case AssetType::Forex: return "forex";
        case AssetType::Crypto: return "crypto";
        case AssetType::Bond: return "bond";
        case AssetType::Index: return "index";
        default: return "unknown";
    }
}

inline AssetType string_to_asset_type(const std::string& s) {
    if (s == "equity") return AssetType::Equity;
    if (s == "etf") return AssetType::ETF;
    if (s == "etn") return AssetType::ETN;
    if (s == "adr") return AssetType::ADR;
    if (s == "reit") return AssetType::REIT;
    if (s == "mlp") return AssetType::MLP;
    if (s == "preferred") return AssetType::PreferredStock;
    if (s == "warrant") return AssetType::Warrant;
    if (s == "right") return AssetType::Right;
    if (s == "unit") return AssetType::Unit;
    if (s == "mutual_fund") return AssetType::MutualFund;
    if (s == "closed_end") return AssetType::ClosedEndFund;
    if (s == "option") return AssetType::Option;
    if (s == "future") return AssetType::Future;
    if (s == "forex") return AssetType::Forex;
    if (s == "crypto") return AssetType::Crypto;
    if (s == "bond") return AssetType::Bond;
    if (s == "index") return AssetType::Index;
    return AssetType::Unknown;
}

/**
 * @brief Exchange information
 */
struct Exchange {
    std::string mic;            // Market Identifier Code (ISO 10383)
    std::string name;
    std::string country;
    std::string timezone;
    std::string open_time;      // HH:MM
    std::string close_time;     // HH:MM
    std::string currency;
    bool is_active{true};
};

/**
 * @brief Complete security master record
 */
struct SecurityMaster {
    // Primary identifiers
    std::string symbol;         // Primary ticker
    std::string cusip;          // 9-character CUSIP
    std::string isin;           // 12-character ISIN
    std::string sedol;          // 7-character SEDOL
    std::string figi;           // 12-character FIGI
    std::string cik;            // SEC Central Index Key
    
    // Basic info
    std::string name;           // Company/security name
    std::string short_name;     // Abbreviated name
    AssetType asset_type{AssetType::Equity};
    
    // Exchange info
    std::string primary_exchange;   // Primary listing exchange (MIC)
    std::vector<std::string> exchanges; // All exchanges where traded
    std::string currency;
    std::string country;
    
    // Classification
    std::string sector;
    std::string industry;
    std::string sub_industry;
    std::string sic_code;       // Standard Industrial Classification
    std::string naics_code;     // North American Industry Classification
    
    // Status
    bool is_active{true};
    bool is_tradeable{true};
    std::string delisted_date;
    std::string ipo_date;
    
    // Metadata
    std::string created_at;
    std::string updated_at;
    std::string data_source;
};

/**
 * @brief Symbol change record (for corporate actions)
 */
struct SymbolChange {
    std::string old_symbol;
    std::string new_symbol;
    std::string effective_date;
    std::string reason;         // merger, name_change, spin_off, etc.
    std::string cusip;          // CUSIP if unchanged
};

/**
 * @brief Symbol search result
 */
struct SymbolSearchResult {
    std::string symbol;
    std::string name;
    AssetType asset_type;
    std::string exchange;
    std::string currency;
    double relevance_score{0};
};

/**
 * @brief Symbol master database
 */
class SymbolMaster {
public:
    explicit SymbolMaster(const std::string& db_path = "symbols.db")
        : db_path_(db_path) {
        initialize_database();
    }
    
    // === Security CRUD Operations ===
    
    /**
     * @brief Add or update security
     */
    bool upsert_security(const SecurityMaster& security) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string exchanges_str;
        for (size_t i = 0; i < security.exchanges.size(); ++i) {
            if (i > 0) exchanges_str += ",";
            exchanges_str += security.exchanges[i];
        }
        
        std::string sql = R"(
            INSERT OR REPLACE INTO securities 
            (symbol, cusip, isin, sedol, figi, cik, name, short_name, asset_type,
             primary_exchange, exchanges, currency, country, sector, industry,
             sub_industry, sic_code, naics_code, is_active, is_tradeable,
             delisted_date, ipo_date, data_source, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        )";
        
        return execute_sql(sql, {
            security.symbol, security.cusip, security.isin, security.sedol,
            security.figi, security.cik, security.name, security.short_name,
            asset_type_to_string(security.asset_type),
            security.primary_exchange, exchanges_str, security.currency,
            security.country, security.sector, security.industry,
            security.sub_industry, security.sic_code, security.naics_code,
            security.is_active ? "1" : "0", security.is_tradeable ? "1" : "0",
            security.delisted_date, security.ipo_date, security.data_source
        });
    }
    
    /**
     * @brief Get security by symbol
     */
    std::optional<SecurityMaster> get_by_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT * FROM securities WHERE symbol = ? OR symbol = ?",
            {symbol, to_upper(symbol)});
        
        if (rows.empty()) return std::nullopt;
        return row_to_security(rows[0]);
    }
    
    /**
     * @brief Get security by CUSIP
     */
    std::optional<SecurityMaster> get_by_cusip(const std::string& cusip) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql("SELECT * FROM securities WHERE cusip = ?", {cusip});
        if (rows.empty()) return std::nullopt;
        return row_to_security(rows[0]);
    }
    
    /**
     * @brief Get security by ISIN
     */
    std::optional<SecurityMaster> get_by_isin(const std::string& isin) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql("SELECT * FROM securities WHERE isin = ?", {isin});
        if (rows.empty()) return std::nullopt;
        return row_to_security(rows[0]);
    }
    
    /**
     * @brief Get security by SEDOL
     */
    std::optional<SecurityMaster> get_by_sedol(const std::string& sedol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql("SELECT * FROM securities WHERE sedol = ?", {sedol});
        if (rows.empty()) return std::nullopt;
        return row_to_security(rows[0]);
    }
    
    /**
     * @brief Resolve any identifier to symbol
     */
    std::optional<std::string> resolve_to_symbol(const std::string& identifier) {
        // Try as symbol first
        auto sec = get_by_symbol(identifier);
        if (sec) return sec->symbol;
        
        // Try as CUSIP (9 chars)
        if (identifier.length() == 9) {
            sec = get_by_cusip(identifier);
            if (sec) return sec->symbol;
        }
        
        // Try as ISIN (12 chars)
        if (identifier.length() == 12) {
            sec = get_by_isin(identifier);
            if (sec) return sec->symbol;
        }
        
        // Try as SEDOL (7 chars)
        if (identifier.length() == 7) {
            sec = get_by_sedol(identifier);
            if (sec) return sec->symbol;
        }
        
        return std::nullopt;
    }
    
    /**
     * @brief Delete security
     */
    bool delete_security(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        return execute_sql("DELETE FROM securities WHERE symbol = ?", {symbol});
    }
    
    // === Search Operations ===
    
    /**
     * @brief Search securities by name or symbol
     */
    std::vector<SymbolSearchResult> search(const std::string& query, int limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string search = "%" + query + "%";
        std::string exact = to_upper(query);
        
        auto rows = query_sql(R"(
            SELECT symbol, name, asset_type, primary_exchange, currency,
                   CASE 
                       WHEN symbol = ? THEN 100
                       WHEN symbol LIKE ? THEN 80
                       WHEN name LIKE ? THEN 60
                       ELSE 40
                   END as score
            FROM securities 
            WHERE is_active = 1 AND (symbol LIKE ? OR name LIKE ?)
            ORDER BY score DESC, symbol
            LIMIT ?
        )", {exact, exact + "%", search, search, search, std::to_string(limit)});
        
        std::vector<SymbolSearchResult> results;
        for (const auto& row : rows) {
            SymbolSearchResult r;
            r.symbol = row.at("symbol");
            r.name = row.at("name");
            r.asset_type = string_to_asset_type(row.at("asset_type"));
            r.exchange = row.at("primary_exchange");
            r.currency = row.at("currency");
            r.relevance_score = std::stod(row.at("score"));
            results.push_back(r);
        }
        
        return results;
    }
    
    /**
     * @brief List all symbols by asset type
     */
    std::vector<std::string> list_by_type(AssetType type) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT symbol FROM securities WHERE asset_type = ? AND is_active = 1 ORDER BY symbol",
            {asset_type_to_string(type)});
        
        std::vector<std::string> symbols;
        for (const auto& row : rows) {
            symbols.push_back(row.at("symbol"));
        }
        return symbols;
    }
    
    /**
     * @brief List all symbols by sector
     */
    std::vector<std::string> list_by_sector(const std::string& sector) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT symbol FROM securities WHERE sector = ? AND is_active = 1 ORDER BY symbol",
            {sector});
        
        std::vector<std::string> symbols;
        for (const auto& row : rows) {
            symbols.push_back(row.at("symbol"));
        }
        return symbols;
    }
    
    /**
     * @brief List all sectors
     */
    std::vector<std::string> list_sectors() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT DISTINCT sector FROM securities WHERE sector != '' AND is_active = 1 ORDER BY sector",
            {});
        
        std::vector<std::string> sectors;
        for (const auto& row : rows) {
            sectors.push_back(row.at("sector"));
        }
        return sectors;
    }
    
    /**
     * @brief List all symbols on exchange
     */
    std::vector<std::string> list_by_exchange(const std::string& exchange) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(
            "SELECT symbol FROM securities WHERE primary_exchange = ? AND is_active = 1 ORDER BY symbol",
            {exchange});
        
        std::vector<std::string> symbols;
        for (const auto& row : rows) {
            symbols.push_back(row.at("symbol"));
        }
        return symbols;
    }
    
    // === Symbol Change History ===
    
    /**
     * @brief Record symbol change
     */
    bool record_symbol_change(const SymbolChange& change) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return execute_sql(R"(
            INSERT INTO symbol_changes (old_symbol, new_symbol, effective_date, reason, cusip)
            VALUES (?, ?, ?, ?, ?)
        )", {change.old_symbol, change.new_symbol, change.effective_date,
             change.reason, change.cusip});
    }
    
    /**
     * @brief Get symbol history (all previous symbols)
     */
    std::vector<SymbolChange> get_symbol_history(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql(R"(
            SELECT old_symbol, new_symbol, effective_date, reason, cusip
            FROM symbol_changes 
            WHERE new_symbol = ? OR old_symbol = ?
            ORDER BY effective_date DESC
        )", {symbol, symbol});
        
        std::vector<SymbolChange> history;
        for (const auto& row : rows) {
            SymbolChange change;
            change.old_symbol = row.at("old_symbol");
            change.new_symbol = row.at("new_symbol");
            change.effective_date = row.at("effective_date");
            change.reason = row.at("reason");
            change.cusip = row.at("cusip");
            history.push_back(change);
        }
        return history;
    }
    
    /**
     * @brief Resolve historical symbol to current
     */
    std::optional<std::string> resolve_historical_symbol(const std::string& old_symbol,
                                                          const std::string& as_of_date = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string current = old_symbol;
        std::set<std::string> visited;
        
        // Follow the chain of symbol changes
        while (true) {
            if (visited.count(current)) break; // Cycle detection
            visited.insert(current);
            
            std::string sql = "SELECT new_symbol FROM symbol_changes WHERE old_symbol = ?";
            std::vector<std::string> params = {current};
            
            if (!as_of_date.empty()) {
                sql += " AND effective_date <= ?";
                params.push_back(as_of_date);
            }
            
            sql += " ORDER BY effective_date DESC LIMIT 1";
            
            auto rows = query_sql(sql, params);
            if (rows.empty()) break;
            
            current = rows[0].at("new_symbol");
        }
        
        // Verify the final symbol exists
        auto sec = get_by_symbol(current);
        if (sec) return current;
        
        return std::nullopt;
    }
    
    // === Exchange Operations ===
    
    /**
     * @brief Add exchange
     */
    bool add_exchange(const Exchange& exchange) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        return execute_sql(R"(
            INSERT OR REPLACE INTO exchanges 
            (mic, name, country, timezone, open_time, close_time, currency, is_active)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        )", {exchange.mic, exchange.name, exchange.country, exchange.timezone,
             exchange.open_time, exchange.close_time, exchange.currency,
             exchange.is_active ? "1" : "0"});
    }
    
    /**
     * @brief Get exchange by MIC
     */
    std::optional<Exchange> get_exchange(const std::string& mic) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql("SELECT * FROM exchanges WHERE mic = ?", {mic});
        if (rows.empty()) return std::nullopt;
        
        const auto& row = rows[0];
        Exchange exchange;
        exchange.mic = row.at("mic");
        exchange.name = row.at("name");
        exchange.country = row.at("country");
        exchange.timezone = row.at("timezone");
        exchange.open_time = row.at("open_time");
        exchange.close_time = row.at("close_time");
        exchange.currency = row.at("currency");
        exchange.is_active = row.at("is_active") == "1";
        return exchange;
    }
    
    /**
     * @brief List all exchanges
     */
    std::vector<Exchange> list_exchanges() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto rows = query_sql("SELECT * FROM exchanges WHERE is_active = 1 ORDER BY name", {});
        
        std::vector<Exchange> exchanges;
        for (const auto& row : rows) {
            Exchange exchange;
            exchange.mic = row.at("mic");
            exchange.name = row.at("name");
            exchange.country = row.at("country");
            exchange.timezone = row.at("timezone");
            exchange.open_time = row.at("open_time");
            exchange.close_time = row.at("close_time");
            exchange.currency = row.at("currency");
            exchange.is_active = row.at("is_active") == "1";
            exchanges.push_back(exchange);
        }
        return exchanges;
    }
    
    // === Bulk Operations ===
    
    /**
     * @brief Bulk import securities
     */
    int bulk_import(const std::vector<SecurityMaster>& securities) {
        int count = 0;
        execute_sql("BEGIN TRANSACTION");
        
        for (const auto& sec : securities) {
            if (upsert_security(sec)) count++;
        }
        
        execute_sql("COMMIT");
        return count;
    }
    
    /**
     * @brief Get statistics
     */
    std::map<std::string, int> get_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int> stats;
        
        auto rows = query_sql("SELECT COUNT(*) as cnt FROM securities", {});
        stats["total_securities"] = rows.empty() ? 0 : std::stoi(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(*) as cnt FROM securities WHERE is_active = 1", {});
        stats["active_securities"] = rows.empty() ? 0 : std::stoi(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(*) as cnt FROM symbol_changes", {});
        stats["symbol_changes"] = rows.empty() ? 0 : std::stoi(rows[0].at("cnt"));
        
        rows = query_sql("SELECT COUNT(*) as cnt FROM exchanges", {});
        stats["exchanges"] = rows.empty() ? 0 : std::stoi(rows[0].at("cnt"));
        
        return stats;
    }
    
    /**
     * @brief Initialize with common exchanges
     */
    void seed_exchanges() {
        std::vector<Exchange> common_exchanges = {
            {"XNYS", "New York Stock Exchange", "US", "America/New_York", "09:30", "16:00", "USD", true},
            {"XNAS", "NASDAQ", "US", "America/New_York", "09:30", "16:00", "USD", true},
            {"XASE", "NYSE American", "US", "America/New_York", "09:30", "16:00", "USD", true},
            {"ARCX", "NYSE Arca", "US", "America/New_York", "04:00", "20:00", "USD", true},
            {"BATS", "CBOE BZX Exchange", "US", "America/New_York", "09:30", "16:00", "USD", true},
            {"XLON", "London Stock Exchange", "GB", "Europe/London", "08:00", "16:30", "GBP", true},
            {"XPAR", "Euronext Paris", "FR", "Europe/Paris", "09:00", "17:30", "EUR", true},
            {"XETR", "Deutsche Boerse Xetra", "DE", "Europe/Berlin", "09:00", "17:30", "EUR", true},
            {"XTKS", "Tokyo Stock Exchange", "JP", "Asia/Tokyo", "09:00", "15:00", "JPY", true},
            {"XHKG", "Hong Kong Exchange", "HK", "Asia/Hong_Kong", "09:30", "16:00", "HKD", true},
            {"XSHG", "Shanghai Stock Exchange", "CN", "Asia/Shanghai", "09:30", "15:00", "CNY", true},
            {"XTOR", "Toronto Stock Exchange", "CA", "America/Toronto", "09:30", "16:00", "CAD", true}
        };
        
        for (const auto& exchange : common_exchanges) {
            add_exchange(exchange);
        }
    }

private:
    std::string db_path_;
    mutable std::mutex mutex_;
    
    void initialize_database() {
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS securities (
                symbol TEXT PRIMARY KEY,
                cusip TEXT,
                isin TEXT,
                sedol TEXT,
                figi TEXT,
                cik TEXT,
                name TEXT,
                short_name TEXT,
                asset_type TEXT DEFAULT 'equity',
                primary_exchange TEXT,
                exchanges TEXT,
                currency TEXT DEFAULT 'USD',
                country TEXT DEFAULT 'US',
                sector TEXT,
                industry TEXT,
                sub_industry TEXT,
                sic_code TEXT,
                naics_code TEXT,
                is_active INTEGER DEFAULT 1,
                is_tradeable INTEGER DEFAULT 1,
                delisted_date TEXT,
                ipo_date TEXT,
                data_source TEXT,
                created_at TEXT DEFAULT (datetime('now')),
                updated_at TEXT DEFAULT (datetime('now'))
            )
        )");
        
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS symbol_changes (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                old_symbol TEXT NOT NULL,
                new_symbol TEXT NOT NULL,
                effective_date TEXT NOT NULL,
                reason TEXT,
                cusip TEXT,
                created_at TEXT DEFAULT (datetime('now'))
            )
        )");
        
        execute_sql(R"(
            CREATE TABLE IF NOT EXISTS exchanges (
                mic TEXT PRIMARY KEY,
                name TEXT,
                country TEXT,
                timezone TEXT,
                open_time TEXT,
                close_time TEXT,
                currency TEXT,
                is_active INTEGER DEFAULT 1
            )
        )");
        
        // Indexes
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_cusip ON securities(cusip)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_isin ON securities(isin)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_sedol ON securities(sedol)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_name ON securities(name)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_sector ON securities(sector)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_securities_exchange ON securities(primary_exchange)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_symbol_changes_old ON symbol_changes(old_symbol)");
        execute_sql("CREATE INDEX IF NOT EXISTS idx_symbol_changes_new ON symbol_changes(new_symbol)");
        
        execute_sql("PRAGMA journal_mode=WAL");
    }
    
    SecurityMaster row_to_security(const std::map<std::string, std::string>& row) {
        SecurityMaster sec;
        sec.symbol = row.at("symbol");
        sec.cusip = row.at("cusip");
        sec.isin = row.at("isin");
        sec.sedol = row.at("sedol");
        sec.figi = row.at("figi");
        sec.cik = row.at("cik");
        sec.name = row.at("name");
        sec.short_name = row.at("short_name");
        sec.asset_type = string_to_asset_type(row.at("asset_type"));
        sec.primary_exchange = row.at("primary_exchange");
        sec.currency = row.at("currency");
        sec.country = row.at("country");
        sec.sector = row.at("sector");
        sec.industry = row.at("industry");
        sec.sub_industry = row.at("sub_industry");
        sec.sic_code = row.at("sic_code");
        sec.naics_code = row.at("naics_code");
        sec.is_active = row.at("is_active") == "1";
        sec.is_tradeable = row.at("is_tradeable") == "1";
        sec.delisted_date = row.at("delisted_date");
        sec.ipo_date = row.at("ipo_date");
        sec.data_source = row.at("data_source");
        sec.created_at = row.at("created_at");
        sec.updated_at = row.at("updated_at");
        
        // Parse exchanges list
        std::string exchanges_str = row.at("exchanges");
        if (!exchanges_str.empty()) {
            std::istringstream iss(exchanges_str);
            std::string exchange;
            while (std::getline(iss, exchange, ',')) {
                sec.exchanges.push_back(exchange);
            }
        }
        
        return sec;
    }
    
    std::string to_upper(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }
    
    bool execute_sql(const std::string& sql,
                     const std::vector<std::string>& params = {}) {
        sqlite3* db = nullptr;
        if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return false;
        }
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return false;
        }
        
        for (size_t i = 0; i < params.size(); ++i) {
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        
        return (rc == SQLITE_DONE || rc == SQLITE_ROW);
    }
    
    std::vector<std::map<std::string, std::string>> query_sql(
        const std::string& sql,
        const std::vector<std::string>& params) {
        
        std::vector<std::map<std::string, std::string>> result;
        
        sqlite3* db = nullptr;
        if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return result;
        }
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
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

#endif // GENIE_MARKET_SYMBOL_MASTER_HPP
