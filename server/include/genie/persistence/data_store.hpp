/**
 * @file data_store.hpp
 * @brief Persistent data store for Metis Genie Platform domain objects
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Maps domain objects to SQLite tables:
 *   - securities:   Asset universe with metadata
 *   - portfolios:   Portfolio configuration and cash balances
 *   - positions:    Current and historical holdings
 *   - orders:       Order lifecycle (New -> Filled/Cancelled)
 *   - trades:       Executed trades with fills
 *   - market_data:  Price history (OHLCV)
 *   - config:       Key-value configuration store
 *   - audit_log:    Change tracking
 *
 * All timestamps stored as ISO-8601 text.
 * Schema auto-creates via SchemaManager on first open.
 *
 * Link with -lsqlite3
 */
#pragma once
#ifndef GENIE_PERSISTENCE_DATA_STORE_HPP
#define GENIE_PERSISTENCE_DATA_STORE_HPP

#include "../core/database.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::persistence {

using namespace genie::db;

// =========================================================================
// Schema Definition
// =========================================================================

static const int SCHEMA_VERSION = 1;

static const std::vector<std::string> MIGRATIONS = {
    // v1: Initial schema
    R"SQL(
    CREATE TABLE IF NOT EXISTS securities (
        id          TEXT PRIMARY KEY,
        name        TEXT NOT NULL,
        asset_class TEXT NOT NULL DEFAULT 'Equity',
        exchange    TEXT DEFAULT '',
        sector      TEXT DEFAULT '',
        currency    TEXT DEFAULT 'USD',
        created_at  TEXT NOT NULL DEFAULT (datetime('now')),
        updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS users (
        user_id       TEXT PRIMARY KEY,
        username      TEXT NOT NULL UNIQUE,
        display_name  TEXT NOT NULL DEFAULT '',
        email         TEXT NOT NULL DEFAULT '',
        role          TEXT NOT NULL DEFAULT 'Viewer',
        active        INTEGER NOT NULL DEFAULT 1,
        created_at    TEXT NOT NULL DEFAULT (datetime('now')),
        last_login    TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS portfolios (
        id            TEXT PRIMARY KEY,
        name          TEXT NOT NULL,
        base_currency TEXT NOT NULL DEFAULT 'USD',
        cash_balance  REAL NOT NULL DEFAULT 0.0,
        status        TEXT NOT NULL DEFAULT 'Active',
        owner_id      TEXT NOT NULL DEFAULT '',
        created_at    TEXT NOT NULL DEFAULT (datetime('now')),
        updated_at    TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS portfolio_grants (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        portfolio_id  TEXT NOT NULL REFERENCES portfolios(id),
        user_id       TEXT NOT NULL REFERENCES users(user_id),
        access_level  TEXT NOT NULL DEFAULT 'ReadOnly',
        granted_by    TEXT NOT NULL DEFAULT '',
        granted_at    TEXT NOT NULL DEFAULT (datetime('now')),
        UNIQUE(portfolio_id, user_id)
    );

    CREATE TABLE IF NOT EXISTS positions (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        portfolio_id  TEXT NOT NULL REFERENCES portfolios(id),
        security_id   TEXT NOT NULL REFERENCES securities(id),
        quantity      REAL NOT NULL DEFAULT 0.0,
        avg_cost      REAL NOT NULL DEFAULT 0.0,
        market_value  REAL NOT NULL DEFAULT 0.0,
        unrealized_pnl REAL NOT NULL DEFAULT 0.0,
        opened_at     TEXT NOT NULL DEFAULT (datetime('now')),
        closed_at     TEXT,
        UNIQUE(portfolio_id, security_id)
    );
    CREATE INDEX IF NOT EXISTS idx_positions_portfolio ON positions(portfolio_id);
    CREATE INDEX IF NOT EXISTS idx_positions_security ON positions(security_id);

    CREATE TABLE IF NOT EXISTS orders (
        id            TEXT PRIMARY KEY,
        portfolio_id  TEXT NOT NULL REFERENCES portfolios(id),
        security_id   TEXT NOT NULL,
        side          TEXT NOT NULL CHECK(side IN ('Buy','Sell')),
        order_type    TEXT NOT NULL DEFAULT 'Market',
        quantity      REAL NOT NULL,
        limit_price   REAL,
        status        TEXT NOT NULL DEFAULT 'New',
        avg_fill_price REAL DEFAULT 0.0,
        filled_qty    REAL DEFAULT 0.0,
        created_at    TEXT NOT NULL DEFAULT (datetime('now')),
        filled_at     TEXT,
        cancelled_at  TEXT
    );
    CREATE INDEX IF NOT EXISTS idx_orders_portfolio ON orders(portfolio_id);
    CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);

    CREATE TABLE IF NOT EXISTS trades (
        id            TEXT PRIMARY KEY,
        order_id      TEXT REFERENCES orders(id),
        portfolio_id  TEXT NOT NULL REFERENCES portfolios(id),
        security_id   TEXT NOT NULL,
        side          TEXT NOT NULL CHECK(side IN ('Buy','Sell')),
        quantity      REAL NOT NULL,
        price         REAL NOT NULL,
        commission    REAL NOT NULL DEFAULT 0.0,
        executed_at   TEXT NOT NULL DEFAULT (datetime('now'))
    );
    CREATE INDEX IF NOT EXISTS idx_trades_portfolio ON trades(portfolio_id);
    CREATE INDEX IF NOT EXISTS idx_trades_order ON trades(order_id);

    CREATE TABLE IF NOT EXISTS market_data (
        security_id   TEXT NOT NULL,
        date          TEXT NOT NULL,
        open          REAL,
        high          REAL,
        low           REAL,
        close         REAL NOT NULL,
        volume        REAL DEFAULT 0,
        PRIMARY KEY(security_id, date)
    );

    CREATE TABLE IF NOT EXISTS config (
        key           TEXT PRIMARY KEY,
        value         TEXT NOT NULL,
        updated_at    TEXT NOT NULL DEFAULT (datetime('now'))
    );

    CREATE TABLE IF NOT EXISTS audit_log (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        entity_type   TEXT NOT NULL,
        entity_id     TEXT NOT NULL,
        action        TEXT NOT NULL,
        details       TEXT,
        timestamp     TEXT NOT NULL DEFAULT (datetime('now'))
    );
    CREATE INDEX IF NOT EXISTS idx_audit_entity ON audit_log(entity_type, entity_id);
    )SQL"
};

// =========================================================================
// Row types for query results
// =========================================================================

struct SecurityRow {
    std::string id, name, asset_class, exchange, sector, currency;
};

struct PortfolioRow {
    std::string id, name, base_currency, status;
    double cash_balance{0};
    std::string owner_id;  // Multi-user: portfolio owner
};

struct PositionRow {
    int64_t rowid{0};
    std::string portfolio_id, security_id;
    double quantity{0}, avg_cost{0}, market_value{0}, unrealized_pnl{0};
    std::string opened_at, closed_at;
};

struct OrderRow {
    std::string id, portfolio_id, security_id, side, order_type, status;
    double quantity{0}, limit_price{0}, avg_fill_price{0}, filled_qty{0};
    std::string created_at, filled_at;
};

struct TradeRow {
    std::string id, order_id, portfolio_id, security_id, side;
    double quantity{0}, price{0}, commission{0};
    std::string executed_at;
};

struct PriceRow {
    std::string security_id, date;
    double open{0}, high{0}, low{0}, close{0}, volume{0};
};

// =========================================================================
// DataStore - Unified persistence layer
// =========================================================================

class DataStore {
    std::shared_ptr<Connection> conn_;

public:
    /** Open or create database at path. Use ":memory:" for in-memory. */
    explicit DataStore(const std::string& path = "genie.db")
        : conn_(std::make_shared<Connection>(path)) {
        SchemaManager schema(*conn_);
        schema.migrate(SCHEMA_VERSION, MIGRATIONS);
    }

    /** Construct with existing connection (for sharing). */
    explicit DataStore(std::shared_ptr<Connection> conn) : conn_(std::move(conn)) {
        SchemaManager schema(*conn_);
        schema.migrate(SCHEMA_VERSION, MIGRATIONS);
    }

    Connection& connection() { return *conn_; }

    // =================================================================
    // Securities
    // =================================================================

    void save_security(const SecurityRow& s) {
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO securities (id, name, asset_class, exchange, sector, currency, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, datetime('now'))");
        stmt.bind(1, s.id).bind(2, s.name).bind(3, s.asset_class)
            .bind(4, s.exchange).bind(5, s.sector).bind(6, s.currency);
        stmt.execute();
    }

    std::optional<SecurityRow> get_security(const std::string& id) {
        auto stmt = conn_->prepare("SELECT id, name, asset_class, exchange, sector, currency FROM securities WHERE id = ?");
        stmt.bind(1, id);
        if (!stmt.step()) return std::nullopt;
        return SecurityRow{stmt.column_text(0), stmt.column_text(1), stmt.column_text(2),
                           stmt.column_text(3), stmt.column_text(4), stmt.column_text(5)};
    }

    std::vector<SecurityRow> list_securities() {
        auto stmt = conn_->prepare("SELECT id, name, asset_class, exchange, sector, currency FROM securities ORDER BY id");
        std::vector<SecurityRow> rows;
        while (stmt.step()) {
            rows.push_back({stmt.column_text(0), stmt.column_text(1), stmt.column_text(2),
                            stmt.column_text(3), stmt.column_text(4), stmt.column_text(5)});
        }
        return rows;
    }

    void delete_security(const std::string& id) {
        conn_->prepare("DELETE FROM securities WHERE id = ?").bind(1, id).execute();
        audit("security", id, "delete", "");
    }

    // =================================================================
    // Portfolios
    // =================================================================

    void save_portfolio(const PortfolioRow& p) {
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO portfolios (id, name, base_currency, cash_balance, status, owner_id, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, datetime('now'))");
        stmt.bind(1, p.id).bind(2, p.name).bind(3, p.base_currency)
            .bind(4, p.cash_balance).bind(5, p.status).bind(6, p.owner_id);
        stmt.execute();
    }

    std::optional<PortfolioRow> get_portfolio(const std::string& id) {
        auto stmt = conn_->prepare(
            "SELECT id, name, base_currency, cash_balance, status, owner_id FROM portfolios WHERE id = ?");
        stmt.bind(1, id);
        if (!stmt.step()) return std::nullopt;
        PortfolioRow row;
        row.id = stmt.column_text(0); row.name = stmt.column_text(1);
        row.base_currency = stmt.column_text(2); row.cash_balance = stmt.column_double(3);
        row.status = stmt.column_text(4); row.owner_id = stmt.column_text(5);
        return row;
    }

    std::vector<PortfolioRow> list_portfolios(const std::string& status = "") {
        std::string sql = "SELECT id, name, base_currency, cash_balance, status, owner_id FROM portfolios";
        if (!status.empty()) sql += " WHERE status = ?";
        sql += " ORDER BY name";
        auto stmt = conn_->prepare(sql);
        if (!status.empty()) stmt.bind(1, status);
        std::vector<PortfolioRow> rows;
        while (stmt.step()) {
            PortfolioRow row;
            row.id = stmt.column_text(0); row.name = stmt.column_text(1);
            row.base_currency = stmt.column_text(2); row.cash_balance = stmt.column_double(3);
            row.status = stmt.column_text(4); row.owner_id = stmt.column_text(5);
            rows.push_back(row);
        }
        return rows;
    }

    /** List portfolios owned by a specific user */
    std::vector<PortfolioRow> list_user_portfolios(const std::string& owner_id) {
        auto stmt = conn_->prepare(
            "SELECT id, name, base_currency, cash_balance, status, owner_id "
            "FROM portfolios WHERE owner_id = ? ORDER BY name");
        stmt.bind(1, owner_id);
        std::vector<PortfolioRow> rows;
        while (stmt.step()) {
            PortfolioRow row;
            row.id = stmt.column_text(0); row.name = stmt.column_text(1);
            row.base_currency = stmt.column_text(2); row.cash_balance = stmt.column_double(3);
            row.status = stmt.column_text(4); row.owner_id = stmt.column_text(5);
            rows.push_back(row);
        }
        return rows;
    }

    void update_cash(const std::string& portfolio_id, double cash) {
        conn_->prepare("UPDATE portfolios SET cash_balance = ?, updated_at = datetime('now') WHERE id = ?")
            .bind(1, cash).bind(2, portfolio_id).execute();
    }

    // =================================================================
    // Positions
    // =================================================================

    void save_position(const PositionRow& p) {
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO positions (portfolio_id, security_id, quantity, avg_cost, market_value, unrealized_pnl, opened_at) "
            "VALUES (?, ?, ?, ?, ?, ?, COALESCE((SELECT opened_at FROM positions WHERE portfolio_id=? AND security_id=?), datetime('now')))");
        stmt.bind(1, p.portfolio_id).bind(2, p.security_id)
            .bind(3, p.quantity).bind(4, p.avg_cost)
            .bind(5, p.market_value).bind(6, p.unrealized_pnl)
            .bind(7, p.portfolio_id).bind(8, p.security_id);
        stmt.execute();
    }

    std::vector<PositionRow> get_positions(const std::string& portfolio_id, bool open_only = true) {
        std::string sql = "SELECT rowid, portfolio_id, security_id, quantity, avg_cost, market_value, "
                          "unrealized_pnl, opened_at, closed_at FROM positions WHERE portfolio_id = ?";
        if (open_only) sql += " AND closed_at IS NULL AND quantity != 0";
        sql += " ORDER BY security_id";
        auto stmt = conn_->prepare(sql);
        stmt.bind(1, portfolio_id);
        std::vector<PositionRow> rows;
        while (stmt.step()) {
            PositionRow r;
            r.rowid = stmt.column_int64(0);
            r.portfolio_id = stmt.column_text(1); r.security_id = stmt.column_text(2);
            r.quantity = stmt.column_double(3); r.avg_cost = stmt.column_double(4);
            r.market_value = stmt.column_double(5); r.unrealized_pnl = stmt.column_double(6);
            r.opened_at = stmt.column_text(7); r.closed_at = stmt.column_text(8);
            rows.push_back(r);
        }
        return rows;
    }

    void close_position(const std::string& portfolio_id, const std::string& security_id) {
        conn_->prepare(
            "UPDATE positions SET quantity = 0, closed_at = datetime('now') "
            "WHERE portfolio_id = ? AND security_id = ? AND closed_at IS NULL")
            .bind(1, portfolio_id).bind(2, security_id).execute();
    }

    // =================================================================
    // Orders
    // =================================================================

    void save_order(const OrderRow& o) {
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO orders "
            "(id, portfolio_id, security_id, side, order_type, quantity, limit_price, status, avg_fill_price, filled_qty, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT created_at FROM orders WHERE id=?), datetime('now')))");
        stmt.bind(1, o.id).bind(2, o.portfolio_id).bind(3, o.security_id)
            .bind(4, o.side).bind(5, o.order_type).bind(6, o.quantity)
            .bind(7, o.limit_price).bind(8, o.status)
            .bind(9, o.avg_fill_price).bind(10, o.filled_qty).bind(11, o.id);
        stmt.execute();
    }

    void fill_order(const std::string& order_id, double fill_price, double fill_qty) {
        conn_->prepare(
            "UPDATE orders SET status = 'Filled', avg_fill_price = ?, filled_qty = ?, "
            "filled_at = datetime('now') WHERE id = ?")
            .bind(1, fill_price).bind(2, fill_qty).bind(3, order_id).execute();
    }

    void cancel_order(const std::string& order_id) {
        conn_->prepare(
            "UPDATE orders SET status = 'Cancelled', cancelled_at = datetime('now') WHERE id = ?")
            .bind(1, order_id).execute();
    }

    std::vector<OrderRow> get_orders(const std::string& portfolio_id, const std::string& status = "") {
        std::string sql = "SELECT id, portfolio_id, security_id, side, order_type, quantity, "
                          "limit_price, status, avg_fill_price, filled_qty, created_at, filled_at "
                          "FROM orders WHERE portfolio_id = ?";
        if (!status.empty()) sql += " AND status = ?";
        sql += " ORDER BY created_at DESC";
        auto stmt = conn_->prepare(sql);
        stmt.bind(1, portfolio_id);
        if (!status.empty()) stmt.bind(2, status);
        std::vector<OrderRow> rows;
        while (stmt.step()) {
            OrderRow r;
            r.id = stmt.column_text(0); r.portfolio_id = stmt.column_text(1);
            r.security_id = stmt.column_text(2); r.side = stmt.column_text(3);
            r.order_type = stmt.column_text(4); r.quantity = stmt.column_double(5);
            r.limit_price = stmt.column_double(6); r.status = stmt.column_text(7);
            r.avg_fill_price = stmt.column_double(8); r.filled_qty = stmt.column_double(9);
            r.created_at = stmt.column_text(10); r.filled_at = stmt.column_text(11);
            rows.push_back(r);
        }
        return rows;
    }

    // =================================================================
    // Trades
    // =================================================================

    void save_trade(const TradeRow& t) {
        auto stmt = conn_->prepare(
            "INSERT INTO trades (id, order_id, portfolio_id, security_id, side, quantity, price, commission) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, t.id).bind(2, t.order_id).bind(3, t.portfolio_id)
            .bind(4, t.security_id).bind(5, t.side).bind(6, t.quantity)
            .bind(7, t.price).bind(8, t.commission);
        stmt.execute();
    }

    std::vector<TradeRow> get_trades(const std::string& portfolio_id, int limit = 100) {
        auto stmt = conn_->prepare(
            "SELECT id, order_id, portfolio_id, security_id, side, quantity, price, commission, executed_at "
            "FROM trades WHERE portfolio_id = ? ORDER BY executed_at DESC LIMIT ?");
        stmt.bind(1, portfolio_id).bind(2, limit);
        std::vector<TradeRow> rows;
        while (stmt.step()) {
            TradeRow r;
            r.id = stmt.column_text(0); r.order_id = stmt.column_text(1);
            r.portfolio_id = stmt.column_text(2); r.security_id = stmt.column_text(3);
            r.side = stmt.column_text(4); r.quantity = stmt.column_double(5);
            r.price = stmt.column_double(6); r.commission = stmt.column_double(7);
            r.executed_at = stmt.column_text(8);
            rows.push_back(r);
        }
        return rows;
    }

    int trade_count(const std::string& portfolio_id) {
        auto v = conn_->scalar<int>("SELECT count(*) FROM trades WHERE portfolio_id = '" + portfolio_id + "'");
        return v.value_or(0);
    }

    // =================================================================
    // Market Data
    // =================================================================

    void save_price(const PriceRow& p) {
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO market_data (security_id, date, open, high, low, close, volume) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, p.security_id).bind(2, p.date)
            .bind(3, p.open).bind(4, p.high).bind(5, p.low)
            .bind(6, p.close).bind(7, p.volume);
        stmt.execute();
    }

    void save_prices_batch(const std::vector<PriceRow>& prices) {
        Transaction txn(*conn_);
        auto stmt = conn_->prepare(
            "INSERT OR REPLACE INTO market_data (security_id, date, open, high, low, close, volume) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
        for (const auto& p : prices) {
            stmt.reset();
            stmt.bind(1, p.security_id).bind(2, p.date)
                .bind(3, p.open).bind(4, p.high).bind(5, p.low)
                .bind(6, p.close).bind(7, p.volume);
            stmt.execute();
        }
        txn.commit();
    }

    std::vector<PriceRow> get_prices(const std::string& security_id,
                                      const std::string& from_date = "",
                                      const std::string& to_date = "",
                                      int limit = 0) {
        std::string sql = "SELECT security_id, date, open, high, low, close, volume "
                          "FROM market_data WHERE security_id = ?";
        if (!from_date.empty()) sql += " AND date >= '" + from_date + "'";
        if (!to_date.empty()) sql += " AND date <= '" + to_date + "'";
        sql += " ORDER BY date ASC";
        if (limit > 0) sql += " LIMIT " + std::to_string(limit);

        auto stmt = conn_->prepare(sql);
        stmt.bind(1, security_id);
        std::vector<PriceRow> rows;
        while (stmt.step()) {
            PriceRow r;
            r.security_id = stmt.column_text(0); r.date = stmt.column_text(1);
            r.open = stmt.column_double(2); r.high = stmt.column_double(3);
            r.low = stmt.column_double(4); r.close = stmt.column_double(5);
            r.volume = stmt.column_double(6);
            rows.push_back(r);
        }
        return rows;
    }

    std::optional<double> latest_price(const std::string& security_id) {
        auto stmt = conn_->prepare(
            "SELECT close FROM market_data WHERE security_id = ? ORDER BY date DESC LIMIT 1");
        stmt.bind(1, security_id);
        if (!stmt.step()) return std::nullopt;
        return stmt.column_double(0);
    }

    // =================================================================
    // Configuration
    // =================================================================

    void set_config(const std::string& key, const std::string& value) {
        conn_->prepare("INSERT OR REPLACE INTO config (key, value, updated_at) VALUES (?, ?, datetime('now'))")
            .bind(1, key).bind(2, value).execute();
    }

    std::optional<std::string> get_config(const std::string& key) {
        auto stmt = conn_->prepare("SELECT value FROM config WHERE key = ?");
        stmt.bind(1, key);
        if (!stmt.step()) return std::nullopt;
        return stmt.column_text(0);
    }

    std::map<std::string, std::string> get_all_config() {
        auto stmt = conn_->prepare("SELECT key, value FROM config ORDER BY key");
        std::map<std::string, std::string> cfg;
        while (stmt.step()) cfg[stmt.column_text(0)] = stmt.column_text(1);
        return cfg;
    }

    // =================================================================
    // Audit Log
    // =================================================================

    void audit(const std::string& entity_type, const std::string& entity_id,
               const std::string& action, const std::string& details = "") {
        conn_->prepare("INSERT INTO audit_log (entity_type, entity_id, action, details) VALUES (?, ?, ?, ?)")
            .bind(1, entity_type).bind(2, entity_id).bind(3, action).bind(4, details).execute();
    }

    std::vector<std::map<std::string, std::string>> get_audit_log(
            const std::string& entity_type = "", const std::string& entity_id = "", int limit = 50) {
        std::string sql = "SELECT id, entity_type, entity_id, action, details, timestamp FROM audit_log";
        std::vector<std::string> conditions;
        if (!entity_type.empty()) conditions.push_back("entity_type = '" + entity_type + "'");
        if (!entity_id.empty()) conditions.push_back("entity_id = '" + entity_id + "'");
        if (!conditions.empty()) {
            sql += " WHERE ";
            for (size_t i = 0; i < conditions.size(); ++i) {
                if (i > 0) sql += " AND ";
                sql += conditions[i];
            }
        }
        sql += " ORDER BY timestamp DESC LIMIT " + std::to_string(limit);
        return conn_->query(sql);
    }

    // =================================================================
    // Statistics
    // =================================================================

    struct DbStats {
        int securities{0}, portfolios{0}, positions{0}, orders{0}, trades{0};
        int price_records{0}, config_entries{0}, audit_entries{0};
        int schema_version{0};
    };

    DbStats stats() {
        DbStats s;
        s.securities = conn_->scalar<int>("SELECT count(*) FROM securities").value_or(0);
        s.portfolios = conn_->scalar<int>("SELECT count(*) FROM portfolios").value_or(0);
        s.positions = conn_->scalar<int>("SELECT count(*) FROM positions WHERE closed_at IS NULL AND quantity != 0").value_or(0);
        s.orders = conn_->scalar<int>("SELECT count(*) FROM orders").value_or(0);
        s.trades = conn_->scalar<int>("SELECT count(*) FROM trades").value_or(0);
        s.price_records = conn_->scalar<int>("SELECT count(*) FROM market_data").value_or(0);
        s.config_entries = conn_->scalar<int>("SELECT count(*) FROM config").value_or(0);
        s.audit_entries = conn_->scalar<int>("SELECT count(*) FROM audit_log").value_or(0);
        SchemaManager schema(*conn_);
        s.schema_version = schema.version();
        return s;
    }
};

} // namespace genie::persistence

#endif // GENIE_PERSISTENCE_DATA_STORE_HPP
