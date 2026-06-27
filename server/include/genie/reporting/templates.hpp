/**
 * @file templates.hpp
 * @brief JSON-based Report Templates
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements template-based report generation:
 * - JSON template definitions
 * - Variable substitution
 * - Multiple output formats (HTML, PDF, CSV)
 */

#pragma once
#ifndef GENIE_REPORTING_TEMPLATES_HPP
#define GENIE_REPORTING_TEMPLATES_HPP

#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace genie::reporting {

/**
 * @brief Report output format
 */
enum class ReportFormat {
    HTML,
    CSV,
    JSON,
    TEXT,
    PDF  // Generates HTML, PDF conversion requires external tool
};

/**
 * @brief Template section definition
 */
struct TemplateSection {
    std::string type;       // "header", "text", "table", "kpi", "chart"
    std::string title;
    std::string data_key;   // Reference to data context
    std::map<std::string, std::string> options;
};

/**
 * @brief Report template definition
 */
struct ReportTemplate {
    std::string name;
    std::string description;
    std::vector<TemplateSection> sections;
    std::map<std::string, std::string> styles;
    std::string header_html;
    std::string footer_html;
};

/**
 * @brief Data table for template rendering
 */
struct DataTable {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
};

/**
 * @brief Template engine for report generation
 */
class TemplateEngine {
    ReportTemplate template_;
    std::map<std::string, std::string> string_data_;
    std::map<std::string, double> numeric_data_;
    std::map<std::string, DataTable> table_data_;
    std::map<std::string, std::vector<std::pair<std::string, double>>> chart_data_;

public:
    /**
     * @brief Load template from JSON string
     * 
     * Expected format:
     * {
     *   "name": "Report Name",
     *   "description": "Description",
     *   "sections": [
     *     {"type": "header", "title": "{{portfolio_name}}"},
     *     {"type": "table", "data_key": "positions"},
     *     {"type": "kpi", "title": "AUM", "data_key": "aum"}
     *   ]
     * }
     */
    void load_template(const std::string& json_template) {
        template_ = parse_template_json(json_template);
    }

    /**
     * @brief Load template from file
     */
    void load_template_file(const std::string& path) {
        std::ifstream file(path);
        if (!file) return;
        
        std::ostringstream oss;
        oss << file.rdbuf();
        load_template(oss.str());
    }

    /**
     * @brief Set string data
     */
    void set_data(const std::string& key, const std::string& value) {
        string_data_[key] = value;
    }

    /**
     * @brief Set numeric data
     */
    void set_data(const std::string& key, double value) {
        numeric_data_[key] = value;
    }

    /**
     * @brief Set table data
     */
    void set_data_table(const std::string& key, const DataTable& table) {
        table_data_[key] = table;
    }

    /**
     * @brief Set chart data (label, value pairs)
     */
    void set_chart_data(const std::string& key, 
                        const std::vector<std::pair<std::string, double>>& data) {
        chart_data_[key] = data;
    }

    /**
     * @brief Render template to string
     */
    [[nodiscard]] std::string render(ReportFormat format = ReportFormat::HTML) {
        switch (format) {
            case ReportFormat::HTML:
                return render_html();
            case ReportFormat::CSV:
                return render_csv();
            case ReportFormat::JSON:
                return render_json();
            case ReportFormat::TEXT:
                return render_text();
            case ReportFormat::PDF:
                return render_html();  // PDF requires external conversion
            default:
                return "";
        }
    }

