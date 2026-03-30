/**
 * @file price_validator.hpp
 * @brief Price data validation (range, gaps, outliers)
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Validates market data quality:
 * - Price range validation (high >= low, etc.)
 * - Gap detection in time series
 * - Outlier detection using statistical methods
 * - Corporate action verification
 * - Data completeness checking
 */
#pragma once
#ifndef GENIE_MARKET_PRICE_VALIDATOR_HPP
#define GENIE_MARKET_PRICE_VALIDATOR_HPP

#include "alpha_vantage.hpp"
#include "price_store.hpp"
#include <string>
#include <vector>
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief Validation issue severity
 */
enum class IssueSeverity {
    Info,       // Informational
    Warning,    // Potential issue
    Error,      // Definite problem
    Critical    // Data should not be used
};

/**
 * @brief Validation issue type
 */
enum class IssueType {
    // Range issues
    InvalidOHLC,        // high < low, close outside range
    NegativePrice,      // Price <= 0
    NegativeVolume,     // Volume < 0
    ZeroVolume,         // Volume = 0 (may be valid for some days)
    
    // Gap issues
    MissingDate,        // Expected trading day missing
    DuplicateDate,      // Same date appears twice
    OutOfSequence,      // Dates not in order
    
    // Outlier issues
    PriceSpike,         // Unusual price movement
    VolumeSpike,        // Unusual volume
    SuspiciousReturn,   // Return exceeds threshold
    
    // Corporate action issues
    UnconfirmedSplit,   // Price jump suggests split
    UnconfirmedDividend,// Price drop suggests dividend
    
    // Completeness issues
    InsufficientHistory,// Not enough data points
    StaleData,          // Data not recent
    SourceMismatch      // Different sources disagree
};

/**
 * @brief Single validation issue
 */
struct ValidationIssue {
    IssueSeverity severity;
    IssueType type;
    std::string symbol;
    std::string date;
    std::string message;
    double value{0};        // The problematic value
    double expected{0};     // Expected value (if applicable)
    
    // Default constructor
    ValidationIssue() = default;
    
    // Constructor with severity, type, symbol, date, message
    ValidationIssue(IssueSeverity sev, IssueType t, const std::string& sym,
                    const std::string& dt, const std::string& msg)
        : severity(sev), type(t), symbol(sym), date(dt), message(msg) {}
    
    // Constructor with all fields
    ValidationIssue(IssueSeverity sev, IssueType t, const std::string& sym,
                    const std::string& dt, const std::string& msg,
                    double val, double exp = 0)
        : severity(sev), type(t), symbol(sym), date(dt), message(msg),
          value(val), expected(exp) {}
};

/**
 * @brief Validation result for a symbol
 */
struct ValidationResult {
    std::string symbol;
    bool is_valid{true};
    int total_bars{0};
    int issues_count{0};
    int errors_count{0};
    int warnings_count{0};
    std::vector<ValidationIssue> issues;
    
    void add_issue(const ValidationIssue& issue) {
        issues.push_back(issue);
        issues_count++;
        if (issue.severity == IssueSeverity::Error || 
            issue.severity == IssueSeverity::Critical) {
            errors_count++;
            is_valid = false;
        } else if (issue.severity == IssueSeverity::Warning) {
            warnings_count++;
        }
    }
};

/**
 * @brief Validation configuration
 */
struct ValidationConfig {
    // Price range thresholds
    double max_daily_return{0.50};      // 50% max single-day return
    double max_weekly_return{1.00};     // 100% max weekly return
    double min_price{0.0001};           // Minimum valid price
    
    // Volume thresholds
    double volume_spike_multiple{10.0}; // X times average = spike
    bool allow_zero_volume{true};       // Allow zero volume days
    
    // Gap detection
    int max_gap_days{5};                // Max consecutive missing days
    bool validate_weekends{false};      // Expect weekends to be missing
    bool validate_holidays{true};       // Check for holiday gaps
    
    // Outlier detection
    double outlier_std_devs{4.0};       // Standard deviations for outlier
    int min_history_for_outliers{20};   // Need N days to detect outliers
    
    // Completeness
    int min_history_days{30};           // Minimum required history
    int max_stale_days{5};              // Max days since last update
    
    // Corporate actions
    double split_threshold{0.40};       // 40% drop suggests split
    double dividend_threshold{0.10};    // 10% drop suggests dividend
};

/**
 * @brief Price data validator
 */
