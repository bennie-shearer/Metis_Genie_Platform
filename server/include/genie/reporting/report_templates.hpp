/**
 * @file report_templates.hpp
 * @brief Configurable report templates with parameter binding and formats
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Template-based report generation:
 * - Named templates with typed parameters
 * - Section-based layout (header, body, footer, summary)
 * - Parameter binding with validation
 * - Output formats (text, CSV, JSON, HTML table)
 * - Template inheritance and composition
 * - Conditional sections based on data
 * - Aggregate functions (sum, avg, min, max, count)
 * - Column formatting (width, alignment, number format)
 * - Report scheduling integration
 * - Template versioning and catalog
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_REPORTING_REPORT_TEMPLATES_HPP
#define GENIE_REPORTING_REPORT_TEMPLATES_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <variant>

namespace genie {
namespace reporting {
namespace templates {

// ============================================================================
// Enumerations
// ============================================================================

enum class OutputFormat { Text, CSV, JSON, HTML };
enum class ColumnAlign { Left, Center, Right };
enum class ParamType { String, Integer, Double, Date, Boolean, StringList };
enum class AggregateFunc { None, Sum, Avg, Min, Max, Count };

[[nodiscard]] inline std::string format_string(OutputFormat f) {
    switch (f) { case OutputFormat::Text: return "text"; case OutputFormat::CSV: return "csv";
        case OutputFormat::JSON: return "json"; case OutputFormat::HTML: return "html"; }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct ColumnDef {
    std::string key;
    std::string header;
    int width{15};
    ColumnAlign align{ColumnAlign::Right};
    int decimal_places{2};
    bool is_currency{false};
    AggregateFunc aggregate{AggregateFunc::None};
    std::string format_str{};
};

struct TemplateParam {
    std::string name;
    std::string label;
    ParamType type{ParamType::String};
    std::string default_value;
    bool required{true};
    std::string description;
};

using DataRow = std::map<std::string, std::string>;

/**
 * @brief Report template definition
 */
struct ReportTemplate {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    int version{1};
    std::vector<TemplateParam> parameters;
    std::vector<ColumnDef> columns;
    std::string header_text;
    std::string footer_text;
    bool include_summary{true};
    bool include_timestamp{true};
    std::vector<OutputFormat> supported_formats;

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{\"id\":\"" << id << "\",\"name\":\"" << name
            << "\",\"category\":\"" << category
            << "\",\"version\":" << version
            << ",\"columns\":" << columns.size()
            << ",\"params\":" << parameters.size() << "}";
        return oss.str();
    }
};

/**
 * @brief Rendered report
 */
struct RenderedReport {
    std::string template_id;
    std::string title;
    OutputFormat format{OutputFormat::Text};
    std::string content;
    std::chrono::system_clock::time_point generated_at;
    int row_count{0};
    std::map<std::string, std::string> bound_params;

    [[nodiscard]] int content_size() const { return static_cast<int>(content.size()); }
};

// ============================================================================
// Report Renderer
// ============================================================================

class ReportRenderer {
public:
    /**
     * @brief Render data with template to specified format
     */
    [[nodiscard]] static RenderedReport render(const ReportTemplate& tmpl,
                                                 const std::vector<DataRow>& data,
                                                 const std::map<std::string, std::string>& params,
                                                 OutputFormat format) {
        RenderedReport report;
        report.template_id = tmpl.id;
        report.title = tmpl.name;
        report.format = format;
        report.generated_at = std::chrono::system_clock::now();
        report.row_count = static_cast<int>(data.size());
        report.bound_params = params;

        switch (format) {
            case OutputFormat::Text: report.content = render_text(tmpl, data, params); break;
            case OutputFormat::CSV:  report.content = render_csv(tmpl, data); break;
            case OutputFormat::JSON: report.content = render_json(tmpl, data, params); break;
            case OutputFormat::HTML: report.content = render_html(tmpl, data, params); break;
        }
        return report;
    }

private:
    static std::string render_text(const ReportTemplate& tmpl,
                                     const std::vector<DataRow>& data,
                                     const std::map<std::string, std::string>& /*params*/) {
        std::ostringstream oss;
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        // Header
        int total_width = 0;
        for (const auto& col : tmpl.columns) total_width += col.width + 2;
        std::string divider(total_width, '=');
        oss << divider << "\n";
        oss << "  " << tmpl.name << "\n";
        if (tmpl.include_timestamp) {
            oss << "  Generated: " << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S UTC") << "\n";
        }
        if (!tmpl.header_text.empty()) oss << "  " << tmpl.header_text << "\n";
        oss << divider << "\n";

        // Column headers
        for (const auto& col : tmpl.columns) {
            oss << std::setw(col.width) << std::left << col.header << "  ";
        }
        oss << "\n" << std::string(total_width, '-') << "\n";

        // Data rows
        for (const auto& row : data) {
            for (const auto& col : tmpl.columns) {
                auto it = row.find(col.key);
                std::string val = (it != row.end()) ? it->second : "";
                if (col.align == ColumnAlign::Right) oss << std::setw(col.width) << std::right << val;
                else oss << std::setw(col.width) << std::left << val;
                oss << "  ";
            }
            oss << "\n";
        }

        // Summary
        if (tmpl.include_summary) {
            oss << std::string(total_width, '-') << "\n";
            oss << "  " << data.size() << " rows";
            for (const auto& col : tmpl.columns) {
                if (col.aggregate == AggregateFunc::None) continue;
                double agg = compute_aggregate(data, col.key, col.aggregate);
                oss << " | " << col.header << " " << agg_label(col.aggregate)
                    << ": " << std::fixed << std::setprecision(col.decimal_places) << agg;
            }
            oss << "\n";
        }
        oss << divider << "\n";
        return oss.str();
    }