    /**
     * @brief Render to file
     */
    void render_to_file(const std::string& path, ReportFormat format = ReportFormat::HTML) {
        std::ofstream file(path);
        if (file) {
            file << render(format);
        }
    }

private:
    [[nodiscard]] ReportTemplate parse_template_json(const std::string& json) {
        ReportTemplate tmpl;
        
        // Simple JSON parsing (production would use a proper JSON library)
        auto get_string = [&](const std::string& key) -> std::string {
            std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch match;
            if (std::regex_search(json, match, re)) {
                return match[1].str();
            }
            return "";
        };
        
        tmpl.name = get_string("name");
        tmpl.description = get_string("description");
        
        // Parse sections
        std::regex section_re("\\{\\s*\"type\"\\s*:\\s*\"([^\"]+)\"[^}]*\\}");
        auto sections_begin = std::sregex_iterator(json.begin(), json.end(), section_re);
        auto sections_end = std::sregex_iterator();
        
        for (auto it = sections_begin; it != sections_end; ++it) {
            std::string section_json = it->str();
            TemplateSection section;
            
            std::regex type_re("\"type\"\\s*:\\s*\"([^\"]+)\"");
            std::regex title_re("\"title\"\\s*:\\s*\"([^\"]+)\"");
            std::regex key_re("\"data_key\"\\s*:\\s*\"([^\"]+)\"");
            
            std::smatch match;
            if (std::regex_search(section_json, match, type_re)) {
                section.type = match[1].str();
            }
            if (std::regex_search(section_json, match, title_re)) {
                section.title = match[1].str();
            }
            if (std::regex_search(section_json, match, key_re)) {
                section.data_key = match[1].str();
            }
            
            tmpl.sections.push_back(section);
        }
        
        return tmpl;
    }

    [[nodiscard]] std::string substitute_variables(const std::string& text) {
        std::string result = text;
        
        // Replace {{key}} with values
        std::regex var_re("\\{\\{([^}]+)\\}\\}");
        std::string output;
        auto begin = std::sregex_iterator(result.begin(), result.end(), var_re);
        auto end = std::sregex_iterator();
        size_t last_pos = 0;
        
        for (auto it = begin; it != end; ++it) {
            output += result.substr(last_pos, it->position() - last_pos);
            std::string key = (*it)[1].str();
            
            // Look up value
            if (auto sit = string_data_.find(key); sit != string_data_.end()) {
                output += sit->second;
            } else if (auto nit = numeric_data_.find(key); nit != numeric_data_.end()) {
                std::ostringstream oss;
                oss << nit->second;
                output += oss.str();
            } else {
                output += "{{" + key + "}}";  // Keep unresolved
            }
            
            last_pos = it->position() + it->length();
        }
        output += result.substr(last_pos);
        
        return output;
    }

    [[nodiscard]] std::string render_html() {
        std::ostringstream html;
        
        html << "<!DOCTYPE html>\n<html>\n<head>\n";
        html << "<title>" << template_.name << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 40px; }\n";
        html << "h1 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }\n";
        html << "h2 { color: #555; margin-top: 30px; }\n";
        html << "table { border-collapse: collapse; width: 100%; margin: 20px 0; }\n";
        html << "th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }\n";
        html << "th { background-color: #007bff; color: white; }\n";
        html << "tr:nth-child(even) { background-color: #f2f2f2; }\n";
        html << ".kpi { display: inline-block; margin: 10px; padding: 20px; ";
        html << "background: #f8f9fa; border-radius: 8px; text-align: center; }\n";
        html << ".kpi-value { font-size: 24px; font-weight: bold; color: #007bff; }\n";
        html << ".kpi-label { font-size: 12px; color: #666; }\n";
        html << "</style>\n</head>\n<body>\n";
        
        // Render sections
        for (const auto& section : template_.sections) {
            if (section.type == "header") {
                html << "<h1>" << substitute_variables(section.title) << "</h1>\n";
            } else if (section.type == "text") {
                html << "<p>" << substitute_variables(section.title) << "</p>\n";
            } else if (section.type == "table") {
                auto it = table_data_.find(section.data_key);
                if (it != table_data_.end()) {
                    html << render_table_html(section.title, it->second);
                }
            } else if (section.type == "kpi") {
                auto nit = numeric_data_.find(section.data_key);
                if (nit != numeric_data_.end()) {
                    html << "<div class=\"kpi\">\n";
                    html << "  <div class=\"kpi-value\">" << nit->second << "</div>\n";
                    html << "  <div class=\"kpi-label\">" << section.title << "</div>\n";
                    html << "</div>\n";
                }
            }
        }
        
