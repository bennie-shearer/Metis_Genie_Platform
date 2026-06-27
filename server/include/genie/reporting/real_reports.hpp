/**
 * @file real_reports.hpp
 * @brief Real data reporting with PDF generation
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Comprehensive reporting functionality:
 * - Account statement PDF with real data
 * - Performance report with actual returns
 * - Trade confirmation generation
 * - Tax report export (Form 8949 format)
 * - Risk report with real VaR calculations
 */
#pragma once
#ifndef GENIE_REPORTING_REAL_REPORTS_HPP
#define GENIE_REPORTING_REAL_REPORTS_HPP

#include "../analytics/real_risk.hpp"
#include "../analytics/real_performance.hpp"
#include "../tax/tax_tracking.hpp"
#include "../trading/broker_abstraction.hpp"
#include "pdf_report.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace genie::reporting {

// ============================================================================
// PDF Builder Wrapper
// ============================================================================

/**
 * @brief Simple PDF builder that wraps ReportDocument
 */
class PdfBuilder {
public:
    void add_title(const std::string& title) {
        doc_.add_title(title);
    }
    
    void add_subtitle(const std::string& subtitle) {
        doc_.add_subtitle(subtitle);
    }
    
    void add_text(const std::string& text) {
        doc_.add_paragraph(text);
    }
    
    void add_section(const std::string& section_title) {
        doc_.add_subtitle(section_title);
    }
    
    void add_separator() {
        doc_.add_divider();
    }
    
    void add_key_value_table(const std::vector<std::vector<std::string>>& rows) {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto& row : rows) {
            if (row.size() >= 2) {
                pairs.emplace_back(row[0], row[1]);
            }
        }
        doc_.add_key_values("", pairs);
    }
    
    void add_two_column_table(const std::string& title1,
                              const std::vector<std::vector<std::string>>& rows1,
                              const std::string& title2,
                              const std::vector<std::vector<std::string>>& rows2) {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto& row : rows1) {
            if (row.size() >= 2) {
                pairs.emplace_back(row[0], row[1]);
            }
        }
        doc_.add_key_values(title1, pairs);
        
        pairs.clear();
        for (const auto& row : rows2) {
            if (row.size() >= 2) {
                pairs.emplace_back(row[0], row[1]);
            }
        }
        doc_.add_key_values(title2, pairs);
    }
    
    void add_table(const std::vector<std::string>& headers,
                   const std::vector<std::vector<std::string>>& rows) {
        std::vector<ColumnDef> cols;
        for (const auto& h : headers) {
            cols.push_back({h, Align::Left, 0});
        }
        doc_.add_table("", cols, rows);
    }
    
    void add_disclaimer(const std::string& text) {
        doc_.add_paragraph(text);
    }
    
    std::string save(const std::string& path) {
        // Convert to HTML and save
        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html><head>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 40px; }\n";
        html << "h1 { color: #333; border-bottom: 2px solid #333; }\n";
        html << "h2 { color: #666; margin-top: 20px; }\n";
        html << "table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n";
        html << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
        html << "th { background-color: #f2f2f2; }\n";
        html << ".kv-table { width: auto; }\n";
        html << ".kv-table td:first-child { font-weight: bold; padding-right: 20px; }\n";
        html << "hr { border: none; border-top: 1px solid #ccc; margin: 20px 0; }\n";
        html << ".disclaimer { font-size: 0.9em; color: #666; font-style: italic; }\n";
        html << "</style>\n</head><body>\n";
        
        for (const auto& el : doc_.elements) {
            switch (el.type) {
                case ElementType::Title:
                    html << "<h1>" << el.text << "</h1>\n";
                    break;
                case ElementType::Subtitle:
                    html << "<h2>" << el.text << "</h2>\n";
                    break;
                case ElementType::Paragraph:
                    html << "<p>" << el.text << "</p>\n";
                    break;
                case ElementType::Divider:
                    html << "<hr>\n";
                    break;
                case ElementType::KeyValue:
                    if (!el.text.empty()) {
                        html << "<h3>" << el.text << "</h3>\n";
                    }
                    html << "<table class='kv-table'>\n";
                    for (const auto& [k, v] : el.kv_pairs) {
                        html << "<tr><td>" << k << "</td><td>" << v << "</td></tr>\n";
                    }
                    html << "</table>\n";
                    break;
                case ElementType::Table:
                    if (!el.text.empty()) {
                        html << "<h3>" << el.text << "</h3>\n";
                    }
                    html << "<table>\n<tr>";
                    for (const auto& col : el.columns) {
                        html << "<th>" << col.header << "</th>";
                    }
                    html << "</tr>\n";
                    for (const auto& row : el.rows) {
                        html << "<tr>";
                        for (const auto& cell : row) {
                            html << "<td>" << cell << "</td>";
                        }
                        html << "</tr>\n";
                    }
                    html << "</table>\n";
                    break;
                default:
                    break;
            }
        }
        
        html << "</body></html>\n";
        
        std::ofstream file(path);
        if (file) {
            file << html.str();
            file.close();
        }
        
        return path;
    }
    
    ReportDocument& document() { return doc_; }

private:
    ReportDocument doc_;
};

// ============================================================================
// Common Report Elements
// ============================================================================

/**
 * @brief Report metadata
 */
struct ReportMetadata {
    std::string title;
    std::string account_id;
    std::string account_name;
    std::string period_start;
    std::string period_end;
    std::string generated_at;
    std::string report_id;
    