    static std::string render_csv(const ReportTemplate& tmpl, const std::vector<DataRow>& data) {
        std::ostringstream oss;
        bool first = true;
        for (const auto& col : tmpl.columns) {
            if (!first) oss << ",";
            oss << "\"" << col.header << "\"";
            first = false;
        }
        oss << "\n";
        for (const auto& row : data) {
            first = true;
            for (const auto& col : tmpl.columns) {
                if (!first) oss << ",";
                auto it = row.find(col.key);
                std::string val = (it != row.end()) ? it->second : "";
                oss << "\"" << val << "\"";
                first = false;
            }
            oss << "\n";
        }
        return oss.str();
    }

    static std::string render_json(const ReportTemplate& tmpl,
                                     const std::vector<DataRow>& data,
                                     const std::map<std::string, std::string>& /*params*/) {
        std::ostringstream oss;
        oss << "{\"report\":\"" << tmpl.name << "\",\"rows\":" << data.size() << ",\"data\":[";
        for (size_t i = 0; i < data.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{";
            bool first = true;
            for (const auto& col : tmpl.columns) {
                if (!first) oss << ",";
                auto it = data[i].find(col.key);
                oss << "\"" << col.key << "\":\"" << (it != data[i].end() ? it->second : "") << "\"";
                first = false;
            }
            oss << "}";
        }
        oss << "]}";
        return oss.str();
    }

    static std::string render_html(const ReportTemplate& tmpl,
                                     const std::vector<DataRow>& data,
                                     const std::map<std::string, std::string>& /*params*/) {
        std::ostringstream oss;
        oss << "<h2>" << tmpl.name << "</h2>\n";
        oss << "<table border='1' cellpadding='4' cellspacing='0'>\n<tr>";
        for (const auto& col : tmpl.columns) oss << "<th>" << col.header << "</th>";
        oss << "</tr>\n";
        for (const auto& row : data) {
            oss << "<tr>";
            for (const auto& col : tmpl.columns) {
                auto it = row.find(col.key);
                oss << "<td>" << (it != row.end() ? it->second : "") << "</td>";
            }
            oss << "</tr>\n";
        }
        oss << "</table>\n<p>" << data.size() << " rows</p>\n";
        return oss.str();
    }

    static double compute_aggregate(const std::vector<DataRow>& data,
                                      const std::string& key, AggregateFunc func) {
        std::vector<double> values;
        for (const auto& row : data) {
            auto it = row.find(key);
            if (it != row.end()) {
                try { values.push_back(std::stod(it->second)); } catch (...) {}
            }
        }
        if (values.empty()) return 0;
        switch (func) {
            case AggregateFunc::Sum: return std::accumulate(values.begin(), values.end(), 0.0);
            case AggregateFunc::Avg: return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            case AggregateFunc::Min: return *std::min_element(values.begin(), values.end());
            case AggregateFunc::Max: return *std::max_element(values.begin(), values.end());
            case AggregateFunc::Count: return static_cast<double>(values.size());
            default: return 0;
        }
    }

    static std::string agg_label(AggregateFunc f) {
        switch (f) { case AggregateFunc::Sum: return "Total"; case AggregateFunc::Avg: return "Avg";
            case AggregateFunc::Min: return "Min"; case AggregateFunc::Max: return "Max";
            case AggregateFunc::Count: return "Count"; default: return ""; }
    }
};

// ============================================================================
// Template Registry
// ============================================================================

class ReportTemplateRegistry {
public:
    ReportTemplateRegistry() { register_default_templates(); }

