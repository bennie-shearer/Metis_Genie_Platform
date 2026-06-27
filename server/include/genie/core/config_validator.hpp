/**
 * @file config_validator.hpp
 * @brief Configuration file validator for startup checks
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Validates config.json for required fields, valid port ranges,
 * file paths, and API key formats before server startup.
 *
 * Platforms: Windows, Linux, macOS (C++20, zero dependencies)
 */

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

namespace genie::core {

/**
 * @brief Configuration validation result
 */
struct ValidationResult {
    bool valid = true;
    int errors = 0;
    int warnings = 0;
    std::vector<std::string> messages;

    void add_error(const std::string& msg) {
        messages.push_back("[ERROR] " + msg);
        errors++;
        valid = false;
    }

    void add_warning(const std::string& msg) {
        messages.push_back("[WARN]  " + msg);
        warnings++;
    }

    void add_ok(const std::string& msg) {
        messages.push_back("[OK]    " + msg);
    }

    void print() const {
        for (const auto& msg : messages) {
            std::cout << "  " << msg << "\n";
        }
        std::cout << "\n  Summary: " << errors << " errors, "
                  << warnings << " warnings\n";
        std::cout << "  Status: " << (valid ? "VALID" : "INVALID") << "\n";
    }
};

/**
 * @brief Validates server configuration
 */
class ConfigValidator {
public:
    /** Validate a port number */
    [[nodiscard]] static ValidationResult validate_port(int port) {
        ValidationResult r;
        if (port < 1 || port > 65535) {
            r.add_error("Port " + std::to_string(port) + " out of range (1-65535)");
        } else if (port < 1024) {
            r.add_warning("Port " + std::to_string(port) + " requires elevated privileges");
        } else {
            r.add_ok("Port " + std::to_string(port) + " is valid");
        }
        return r;
    }

    /** Validate a directory path exists or can be created */
    [[nodiscard]] static bool validate_directory(const std::string& path, ValidationResult& r) {
        if (path.empty()) {
            r.add_error("Directory path is empty");
            return false;
        }
        if (std::filesystem::exists(path)) {
            if (std::filesystem::is_directory(path)) {
                r.add_ok("Directory exists: " + path);
                return true;
            }
            r.add_error("Path exists but is not a directory: " + path);
            return false;
        }
        r.add_warning("Directory does not exist (will be created): " + path);
        return true;
    }

    /** Validate an API key format (non-empty, reasonable length) */
    [[nodiscard]] static bool validate_api_key(const std::string& provider,
                                                const std::string& key,
                                                ValidationResult& r) {
        if (key.empty()) {
            r.add_warning(provider + ": not configured (empty key)");
            return false;
        }
        if (key.size() < 8) {
            r.add_error(provider + ": key too short (" + std::to_string(key.size()) + " chars)");
            return false;
        }
        r.add_ok(provider + ": configured (" + std::to_string(key.size()) + " chars)");
        return true;
    }
};

} // namespace genie::core