    static ReportMetadata create(const std::string& title,
                                  const std::string& account_id,
                                  const std::string& start,
                                  const std::string& end) {
        ReportMetadata meta;
        meta.title = title;
        meta.account_id = account_id;
        meta.period_start = start;
        meta.period_end = end;
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        meta.generated_at = oss.str();
        
        // Generate report ID
        oss.str("");
        oss << "RPT-" << std::put_time(std::localtime(&time), "%Y%m%d%H%M%S");
        meta.report_id = oss.str();
        
        return meta;
    }
};

/**
 * @brief Format currency
 */
inline std::string format_currency(double amount, bool show_sign = false) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (show_sign && amount > 0) oss << "+";
    oss << "$" << amount;
    return oss.str();
}

/**
 * @brief Format percentage
 */
inline std::string format_percent(double pct, bool show_sign = true) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (show_sign && pct > 0) oss << "+";
    oss << pct << "%";
    return oss.str();
}

// ============================================================================
// Account Statement
// ============================================================================

/**
 * @brief Account statement data
 */
struct AccountStatementData {
    ReportMetadata metadata;
    
    // Account summary
    double beginning_value{0};
    double ending_value{0};
    double net_deposits{0};
    double net_withdrawals{0};
    double dividends_received{0};
    double interest_received{0};
    double fees_paid{0};
    double realized_gains{0};
    double unrealized_gains{0};
    
    // Holdings
    struct Holding {
        std::string symbol;
        std::string name;
        double shares{0};
        double price{0};
        double market_value{0};
        double cost_basis{0};
        double unrealized_gain{0};
        double unrealized_gain_pct{0};
        double weight{0};
    };
    std::vector<Holding> holdings;
    
    // Activity
    struct Transaction {
        std::string date;
        std::string type;
        std::string symbol;
        std::string description;
        double amount{0};
        double shares{0};
        double price{0};
    };
    std::vector<Transaction> transactions;
    
    // Cash activity
    double beginning_cash{0};
    double ending_cash{0};
    
    double total_change() const {
        return ending_value - beginning_value;
    }
    
    double investment_return() const {
        double adjusted_begin = beginning_value + net_deposits - net_withdrawals;
        if (adjusted_begin <= 0) return 0;
        return (ending_value - adjusted_begin) / adjusted_begin * 100;
    }
};

/**
 * @brief Generate account statement PDF
 */
class AccountStatementGenerator {
public:
    std::string generate(const AccountStatementData& data,
                         const std::string& output_path) {
        
        PdfBuilder pdf;
        
        // Header
        pdf.add_title(data.metadata.title);
        pdf.add_subtitle("Account: " + data.metadata.account_id);
        pdf.add_text("Period: " + data.metadata.period_start + " to " + 
                    data.metadata.period_end);
        pdf.add_text("Generated: " + data.metadata.generated_at);
        pdf.add_separator();
        
        // Account Summary
        pdf.add_section("Account Summary");
        
        std::vector<std::vector<std::string>> summary = {
            {"Beginning Value", format_currency(data.beginning_value)},
            {"Net Deposits", format_currency(data.net_deposits)},
            {"Net Withdrawals", format_currency(data.net_withdrawals)},
            {"Dividends", format_currency(data.dividends_received)},
            {"Interest", format_currency(data.interest_received)},
            {"Fees", format_currency(-data.fees_paid)},
            {"Realized Gains", format_currency(data.realized_gains, true)},
            {"Unrealized Gains", format_currency(data.unrealized_gains, true)},
            {"Ending Value", format_currency(data.ending_value)},
            {"Total Change", format_currency(data.total_change(), true)},
            {"Period Return", format_percent(data.investment_return())}
        };
        
        pdf.add_key_value_table(summary);
        pdf.add_separator();
        
        // Holdings
        if (!data.holdings.empty()) {
            pdf.add_section("Holdings");
            
            std::vector<std::string> headers = {
                "Symbol", "Shares", "Price", "Market Value", 
                "Cost Basis", "Gain/Loss", "Weight"
            };
            
            std::vector<std::vector<std::string>> rows;
            double total_value = 0;
            
            for (const auto& h : data.holdings) {
                rows.push_back({
                    h.symbol,
                    std::to_string(static_cast<int>(h.shares)),
                    format_currency(h.price),
                    format_currency(h.market_value),
                    format_currency(h.cost_basis),
                    format_currency(h.unrealized_gain, true),
                    format_percent(h.weight, false)
                });
                total_value += h.market_value;
            }
            
            // Add total row
            rows.push_back({
                "TOTAL", "", "", format_currency(total_value),
                "", "", "100.00%"
            });
            
            pdf.add_table(headers, rows);
            pdf.add_separator();
        }
        
        // Activity
        if (!data.transactions.empty()) {
            pdf.add_section("Account Activity");
            
            std::vector<std::string> headers = {
                "Date", "Type", "Symbol", "Description", "Amount"
            };
            
            std::vector<std::vector<std::string>> rows;
            for (const auto& t : data.transactions) {
                rows.push_back({
                    t.date, t.type, t.symbol, t.description,
                    format_currency(t.amount, true)
                });
            }
            
            pdf.add_table(headers, rows);
        }
        
        // Footer
        pdf.add_separator();
        pdf.add_text("Report ID: " + data.metadata.report_id);
        pdf.add_disclaimer("This statement is provided for informational purposes only.");
        
        return pdf.save(output_path);
    }
};

// ============================================================================
// Performance Report
// ============================================================================

/**
 * @brief Performance report data
 */
struct PerformanceReportData {
    ReportMetadata metadata;
    std::string benchmark_name{"S&P 500"};
    
    // Returns
    double total_return{0};
    double annualized_return{0};
    double benchmark_return{0};
    double benchmark_annualized{0};
    double excess_return{0};
    
