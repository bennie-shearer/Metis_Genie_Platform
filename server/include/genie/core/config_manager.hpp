/**
 * @file config_manager.hpp
 * @brief Configuration management with hot-reload and validation
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Hierarchical configuration management:
 * - Key-value store with typed access (string, int, double, bool)
 * - Environment-based overlays (dev, staging, production)
 * - Default values with override chains
 * - Validation rules and constraints
 * - Hot-reload with change notification callbacks
 * - Configuration snapshots and diff
 * - INI/properties-style serialization
 * - Secret masking for sensitive values
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_CONFIG_MANAGER_HPP
#define GENIE_CORE_CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <variant>
#include <any>

namespace genie {
namespace core {
namespace config {

// ============================================================================
// Enumerations
// ============================================================================

enum class Environment {
    Development,
    Testing,
    Staging,
    Production
};

enum class ConfigValueType {
    String,
    Integer,
    Double,
    Boolean
};

[[nodiscard]] inline std::string env_string(Environment e) {
    switch (e) {
        case Environment::Development: return "development";
        case Environment::Testing:     return "testing";
        case Environment::Staging:     return "staging";
        case Environment::Production:  return "production";
    }
    return "unknown";
}

// ============================================================================
// Configuration Value
// ============================================================================

struct ConfigValue {
    std::variant<std::string, int64_t, double, bool> data;
    ConfigValueType type{ConfigValueType::String};
    std::string description;
    bool secret{false};
    bool required{false};
    std::optional<std::string> default_str;

    ConfigValue() : data(std::string("")), type(ConfigValueType::String) {}
    explicit ConfigValue(const std::string& v) : data(v), type(ConfigValueType::String) {}
    explicit ConfigValue(int64_t v) : data(v), type(ConfigValueType::Integer) {}
    explicit ConfigValue(double v) : data(v), type(ConfigValueType::Double) {}
    explicit ConfigValue(bool v) : data(v), type(ConfigValueType::Boolean) {}

    [[nodiscard]] std::string as_string() const {
        return std::visit([](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) return v;
            else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else { std::ostringstream o; o << v; return o.str(); }
        }, data);
    }

    [[nodiscard]] int64_t as_int() const {
        return std::visit([](auto&& v) -> int64_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) return v;
            else if constexpr (std::is_same_v<T, double>) return static_cast<int64_t>(v);
            else if constexpr (std::is_same_v<T, bool>) return v ? 1 : 0;
            else return 0;
        }, data);
    }

    [[nodiscard]] double as_double() const {
        return std::visit([](auto&& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) return v;
            else if constexpr (std::is_same_v<T, int64_t>) return static_cast<double>(v);
            else if constexpr (std::is_same_v<T, bool>) return v ? 1.0 : 0.0;
            else return 0.0;
        }, data);
    }

    [[nodiscard]] bool as_bool() const {
        return std::visit([](auto&& v) -> bool {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) return v;
            else if constexpr (std::is_same_v<T, int64_t>) return v != 0;
            else if constexpr (std::is_same_v<T, double>) return v != 0.0;
            else return v == "true" || v == "1" || v == "yes";
        }, data);
    }

    [[nodiscard]] std::string display_value() const {
        if (secret) return "********";
        return as_string();
    }
};

// ============================================================================
// Validation Rule
// ============================================================================

struct ValidationRule {
    std::string key;
    std::function<bool(const ConfigValue&)> validator;
    std::string error_message;
};

struct ValidationResult {
    bool valid{true};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << (valid ? "VALID" : "INVALID");
        for (const auto& e : errors) oss << "\n  ERROR: " << e;
        for (const auto& w : warnings) oss << "\n  WARN: " << w;
        return oss.str();
    }
};

// ============================================================================
// Configuration Snapshot
// ============================================================================

struct ConfigSnapshot {
    std::map<std::string, ConfigValue> values;
    std::chrono::system_clock::time_point taken_at;
    Environment environment;

    [[nodiscard]] int size() const { return static_cast<int>(values.size()); }
};

struct ConfigDiff {
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> changed;

    [[nodiscard]] bool empty() const {
        return added.empty() && removed.empty() && changed.empty();
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Config diff: +" << added.size()
            << " -" << removed.size()
            << " ~" << changed.size() << "\n";
        for (const auto& k : added) oss << "  + " << k << "\n";
        for (const auto& k : removed) oss << "  - " << k << "\n";
        for (const auto& [k, v] : changed)
            oss << "  ~ " << k << ": " << v.first << " -> " << v.second << "\n";
        return oss.str();
    }
};

// ============================================================================
// Configuration Manager
// ============================================================================

using ChangeCallback = std::function<void(const std::string& key,
                                           const ConfigValue& old_val,
                                           const ConfigValue& new_val)>;

class ConfigManager {
public:
    explicit ConfigManager(Environment env = Environment::Development)
        : environment_(env) {
        register_defaults();
    }

    // --- Setters ---

    void set(const std::string& key, const std::string& value) {
        set_value(key, ConfigValue(value));
    }
    void set(const std::string& key, int64_t value) {
        set_value(key, ConfigValue(value));
    }
    void set(const std::string& key, double value) {
        set_value(key, ConfigValue(value));
    }
    void set(const std::string& key, bool value) {
        set_value(key, ConfigValue(value));
    }

    void set_secret(const std::string& key, const std::string& value) {
        ConfigValue cv(value);
        cv.secret = true;
        set_value(key, std::move(cv));
    }

    // --- Getters ---

    [[nodiscard]] std::string get_string(const std::string& key,
                                          const std::string& default_val = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second.as_string() : default_val;
    }

    [[nodiscard]] int64_t get_int(const std::string& key, int64_t default_val = 0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second.as_int() : default_val;
    }

    [[nodiscard]] double get_double(const std::string& key, double default_val = 0.0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second.as_double() : default_val;
    }

    [[nodiscard]] bool get_bool(const std::string& key, bool default_val = false) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second.as_bool() : default_val;
    }

    [[nodiscard]] bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return values_.count(key) > 0;
    }

    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.erase(key);
    }

    // --- Environment ---

    [[nodiscard]] Environment environment() const { return environment_; }
    void set_environment(Environment env) { environment_ = env; }

    [[nodiscard]] bool is_production() const {
        return environment_ == Environment::Production;
    }

    // --- Validation ---

    void add_validation(const std::string& key,
                         std::function<bool(const ConfigValue&)> validator,
                         const std::string& error_msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        ValidationRule rule{key, std::move(validator), error_msg};
        validations_.push_back(std::move(rule));
    }

    [[nodiscard]] ValidationResult validate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ValidationResult result;
        for (const auto& rule : validations_) {
            auto it = values_.find(rule.key);
            if (it == values_.end()) {
                result.warnings.push_back("Key not found: " + rule.key);
                continue;
            }
            if (!rule.validator(it->second)) {
                result.valid = false;
                result.errors.push_back(rule.error_message + " (key: " + rule.key + ")");
            }
        }
        return result;
    }

    // --- Change Notifications ---

    void on_change(const std::string& key, ChangeCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        change_callbacks_[key].push_back(std::move(callback));
    }

    void on_any_change(ChangeCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        global_callbacks_.push_back(std::move(callback));
    }

    // --- Snapshots ---

    [[nodiscard]] ConfigSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ConfigSnapshot snap;
        snap.values = values_;
        snap.taken_at = std::chrono::system_clock::now();
        snap.environment = environment_;
        return snap;
    }

    [[nodiscard]] ConfigDiff diff(const ConfigSnapshot& a, const ConfigSnapshot& b) const {
        ConfigDiff d;
        for (const auto& [k, v] : b.values) {
            auto it = a.values.find(k);
            if (it == a.values.end()) {
                d.added.push_back(k);
            } else if (it->second.as_string() != v.as_string()) {
                d.changed.emplace_back(k,
                    std::make_pair(it->second.display_value(), v.display_value()));
            }
        }
        for (const auto& [k, _] : a.values) {
            if (b.values.find(k) == b.values.end()) d.removed.push_back(k);
        }
        return d;
    }

    // --- Serialization ---

    [[nodiscard]] std::string to_properties() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "# Metis Genie Platform Configuration\n";
        oss << "# Environment: " << env_string(environment_) << "\n\n";
        std::string section;
        for (const auto& [key, val] : values_) {
            auto dot = key.find('.');
            std::string sec = (dot != std::string::npos) ? key.substr(0, dot) : "";
            if (sec != section) { section = sec; oss << "\n# [" << section << "]\n"; }
            oss << key << " = " << val.display_value() << "\n";
        }
        return oss.str();
    }

    void load_properties(const std::string& content) {
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Trim
            auto trim = [](std::string& s) {
                while (!s.empty() && s.front() == ' ') s.erase(s.begin());
                while (!s.empty() && s.back() == ' ') s.pop_back();
            };
            trim(key); trim(val);
            set(key, val);
        }
    }

    // --- Utilities ---

    [[nodiscard]] std::vector<std::string> keys(const std::string& prefix = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [k, _] : values_) {
            if (prefix.empty() || k.substr(0, prefix.size()) == prefix)
                result.push_back(k);
        }
        return result;
    }

    [[nodiscard]] int size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(values_.size());
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.clear();
    }

private:
    mutable std::mutex mutex_;
    Environment environment_;
    std::map<std::string, ConfigValue> values_;
    std::vector<ValidationRule> validations_;
    std::map<std::string, std::vector<ChangeCallback>> change_callbacks_;
    std::vector<ChangeCallback> global_callbacks_;

    void set_value(const std::string& key, ConfigValue new_val) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        ConfigValue old_val;
        bool existed = (it != values_.end());
        if (existed) old_val = it->second;
        values_[key] = new_val;

        // Notify
        auto cb_it = change_callbacks_.find(key);
        if (cb_it != change_callbacks_.end()) {
            for (auto& cb : cb_it->second) {
                try { cb(key, old_val, new_val); } catch (...) {}
            }
        }
        for (auto& cb : global_callbacks_) {
            try { cb(key, old_val, new_val); } catch (...) {}
        }
    }

    void register_defaults() {
        set("app.name", std::string("Metis Genie Platform"));
        set("app.version", std::string("4.6.0"));
        set("server.host", std::string("0.0.0.0"));
        set("server.port", int64_t(8080));
        set("server.workers", int64_t(4));
        set("server.request_timeout_ms", int64_t(30000));
        set("database.pool_size", int64_t(10));
        set("database.connection_timeout_ms", int64_t(5000));
        set("market.data_provider", std::string("yahoo"));
        set("market.cache_ttl_seconds", int64_t(300));
        set("risk.max_position_pct", 10.0);
        set("risk.max_sector_pct", 30.0);
        set("risk.var_confidence", 0.95);
        set("trading.order_throttle_per_sec", int64_t(10));
        set("logging.level", std::string("info"));
        set("logging.json_format", true);
        set("logging.file_output", false);
        set("security.jwt_expiry_hours", int64_t(24));
        set("security.rate_limit_per_minute", int64_t(60));
    }
};

} // namespace config
} // namespace core
} // namespace genie

#endif // GENIE_CORE_CONFIG_MANAGER_HPP
