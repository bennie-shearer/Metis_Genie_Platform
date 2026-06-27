/**
 * @file input_sanitizer.hpp
 * @brief Input sanitization utilities for SQL injection and XSS prevention
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides comprehensive input validation and sanitization for web inputs,
 * API parameters, database queries, and file paths. Prevents SQL injection,
 * XSS attacks, path traversal, and other common injection vectors.
 *
 * Zero external dependencies. Thread-safe. Cross-platform.
 */
#pragma once
#ifndef GENIE_INPUT_SANITIZER_HPP
#define GENIE_INPUT_SANITIZER_HPP

#include <string>
#include <regex>
#include <algorithm>
#include <vector>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <functional>

namespace genie {

// ============================================================================
// Sanitization Result
// ============================================================================

struct SanitizeResult {
    bool        is_safe{true};
    std::string sanitized;          // Cleaned value
    std::string original;           // Original input
    std::vector<std::string> warnings;  // What was cleaned/rejected

    operator bool() const { return is_safe; }
};

// ============================================================================
// HTML/XSS Sanitizer
// ============================================================================

class HtmlSanitizer {
public:
    // Escape HTML special characters to prevent XSS
    static std::string escape(const std::string& input) {
        std::string result;
        result.reserve(input.size() * 1.1);
        for (char c : input) {
            switch (c) {
                case '&':  result += "&amp;";   break;
                case '<':  result += "&lt;";    break;
                case '>':  result += "&gt;";    break;
                case '"':  result += "&quot;";  break;
                case '\'': result += "&#x27;";  break;
                case '/':  result += "&#x2F;";  break;
                default:   result += c;         break;
            }
        }
        return result;
    }

    // Strip all HTML tags
    static std::string strip_tags(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        bool in_tag = false;
        for (char c : input) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                result += c;
            }
        }
        return result;
    }

    // Remove script tags and event handlers
    static SanitizeResult sanitize(const std::string& input) {
        SanitizeResult result;
        result.original = input;
        result.sanitized = input;

        // Remove script tags
        static const std::regex script_re(
            "<script[^>]*>[\\s\\S]*?</script>",
            std::regex::icase);
        if (std::regex_search(result.sanitized, script_re)) {
            result.sanitized = std::regex_replace(result.sanitized, script_re, "");
            result.warnings.push_back("Removed script tags");
        }

        // Remove event handlers (onclick, onerror, etc.)
        static const std::regex event_re(
            "\\bon\\w+\\s*=\\s*[\"'][^\"']*[\"']",
            std::regex::icase);
        if (std::regex_search(result.sanitized, event_re)) {
            result.sanitized = std::regex_replace(result.sanitized, event_re, "");
            result.warnings.push_back("Removed event handlers");
        }

        // Remove javascript: protocol
        static const std::regex js_proto_re(
            "javascript\\s*:",
            std::regex::icase);
        if (std::regex_search(result.sanitized, js_proto_re)) {
            result.sanitized = std::regex_replace(result.sanitized, js_proto_re, "");
            result.warnings.push_back("Removed javascript: protocol");
        }

        result.is_safe = result.warnings.empty();
        return result;
    }
};

// ============================================================================
// SQL Sanitizer
// ============================================================================

class SqlSanitizer {
public:
    // Escape single quotes for SQL strings (parameterized queries preferred)
    static std::string escape_string(const std::string& input) {
        std::string result;
        result.reserve(input.size() * 1.1);
        for (char c : input) {
            if (c == '\'') {
                result += "''";     // SQL standard escape
            } else if (c == '\\') {
                result += "\\\\";
            } else if (c == '\0') {
                // Skip null bytes
            } else {
                result += c;
            }
        }
        return result;
    }

    // Check if input contains SQL injection patterns
    static SanitizeResult check(const std::string& input) {
        SanitizeResult result;
        result.original = input;
        result.sanitized = input;

        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                      [](char c){ return static_cast<char>(std::tolower(c)); });

        // Check for common SQL injection patterns
        static const std::vector<std::string> patterns = {
            "' or ",  "' and ",  "1=1",  "1 = 1",
            "; drop ", "; delete ", "; update ", "; insert ",
            "union select", "union all select",
            "/*", "*/",  "-- ", "xp_",
            "exec(", "execute(",
            "char(", "nchar(",
            "cast(", "convert(",
            "waitfor delay",
            "benchmark(",
            "sleep("
        };

        for (const auto& pattern : patterns) {
            if (lower.find(pattern) != std::string::npos) {
                result.is_safe = false;
                result.warnings.push_back("SQL injection pattern detected: " + pattern);
            }
        }

        return result;
    }

    // Validate identifier (table name, column name)
    static bool is_valid_identifier(const std::string& name) {
        if (name.empty() || name.size() > 128) return false;
        if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') return false;
        return std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        });
    }

    // Whitelist-based column name validator
    static bool validate_column(const std::string& col,
                               const std::vector<std::string>& allowed) {
        return std::find(allowed.begin(), allowed.end(), col) != allowed.end();
    }
};