    // Risk metrics
    double volatility{0};
    double benchmark_volatility{0};
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double beta{0};
    double alpha{0};
    double r_squared{0};
    double tracking_error{0};
    double information_ratio{0};
    double max_drawdown{0};
    double calmar_ratio{0};
    
    // Capture ratios
    double up_capture{0};
    double down_capture{0};
    
    // Rolling returns
    struct RollingPeriod {
        std::string period_name;
        double portfolio_return{0};
        double benchmark_return{0};
        double excess{0};
    };
    std::vector<RollingPeriod> rolling_returns;
    
    // Monthly returns
    struct MonthlyReturn {
        std::string month;
        double portfolio{0};
        double benchmark{0};
    };
    std::vector<MonthlyReturn> monthly_returns;
    
    // Best/Worst periods
    std::string best_month;
    double best_month_return{0};
    std::string worst_month;
    double worst_month_return{0};
    
    // Win rate
    int positive_months{0};
    int negative_months{0};
    double win_rate{0};
};

/**
 * @brief Generate performance report PDF
 */
class PerformanceReportGenerator {
public:
    std::string generate(const PerformanceReportData& data,
                         const std::string& output_path) {
        
        PdfBuilder pdf;
        
        // Header
        pdf.add_title(data.metadata.title);
        pdf.add_subtitle("Performance Analysis");
        pdf.add_text("Period: " + data.metadata.period_start + " to " + 
                    data.metadata.period_end);
        pdf.add_text("Benchmark: " + data.benchmark_name);
        pdf.add_separator();
        
        // Return Summary
        pdf.add_section("Return Summary");
        
        std::vector<std::vector<std::string>> returns = {
            {"", "Portfolio", "Benchmark", "Excess"},
            {"Total Return", format_percent(data.total_return),
             format_percent(data.benchmark_return),
             format_percent(data.excess_return)},
            {"Annualized Return", format_percent(data.annualized_return),
             format_percent(data.benchmark_annualized),
             format_percent(data.annualized_return - data.benchmark_annualized)}
        };
        
        pdf.add_table(returns[0], {returns[1], returns[2]});
        pdf.add_separator();
        
        // Risk Metrics
        pdf.add_section("Risk Metrics");
        
        std::vector<std::vector<std::string>> risk = {
            {"Volatility (Ann.)", format_percent(data.volatility)},
            {"Sharpe Ratio", std::to_string(data.sharpe_ratio).substr(0, 5)},
            {"Sortino Ratio", std::to_string(data.sortino_ratio).substr(0, 5)},
            {"Max Drawdown", format_percent(data.max_drawdown)},
            {"Calmar Ratio", std::to_string(data.calmar_ratio).substr(0, 5)}
        };
        
        std::vector<std::vector<std::string>> benchmark_rel = {
            {"Beta", std::to_string(data.beta).substr(0, 5)},
            {"Alpha (Ann.)", format_percent(data.alpha)},
            {"R-Squared", format_percent(data.r_squared)},
            {"Tracking Error", format_percent(data.tracking_error)},
            {"Information Ratio", std::to_string(data.information_ratio).substr(0, 5)}
        };
        
        pdf.add_two_column_table("Risk Metrics", risk, "Benchmark Relative", benchmark_rel);
        pdf.add_separator();
        
        // Capture Ratios
        pdf.add_section("Capture Analysis");
        
        std::vector<std::vector<std::string>> capture = {
            {"Up Capture", format_percent(data.up_capture)},
            {"Down Capture", format_percent(data.down_capture)},
            {"Capture Ratio", std::to_string(data.up_capture / 
                (data.down_capture > 0 ? data.down_capture : 1)).substr(0, 5)}
        };
        
        pdf.add_key_value_table(capture);
        pdf.add_separator();
        
        // Rolling Returns
        if (!data.rolling_returns.empty()) {
            pdf.add_section("Rolling Returns");
            
            std::vector<std::string> headers = {"Period", "Portfolio", "Benchmark", "Excess"};
            std::vector<std::vector<std::string>> rows;
            
            for (const auto& r : data.rolling_returns) {
                rows.push_back({
                    r.period_name,
                    format_percent(r.portfolio_return),
                    format_percent(r.benchmark_return),
                    format_percent(r.excess)
                });
            }
            
            pdf.add_table(headers, rows);
            pdf.add_separator();
        }
        
        // Monthly Returns
        if (!data.monthly_returns.empty()) {
            pdf.add_section("Monthly Returns");
            
            std::vector<std::string> headers = {"Month", "Portfolio", "Benchmark"};
            std::vector<std::vector<std::string>> rows;
            
            for (const auto& m : data.monthly_returns) {
                rows.push_back({
                    m.month,
                    format_percent(m.portfolio),
                    format_percent(m.benchmark)
                });
            }
            
            pdf.add_table(headers, rows);
        }
        
        // Win Rate
        pdf.add_section("Statistics");
        
        std::vector<std::vector<std::string>> stats = {
            {"Positive Months", std::to_string(data.positive_months)},
            {"Negative Months", std::to_string(data.negative_months)},
            {"Win Rate", format_percent(data.win_rate)},
            {"Best Month", data.best_month + " (" + format_percent(data.best_month_return) + ")"},
            {"Worst Month", data.worst_month + " (" + format_percent(data.worst_month_return) + ")"}
        };
        
        pdf.add_key_value_table(stats);
        
        // Footer
        pdf.add_separator();
        pdf.add_text("Report ID: " + data.metadata.report_id);
        pdf.add_disclaimer("Past performance does not guarantee future results.");
        
        return pdf.save(output_path);
    }
    