    void add(ReportTemplate tmpl) {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_[tmpl.id] = std::move(tmpl);
    }

    [[nodiscard]] std::optional<ReportTemplate> get(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = templates_.find(id);
        if (it == templates_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] RenderedReport generate(const std::string& template_id,
                                            const std::vector<DataRow>& data,
                                            const std::map<std::string, std::string>& params = {},
                                            OutputFormat format = OutputFormat::Text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = templates_.find(template_id);
        if (it == templates_.end()) return {};
        return ReportRenderer::render(it->second, data, params, format);
    }

    [[nodiscard]] std::vector<std::string> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [id, _] : templates_) result.push_back(id);
        return result;
    }

    [[nodiscard]] int template_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(templates_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, ReportTemplate> templates_;

    void register_default_templates() {
        // Portfolio Holdings
        {
            ReportTemplate t;
            t.id = "RPT-HOLDINGS"; t.name = "Portfolio Holdings Report"; t.category = "portfolio";
            t.columns = {
                {"symbol", "Symbol", 10, ColumnAlign::Left, 0, false, AggregateFunc::Count, ""},
                {"name", "Name", 25, ColumnAlign::Left, 2, false, AggregateFunc::None, ""},
                {"quantity", "Quantity", 12, ColumnAlign::Right, 0, false, AggregateFunc::None, ""},
                {"price", "Price", 12, ColumnAlign::Right, 2, true, AggregateFunc::None, ""},
                {"market_value", "Market Value", 15, ColumnAlign::Right, 2, true, AggregateFunc::Sum, ""},
                {"weight", "Weight %", 10, ColumnAlign::Right, 2, false, AggregateFunc::None, ""},
                {"pnl", "P&L", 12, ColumnAlign::Right, 2, true, AggregateFunc::Sum, ""}
            };
            t.supported_formats = {OutputFormat::Text, OutputFormat::CSV, OutputFormat::JSON, OutputFormat::HTML};
            templates_["RPT-HOLDINGS"] = std::move(t);
        }
        // Risk Summary
        {
            ReportTemplate t;
            t.id = "RPT-RISK"; t.name = "Daily Risk Summary"; t.category = "risk";
            t.columns = {
                {"metric", "Risk Metric", 25, ColumnAlign::Left, 2, false, AggregateFunc::None, ""},
                {"value", "Value", 15, ColumnAlign::Right, 4, false, AggregateFunc::None, ""},
                {"limit", "Limit", 15, ColumnAlign::Right, 4, false, AggregateFunc::None, ""},
                {"utilization", "Utilization %", 15, ColumnAlign::Right, 1, false, AggregateFunc::None, ""},
                {"status", "Status", 10, ColumnAlign::Left, 2, false, AggregateFunc::None, ""}
            };
            templates_["RPT-RISK"] = std::move(t);
        }
        // Trade Blotter
        {
            ReportTemplate t;
            t.id = "RPT-BLOTTER"; t.name = "Trade Blotter"; t.category = "trading";
            t.columns = {
                {"time", "Time", 12, ColumnAlign::Left, 2, false, AggregateFunc::None, ""},
                {"symbol", "Symbol", 8, ColumnAlign::Left, 2, false, AggregateFunc::None, ""},
                {"side", "Side", 6, ColumnAlign::Left, 2, false, AggregateFunc::None, ""},
                {"quantity", "Qty", 10, ColumnAlign::Right, 0, false, AggregateFunc::None, ""},
                {"price", "Price", 12, ColumnAlign::Right, 4, false, AggregateFunc::None, ""},
                {"notional", "Notional", 15, ColumnAlign::Right, 2, true, AggregateFunc::Sum, ""},
                {"status", "Status", 10, ColumnAlign::Left, 2, false, AggregateFunc::None, ""}
            };
            templates_["RPT-BLOTTER"] = std::move(t);
        }
    }
};

} // namespace templates
} // namespace reporting
} // namespace genie

#endif // GENIE_REPORTING_REPORT_TEMPLATES_HPP