class PriceValidator {
public:
    explicit PriceValidator(ValidationConfig config = {})
        : config_(config) {}
    
    /**
     * @brief Validate a single price bar
     */
    std::vector<ValidationIssue> validate_bar(const std::string& symbol,
                                               const PriceBar& bar) {
        std::vector<ValidationIssue> issues;
        
        // Check for negative/zero prices
        if (bar.open <= 0 || bar.close <= 0) {
            issues.push_back({
                IssueSeverity::Critical,
                IssueType::NegativePrice,
                symbol, bar.date,
                "Negative or zero price detected",
                std::min(bar.open, bar.close), config_.min_price
            });
        }
        
        // Check OHLC consistency
        if (bar.high < bar.low) {
            issues.push_back({
                IssueSeverity::Error,
                IssueType::InvalidOHLC,
                symbol, bar.date,
                "High (" + std::to_string(bar.high) + ") < Low (" + std::to_string(bar.low) + ")",
                bar.high, bar.low
            });
        }
        
        if (bar.close > bar.high || bar.close < bar.low) {
            issues.push_back({
                IssueSeverity::Error,
                IssueType::InvalidOHLC,
                symbol, bar.date,
                "Close outside high-low range",
                bar.close, 0
            });
        }
        
        if (bar.open > bar.high || bar.open < bar.low) {
            issues.push_back({
                IssueSeverity::Error,
                IssueType::InvalidOHLC,
                symbol, bar.date,
                "Open outside high-low range",
                bar.open, 0
            });
        }
        
        // Check volume
        if (bar.volume < 0) {
            issues.push_back({
                IssueSeverity::Error,
                IssueType::NegativeVolume,
                symbol, bar.date,
                "Negative volume",
                static_cast<double>(bar.volume), 0
            });
        }
        
        if (bar.volume == 0 && !config_.allow_zero_volume) {
            issues.push_back({
                IssueSeverity::Warning,
                IssueType::ZeroVolume,
                symbol, bar.date,
                "Zero volume",
                0, 0
            });
        }
        
        return issues;
    }
    
    /**
     * @brief Validate a series of price bars
     */
    ValidationResult validate_series(const std::string& symbol,
                                     const std::vector<PriceBar>& bars) {
        ValidationResult result;
        result.symbol = symbol;
        result.total_bars = static_cast<int>(bars.size());
        
        if (bars.empty()) {
            result.add_issue({
                IssueSeverity::Error,
                IssueType::InsufficientHistory,
                symbol, "",
                "No price data available",
                0, static_cast<double>(config_.min_history_days)
            });
            return result;
        }
        
        // Check minimum history
        if (static_cast<int>(bars.size()) < config_.min_history_days) {
            result.add_issue({
                IssueSeverity::Warning,
                IssueType::InsufficientHistory,
                symbol, "",
                "Insufficient history: " + std::to_string(bars.size()) + " days",
                static_cast<double>(bars.size()),
                static_cast<double>(config_.min_history_days)
            });
        }
        
        // Validate each bar
        for (const auto& bar : bars) {
            auto bar_issues = validate_bar(symbol, bar);
            for (const auto& issue : bar_issues) {
                result.add_issue(issue);
            }
        }
        
        // Check for gaps and sequence
        validate_gaps(symbol, bars, result);
        
        // Check for outliers
        validate_outliers(symbol, bars, result);
        
        // Check for unconfirmed corporate actions
        validate_corporate_actions(symbol, bars, result);
        
        // Check staleness
        validate_staleness(symbol, bars, result);
        
        return result;
    }
    
    /**
     * @brief Validate data from price store
     */
    ValidationResult validate_stored_data(PriceStore& store,
                                          const std::string& symbol) {
        auto stored_bars = store.get_prices(symbol);
        
        // Convert StoredPriceBar to PriceBar
        std::vector<PriceBar> bars;
        bars.reserve(stored_bars.size());
        for (const auto& sb : stored_bars) {
            PriceBar bar;
            bar.date = sb.date;
            bar.open = sb.open;
            bar.high = sb.high;
            bar.low = sb.low;
            bar.close = sb.close;
            bar.adjusted_close = sb.adjusted_close;
            bar.volume = sb.volume;
            bar.dividend_amount = sb.dividend;
            bar.split_coefficient = sb.split_factor;
            bars.push_back(bar);
        }
        
        return validate_series(symbol, bars);
    }
    