    /**
     * @brief Generate from real analytics data
     */
    PerformanceReportData build_from_analytics(
        const std::string& account_id,
        const std::vector<double>& portfolio_returns,
        const std::vector<double>& benchmark_returns,
        const std::vector<std::string>& dates,
        const std::string& benchmark_name = "S&P 500") {
        
        PerformanceReportData data;
        data.metadata = ReportMetadata::create(
            "Performance Report",
            account_id,
            dates.front(),
            dates.back());
        data.benchmark_name = benchmark_name;
        
        // Calculate TWR
        auto twr = analytics::TWRCalculator::calculate(
            accumulate_returns(portfolio_returns), dates, {});
        data.total_return = twr.total_return * 100;
        data.annualized_return = twr.annualized_return * 100;
        
        auto bench_twr = analytics::TWRCalculator::calculate(
            accumulate_returns(benchmark_returns), dates, {});
        data.benchmark_return = bench_twr.total_return * 100;
        data.benchmark_annualized = bench_twr.annualized_return * 100;
        data.excess_return = data.total_return - data.benchmark_return;
        
        // Risk metrics
        auto vol = analytics::VolatilityCalculator::calculate(portfolio_returns);
        data.volatility = vol.annualized * 100;
        
        auto bench_vol = analytics::VolatilityCalculator::calculate(benchmark_returns);
        data.benchmark_volatility = bench_vol.annualized * 100;
        
        // Risk-adjusted
        auto risk_adj = analytics::RiskAdjustedCalculator::calculate(
            portfolio_returns, 0.02, 1.0, 0.1);
        data.sharpe_ratio = risk_adj.sharpe_ratio;
        data.sortino_ratio = risk_adj.sortino_ratio;
        data.calmar_ratio = risk_adj.calmar_ratio;
        
        // Beta
        auto beta_result = analytics::BetaCalculator::calculate(
            portfolio_returns, benchmark_returns, "", "");
        data.beta = beta_result.beta;
        data.alpha = beta_result.alpha * 100;
        data.r_squared = beta_result.r_squared * 100;
        data.tracking_error = beta_result.tracking_error * 100;
        
        // Benchmark comparison
        auto bench_comp = analytics::BenchmarkCalculator::compare(
            portfolio_returns, benchmark_returns, benchmark_name);
        data.information_ratio = bench_comp.information_ratio;
        data.up_capture = bench_comp.up_capture * 100;
        data.down_capture = bench_comp.down_capture * 100;
        
        // Drawdown
        auto dd = analytics::DrawdownTracker::analyze(
            accumulate_returns(portfolio_returns), dates);
        data.max_drawdown = dd.max_drawdown_pct;
        
        // Monthly aggregation
        calculate_monthly_stats(data, portfolio_returns, benchmark_returns, dates);
        
        return data;
    }

private:
    std::vector<double> accumulate_returns(const std::vector<double>& returns) {
        std::vector<double> nav;
        nav.reserve(returns.size() + 1);
        nav.push_back(1.0);
        
        for (double r : returns) {
            nav.push_back(nav.back() * (1 + r));
        }
        
        return nav;
    }
    
    void calculate_monthly_stats(PerformanceReportData& data,
                                 const std::vector<double>& port_returns,
                                 const std::vector<double>& bench_returns,
                                 const std::vector<std::string>& dates) {
        
        // Simplified monthly aggregation
        std::map<std::string, double> monthly_port;
        std::map<std::string, double> monthly_bench;
        
        for (size_t i = 0; i < dates.size() && i < port_returns.size(); ++i) {
            std::string month = dates[i].substr(0, 7);  // YYYY-MM
            monthly_port[month] = (1 + monthly_port[month]) * (1 + port_returns[i]) - 1;
            if (i < bench_returns.size()) {
                monthly_bench[month] = (1 + monthly_bench[month]) * (1 + bench_returns[i]) - 1;
            }
        }
        
        double best = -999, worst = 999;
        
        for (const auto& [month, ret] : monthly_port) {
            PerformanceReportData::MonthlyReturn mr;
            mr.month = month;
            mr.portfolio = ret * 100;
            mr.benchmark = monthly_bench[month] * 100;
            data.monthly_returns.push_back(mr);
            
            if (ret > 0) data.positive_months++;
            else data.negative_months++;
            
            if (ret > best) {
                best = ret;
                data.best_month = month;
                data.best_month_return = ret * 100;
            }
            if (ret < worst) {
                worst = ret;
                data.worst_month = month;
                data.worst_month_return = ret * 100;
            }
        }
        
        int total = data.positive_months + data.negative_months;
        data.win_rate = total > 0 ? (data.positive_months * 100.0 / total) : 0;
    }
};

// ============================================================================
// Trade Confirmation
// ============================================================================

/**
 * @brief Trade confirmation data
 */
struct TradeConfirmationData {
    std::string confirmation_number;
    std::string trade_date;
    std::string settlement_date;
    std::string account_id;
    std::string account_name;
    
    // Trade details
    std::string symbol;
    std::string security_name;
    std::string action;               // BUY, SELL, etc.
    double quantity{0};
    double price{0};
    double principal{0};
    
    // Fees
    double commission{0};
    double sec_fee{0};
    double taf_fee{0};
    double exchange_fee{0};
    double total_fees{0};
    
    // Settlement
    double net_amount{0};             // Principal +/- fees
    std::string currency{"USD"};
    
    // Order details
    std::string order_type;
    std::string time_in_force;
    std::string execution_venue;
    std::string capacity;             // Principal, Agency
    
    std::string format_summary() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << action << " " << quantity << " " << symbol << " @ $" << price;
        oss << " = $" << principal;
        return oss.str();
    }
};

