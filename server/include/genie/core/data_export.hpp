/**
 * @file data_export.hpp
 * @brief CSV, JSON, and XML data export framework
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides a unified interface for exporting tabular data to CSV, JSON,
 * and XML formats. Supports streaming export for large datasets,
 * custom column formatting, and file or string output.
 *
 * Zero external dependencies. Thread-safe. Cross-platform.
 */
#pragma once
#ifndef GENIE_DATA_EXPORT_HPP
#define GENIE_DATA_EXPORT_HPP

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <functional>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <stdexcept>
#include <variant>
#include <algorithm>

namespace genie {

// ============================================================================
// Export Format
// ============================================================================

enum class ExportFormat {
    CSV,
    JSON,
    XML
};

inline const char* export_format_name(ExportFormat f) {
    switch (f) {
        case ExportFormat::CSV:  return "CSV";
        case ExportFormat::JSON: return "JSON";
        case ExportFormat::XML:  return "XML";
        default:                 return "UNKNOWN";
    }
}

inline const char* export_format_extension(ExportFormat f) {
    switch (f) {
        case ExportFormat::CSV:  return ".csv";
        case ExportFormat::JSON: return ".json";
        case ExportFormat::XML:  return ".xml";
        default:                 return ".txt";
    }
}

inline const char* export_format_mime(ExportFormat f) {
    switch (f) {
        case ExportFormat::CSV:  return "text/csv";
        case ExportFormat::JSON: return "application/json";
        case ExportFormat::XML:  return "application/xml";
        default:                 return "text/plain";
    }
}

// ============================================================================
// Cell Value (variant type for table cells)
// ============================================================================

using CellValue = std::variant<std::string, double, int64_t, bool>;

inline std::string cell_to_string(const CellValue& cell) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::setprecision(6) << arg;
            return oss.str();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        }
    }, cell);
}

// ============================================================================
// Export Configuration
// ============================================================================

struct ExportConfig {
    char        csv_delimiter       = ',';
    char        csv_quote           = '"';
    bool        csv_header          = true;
    std::string csv_line_ending     = "\n";
    int         json_indent         = 2;
    bool        json_pretty         = true;
    std::string xml_root_element    = "data";
    std::string xml_row_element     = "row";
    bool        xml_declaration     = true;
    std::string timestamp_format    = "%Y-%m-%dT%H:%M:%S";
};

// ============================================================================
// Table Row
// ============================================================================

using Row = std::vector<CellValue>;

// ============================================================================
// Data Table (in-memory tabular data)
// ============================================================================

class DataTable {
public:
    DataTable() = default;

    explicit DataTable(std::vector<std::string> columns)
        : columns_(std::move(columns)) {}

    void set_columns(std::vector<std::string> columns) {
        columns_ = std::move(columns);
    }

    void add_row(Row row) {
        if (!columns_.empty() && row.size() != columns_.size()) {
            throw std::runtime_error(
                "Row size (" + std::to_string(row.size()) +
                ") does not match column count (" +
                std::to_string(columns_.size()) + ")");
        }
        rows_.push_back(std::move(row));
    }

    void add_row(std::initializer_list<CellValue> values) {
        add_row(Row(values));
    }

    const std::vector<std::string>& columns() const { return columns_; }
    const std::vector<Row>& rows() const { return rows_; }
    size_t row_count() const { return rows_.size(); }
    size_t column_count() const { return columns_.size(); }
    bool empty() const { return rows_.empty(); }

    void clear() { rows_.clear(); }

private:
    std::vector<std::string>    columns_;
    std::vector<Row>            rows_;
};

// ============================================================================
// CSV Exporter
// ============================================================================

class CsvExporter {
public:
    explicit CsvExporter(const ExportConfig& cfg = {}) : config_(cfg) {}

    std::string export_string(const DataTable& table) const {
        std::ostringstream oss;
        export_stream(table, oss);
        return oss.str();
    }

    bool export_file(const DataTable& table, const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        export_stream(table, file);
        return true;
    }

    void export_stream(const DataTable& table, std::ostream& out) const {
        // Header
        if (config_.csv_header && !table.columns().empty()) {
            for (size_t i = 0; i < table.columns().size(); ++i) {
                if (i > 0) out << config_.csv_delimiter;
                write_csv_field(out, table.columns()[i]);
            }
            out << config_.csv_line_ending;
        }

        // Rows
        for (const auto& row : table.rows()) {
            for (size_t i = 0; i < row.size(); ++i) {
                if (i > 0) out << config_.csv_delimiter;
                write_csv_field(out, cell_to_string(row[i]));
            }
            out << config_.csv_line_ending;
        }
    }

private:
    void write_csv_field(std::ostream& out, const std::string& field) const {
        bool needs_quoting = field.find(config_.csv_delimiter) != std::string::npos
                          || field.find(config_.csv_quote) != std::string::npos
                          || field.find('\n') != std::string::npos
                          || field.find('\r') != std::string::npos;

        if (needs_quoting) {
            out << config_.csv_quote;
            for (char c : field) {
                if (c == config_.csv_quote) {
                    out << config_.csv_quote << config_.csv_quote;
                } else {
                    out << c;
                }
            }
            out << config_.csv_quote;
        } else {
            out << field;
        }
    }

    ExportConfig config_;
};

// ============================================================================
// JSON Exporter
// ============================================================================