    /**
     * @brief Detect price spike (potential corporate action or error)
     */
    bool is_price_spike(double prev_close, double curr_close) {
        if (prev_close <= 0) return false;
        double ret = std::abs((curr_close - prev_close) / prev_close);
        return ret > config_.max_daily_return;
    }
    
    /**
     * @brief Detect volume spike
     */
    bool is_volume_spike(int64_t volume, double avg_volume) {
        if (avg_volume <= 0) return false;
        return volume > avg_volume * config_.volume_spike_multiple;
    }
    
    /**
     * @brief Calculate Z-score for outlier detection
     */
    double calculate_zscore(double value, double mean, double std_dev) {
        if (std_dev <= 0) return 0;
        return std::abs((value - mean) / std_dev);
    }
    
    /**
     * @brief Get configuration
     */
    ValidationConfig& config() { return config_; }
    const ValidationConfig& config() const { return config_; }

private:
    ValidationConfig config_;
    
    void validate_gaps(const std::string& symbol,
                       const std::vector<PriceBar>& bars,
                       ValidationResult& result) {
        if (bars.size() < 2) return;
        
        std::set<std::string> dates;
        std::string prev_date;
        
        for (const auto& bar : bars) {
            // Check for duplicates
            if (dates.count(bar.date)) {
                result.add_issue({
                    IssueSeverity::Error,
                    IssueType::DuplicateDate,
                    symbol, bar.date,
                    "Duplicate date in series",
                    0, 0
                });
            }
            dates.insert(bar.date);
            
            // Check sequence (assuming bars are sorted newest first)
            if (!prev_date.empty() && bar.date >= prev_date) {
                result.add_issue({
                    IssueSeverity::Error,
                    IssueType::OutOfSequence,
                    symbol, bar.date,
                    "Dates out of sequence",
                    0, 0
                });
            }
            
            // Check for large gaps
            if (!prev_date.empty()) {
                int gap = estimate_trading_days_between(bar.date, prev_date);
                if (gap > config_.max_gap_days) {
                    result.add_issue({
                        IssueSeverity::Warning,
                        IssueType::MissingDate,
                        symbol, bar.date,
                        "Gap of " + std::to_string(gap) + " trading days",
                        static_cast<double>(gap),
                        static_cast<double>(config_.max_gap_days)
                    });
                }
            }
            
            prev_date = bar.date;
        }
    }
    