/**
 * @brief Generate trade confirmation PDF
 */
class TradeConfirmationGenerator {
public:
    std::string generate(const TradeConfirmationData& data,
                         const std::string& output_path) {
        
        PdfBuilder pdf;
        
        // Header
        pdf.add_title("Trade Confirmation");
        pdf.add_text("Confirmation #: " + data.confirmation_number);
        pdf.add_separator();
        
        // Account Info
        pdf.add_section("Account Information");
        std::vector<std::vector<std::string>> account_info = {
            {"Account", data.account_id},
            {"Name", data.account_name},
            {"Trade Date", data.trade_date},
            {"Settlement Date", data.settlement_date}
        };
        pdf.add_key_value_table(account_info);
        pdf.add_separator();
        
        // Trade Details
        pdf.add_section("Trade Details");
        std::vector<std::vector<std::string>> trade_info = {
            {"Action", data.action},
            {"Symbol", data.symbol},
            {"Security", data.security_name},
            {"Quantity", std::to_string(static_cast<int>(data.quantity))},
            {"Price", format_currency(data.price)},
            {"Principal", format_currency(data.principal)}
        };
        pdf.add_key_value_table(trade_info);
        pdf.add_separator();
        
        // Fees
        pdf.add_section("Fees and Charges");
        std::vector<std::vector<std::string>> fees = {
            {"Commission", format_currency(data.commission)},
            {"SEC Fee", format_currency(data.sec_fee)},
            {"TAF Fee", format_currency(data.taf_fee)},
            {"Exchange Fee", format_currency(data.exchange_fee)},
            {"Total Fees", format_currency(data.total_fees)}
        };
        pdf.add_key_value_table(fees);
        pdf.add_separator();
        
        // Settlement
        pdf.add_section("Settlement");
        std::string net_label = data.action == "BUY" ? "Amount Due" : "Net Proceeds";
        std::vector<std::vector<std::string>> settlement = {
            {net_label, format_currency(data.net_amount)},
            {"Currency", data.currency}
        };
        pdf.add_key_value_table(settlement);
        pdf.add_separator();
        
        // Order Details
        pdf.add_section("Order Information");
        std::vector<std::vector<std::string>> order_info = {
            {"Order Type", data.order_type},
            {"Time in Force", data.time_in_force},
            {"Execution Venue", data.execution_venue},
            {"Capacity", data.capacity}
        };
        pdf.add_key_value_table(order_info);
        
        // Footer
        pdf.add_separator();
        pdf.add_disclaimer(
            "This confirmation is subject to the terms and conditions of your "
            "account agreement. Please review and report any discrepancies within "
            "10 business days.");
        
        return pdf.save(output_path);
    }
    
    /**
     * @brief Generate from order data
     */
    TradeConfirmationData build_from_order(
        const trading::UnifiedOrder& order,
        const trading::UnifiedAccount& account,
        double commission_rate = 0.0,
        const std::string& security_name = "") {
        
        TradeConfirmationData data;
        
        // Generate confirmation number
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream conf_oss;
        conf_oss << "CONF-" << std::put_time(std::localtime(&time), "%Y%m%d")
                 << "-" << order.id.substr(0, 8);
        data.confirmation_number = conf_oss.str();
        
        data.account_id = account.id;
        data.account_name = account.name;
        data.trade_date = order.filled_at.empty() ? order.created_at : order.filled_at;
        
        // Settlement T+1 for equities
        data.settlement_date = data.trade_date;  // Simplified
        
        data.symbol = order.symbol;
        data.security_name = security_name.empty() ? order.symbol + " Common Stock" : security_name;
        data.action = order.side == trading::OrderSide::Buy ? "BUY" : "SELL";
        data.quantity = order.filled_qty > 0 ? order.filled_qty : order.qty;
        data.price = order.filled_avg_price > 0 ? order.filled_avg_price : 0;
        data.principal = data.quantity * data.price;
        
        // Calculate fees
        data.commission = data.quantity * commission_rate;
        if (data.action == "SELL") {
            data.sec_fee = data.principal * 0.0000278;  // SEC fee
            data.taf_fee = data.quantity * 0.000166;    // TAF fee
        }
        data.total_fees = data.commission + data.sec_fee + data.taf_fee + data.exchange_fee;
        
        // Net amount
        if (data.action == "BUY") {
            data.net_amount = data.principal + data.total_fees;
        } else {
            data.net_amount = data.principal - data.total_fees;
        }
        
        data.order_type = trading::order_type_to_string(order.type);
        data.time_in_force = trading::time_in_force_to_string(order.time_in_force);
        data.execution_venue = "NASDAQ";  // Simplified
        data.capacity = "Agency";
        
        return data;
    }
};

// ============================================================================
// Tax Report (Form 8949)
// ============================================================================

/**
 * @brief Tax report data
 */
struct TaxReportData {
    ReportMetadata metadata;
    int tax_year{0};
    
    // Summary
    tax::TaxSummary summary;
    
    // Form 8949 lines
    std::vector<tax::Form8949Line> form_8949_lines;
    
    // Grouped by box
    std::map<char, std::vector<tax::Form8949Line>> by_box;
    std::map<char, double> box_totals;
};

/**
 * @brief Generate tax report PDF
 */
