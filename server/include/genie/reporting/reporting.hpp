/**
 * @file reporting.hpp
 * @brief Report Generation for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_REPORTING_HPP
#define GENIE_REPORTING_HPP
#include "../portfolio/portfolio.hpp"
#include "../risk/var_engine.hpp"
#include "../compliance/compliance_engine.hpp"
#include "../performance/performance_engine.hpp"

namespace genie::reporting {
using namespace genie::portfolio;
using namespace genie::risk;
using namespace genie::compliance;
using namespace genie::performance;

enum class ReportFormat { Text, CSV, JSON, HTML };

struct ReportSection { std::string title, content; std::vector<std::pair<std::string, std::string>> data; };

class ReportBuilder {
    std::string title_{"Portfolio Report"}; std::vector<ReportSection> sections_; ReportFormat format_{ReportFormat::Text};
public:
    ReportBuilder& set_title(const std::string& t) { title_ = t; return *this; }
    ReportBuilder& set_format(ReportFormat f) { format_ = f; return *this; }
    ReportBuilder& add_section(const std::string& title, const std::vector<std::pair<std::string, std::string>>& data) {
        ReportSection s; s.title = title; s.data = data; sections_.push_back(s); return *this;
    }
    ReportBuilder& add_portfolio_overview(const Portfolio& p) {
        std::vector<std::pair<std::string, std::string>> data;
        data.emplace_back("Portfolio ID", p.id()); data.emplace_back("Portfolio Name", p.name());
        data.emplace_back("NAV", "$" + std::to_string(static_cast<int>(p.nav().amount)));
        data.emplace_back("Cash", "$" + std::to_string(static_cast<int>(p.cash_balance().amount)));
        data.emplace_back("Positions", std::to_string(p.position_count()));
        data.emplace_back("Leverage", std::to_string(p.leverage()).substr(0, 4) + "x");
        return add_section("Portfolio Overview", data);
    }
    ReportBuilder& add_var_summary(const VaRResult& var) {
        std::vector<std::pair<std::string, std::string>> data;
        data.emplace_back("Method", var.method == VaRMethod::Parametric ? "Parametric" : (var.method == VaRMethod::Historical ? "Historical" : "Monte Carlo"));
        data.emplace_back("Confidence", std::to_string(static_cast<int>(var.confidence_level * 100)) + "%");
        data.emplace_back("VaR", "$" + std::to_string(static_cast<int>(var.var)));
        data.emplace_back("CVaR", "$" + std::to_string(static_cast<int>(var.cvar)));
        data.emplace_back("VaR %", std::to_string(var.var_percent).substr(0, 4) + "%");
        return add_section("Value at Risk", data);
    }
    ReportBuilder& add_compliance_summary(const ComplianceReport& c) {
        std::vector<std::pair<std::string, std::string>> data;
        data.emplace_back("Total Rules", std::to_string(c.total)); data.emplace_back("Compliant", std::to_string(c.compliant));
        data.emplace_back("Warnings", std::to_string(c.warnings)); data.emplace_back("Breaches", std::to_string(c.breaches));
        data.emplace_back("Compliance Rate", std::to_string(static_cast<int>(c.compliance_rate())) + "%");
        return add_section("Compliance Summary", data);
    }
    ReportBuilder& add_performance_summary(const PerformanceStats& s) {
        std::vector<std::pair<std::string, std::string>> data;
        data.emplace_back("Total Return", std::to_string(s.total_return * 100).substr(0, 5) + "%");
        data.emplace_back("Annualized Return", std::to_string(s.annualized_return * 100).substr(0, 5) + "%");
        data.emplace_back("Volatility", std::to_string(s.annualized_volatility * 100).substr(0, 5) + "%");
        data.emplace_back("Sharpe Ratio", std::to_string(s.sharpe_ratio).substr(0, 5));
        data.emplace_back("Max Drawdown", std::to_string(s.max_drawdown * 100).substr(0, 5) + "%");
        return add_section("Performance Summary", data);
    }
    [[nodiscard]] std::string build() const {
        switch (format_) { case ReportFormat::CSV: return build_csv(); case ReportFormat::JSON: return build_json(); case ReportFormat::HTML: return build_html(); default: return build_text(); }
    }
private:
    [[nodiscard]] std::string build_text() const {
        std::ostringstream s; s << std::string(60, '=') << "\n" << title_ << "\n" << std::string(60, '=') << "\n\n";
        for (const auto& sec : sections_) { s << "--- " << sec.title << " ---\n"; for (const auto& [k, v] : sec.data) s << std::setw(20) << std::left << k << ": " << v << "\n"; s << "\n"; }
        return s.str();
    }
    [[nodiscard]] std::string build_csv() const {
        std::ostringstream s; s << "Section,Field,Value\n";
        for (const auto& sec : sections_) for (const auto& [k, v] : sec.data) s << sec.title << "," << k << "," << v << "\n";
        return s.str();
    }
    [[nodiscard]] std::string build_json() const {
        std::ostringstream s; s << "{\n  \"title\": \"" << title_ << "\",\n  \"sections\": [\n";
        for (size_t i = 0; i < sections_.size(); ++i) {
            const auto& sec = sections_[i]; s << "    {\n      \"name\": \"" << sec.title << "\",\n      \"data\": {\n";
            for (size_t j = 0; j < sec.data.size(); ++j) { s << "        \"" << sec.data[j].first << "\": \"" << sec.data[j].second << "\""; if (j < sec.data.size() - 1) s << ","; s << "\n"; }
            s << "      }\n    }"; if (i < sections_.size() - 1) s << ","; s << "\n";
        }
        s << "  ]\n}\n"; return s.str();
    }
    [[nodiscard]] std::string build_html() const {
        std::ostringstream s;
        s << "<!DOCTYPE html>\n<html><head><title>" << title_ << "</title>\n"
          << "<style>body{font-family:Arial;margin:20px;}table{border-collapse:collapse;margin:10px 0;}"
          << "th,td{border:1px solid #ddd;padding:8px;text-align:left;}th{background:#f4f4f4;}</style></head>\n"
          << "<body><h1>" << title_ << "</h1>\n";
        for (const auto& sec : sections_) {
            s << "<h2>" << sec.title << "</h2>\n<table><tr><th>Field</th><th>Value</th></tr>\n";
            for (const auto& [k, v] : sec.data) s << "<tr><td>" << k << "</td><td>" << v << "</td></tr>\n";
            s << "</table>\n";
        }
        s << "</body></html>\n"; return s.str();
    }
};
} // namespace genie::reporting
#endif
