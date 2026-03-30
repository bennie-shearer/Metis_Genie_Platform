/**
 * @file pdf_report.hpp
 * @brief PDF report generation framework for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Generates structured report data for PDF rendering.
 * Output is a ReportDocument that can be serialized to JSON for
 * external PDF rendering (wkhtmltopdf, Puppeteer, WeasyPrint).
 */
#pragma once
#ifndef GENIE_REPORTING_PDF_REPORT_HPP
#define GENIE_REPORTING_PDF_REPORT_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <ctime>

namespace genie::reporting {

/** Report element types */
enum class ElementType { Title, Subtitle, Paragraph, Table, KeyValue, Chart, Divider, PageBreak };

/** Table cell alignment */
enum class Align { Left, Center, Right };

/** A single table column definition */
struct ColumnDef {
    std::string header;
    Align align{Align::Left};
    int width_pct{0}; // 0 = auto
};

/** Report element (union-like via type + data maps) */
struct ReportElement {
    ElementType type;
    std::string text;
    // Table data
    std::vector<ColumnDef> columns;
    std::vector<std::vector<std::string>> rows;
    // Key-value pairs
    std::vector<std::pair<std::string, std::string>> kv_pairs;
    // Chart spec
    std::string chart_type; // "bar", "line", "pie"
    std::vector<std::pair<std::string, double>> chart_data;
};

/** Complete report document */
struct ReportDocument {
    std::string title;
    std::string subtitle;
    std::string generated_at;
    std::string generated_by{"Metis Genie Platform"};
    std::string report_type;
    std::vector<ReportElement> elements;

    void add_title(const std::string& t) {
        elements.push_back({ElementType::Title, t, {}, {}, {}, "", {}});
    }
    void add_subtitle(const std::string& t) {
        elements.push_back({ElementType::Subtitle, t, {}, {}, {}, "", {}});
    }
    void add_paragraph(const std::string& t) {
        elements.push_back({ElementType::Paragraph, t, {}, {}, {}, "", {}});
    }
    void add_divider() {
        elements.push_back({ElementType::Divider, "", {}, {}, {}, "", {}});
    }
    void add_page_break() {
        elements.push_back({ElementType::PageBreak, "", {}, {}, {}, "", {}});
    }

    void add_key_values(const std::string& section_title,
                        const std::vector<std::pair<std::string, std::string>>& pairs) {
        ReportElement el;
        el.type = ElementType::KeyValue;
        el.text = section_title;
        el.kv_pairs = pairs;
        elements.push_back(el);
    }

    void add_table(const std::string& caption,
                   const std::vector<ColumnDef>& cols,
                   const std::vector<std::vector<std::string>>& data) {
        ReportElement el;
        el.type = ElementType::Table;
        el.text = caption;
        el.columns = cols;
        el.rows = data;
        elements.push_back(el);
    }

    void add_chart(const std::string& title, const std::string& chart_type,
                   const std::vector<std::pair<std::string, double>>& data) {
        ReportElement el;
        el.type = ElementType::Chart;
        el.text = title;
        el.chart_type = chart_type;
        el.chart_data = data;
        elements.push_back(el);
    }

    /** Serialize to JSON for external rendering */
    [[nodiscard]] std::string to_json() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "{\"title\":\"" << title << "\",\"subtitle\":\"" << subtitle
           << "\",\"generated_at\":\"" << generated_at << "\",\"report_type\":\""
           << report_type << "\",\"elements\":[";
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& el = elements[i];
            ss << "{\"type\":" << static_cast<int>(el.type) << ",\"text\":\"" << el.text << "\"";
            if (!el.kv_pairs.empty()) {
                ss << ",\"pairs\":[";
                for (size_t j = 0; j < el.kv_pairs.size(); ++j) {
                    if (j > 0) ss << ",";
                    ss << "[\"" << el.kv_pairs[j].first << "\",\"" << el.kv_pairs[j].second << "\"]";
                }
                ss << "]";
            }
            if (!el.rows.empty()) {
                ss << ",\"rows\":" << el.rows.size() << ",\"cols\":" << el.columns.size();
            }
            ss << "}";
        }
        ss << "]}";
        return ss.str();
    }

    /** Generate HTML representation (can be converted to PDF via wkhtmltopdf) */
    [[nodiscard]] std::string to_html() const {
        std::ostringstream ss;
        ss << "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
           << "<style>"
           << "body{font-family:Arial,sans-serif;margin:40px;color:#333;}"
           << "h1{color:#1a365d;border-bottom:2px solid #3182ce;padding-bottom:8px;}"
           << "h2{color:#2d3748;margin-top:24px;}"
           << "h3{color:#4a5568;}"
           << "table{width:100%;border-collapse:collapse;margin:12px 0;}"
           << "th{background:#edf2f7;padding:8px 12px;text-align:left;border:1px solid #e2e8f0;font-size:13px;}"
           << "td{padding:6px 12px;border:1px solid #e2e8f0;font-size:13px;}"
           << "tr:nth-child(even){background:#f7fafc;}"
           << ".kv{display:grid;grid-template-columns:200px 1fr;gap:4px 16px;margin:8px 0;}"
           << ".kv-key{font-weight:bold;color:#4a5568;}"
           << ".divider{border-top:1px solid #e2e8f0;margin:20px 0;}"
           << ".footer{margin-top:32px;padding-top:8px;border-top:1px solid #e2e8f0;font-size:11px;color:#a0aec0;}"
           << "</style></head><body>";

        for (const auto& el : elements) {
            switch (el.type) {
                case ElementType::Title:
                    ss << "<h1>" << el.text << "</h1>"; break;
                case ElementType::Subtitle:
                    ss << "<h2>" << el.text << "</h2>"; break;
                case ElementType::Paragraph:
                    ss << "<p>" << el.text << "</p>"; break;
                case ElementType::Divider:
                    ss << "<div class='divider'></div>"; break;
                case ElementType::PageBreak:
                    ss << "<div style='page-break-after:always;'></div>"; break;
                case ElementType::KeyValue:
                    ss << "<h3>" << el.text << "</h3><div class='kv'>";
                    for (const auto& [k, v] : el.kv_pairs)
                        ss << "<span class='kv-key'>" << k << "</span><span>" << v << "</span>";
                    ss << "</div>"; break;
                case ElementType::Table:
                    ss << "<h3>" << el.text << "</h3><table><thead><tr>";
                    for (const auto& col : el.columns) {
                        ss << "<th";
                        if (col.align == Align::Right) ss << " style='text-align:right'";
                        else if (col.align == Align::Center) ss << " style='text-align:center'";
                        ss << ">" << col.header << "</th>";
                    }
                    ss << "</tr></thead><tbody>";
                    for (const auto& row : el.rows) {
                        ss << "<tr>";
                        for (size_t c = 0; c < row.size(); ++c) {
                            ss << "<td";
                            if (c < el.columns.size() && el.columns[c].align == Align::Right)
                                ss << " style='text-align:right'";
                            ss << ">" << row[c] << "</td>";
                        }
                        ss << "</tr>";
                    }
                    ss << "</tbody></table>"; break;
                case ElementType::Chart:
                    ss << "<h3>" << el.text << " [" << el.chart_type << " chart - "
                       << el.chart_data.size() << " data points]</h3>"; break;
            }
        }

        ss << "<div class='footer'>Generated by " << generated_by
           << " | " << generated_at << "</div></body></html>";
        return ss.str();
    }
};