class TaxReportGenerator {
public:
    std::string generate(const TaxReportData& data,
                         const std::string& output_path) {
        
        PdfBuilder pdf;
        
        // Header
        pdf.add_title("Tax Report - Form 8949 Data");
        pdf.add_subtitle("Tax Year " + std::to_string(data.tax_year));
        pdf.add_text("Account: " + data.metadata.account_id);
        pdf.add_text("Generated: " + data.metadata.generated_at);
        pdf.add_separator();
        
        // Summary
        pdf.add_section("Summary");
        
        std::vector<std::vector<std::string>> summary = {
            {"", "Short-Term", "Long-Term", "Total"},
            {"Gains", format_currency(data.summary.short_term_gains),
             format_currency(data.summary.long_term_gains),
             format_currency(data.summary.total_gains())},
            {"Losses", format_currency(data.summary.short_term_losses),
             format_currency(data.summary.long_term_losses),
             format_currency(data.summary.total_losses())},
            {"Net", format_currency(data.summary.net_short_term()),
             format_currency(data.summary.net_long_term()),
             format_currency(data.summary.net_gain_loss())},
            {"Transactions", std::to_string(data.summary.short_term_transactions),
             std::to_string(data.summary.long_term_transactions),
             std::to_string(data.summary.total_transactions())}
        };
        
        pdf.add_table(summary[0], {summary[1], summary[2], summary[3], summary[4]});
        pdf.add_separator();
        
        // Form 8949 by box
        const std::map<char, std::string> box_descriptions = {
            {'A', "Short-term - Basis reported to IRS"},
            {'B', "Short-term - Basis reported, adjustment needed"},
            {'C', "Short-term - Basis NOT reported to IRS"},
            {'D', "Long-term - Basis reported to IRS"},
            {'E', "Long-term - Basis reported, adjustment needed"},
            {'F', "Long-term - Basis NOT reported to IRS"}
        };
        
        for (const auto& [box, lines] : data.by_box) {
            if (lines.empty()) continue;
            
            std::string box_title = std::string("Box ") + box + " - " + 
                box_descriptions.at(box);
            pdf.add_section(box_title);
            
            std::vector<std::string> headers = {
                "Description", "Acquired", "Sold", "Proceeds", 
                "Cost", "Adj", "Gain/Loss"
            };
            
            std::vector<std::vector<std::string>> rows;
            double box_total = 0;
            
            for (const auto& line : lines) {
                rows.push_back({
                    line.description,
                    line.date_acquired,
                    line.date_sold,
                    format_currency(line.proceeds),
                    format_currency(line.cost_basis),
                    line.adjustment_code.empty() ? "" : 
                        line.adjustment_code + " " + format_currency(line.adjustment_amount),
                    format_currency(line.gain_loss, true)
                });
                box_total += line.gain_loss;
            }
            
            // Add subtotal
            rows.push_back({
                "SUBTOTAL BOX " + std::string(1, box), "", "", "", "", "",
                format_currency(box_total, true)
            });
            
            pdf.add_table(headers, rows);
            pdf.add_separator();
        }
        
        // Wash Sale Summary
        double total_wash = data.summary.short_term_wash_adjustments + 
                           data.summary.long_term_wash_adjustments;
        if (total_wash > 0) {
            pdf.add_section("Wash Sale Adjustments");
            std::vector<std::vector<std::string>> wash = {
                {"Short-Term Wash Sales", format_currency(data.summary.short_term_wash_adjustments)},
                {"Long-Term Wash Sales", format_currency(data.summary.long_term_wash_adjustments)},
                {"Total Disallowed Losses", format_currency(total_wash)}
            };
            pdf.add_key_value_table(wash);
        }
        
        // Footer
        pdf.add_separator();
        pdf.add_disclaimer(
            "This report is provided for informational purposes and should be reviewed "
            "by a tax professional. Consult IRS Form 8949 instructions for filing requirements.");
        
        return pdf.save(output_path);
    }
    
    /**
     * @brief Build from tax tracker
     */
    TaxReportData build_from_tracker(const tax::TaxTracker& tracker,
                                      int year,
                                      const std::string& account_id) {
        
        TaxReportData data;
        data.metadata = ReportMetadata::create(
            "Tax Report",
            account_id,
            std::to_string(year) + "-01-01",
            std::to_string(year) + "-12-31");
        data.tax_year = year;
        
        // Get summary
        data.summary = tracker.get_summary(year);
        
        // Get realized gains and convert to Form 8949
        std::string year_start = std::to_string(year) + "-01-01";
        std::string year_end = std::to_string(year) + "-12-31";
        auto realized = tracker.get_all_realized(year_start, year_end);
        
        data.form_8949_lines = tax::generate_form_8949(realized, true);
        
        // Group by box
        for (const auto& line : data.form_8949_lines) {
            data.by_box[line.form_box].push_back(line);
            data.box_totals[line.form_box] += line.gain_loss;
        }
        
        return data;
    }
};

/**
 * @brief Export Form 8949 to CSV
 */
inline std::string export_form_8949_csv(const TaxReportData& data,
                                         const std::string& output_path) {
    std::ofstream file(output_path);
    if (!file) return "";
    
    // Header
    file << "Box,Description,Date Acquired,Date Sold,Proceeds,Cost Basis,"
         << "Adjustment Code,Adjustment Amount,Gain/Loss\n";
    
    for (const auto& line : data.form_8949_lines) {
        file << line.form_box << ","
             << "\"" << line.description << "\","
             << line.date_acquired << ","
             << line.date_sold << ","
             << std::fixed << std::setprecision(2) << line.proceeds << ","
             << line.cost_basis << ","
             << line.adjustment_code << ","
             << line.adjustment_amount << ","
             << line.gain_loss << "\n";
    }
    
    file.close();
    return output_path;
}

// ============================================================================
// Risk Report
// ============================================================================

/**
 * @brief Risk report data
 */
struct RiskReportData {
    ReportMetadata metadata;
    
    // VaR metrics
    double var_95_1day{0};
    double var_99_1day{0};
    double var_95_10day{0};
    double cvar_95{0};                // Expected Shortfall
    std::string var_method{"Historical"};
    
