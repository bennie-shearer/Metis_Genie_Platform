/**
 * @file config.hpp
 * @brief PSON configuration management for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * PSON (Permissive Structured Object Notation) is JSON extended with:
 *   - Single-line comments beginning with //
 *   - Trailing commas before } or ]
 *   - Flat key notation: "section.key": value
 *
 * Zero external dependencies. Cross-platform: Windows/Linux/macOS.
 */
#pragma once
#ifndef GENIE_CONFIG_HPP
#define GENIE_CONFIG_HPP

#include <string>
#include <map>
#include <variant>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <optional>
#include <algorithm>

namespace genie {

using ConfigValue = std::variant<bool, int, double, std::string>;

// ============================================================================
// PSON Parser - strips // comments and trailing commas, then parses flat JSON
// ============================================================================

inline std::string strip_pson_comments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool in_string = false;
    bool escaped = false;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (escaped) {
            out += c;
            escaped = false;
            ++i;
            continue;
        }
        if (c == '\\' && in_string) {
            out += c;
            escaped = true;
            ++i;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            out += c;
            ++i;
            continue;
        }
        if (!in_string && c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            // Skip until end of line
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// Strip trailing commas before } and ] (PSON permissive syntax)
inline std::string strip_trailing_commas(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (src[i] == ',') {
            // Look ahead (skip whitespace) for } or ]
            size_t j = i + 1;
            while (j < src.size() && (src[j] == ' ' || src[j] == '\t' || src[j] == '\n' || src[j] == '\r')) ++j;
            if (j < src.size() && (src[j] == '}' || src[j] == ']')) {
                ++i; // skip comma
                continue;
            }
        }
        out += src[i++];
    }
    return out;
}

// ============================================================================
// Config
// ============================================================================

class Config {
    std::map<std::string, ConfigValue> values_;

public:
    static Config& instance() { static Config inst; return inst; }

    void set(const std::string& key, ConfigValue value) { values_[key] = value; }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = values_.find(key);
        if (it == values_.end()) return std::nullopt;
        if (auto* val = std::get_if<T>(&it->second)) return *val;
        // Allow int -> double promotion
        if constexpr (std::is_same_v<T, double>) {
            if (auto* iv = std::get_if<int>(&it->second)) return static_cast<double>(*iv);
        }
        // Allow bool stored as int
        if constexpr (std::is_same_v<T, bool>) {
            if (auto* iv = std::get_if<int>(&it->second)) return static_cast<bool>(*iv);
        }
        return std::nullopt;
    }

    template<typename T>
    T get_or(const std::string& key, T default_val) const {
        auto val = get<T>(key);
        return val.value_or(default_val);
    }

    template<typename T>
    T require(const std::string& key) const {
        auto val = get<T>(key);
        if (!val.has_value())
            throw std::runtime_error("Required config key missing: " + key);
        return val.value();
    }

    bool has(const std::string& key) const { return values_.count(key) > 0; }
    void remove(const std::string& key) { values_.erase(key); }
    void clear() { values_.clear(); }
    size_t size() const { return values_.size(); }

    /**
     * @brief Load from a PSON or JSON string.
     *
     * Handles flat "section.key": value pairs at any nesting depth.
     * Nested objects are flattened: {"server": {"port": 8080}} is stored
     * as "server.port" = 8080.  Arrays are stored as comma-joined strings.
     *
     * @param src PSON or JSON source text
     */
    void load_from_string(const std::string& src) {
        // 1. Strip // comments (PSON extension)
        std::string cleaned = strip_pson_comments(src);
        // 2. Strip trailing commas (PSON extension)
        cleaned = strip_trailing_commas(cleaned);
        // 3. Parse flat/nested key-value pairs
        parse_object(cleaned, "");
    }

    void load_from_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) throw std::runtime_error("Cannot open config file: " + filename);
        std::ostringstream ss;
        ss << file.rdbuf();
        load_from_string(ss.str());
    }

    std::string to_pson() const {
        std::ostringstream ss;
        ss << "// Metis Genie Platform Configuration (PSON format)\n";
        ss << "// PSON supports // comments and trailing commas.\n{\n";
        for (const auto& [key, value] : values_) {
            ss << "  \"" << key << "\": ";
            std::visit([&ss](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>)        ss << (v ? "true" : "false");
                else if constexpr (std::is_same_v<T, std::string>) ss << "\"" << v << "\"";
                else                                           ss << v;
            }, value);
            ss << ",\n";
        }
        ss << "}\n";
        return ss.str();
    }

    void save_to_file(const std::string& filename) const {
        std::ofstream file(filename);
        if (!file) throw std::runtime_error("Cannot write config file: " + filename);
        file << to_pson();
    }

    // Default configuration
    void load_defaults() {
        set("var.confidence_level", 0.95);
        set("var.holding_period", 1);
        set("var.monte_carlo_sims", 10000);
        set("portfolio.base_currency", std::string("USD"));
        set("trading.slippage_bps", 5.0);
        set("trading.commission_per_share", 0.01);
        set("compliance.single_position_limit", 10.0);
        set("compliance.max_leverage", 1.5);
        set("logging.level", std::string("INFO"));
        set("logging.file_enabled", false);
    }