// ============================================================================
// Path Sanitizer
// ============================================================================

class PathSanitizer {
public:
    // Prevent path traversal attacks
    static SanitizeResult sanitize(const std::string& path) {
        SanitizeResult result;
        result.original = path;
        result.sanitized = path;

        // Check for path traversal
        if (path.find("..") != std::string::npos) {
            result.is_safe = false;
            result.warnings.push_back("Path traversal detected (..)");
        }

        // Check for null bytes
        if (path.find('\0') != std::string::npos) {
            result.is_safe = false;
            result.warnings.push_back("Null byte in path");
        }

        // Remove leading/trailing whitespace
        result.sanitized = trim(result.sanitized);

        // Normalize path separators
        std::replace(result.sanitized.begin(), result.sanitized.end(), '\\', '/');

        // Remove double slashes
        std::string cleaned;
        cleaned.reserve(result.sanitized.size());
        char prev = 0;
        for (char c : result.sanitized) {
            if (c == '/' && prev == '/') continue;
            cleaned += c;
            prev = c;
        }
        result.sanitized = cleaned;

        return result;
    }

    // Validate filename (no directory components)
    static bool is_safe_filename(const std::string& name) {
        if (name.empty() || name.size() > 255) return false;
        if (name.find('/') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false;
        if (name.find("..") != std::string::npos) return false;
        if (name.find('\0') != std::string::npos) return false;
        if (name[0] == '.') return false;  // No hidden files

        // Check for invalid characters on Windows
        static const std::string invalid_chars = "<>:\"|?*";
        for (char c : name) {
            if (invalid_chars.find(c) != std::string::npos) return false;
            if (static_cast<unsigned char>(c) < 32) return false;
        }
        return true;
    }

private:
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r");
        auto end = s.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }
};

// ============================================================================
// General Input Sanitizer
// ============================================================================

class InputSanitizer {
public:
    // Validate string length
    static bool check_length(const std::string& input,
                            size_t min_len = 0, size_t max_len = 10000) {
        return input.size() >= min_len && input.size() <= max_len;
    }

    // Validate email format (basic check)
    static bool is_valid_email(const std::string& email) {
        if (email.size() < 5 || email.size() > 254) return false;
        auto at_pos = email.find('@');
        if (at_pos == std::string::npos || at_pos == 0) return false;
        auto dot_pos = email.rfind('.');
        if (dot_pos == std::string::npos || dot_pos < at_pos + 2) return false;
        if (dot_pos >= email.size() - 2) return false;
        return true;
    }

    // Validate IP address (IPv4)
    static bool is_valid_ipv4(const std::string& ip) {
        std::istringstream stream(ip);
        std::string segment;
        int count = 0;
        while (std::getline(stream, segment, '.')) {
            if (segment.empty() || segment.size() > 3) return false;
            for (char c : segment) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            int val = std::stoi(segment);
            if (val < 0 || val > 255) return false;
            count++;
        }
        return count == 4;
    }

    // Validate numeric string
    static bool is_numeric(const std::string& s) {
        if (s.empty()) return false;
        size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        bool has_dot = false;
        for (size_t i = start; i < s.size(); ++i) {
            if (s[i] == '.') {
                if (has_dot) return false;
                has_dot = true;
            } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                return false;
            }
        }
        return s.size() > start;
    }

    // Validate alphanumeric string
    static bool is_alphanumeric(const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c));
        });
    }

    // Strip control characters
    static std::string strip_control_chars(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        for (char c : input) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc >= 32 || c == '\n' || c == '\r' || c == '\t') {
                result += c;
            }
        }
        return result;
    }

    // Trim whitespace
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r");
        auto end = s.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    // Comprehensive sanitize: strip tags, control chars, trim
    static SanitizeResult sanitize(const std::string& input,
                                  size_t max_length = 10000) {
        SanitizeResult result;
        result.original = input;

        // Length check
        if (input.size() > max_length) {
            result.sanitized = input.substr(0, max_length);
            result.warnings.push_back("Truncated to max length");
        } else {
            result.sanitized = input;
        }

        // Strip control characters
        std::string cleaned = strip_control_chars(result.sanitized);
        if (cleaned.size() != result.sanitized.size()) {
            result.warnings.push_back("Removed control characters");
        }

        // Trim
        result.sanitized = trim(cleaned);

        // Check for XSS
        auto html_result = HtmlSanitizer::sanitize(result.sanitized);
        if (!html_result.is_safe) {
            result.sanitized = HtmlSanitizer::escape(result.sanitized);
            result.warnings.insert(result.warnings.end(),
                                  html_result.warnings.begin(),
                                  html_result.warnings.end());
        }

        result.is_safe = result.warnings.empty();
        return result;
    }
};

} // namespace genie

#endif // GENIE_INPUT_SANITIZER_HPP
