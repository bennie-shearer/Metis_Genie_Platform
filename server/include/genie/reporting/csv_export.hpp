/**
 * @file csv_export.hpp
 * @brief Universal CSV Export Engine
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Generates CSV output for all data endpoints:
 * - Portfolio positions, orders, transactions
 * - Risk metrics (VaR, factor exposures, stress results)
 * - Performance attribution, benchmark comparison
 * - Compliance status, audit logs
 * - Market data, trading history
 * - Tax lot details, P&L reports
 * - Configurable column selection and formatting
 * - Excel-compatible output with BOM marker
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_REPORTING_CSV_EXPORT_HPP
#define GENIE_REPORTING_CSV_EXPORT_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <functional>
#include <mutex>

namespace genie::reporting {

struct CsvColumn {
    std::string header;
    std::string key;
    int width{0}; // For fixed-width display (0 = auto)
    int decimal_places{-1}; // -1 = auto
};

struct CsvRow {
    std::map<std::string, std::string> fields;

    void set(const std::string& key, const std::string& value) {
        fields[key] = value;
    }

    void set(const std::string& key, double value, int decimals = 2) {
        std::ostringstream os;
        os << std::fixed << std::setprecision(decimals) << value;
        fields[key] = os.str();
    }

    void set(const std::string& key, int value) {
        fields[key] = std::to_string(value);
    }

    void set(const std::string& key, bool value) {
        fields[key] = value ? "true" : "false";
    }

    std::string get(const std::string& key) const {
        auto it = fields.find(key);
        return (it != fields.end()) ? it->second : "";
    }
};

struct CsvDocument {
    std::string title;
    std::vector<CsvColumn> columns;
    std::vector<CsvRow> rows;
    char delimiter{','};
    bool include_bom{true}; // Excel-compatible UTF-8 BOM
    bool quote_strings{true};
    std::string line_ending{"\r\n"}; // Windows-compatible

    std::string render() const {
        std::ostringstream os;

        // BOM for Excel compatibility
        if (include_bom) {
            os << "\xEF\xBB\xBF";
        }

        // Header row
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) os << delimiter;
            os << escape_field(columns[i].header);
        }
        os << line_ending;

        // Data rows
        for (const auto& row : rows) {
            for (size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) os << delimiter;
                std::string value = row.get(columns[i].key);
                os << escape_field(value);
            }
            os << line_ending;
        }

        return os.str();
    }

    size_t row_count() const { return rows.size(); }
    size_t column_count() const { return columns.size(); }

private:
    std::string escape_field(const std::string& field) const {
        if (!quote_strings) return field;
        bool needs_quotes = false;
        for (char c : field) {
            if (c == delimiter || c == '"' || c == '\n' || c == '\r') {
                needs_quotes = true;
                break;
            }
        }
        if (!needs_quotes) return field;
        std::string escaped = "\"";
        for (char c : field) {
            if (c == '"') escaped += "\"\"";
            else escaped += c;
        }
        escaped += "\"";
        return escaped;
    }
};

// ---------------------------------------------------------------
// Predefined CSV templates for common exports
// ---------------------------------------------------------------
class CsvTemplates {
public:
    // Portfolio positions export
    static CsvDocument positions_template() {
        CsvDocument doc;
        doc.title = "Portfolio Positions";
        doc.columns = {
            {"Symbol", "symbol"},
            {"Name", "name"},
            {"Quantity", "quantity"},
            {"Avg Cost", "avg_cost"},
            {"Market Price", "market_price"},
            {"Market Value", "market_value"},
            {"Unrealized P&L", "unrealized_pnl"},
            {"Unrealized %", "unrealized_pct"},
            {"Weight %", "weight_pct"},
            {"Sector", "sector"},
            {"Asset Class", "asset_class"}
        };
        return doc;
    }

    // Orders export
    static CsvDocument orders_template() {
        CsvDocument doc;
        doc.title = "Order History";
        doc.columns = {
            {"Order ID", "order_id"},
            {"Date", "date"},
            {"Symbol", "symbol"},
            {"Side", "side"},
            {"Type", "order_type"},
            {"Quantity", "quantity"},
            {"Price", "price"},
            {"Filled Qty", "filled_qty"},
            {"Avg Fill Price", "avg_fill_price"},
            {"Status", "status"},
            {"Commission", "commission"}
        };
        return doc;
    }

    // Risk metrics export
    static CsvDocument risk_template() {
        CsvDocument doc;
        doc.title = "Risk Metrics";
        doc.columns = {
            {"Metric", "metric"},
            {"Value", "value"},
            {"Confidence", "confidence"},
            {"Period", "period"},
            {"Method", "method"},
            {"Notes", "notes"}
        };
        return doc;
    }

    // Performance attribution export
    static CsvDocument attribution_template() {
        CsvDocument doc;
        doc.title = "Performance Attribution";
        doc.columns = {
            {"Segment", "segment"},
            {"Portfolio Weight", "portfolio_weight"},
            {"Benchmark Weight", "benchmark_weight"},
            {"Portfolio Return", "portfolio_return"},
            {"Benchmark Return", "benchmark_return"},
            {"Allocation Effect", "allocation_effect"},
            {"Selection Effect", "selection_effect"},
            {"Interaction Effect", "interaction_effect"},
            {"Total Effect", "total_effect"}
        };
        return doc;
    }

    // Tax lots export
    static CsvDocument tax_lots_template() {
        CsvDocument doc;
        doc.title = "Tax Lot Detail";
        doc.columns = {
            {"Symbol", "symbol"},
            {"Lot ID", "lot_id"},
            {"Acquisition Date", "acquisition_date"},
            {"Quantity", "quantity"},
            {"Cost Basis", "cost_basis"},
            {"Current Value", "current_value"},
            {"Gain/Loss", "gain_loss"},
            {"Gain/Loss %", "gain_loss_pct"},
            {"Holding Period", "holding_period"},
            {"Term", "term"},
            {"Wash Sale", "wash_sale"}
        };
        return doc;
    }

    // Compliance report export
    static CsvDocument compliance_template() {
        CsvDocument doc;
        doc.title = "Compliance Status";
        doc.columns = {
            {"Rule ID", "rule_id"},
            {"Rule Name", "rule_name"},
            {"Framework", "framework"},
            {"Status", "status"},
            {"Current Value", "current_value"},
            {"Limit", "limit"},
            {"Utilization %", "utilization_pct"},
            {"Last Checked", "last_checked"},
            {"Notes", "notes"}
        };
        return doc;
    }

    // Audit log export
    static CsvDocument audit_template() {
        CsvDocument doc;
        doc.title = "Audit Log";
        doc.columns = {
            {"Timestamp", "timestamp"},
            {"User", "user"},
            {"Action", "action"},
            {"Resource", "resource"},
            {"Details", "details"},
            {"IP Address", "ip_address"},
            {"Result", "result"}
        };
        return doc;
    }

    // Market data export
    static CsvDocument market_data_template() {
        CsvDocument doc;
        doc.title = "Market Data";
        doc.columns = {
            {"Symbol", "symbol"},
            {"Date", "date"},
            {"Open", "open"},
            {"High", "high"},
            {"Low", "low"},
            {"Close", "close"},
            {"Volume", "volume"},
            {"Adj Close", "adj_close"}
        };
        return doc;
    }

    // Transactions export
    static CsvDocument transactions_template() {
        CsvDocument doc;
        doc.title = "Transaction History";
        doc.columns = {
            {"Date", "date"},
            {"Symbol", "symbol"},
            {"Type", "type"},
            {"Quantity", "quantity"},
            {"Price", "price"},
            {"Amount", "amount"},
            {"Commission", "commission"},
            {"Net Amount", "net_amount"},
            {"Settlement Date", "settlement_date"},
            {"Status", "status"}
        };
        return doc;
    }
};

// ---------------------------------------------------------------
// CSV Export Engine (aggregates all templates)
// ---------------------------------------------------------------
class CsvExportEngine {
public:
    // Register a custom template
    void register_template(const std::string& name, const CsvDocument& tmpl) {
        std::lock_guard<std::mutex> lock(mtx_);
        templates_[name] = tmpl;
    }

    // Get available export types
    std::vector<std::string> available_exports() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<std::string> names = {
            "positions", "orders", "risk", "attribution",
            "tax_lots", "compliance", "audit", "market_data", "transactions"
        };
        for (const auto& [name, _] : templates_) {
            names.push_back(name);
        }
        return names;
    }

    // Get template by name
    CsvDocument get_template(const std::string& name) const {
        if (name == "positions") return CsvTemplates::positions_template();
        if (name == "orders") return CsvTemplates::orders_template();
        if (name == "risk") return CsvTemplates::risk_template();
        if (name == "attribution") return CsvTemplates::attribution_template();
        if (name == "tax_lots") return CsvTemplates::tax_lots_template();
        if (name == "compliance") return CsvTemplates::compliance_template();
        if (name == "audit") return CsvTemplates::audit_template();
        if (name == "market_data") return CsvTemplates::market_data_template();
        if (name == "transactions") return CsvTemplates::transactions_template();

        std::lock_guard<std::mutex> lock(mtx_);
        auto it = templates_.find(name);
        if (it != templates_.end()) return it->second;
        return CsvDocument{};
    }

private:
    mutable std::mutex mtx_;
    std::map<std::string, CsvDocument> templates_;
};

} // namespace genie::reporting

#endif // GENIE_REPORTING_CSV_EXPORT_HPP