/** Get current date as YYYY-MM-DD string */
inline std::string current_date_string() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::gmtime(&time));
    return std::string(buf);
}

/** Pre-built report templates */
class ReportTemplates {
public:
    /** Portfolio tearsheet */
    static ReportDocument portfolio_tearsheet(
            const std::string& portfolio_name,
            double nav, double cash, int position_count,
            const std::vector<std::tuple<std::string, double, double, double>>& holdings,
            double ytd_return, double sharpe, double var_95) {

        ReportDocument doc;
        doc.title = portfolio_name + " - Portfolio Tearsheet";
        doc.subtitle = "Investment Summary";
        doc.report_type = "tearsheet";
        doc.generated_at = current_date_string();

        doc.add_title(portfolio_name);
        doc.add_key_values("Summary", {
            {"NAV", "$" + format_num(nav)},
            {"Cash", "$" + format_num(cash)},
            {"Positions", std::to_string(position_count)},
            {"YTD Return", format_pct(ytd_return)},
            {"Sharpe Ratio", format_dec(sharpe, 2)},
            {"VaR (95%)", "$" + format_num(var_95)}
        });

        doc.add_divider();

        std::vector<ColumnDef> cols = {
            {"Security", Align::Left}, {"Quantity", Align::Right},
            {"Price", Align::Right}, {"Value", Align::Right}
        };
        std::vector<std::vector<std::string>> rows;
        for (const auto& [id, qty, px, val] : holdings) {
            rows.push_back({id, format_dec(qty, 0), "$" + format_dec(px, 2), "$" + format_num(val)});
        }
        doc.add_table("Holdings", cols, rows);

        return doc;
    }

    /** Risk dashboard report */
    static ReportDocument risk_dashboard(
            const std::string& portfolio_name,
            double var_95, double var_99, double cvar_95,
            double portfolio_vol, double beta,
            const std::vector<std::pair<std::string, double>>& factor_exposures) {

        ReportDocument doc;
        doc.title = portfolio_name + " - Risk Dashboard";
        doc.report_type = "risk";
        doc.generated_at = current_date_string();

        doc.add_title("Risk Dashboard");
        doc.add_key_values("Value at Risk", {
            {"VaR 95%", "$" + format_num(var_95)},
            {"VaR 99%", "$" + format_num(var_99)},
            {"CVaR 95%", "$" + format_num(cvar_95)},
            {"Portfolio Vol", format_pct(portfolio_vol)},
            {"Beta", format_dec(beta, 2)}
        });

        doc.add_divider();

        std::vector<ColumnDef> cols = {{"Factor", Align::Left}, {"Exposure", Align::Right}};
        std::vector<std::vector<std::string>> rows;
        for (const auto& [name, exp] : factor_exposures)
            rows.push_back({name, format_dec(exp, 3)});
        doc.add_table("Factor Exposures", cols, rows);

        doc.add_chart("Factor Exposure", "bar", factor_exposures);
        return doc;
    }

private:
    static std::string format_num(double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(0) << v;
        // Add commas (simple approach)
        std::string s = ss.str();
        int n = static_cast<int>(s.length());
        std::string result;
        int count = 0;
        bool neg = (!s.empty() && s[0] == '-');
        for (int i = n - 1; i >= (neg ? 1 : 0); --i) {
            if (count > 0 && count % 3 == 0) result = "," + result;
            result = s[i] + result;
            count++;
        }
        return neg ? "-" + result : result;
    }

    static std::string format_dec(double v, int d) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(d) << v;
        return ss.str();
    }

    static std::string format_pct(double v) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(2) << (v * 100) << "%";
        return ss.str();
    }
};

} // namespace genie::reporting

#endif // GENIE_REPORTING_PDF_REPORT_HPP