class JsonExporter {
public:
    explicit JsonExporter(const ExportConfig& cfg = {}) : config_(cfg) {}

    std::string export_string(const DataTable& table) const {
        std::ostringstream oss;
        export_stream(table, oss);
        return oss.str();
    }

    bool export_file(const DataTable& table, const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        export_stream(table, file);
        return true;
    }

    void export_stream(const DataTable& table, std::ostream& out) const {
        std::string indent = config_.json_pretty
            ? std::string(config_.json_indent, ' ') : "";
        std::string nl = config_.json_pretty ? "\n" : "";
        std::string indent2 = config_.json_pretty
            ? std::string(config_.json_indent * 2, ' ') : "";

        out << "[" << nl;
        for (size_t r = 0; r < table.rows().size(); ++r) {
            if (r > 0) out << "," << nl;
            out << indent << "{" << nl;
            const auto& row = table.rows()[r];
            for (size_t c = 0; c < row.size(); ++c) {
                if (c > 0) out << "," << nl;
                out << indent2 << "\"" << json_escape(table.columns()[c])
                    << "\": " << cell_to_json(row[c]);
            }
            out << nl << indent << "}";
        }
        out << nl << "]" << nl;
    }

private:
    std::string cell_to_json(const CellValue& cell) const {
        return std::visit([this](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + json_escape(arg) + "\"";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream oss;
                oss << std::setprecision(6) << arg;
                return oss.str();
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(arg);
            } else if constexpr (std::is_same_v<T, bool>) {
                return arg ? "true" : "false";
            }
        }, cell);
    }

    std::string json_escape(const std::string& s) const {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }

    ExportConfig config_;
};

// ============================================================================
// XML Exporter
// ============================================================================

class XmlExporter {
public:
    explicit XmlExporter(const ExportConfig& cfg = {}) : config_(cfg) {}

    std::string export_string(const DataTable& table) const {
        std::ostringstream oss;
        export_stream(table, oss);
        return oss.str();
    }

    bool export_file(const DataTable& table, const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        export_stream(table, file);
        return true;
    }

    void export_stream(const DataTable& table, std::ostream& out) const {
        if (config_.xml_declaration) {
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        }
        out << "<" << config_.xml_root_element << ">\n";

        for (const auto& row : table.rows()) {
            out << "  <" << config_.xml_row_element << ">\n";
            for (size_t c = 0; c < row.size(); ++c) {
                std::string tag = sanitize_xml_tag(table.columns()[c]);
                out << "    <" << tag << ">"
                    << xml_escape(cell_to_string(row[c]))
                    << "</" << tag << ">\n";
            }
            out << "  </" << config_.xml_row_element << ">\n";
        }

        out << "</" << config_.xml_root_element << ">\n";
    }

private:
    std::string xml_escape(const std::string& s) const {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&':  result += "&amp;";  break;
                case '<':  result += "&lt;";   break;
                case '>':  result += "&gt;";   break;
                case '"':  result += "&quot;";  break;
                case '\'': result += "&apos;"; break;
                default:   result += c;        break;
            }
        }
        return result;
    }

    std::string sanitize_xml_tag(const std::string& name) const {
        std::string result;
        result.reserve(name.size());
        for (size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if ((i == 0 && std::isdigit(static_cast<unsigned char>(c))) ||
                (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')) {
                result += '_';
            } else {
                result += c;
            }
        }
        return result.empty() ? "field" : result;
    }

    ExportConfig config_;
};

// ============================================================================
// Unified Export Manager
// ============================================================================

class ExportManager {
public:
    ExportManager() = default;

    std::string export_string(const DataTable& table, ExportFormat format,
                             const ExportConfig& cfg = {}) const {
        switch (format) {
            case ExportFormat::CSV:  return CsvExporter(cfg).export_string(table);
            case ExportFormat::JSON: return JsonExporter(cfg).export_string(table);
            case ExportFormat::XML:  return XmlExporter(cfg).export_string(table);
            default:
                throw std::runtime_error("Unsupported export format");
        }
    }

    bool export_file(const DataTable& table, const std::string& path,
                    ExportFormat format, const ExportConfig& cfg = {}) const {
        switch (format) {
            case ExportFormat::CSV:  return CsvExporter(cfg).export_file(table, path);
            case ExportFormat::JSON: return JsonExporter(cfg).export_file(table, path);
            case ExportFormat::XML:  return XmlExporter(cfg).export_file(table, path);
            default: return false;
        }
    }

    // Auto-detect format from file extension
    bool export_auto(const DataTable& table, const std::string& path,
                    const ExportConfig& cfg = {}) const {
        ExportFormat fmt = detect_format(path);
        return export_file(table, path, fmt, cfg);
    }

    static ExportFormat detect_format(const std::string& path) {
        auto pos = path.rfind('.');
        if (pos == std::string::npos) return ExportFormat::CSV;
        std::string ext = path.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                      [](char c){ return static_cast<char>(std::tolower(c)); });
        if (ext == ".json") return ExportFormat::JSON;
        if (ext == ".xml")  return ExportFormat::XML;
        return ExportFormat::CSV;
    }

    // Generate timestamped filename
    static std::string timestamped_filename(const std::string& prefix,
                                           ExportFormat format) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        std::ostringstream oss;
        oss << prefix << "_"
            << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
            << export_format_extension(format);
        return oss.str();
    }
};

} // namespace genie

#endif // GENIE_DATA_EXPORT_HPP