    void validate_outliers(const std::string& symbol,
                           const std::vector<PriceBar>& bars,
                           ValidationResult& result) {
        if (static_cast<int>(bars.size()) < config_.min_history_for_outliers) {
            return;
        }
        
        // Calculate returns
        std::vector<double> returns;
        returns.reserve(bars.size() - 1);
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i].close > 0) {
                double ret = (bars[i-1].close - bars[i].close) / bars[i].close;
                returns.push_back(ret);
            }
        }
        
        if (returns.size() < 10) return;
        
        // Calculate mean and std dev
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        
        double sq_sum = 0;
        for (double r : returns) {
            sq_sum += (r - mean) * (r - mean);
        }
        double std_dev = std::sqrt(sq_sum / returns.size());
        
        // Check each return for outliers
        for (size_t i = 0; i < returns.size(); ++i) {
            double zscore = calculate_zscore(returns[i], mean, std_dev);
            if (zscore > config_.outlier_std_devs) {
                result.add_issue({
                    IssueSeverity::Warning,
                    IssueType::SuspiciousReturn,
                    symbol, bars[i].date,
                    "Suspicious return: " + format_percent(returns[i]) + 
                    " (Z-score: " + format_number(zscore, 2) + ")",
                    returns[i], mean
                });
            }
        }
        
        // Check for volume spikes
        std::vector<double> volumes;
        for (const auto& bar : bars) {
            volumes.push_back(static_cast<double>(bar.volume));
        }
        
        double avg_volume = std::accumulate(volumes.begin(), volumes.end(), 0.0) / volumes.size();
        
        for (size_t i = 0; i < bars.size(); ++i) {
            if (is_volume_spike(bars[i].volume, avg_volume)) {
                result.add_issue({
                    IssueSeverity::Info,
                    IssueType::VolumeSpike,
                    symbol, bars[i].date,
                    "Volume spike: " + std::to_string(bars[i].volume) + 
                    " (avg: " + format_number(avg_volume, 0) + ")",
                    static_cast<double>(bars[i].volume), avg_volume
                });
            }
        }
    }
    
    void validate_corporate_actions(const std::string& symbol,
                                    const std::vector<PriceBar>& bars,
                                    ValidationResult& result) {
        if (bars.size() < 2) return;
        
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i].close <= 0) continue;
            
            double prev_close = bars[i].close;
            double curr_close = bars[i-1].close;
            double change = (curr_close - prev_close) / prev_close;
            
            // Check for potential unconfirmed split
            if (change < -config_.split_threshold && bars[i].split_coefficient == 1.0) {
                result.add_issue({
                    IssueSeverity::Warning,
                    IssueType::UnconfirmedSplit,
                    symbol, bars[i-1].date,
                    "Potential unconfirmed split: " + format_percent(change) + " drop",
                    change, -config_.split_threshold
                });
            }
            
            // Check for potential unconfirmed dividend (smaller drops)
            if (change < -config_.dividend_threshold && change >= -config_.split_threshold &&
                bars[i].dividend_amount == 0) {
                result.add_issue({
                    IssueSeverity::Info,
                    IssueType::UnconfirmedDividend,
                    symbol, bars[i-1].date,
                    "Potential ex-dividend: " + format_percent(change) + " drop",
                    change, -config_.dividend_threshold
                });
            }
        }
    }
    
    void validate_staleness(const std::string& symbol,
                            const std::vector<PriceBar>& bars,
                            ValidationResult& result) {
        if (bars.empty()) return;
        
        // Get most recent date
        std::string latest_date = bars.front().date;
        
        // Compare to today
        std::string today = get_today_string();
        int days_old = estimate_calendar_days_between(latest_date, today);
        
        if (days_old > config_.max_stale_days) {
            result.add_issue({
                IssueSeverity::Warning,
                IssueType::StaleData,
                symbol, latest_date,
                "Data is " + std::to_string(days_old) + " days old",
                static_cast<double>(days_old),
                static_cast<double>(config_.max_stale_days)
            });
        }
    }
    
    // === Utility Functions ===
    
    std::string get_today_string() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }
    
    int estimate_trading_days_between(const std::string& date1, const std::string& date2) {
        // Simple estimate: calendar days * 5/7
        int calendar_days = estimate_calendar_days_between(date1, date2);
        return static_cast<int>(calendar_days * 5.0 / 7.0);
    }
    
    int estimate_calendar_days_between(const std::string& date1, const std::string& date2) {
        // Parse dates (YYYY-MM-DD format)
        std::tm tm1 = {}, tm2 = {};
        std::istringstream ss1(date1), ss2(date2);
        ss1 >> std::get_time(&tm1, "%Y-%m-%d");
        ss2 >> std::get_time(&tm2, "%Y-%m-%d");
        
        auto time1 = std::mktime(&tm1);
        auto time2 = std::mktime(&tm2);
        
        return std::abs(static_cast<int>(difftime(time2, time1) / (60 * 60 * 24)));
    }
    
    std::string format_percent(double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (value * 100) << "%";
        return oss.str();
    }
    
    std::string format_number(double value, int decimals) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(decimals) << value;
        return oss.str();
    }
};

/**
 * @brief Generate validation report
 */
inline std::string format_validation_report(const ValidationResult& result) {
    std::ostringstream oss;
    
    oss << "Validation Report for " << result.symbol << "\n";
    oss << std::string(50, '=') << "\n\n";
    
    oss << "Summary:\n";
    oss << "  Total bars: " << result.total_bars << "\n";
    oss << "  Status: " << (result.is_valid ? "VALID" : "INVALID") << "\n";
    oss << "  Errors: " << result.errors_count << "\n";
    oss << "  Warnings: " << result.warnings_count << "\n";
    oss << "  Total issues: " << result.issues_count << "\n\n";
    
    if (!result.issues.empty()) {
        oss << "Issues:\n";
        oss << std::string(50, '-') << "\n";
        
        for (const auto& issue : result.issues) {
            std::string severity;
            switch (issue.severity) {
                case IssueSeverity::Info: severity = "INFO"; break;
                case IssueSeverity::Warning: severity = "WARN"; break;
                case IssueSeverity::Error: severity = "ERROR"; break;
                case IssueSeverity::Critical: severity = "CRIT"; break;
            }
            
            oss << "[" << severity << "] " << issue.date << ": " << issue.message << "\n";
        }
    }
    
    return oss.str();
}

} // namespace genie::market

#endif // GENIE_MARKET_PRICE_VALIDATOR_HPP