private:
    // ------------------------------------------------------------------
    // Recursive JSON/PSON object parser - flattens nested keys
    // ------------------------------------------------------------------

    void parse_object(const std::string& text, const std::string& prefix) {
        // Find the outermost {} or start at first {
        size_t start = text.find('{');
        if (start == std::string::npos) return;
        size_t pos = start + 1;

        auto trim_ws = [](const std::string& s) -> std::string {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        };

        auto unquote = [](const std::string& s) -> std::string {
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        };

        while (pos < text.size()) {
            // Skip whitespace
            while (pos < text.size() && std::string(" \t\r\n,").find(text[pos]) != std::string::npos) ++pos;
            if (pos >= text.size() || text[pos] == '}') break;

            // Parse key
            if (text[pos] != '"') { ++pos; continue; }
            size_t key_start = pos + 1;
            size_t key_end = text.find('"', key_start);
            if (key_end == std::string::npos) break;
            std::string raw_key = text.substr(key_start, key_end - key_start);
            std::string full_key = prefix.empty() ? raw_key : prefix + "." + raw_key;
            pos = key_end + 1;

            // Skip colon
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == ':' || text[pos] == '\t')) ++pos;
            if (pos >= text.size()) break;

            char vc = text[pos];

            if (vc == '{') {
                // Nested object — recurse with extended prefix
                size_t obj_start = pos;
                size_t depth = 1;
                size_t p = pos + 1;
                bool in_str = false;
                while (p < text.size() && depth > 0) {
                    if (text[p] == '"' && (p == 0 || text[p-1] != '\\')) in_str = !in_str;
                    if (!in_str) {
                        if (text[p] == '{') ++depth;
                        else if (text[p] == '}') --depth;
                    }
                    ++p;
                }
                std::string sub = text.substr(obj_start, p - obj_start);
                parse_object(sub, full_key);
                pos = p;
            } else if (vc == '[') {
                // Array — collect as comma-joined string
                size_t arr_start = pos + 1;
                size_t depth = 1;
                size_t p = pos + 1;
                bool in_str = false;
                while (p < text.size() && depth > 0) {
                    if (text[p] == '"' && (p == 0 || text[p-1] != '\\')) in_str = !in_str;
                    if (!in_str) {
                        if (text[p] == '[') ++depth;
                        else if (text[p] == ']') --depth;
                    }
                    ++p;
                }
                std::string arr_content = text.substr(arr_start, p - arr_start - 1);
                // Strip whitespace and quotes from simple string arrays
                std::string arr_val;
                std::istringstream iss(arr_content);
                std::string tok;
                bool first = true;
                while (std::getline(iss, tok, ',')) {
                    std::string t = trim_ws(tok);
                    t = unquote(t);
                    if (!t.empty()) {
                        if (!first) arr_val += ",";
                        arr_val += t;
                        first = false;
                    }
                }
                values_[full_key] = arr_val;
                pos = p;
            } else if (vc == '"') {
                // String value
                size_t vs = pos + 1;
                size_t ve = pos + 1;
                while (ve < text.size()) {
                    if (text[ve] == '\\') { ve += 2; continue; }
                    if (text[ve] == '"') break;
                    ++ve;
                }
                values_[full_key] = text.substr(vs, ve - vs);
                pos = ve + 1;
            } else {
                // Bool, null, or number — read until comma/} /whitespace
                size_t ve = pos;
                while (ve < text.size() && std::string(",}\n\r ").find(text[ve]) == std::string::npos) ++ve;
                std::string val = trim_ws(text.substr(pos, ve - pos));
                if (val == "true")       values_[full_key] = true;
                else if (val == "false") values_[full_key] = false;
                else if (val == "null")  values_[full_key] = std::string("");
                else {
                    try      { values_[full_key] = std::stoi(val); }
                    catch(...) {
                        try  { values_[full_key] = std::stod(val); }
                        catch(...) { values_[full_key] = val; }
                    }
                }
                pos = ve;
            }
        }
    }
};

inline Config& config() { return Config::instance(); }

} // namespace genie
#endif // GENIE_CONFIG_HPP
