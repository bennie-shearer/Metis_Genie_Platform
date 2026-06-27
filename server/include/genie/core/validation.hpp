/**
 * @file validation.hpp
 * @brief Data validation utilities for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_VALIDATION_HPP
#define GENIE_VALIDATION_HPP

#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <cmath>
#include <limits>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace genie {
namespace validation {

enum class ValidationLevel { Info, Warning, Error, Critical };

struct ValidationResult {
    bool valid{true};
    ValidationLevel level{ValidationLevel::Info};
    std::string field;
    std::string message;
    std::optional<std::string> suggestion;
};

struct DataQualityScore {
    double completeness{1.0};   // % of non-null values
    double accuracy{1.0};       // % passing validation rules
    double timeliness{1.0};     // Data freshness score
    double consistency{1.0};    // Cross-field consistency
    double overall{1.0};        // Weighted average
    std::vector<ValidationResult> issues;
};

class Validator {
public:
    // String validation
    static ValidationResult validate_not_empty(const std::string& value, const std::string& field) {
        ValidationResult r;
        r.field = field;
        if (value.empty()) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " cannot be empty";
        }
        return r;
    }
    
    static ValidationResult validate_length(const std::string& value, const std::string& field,
                                            size_t min_len, size_t max_len) {
        ValidationResult r;
        r.field = field;
        if (value.length() < min_len || value.length() > max_len) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " length must be between " + std::to_string(min_len) + 
                       " and " + std::to_string(max_len);
        }
        return r;
    }
    
    static ValidationResult validate_pattern(const std::string& value, const std::string& field,
                                             const std::string& pattern, const std::string& desc = "") {
        ValidationResult r;
        r.field = field;
        std::regex re(pattern);
        if (!std::regex_match(value, re)) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " does not match required format" + (desc.empty() ? "" : ": " + desc);
        }
        return r;
    }
    
    // Numeric validation
    static ValidationResult validate_positive(double value, const std::string& field) {
        ValidationResult r;
        r.field = field;
        if (value <= 0) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " must be positive";
        }
        return r;
    }
    
    static ValidationResult validate_non_negative(double value, const std::string& field) {
        ValidationResult r;
        r.field = field;
        if (value < 0) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " cannot be negative";
        }
        return r;
    }
    
    static ValidationResult validate_range(double value, const std::string& field,
                                           double min_val, double max_val) {
        ValidationResult r;
        r.field = field;
        if (value < min_val || value > max_val) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " must be between " + std::to_string(min_val) + 
                       " and " + std::to_string(max_val);
        }
        return r;
    }
    
    static ValidationResult validate_percentage(double value, const std::string& field) {
        return validate_range(value, field, 0.0, 1.0);
    }
    
    static ValidationResult validate_not_nan(double value, const std::string& field) {
        ValidationResult r;
        r.field = field;
        if (std::isnan(value) || std::isinf(value)) {
            r.valid = false;
            r.level = ValidationLevel::Critical;
            r.message = field + " contains invalid numeric value (NaN or Inf)";
        }
        return r;
    }
    
    // Price validation
    static ValidationResult validate_price(double price, const std::string& field,
                                           double max_change_pct [[maybe_unused]] = 0.50) {
        ValidationResult r;
        r.field = field;
        if (price <= 0) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " must be positive";
        } else if (price > 1000000) {
            r.valid = false;
            r.level = ValidationLevel::Warning;
            r.message = field + " seems unusually high";
            r.suggestion = "Verify price is not in minor currency units";
        }
        return r;
    }
    
    // Outlier detection
    static ValidationResult check_outlier(double value, double mean, double stddev,
                                          const std::string& field, double z_threshold = 3.0) {
        ValidationResult r;
        r.field = field;
        double z_score = std::abs((value - mean) / stddev);
        if (z_score > z_threshold) {
            r.valid = false;
            r.level = ValidationLevel::Warning;
            r.message = field + " is a potential outlier (z-score: " + std::to_string(z_score) + ")";
            r.suggestion = "Review data for accuracy";
        }
        return r;
    }
    
    // Security ID validation (common formats)
    static ValidationResult validate_security_id(const std::string& id, const std::string& field) {
        ValidationResult r;
        r.field = field;
        
        // Check common formats: CUSIP (9), ISIN (12), SEDOL (7), ticker (1-10)
        bool valid_format = false;
        
        if (id.length() == 9 && std::all_of(id.begin(), id.end(), ::isalnum)) {
            valid_format = true;  // CUSIP
        } else if (id.length() == 12 && std::all_of(id.begin(), id.end(), ::isalnum)) {
            valid_format = true;  // ISIN
        } else if (id.length() == 7 && std::all_of(id.begin(), id.end(), ::isalnum)) {
            valid_format = true;  // SEDOL
        } else if (id.length() >= 1 && id.length() <= 10) {
            valid_format = true;  // Ticker
        }
        
        if (!valid_format) {
            r.valid = false;
            r.level = ValidationLevel::Warning;
            r.message = field + " does not match common security ID formats";
        }
        
        return r;
    }
    
    // Currency code validation
    static ValidationResult validate_currency(const std::string& ccy, const std::string& field) {
        static const std::vector<std::string> valid_currencies = {
            "USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "NZD", "CNY", "HKD",
            "SGD", "INR", "KRW", "TWD", "THB", "MXN", "BRL", "ZAR", "SEK", "NOK"
        };
        
        ValidationResult r;
        r.field = field;
        
        if (ccy.length() != 3) {
            r.valid = false;
            r.level = ValidationLevel::Error;
            r.message = field + " must be a 3-letter currency code";
        } else if (std::find(valid_currencies.begin(), valid_currencies.end(), ccy) == valid_currencies.end()) {
            r.valid = false;
            r.level = ValidationLevel::Warning;
            r.message = field + " is not a recognized major currency";
        }
        
        return r;
    }
};

// Data quality analyzer
class DataQualityAnalyzer {
public:
    template<typename T>
    static DataQualityScore analyze(const std::vector<std::optional<T>>& data,
                                    std::function<ValidationResult(const T&)> validator) {
        DataQualityScore score;
        
        int total = data.size();
        int non_null = 0;
        int valid = 0;
        
        for (const auto& item : data) {
            if (item.has_value()) {
                ++non_null;
                auto result = validator(item.value());
                if (result.valid) {
                    ++valid;
                } else {
                    score.issues.push_back(result);
                }
            }
        }
        
        score.completeness = (total > 0) ? static_cast<double>(non_null) / total : 0;
        score.accuracy = (non_null > 0) ? static_cast<double>(valid) / non_null : 0;
        score.overall = (score.completeness + score.accuracy) / 2.0;
        
        return score;
    }
    
    static std::string report(const DataQualityScore& score) {
        std::ostringstream ss;
        ss << "=== DATA QUALITY REPORT ===\n";
        ss << std::fixed << std::setprecision(1);
        ss << "Completeness: " << (score.completeness * 100) << "%\n";
        ss << "Accuracy: " << (score.accuracy * 100) << "%\n";
        ss << "Overall Score: " << (score.overall * 100) << "%\n";
        
        if (!score.issues.empty()) {
            ss << "\nIssues Found: " << score.issues.size() << "\n";
            for (const auto& issue : score.issues) {
                ss << "  [" << static_cast<int>(issue.level) << "] " 
                   << issue.field << ": " << issue.message << "\n";
            }
        }
        
        return ss.str();
    }
};

// Sanitization utilities
class Sanitizer {
public:
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\n\r\f\v");
        return s.substr(start, end - start + 1);
    }
    
    static std::string to_upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    }
    
    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }
    
    static std::string remove_special_chars(const std::string& s) {
        std::string result;
        std::copy_if(s.begin(), s.end(), std::back_inserter(result), ::isalnum);
        return result;
    }
    
    static double clamp(double value, double min_val, double max_val) {
        return std::max(min_val, std::min(value, max_val));
    }
    
    static double round_to_decimals(double value, int decimals) {
        double multiplier = std::pow(10.0, decimals);
        return std::round(value * multiplier) / multiplier;
    }
};

} // namespace validation
} // namespace genie
#endif // GENIE_VALIDATION_HPP