    // Portfolio VaR
    double portfolio_var_95{0};
    double portfolio_var_99{0};
    
    // Volatility
    double daily_volatility{0};
    double annual_volatility{0};
    double downside_volatility{0};
    double upside_volatility{0};
    
    // Drawdown
    double max_drawdown{0};
    std::string max_dd_start;
    std::string max_dd_end;
    int max_dd_days{0};
    double current_drawdown{0};
    double avg_drawdown{0};
    
    // Correlation
    struct CorrelationPair {
        std::string asset1;
        std::string asset2;
        double correlation{0};
    };
    std::vector<CorrelationPair> correlations;
    
    // Position risk
    struct PositionRisk {
        std::string symbol;
        double weight{0};
        double var_contribution{0};
        double beta{0};
        double volatility{0};
    };
    std::vector<PositionRisk> position_risks;
    
    // Concentration
    double top_holding_weight{0};
    double top_5_weight{0};
    double herfindahl_index{0};       // Concentration measure
    int position_count{0};
};

/**
 * @brief Generate risk report PDF
 */
class RiskReportGenerator {
public:
    std::string generate(const RiskReportData& data,
                         const std::string& output_path) {
        
        PdfBuilder pdf;
        
        // Header
        pdf.add_title("Risk Analysis Report");
        pdf.add_subtitle("Portfolio Risk Assessment");
        pdf.add_text("Period: " + data.metadata.period_start + " to " + 
                    data.metadata.period_end);
        pdf.add_text("Generated: " + data.metadata.generated_at);
        pdf.add_separator();
        
        // VaR Summary
        pdf.add_section("Value at Risk (VaR)");
        pdf.add_text("Method: " + data.var_method);
        
        std::vector<std::vector<std::string>> var_table = {
            {"Metric", "1-Day", "10-Day"},
            {"VaR (95%)", format_percent(data.var_95_1day),
             format_percent(data.var_95_10day)},
            {"VaR (99%)", format_percent(data.var_99_1day), "-"},
            {"CVaR/ES (95%)", format_percent(data.cvar_95), "-"}
        };
        
        pdf.add_table(var_table[0], {var_table[1], var_table[2], var_table[3]});
        
        pdf.add_text("\nInterpretation: With 95% confidence, the portfolio will not lose more than " +
                    format_percent(std::abs(data.var_95_1day)) + " in a single day.");
        pdf.add_separator();
        
        // Volatility
        pdf.add_section("Volatility Analysis");
        
        std::vector<std::vector<std::string>> vol_table = {
            {"Daily Volatility", format_percent(data.daily_volatility)},
            {"Annualized Volatility", format_percent(data.annual_volatility)},
            {"Downside Volatility", format_percent(data.downside_volatility)},
            {"Upside Volatility", format_percent(data.upside_volatility)}
        };
        
        pdf.add_key_value_table(vol_table);
        pdf.add_separator();
        
        // Drawdown
        pdf.add_section("Drawdown Analysis");
        
        std::vector<std::vector<std::string>> dd_table = {
            {"Maximum Drawdown", format_percent(data.max_drawdown)},
            {"Max DD Start", data.max_dd_start},
            {"Max DD End", data.max_dd_end},
            {"Max DD Duration", std::to_string(data.max_dd_days) + " days"},
            {"Current Drawdown", format_percent(data.current_drawdown)},
            {"Average Drawdown", format_percent(data.avg_drawdown)}
        };
        
        pdf.add_key_value_table(dd_table);
        pdf.add_separator();
        
        // Concentration
        pdf.add_section("Concentration Risk");
        
        std::vector<std::vector<std::string>> conc_table = {
            {"Number of Positions", std::to_string(data.position_count)},
            {"Top Holding Weight", format_percent(data.top_holding_weight)},
            {"Top 5 Holdings Weight", format_percent(data.top_5_weight)},
            {"Herfindahl Index", std::to_string(data.herfindahl_index).substr(0, 6)}
        };
        
        pdf.add_key_value_table(conc_table);
        pdf.add_separator();
        
        // Position Risk
        if (!data.position_risks.empty()) {
            pdf.add_section("Position Risk Breakdown");
            
            std::vector<std::string> headers = {
                "Symbol", "Weight", "VaR Contrib", "Beta", "Volatility"
            };
            
            std::vector<std::vector<std::string>> rows;
            for (const auto& pos : data.position_risks) {
                rows.push_back({
                    pos.symbol,
                    format_percent(pos.weight),
                    format_percent(pos.var_contribution),
                    std::to_string(pos.beta).substr(0, 5),
                    format_percent(pos.volatility)
                });
            }
            
            pdf.add_table(headers, rows);
            pdf.add_separator();
        }
        
        // Correlation Matrix
        if (!data.correlations.empty()) {
            pdf.add_section("Key Correlations");
            
            std::vector<std::string> headers = {"Asset 1", "Asset 2", "Correlation"};
            std::vector<std::vector<std::string>> rows;
            
            for (const auto& corr : data.correlations) {
                rows.push_back({
                    corr.asset1,
                    corr.asset2,
                    std::to_string(corr.correlation).substr(0, 6)
                });
            }
            
            pdf.add_table(headers, rows);
        }
        
        // Footer
        pdf.add_separator();
        pdf.add_text("Report ID: " + data.metadata.report_id);
        pdf.add_disclaimer(
            "Risk metrics are based on historical data and may not predict future results. "
            "VaR is a statistical measure and actual losses may exceed VaR estimates.");
        
        return pdf.save(output_path);
    }
    
