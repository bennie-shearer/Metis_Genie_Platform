/**
 * @file persistence.hpp
 * @brief Data persistence (CSV/JSON) for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_PERSISTENCE_HPP
#define GENIE_PERSISTENCE_HPP

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace genie {
namespace persistence {

// CSV Writer
class CsvWriter {
    std::ofstream file_;
    char delimiter_;
    bool header_written_{false};
    
public:
    explicit CsvWriter(const std::string& filename, char delim = ',') 
        : file_(filename), delimiter_(delim) {}
    
    void write_header(const std::vector<std::string>& headers) {
        for (size_t i = 0; i < headers.size(); ++i) {
            file_ << headers[i];
            if (i < headers.size() - 1) file_ << delimiter_;
        }
        file_ << "\n";
        header_written_ = true;
    }
    
    void write_row(const std::vector<std::string>& values) {
        for (size_t i = 0; i < values.size(); ++i) {
            // Quote if contains delimiter or quotes
            if (values[i].find(delimiter_) != std::string::npos || 
                values[i].find('"') != std::string::npos) {
                file_ << '"';
                for (char c : values[i]) {
                    if (c == '"') file_ << '"';
                    file_ << c;
                }
                file_ << '"';
            } else {
                file_ << values[i];
            }
            if (i < values.size() - 1) file_ << delimiter_;
        }
        file_ << "\n";
    }
    
    void flush() { file_.flush(); }
    void close() { file_.close(); }
};

// CSV Reader
class CsvReader {
    std::ifstream file_;
    char delimiter_;
    std::vector<std::string> headers_;
    
    std::vector<std::string> parse_line(const std::string& line) {
        std::vector<std::string> result;
        std::string cell;
        bool in_quotes = false;
        
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    in_quotes = !in_quotes;
                }
            } else if (c == delimiter_ && !in_quotes) {
                result.push_back(cell);
                cell.clear();
            } else {
                cell += c;
            }
        }
        result.push_back(cell);
        return result;
    }
    
public:
    explicit CsvReader(const std::string& filename, char delim = ',') 
        : file_(filename), delimiter_(delim) {
        std::string header_line;
        if (std::getline(file_, header_line)) {
            headers_ = parse_line(header_line);
        }
    }
    
    const std::vector<std::string>& headers() const { return headers_; }
    
    bool read_row(std::vector<std::string>& row) {
        std::string line;
        if (std::getline(file_, line)) {
            row = parse_line(line);
            return true;
        }
        return false;
    }
    
    bool read_row(std::map<std::string, std::string>& row) {
        std::vector<std::string> values;
        if (read_row(values)) {
            row.clear();
            for (size_t i = 0; i < std::min(headers_.size(), values.size()); ++i) {
                row[headers_[i]] = values[i];
            }
            return true;
        }
        return false;
    }
    
    std::vector<std::map<std::string, std::string>> read_all() {
        std::vector<std::map<std::string, std::string>> result;
        std::map<std::string, std::string> row;
        while (read_row(row)) result.push_back(row);
        return result;
    }
};

// JSON Builder (simple)
class JsonBuilder {
    std::ostringstream ss_;
    int indent_{0};
    bool first_{true};
    
    void write_indent() { for (int i = 0; i < indent_; ++i) ss_ << "  "; }
    void maybe_comma() { if (!first_) ss_ << ","; first_ = false; }
    
public:
    JsonBuilder& begin_object() { ss_ << "{\n"; ++indent_; first_ = true; return *this; }
    JsonBuilder& end_object() { --indent_; ss_ << "\n"; write_indent(); ss_ << "}"; return *this; }
    JsonBuilder& begin_array() { ss_ << "[\n"; ++indent_; first_ = true; return *this; }
    JsonBuilder& end_array() { --indent_; ss_ << "\n"; write_indent(); ss_ << "]"; return *this; }
    
    JsonBuilder& key(const std::string& k) { 
        maybe_comma(); ss_ << "\n"; write_indent(); ss_ << "\"" << k << "\": "; first_ = true;
        return *this; 
    }
    
    JsonBuilder& value(const std::string& v) { ss_ << "\"" << v << "\""; return *this; }
    JsonBuilder& value(int v) { ss_ << v; return *this; }
    JsonBuilder& value(double v) { ss_ << std::fixed << std::setprecision(6) << v; return *this; }
    JsonBuilder& value(bool v) { ss_ << (v ? "true" : "false"); return *this; }
    JsonBuilder& null_value() { ss_ << "null"; return *this; }
    
    JsonBuilder& array_value(const std::string& v) { maybe_comma(); ss_ << "\n"; write_indent(); value(v); return *this; }
    JsonBuilder& array_value(int v) { maybe_comma(); ss_ << "\n"; write_indent(); value(v); return *this; }
    JsonBuilder& array_value(double v) { maybe_comma(); ss_ << "\n"; write_indent(); value(v); return *this; }
    
    std::string str() const { return ss_.str(); }
    
    void save_to_file(const std::string& filename) const {
        std::ofstream f(filename);
        f << ss_.str();
    }
};

// Portfolio State Snapshot
struct PortfolioSnapshot {
    std::string portfolio_id;
    std::string name;
    std::string timestamp;
    double cash_balance;
    std::string currency;
    std::vector<std::tuple<std::string, double, double, double>> positions; // id, qty, cost, value
    
    std::string to_json() const {
        JsonBuilder j;
        j.begin_object()
            .key("portfolio_id").value(portfolio_id)
            .key("name").value(name)
            .key("timestamp").value(timestamp)
            .key("cash_balance").value(cash_balance)
            .key("currency").value(currency)
            .key("positions").begin_array();
        for (const auto& [id, qty, cost, val] : positions) {
            j.begin_object()
                .key("security_id").value(id)
                .key("quantity").value(qty)
                .key("cost_basis").value(cost)
                .key("market_value").value(val)
                .end_object();
        }
        j.end_array().end_object();
        return j.str();
    }
    
    void save_to_file(const std::string& filename) const {
        std::ofstream f(filename);
        f << to_json();
    }
};

// Trade History Export
inline void export_trades_csv(const std::string& filename,
                              const std::vector<std::tuple<std::string, std::string, std::string, 
                                                           double, double, double, std::string>>& trades) {
    CsvWriter csv(filename);
    csv.write_header({"TradeID", "SecurityID", "Side", "Quantity", "Price", "Value", "Timestamp"});
    for (const auto& [id, sec, side, qty, price, value, ts] : trades) {
        csv.write_row({id, sec, side, std::to_string(qty), std::to_string(price), 
                       std::to_string(value), ts});
    }
}

// Price History Export
inline void export_prices_csv(const std::string& filename,
                              const std::string& security_id,
                              const std::vector<std::pair<std::string, double>>& prices) {
    CsvWriter csv(filename);
    csv.write_header({"Date", "SecurityID", "Price"});
    for (const auto& [date, price] : prices) {
        csv.write_row({date, security_id, std::to_string(price)});
    }
}

} // namespace persistence
} // namespace genie
#endif // GENIE_PERSISTENCE_HPP
