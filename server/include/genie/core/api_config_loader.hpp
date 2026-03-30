/**
 * @file api_config_loader.hpp
 * @brief Load API keys from config.json into ApiCredentials vault
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Reads the "api_keys" section of config.json and populates:
 *   - ApiCredentials vault (key/secret pairs for all providers)
 *   - Extended config structures (ports, sandbox flags, etc.)
 *
 * Called once at startup from main.cpp after Config::load_from_file().
 */
#pragma once
#ifndef GENIE_CORE_API_CONFIG_LOADER_HPP
#define GENIE_CORE_API_CONFIG_LOADER_HPP

#include "http_client.hpp"
#include "logging.hpp"
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>

namespace genie::core {

/**
 * @brief Extended API configuration (beyond key/secret)
 */
struct ApiProviderConfig {
    std::string provider;
    std::map<std::string, std::string> string_values;
    std::map<std::string, int> int_values;
    std::map<std::string, bool> bool_values;

    std::string get_string(const std::string& k, const std::string& def = "") const {
        auto it = string_values.find(k);
        return it != string_values.end() ? it->second : def;
    }
    int get_int(const std::string& k, int def = 0) const {
        auto it = int_values.find(k);
        return it != int_values.end() ? it->second : def;
    }
    bool get_bool(const std::string& k, bool def = false) const {
        auto it = bool_values.find(k);
        return it != bool_values.end() ? it->second : def;
    }
};

/**
 * @brief Global API provider config registry
 */
inline std::map<std::string, ApiProviderConfig>& api_provider_configs() {
    static std::map<std::string, ApiProviderConfig> instance;
    return instance;
}

/**
 * @brief Load API keys from a config.json file
 *
 * Parses the "api_keys" JSON object and populates:
 *   1. ApiCredentials vault with key/secret pairs
 *   2. ApiProviderConfig registry with all settings
 *
 * @param config_path Path to config.json
 * @return Number of providers loaded
 */
inline int load_api_keys(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file) {
        ::genie::logger().log(::genie::LogLevel::WARN, "CONFIG",
            "Cannot open config for API keys: " + config_path);
        return 0;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string json_str = ss.str();

    // Parse the entire config
    auto root = JsonParser::parse(json_str);
    if (!root.is_object() || !root.has("api_keys")) {
        // No api_keys section is normal when no market data providers are configured
        ::genie::logger().log(::genie::LogLevel::DEBUG, "CONFIG",
            "No api_keys section in config -- market data providers not configured");
        return 0;
    }

    const auto& api_keys = root["api_keys"];
    if (!api_keys.is_object()) return 0;

    int count = 0;
    auto& creds = api_credentials();
    auto& configs = api_provider_configs();

    for (const auto& [provider, settings] : api_keys.as_object()) {
        if (!settings.is_object()) continue;

        ApiProviderConfig pconfig;
        pconfig.provider = provider;

        // Extract all values
        for (const auto& [k, v] : settings.as_object()) {
            if (v.is_string()) {
                pconfig.string_values[k] = v.as_string();
            } else if (v.is_number()) {
                pconfig.int_values[k] = v.as_int();
            } else if (v.is_bool()) {
                pconfig.bool_values[k] = v.as_bool();
            }
        }

        // Wire to ApiCredentials vault using provider-specific key mapping
        std::string key, secret;

        if (provider == "alpha_vantage") {
            key = pconfig.get_string("key");
        } else if (provider == "alpaca") {
            key = pconfig.get_string("key_id");
            secret = pconfig.get_string("secret");
        } else if (provider == "iex_cloud") {
            key = pconfig.get_string("token");
        } else if (provider == "finnhub") {
            key = pconfig.get_string("token");
        } else if (provider == "polygon") {
            key = pconfig.get_string("key");
        } else if (provider == "fred") {
            key = pconfig.get_string("key");
        } else if (provider == "ibkr") {
            key = pconfig.get_string("account_id");
        } else if (provider == "tda") {
            key = pconfig.get_string("client_id");
            secret = pconfig.get_string("redirect_uri");
        } else if (provider == "tradier") {
            key = pconfig.get_string("access_token");
            secret = pconfig.get_string("account_id");
        } else if (provider == "webull") {
            key = pconfig.get_string("device_id");
        } else if (provider == "twilio") {
            key = pconfig.get_string("account_sid");
            secret = pconfig.get_string("auth_token");
        } else if (provider == "sendgrid") {
            key = pconfig.get_string("api_key");
        } else if (provider == "mailgun") {
            key = pconfig.get_string("api_key");
            secret = pconfig.get_string("domain");
        } else if (provider == "slack") {
            key = pconfig.get_string("webhook_url");
        } else if (provider == "smtp") {
            key = pconfig.get_string("username");
            secret = pconfig.get_string("password");
        } else if (provider == "sec_edgar") {
            key = pconfig.get_string("user_agent");
        } else if (provider == "yahoo_finance") {
            // No key needed, just enabled flag
        }

        if (!key.empty()) {
            creds.set(provider, key, secret);
        }

        configs[provider] = pconfig;
        count++;

        // Log (without exposing secrets)
        bool has_key = !key.empty();
        ::genie::logger().log(::genie::LogLevel::INFO, "CONFIG",
            provider + ": " + (has_key ? "configured" : "not configured (empty key)"));
    }

    return count;
}

/**
 * @brief Check if a provider is configured with valid credentials
 */
inline bool is_provider_configured(const std::string& provider) {
    return api_credentials().has(provider);
}

/**
 * @brief Get extended config for a provider
 */
inline const ApiProviderConfig* get_provider_config(const std::string& provider) {
    auto& configs = api_provider_configs();
    auto it = configs.find(provider);
    return (it != configs.end()) ? &it->second : nullptr;
}

} // namespace genie::core

#endif // GENIE_CORE_API_CONFIG_LOADER_HPP