        html << "</body>\n</html>\n";
        return html.str();
    }

    [[nodiscard]] std::string render_table_html(const std::string& title, const DataTable& table) {
        std::ostringstream html;
        
        if (!title.empty()) {
            html << "<h2>" << substitute_variables(title) << "</h2>\n";
        }
        
        html << "<table>\n<thead>\n<tr>\n";
        for (const auto& header : table.headers) {
            html << "  <th>" << header << "</th>\n";
        }
        html << "</tr>\n</thead>\n<tbody>\n";
        
        for (const auto& row : table.rows) {
            html << "<tr>\n";
            for (const auto& cell : row) {
                html << "  <td>" << cell << "</td>\n";
            }
            html << "</tr>\n";
        }
        
        html << "</tbody>\n</table>\n";
        return html.str();
    }

    [[nodiscard]] std::string render_csv() {
        std::ostringstream csv;
        
        // Export first table found
        for (const auto& section : template_.sections) {
            if (section.type == "table") {
                auto it = table_data_.find(section.data_key);
                if (it != table_data_.end()) {
                    const auto& table = it->second;
                    
                    // Headers
                    for (size_t i = 0; i < table.headers.size(); ++i) {
                        if (i > 0) csv << ",";
                        csv << "\"" << table.headers[i] << "\"";
                    }
                    csv << "\n";
                    
                    // Rows
                    for (const auto& row : table.rows) {
                        for (size_t i = 0; i < row.size(); ++i) {
                            if (i > 0) csv << ",";
                            csv << "\"" << row[i] << "\"";
                        }
                        csv << "\n";
                    }
                    break;
                }
            }
        }
        
        return csv.str();
    }

    [[nodiscard]] std::string render_json() {
        std::ostringstream json;
        json << "{\n";
        json << "  \"name\": \"" << template_.name << "\",\n";
        json << "  \"data\": {\n";
        
        bool first = true;
        for (const auto& [key, value] : string_data_) {
            if (!first) json << ",\n";
            json << "    \"" << key << "\": \"" << value << "\"";
            first = false;
        }
        for (const auto& [key, value] : numeric_data_) {
            if (!first) json << ",\n";
            json << "    \"" << key << "\": " << value;
            first = false;
        }
        
        json << "\n  }\n}\n";
        return json.str();
    }

    [[nodiscard]] std::string render_text() {
        std::ostringstream text;
        
        text << "=" << std::string(60, '=') << "\n";
        text << template_.name << "\n";
        text << "=" << std::string(60, '=') << "\n\n";
        
        for (const auto& section : template_.sections) {
            if (section.type == "header") {
                text << substitute_variables(section.title) << "\n";
                text << std::string(section.title.size(), '-') << "\n\n";
            } else if (section.type == "table") {
                auto it = table_data_.find(section.data_key);
                if (it != table_data_.end()) {
                    text << render_table_text(it->second);
                }
            } else if (section.type == "kpi") {
                auto nit = numeric_data_.find(section.data_key);
                if (nit != numeric_data_.end()) {
                    text << section.title << ": " << nit->second << "\n";
                }
            }
        }
        
        return text.str();
    }

    [[nodiscard]] std::string render_table_text(const DataTable& table) {
        std::ostringstream text;
        
        // Simple text table
        for (const auto& header : table.headers) {
            text << header << "\t";
        }
        text << "\n";
        
        for (const auto& row : table.rows) {
            for (const auto& cell : row) {
                text << cell << "\t";
            }
            text << "\n";
        }
        text << "\n";
        
        return text.str();
    }
};

} // namespace genie::reporting

#endif // GENIE_REPORTING_TEMPLATES_HPP