    /**
     * @brief Build from real analytics
     */
    RiskReportData build_from_analytics(
        const std::string& account_id,
        const std::vector<double>& portfolio_returns,
        const std::vector<std::string>& dates,
        const std::map<std::string, std::vector<double>>& position_returns,
        const std::map<std::string, double>& weights) {
        
        RiskReportData data;
        data.metadata = ReportMetadata::create(
            "Risk Report",
            account_id,
            dates.front(),
            dates.back());
        
        // VaR calculations
        auto var_hist = analytics::VaRCalculator::calculate(
            portfolio_returns, 0.95, 1, analytics::VaRMethod::Historical);
        data.var_95_1day = var_hist.var * 100;
        data.cvar_95 = var_hist.cvar * 100;
        data.var_method = "Historical Simulation";
        
        auto var_99 = analytics::VaRCalculator::calculate(
            portfolio_returns, 0.99, 1, analytics::VaRMethod::Historical);
        data.var_99_1day = var_99.var * 100;
        
        auto var_10d = analytics::VaRCalculator::calculate(
            portfolio_returns, 0.95, 10, analytics::VaRMethod::Historical);
        data.var_95_10day = var_10d.var * 100;
        
        // Volatility
        auto vol = analytics::VolatilityCalculator::calculate(portfolio_returns);
        data.daily_volatility = vol.daily * 100;
        data.annual_volatility = vol.annualized * 100;
        data.downside_volatility = vol.downside * 100;
        data.upside_volatility = vol.upside * 100;
        
        // Drawdown
        std::vector<double> nav;
        nav.push_back(1.0);
        for (double r : portfolio_returns) {
            nav.push_back(nav.back() * (1 + r));
        }
        
        auto dd = analytics::DrawdownTracker::analyze(nav, dates);
        data.max_drawdown = dd.max_drawdown_pct;
        data.max_dd_start = dd.max_dd_start_date;
        data.max_dd_end = dd.max_dd_end_date;
        data.max_dd_days = dd.max_dd_duration;
        data.current_drawdown = dd.current_drawdown * 100;
        data.avg_drawdown = dd.avg_drawdown * 100;
        
        // Concentration
        data.position_count = static_cast<int>(weights.size());
        std::vector<double> weight_vec;
        for (const auto& [sym, w] : weights) {
            weight_vec.push_back(w);
        }
        std::sort(weight_vec.rbegin(), weight_vec.rend());
        
        if (!weight_vec.empty()) {
            data.top_holding_weight = weight_vec[0] * 100;
        }
        
        double top5 = 0;
        for (size_t i = 0; i < std::min(size_t(5), weight_vec.size()); ++i) {
            top5 += weight_vec[i];
        }
        data.top_5_weight = top5 * 100;
        
        // Herfindahl Index
        double hhi = 0;
        for (double w : weight_vec) {
            hhi += w * w;
        }
        data.herfindahl_index = hhi;
        
        // Position risk
        for (const auto& [symbol, w] : weights) {
            RiskReportData::PositionRisk pos_risk;
            pos_risk.symbol = symbol;
            pos_risk.weight = w * 100;
            
            auto it = position_returns.find(symbol);
            if (it != position_returns.end() && !it->second.empty()) {
                auto pos_vol = analytics::VolatilityCalculator::calculate(it->second);
                pos_risk.volatility = pos_vol.annualized * 100;
                
                // Marginal VaR contribution (simplified)
                pos_risk.var_contribution = w * pos_risk.volatility * 1.65 / 100;  // 95% z-score
            }
            
            data.position_risks.push_back(pos_risk);
        }
        
        // Sort by weight
        std::sort(data.position_risks.begin(), data.position_risks.end(),
            [](const RiskReportData::PositionRisk& a, 
               const RiskReportData::PositionRisk& b) {
                return a.weight > b.weight;
            });
        
        // Correlations
        if (position_returns.size() >= 2) {
            auto corr_matrix = analytics::CorrelationCalculator::build_matrix(position_returns);
            
            // Get top correlations
            for (size_t i = 0; i < corr_matrix.symbols.size(); ++i) {
                for (size_t j = i + 1; j < corr_matrix.symbols.size(); ++j) {
                    RiskReportData::CorrelationPair pair;
                    pair.asset1 = corr_matrix.symbols[i];
                    pair.asset2 = corr_matrix.symbols[j];
                    pair.correlation = corr_matrix.get(pair.asset1, pair.asset2);
                    data.correlations.push_back(pair);
                }
            }
            
            // Sort by absolute correlation
            std::sort(data.correlations.begin(), data.correlations.end(),
                [](const RiskReportData::CorrelationPair& a,
                   const RiskReportData::CorrelationPair& b) {
                    return std::abs(a.correlation) > std::abs(b.correlation);
                });
            
            // Keep top 10
            if (data.correlations.size() > 10) {
                data.correlations.resize(10);
            }
        }
        
        return data;
    }
};

// ============================================================================
// Report Factory
// ============================================================================

/**
 * @brief Factory for creating reports
 */
class ReportFactory {
public:
    static AccountStatementGenerator create_statement_generator() {
        return AccountStatementGenerator();
    }
    
    static PerformanceReportGenerator create_performance_generator() {
        return PerformanceReportGenerator();
    }
    
    static TradeConfirmationGenerator create_confirmation_generator() {
        return TradeConfirmationGenerator();
    }
    
    static TaxReportGenerator create_tax_generator() {
        return TaxReportGenerator();
    }
    
    static RiskReportGenerator create_risk_generator() {
        return RiskReportGenerator();
    }
};

} // namespace genie::reporting

#endif // GENIE_REPORTING_REAL_REPORTS_HPP
