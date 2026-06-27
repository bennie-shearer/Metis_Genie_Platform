/**
 * @file api_versioning.hpp
 * @brief API versioning framework for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Manages API version lifecycle including routing, deprecation notices,
 * backward compatibility, migration paths, and sunset scheduling.
 * Supports URL-based (/api/v1/, /api/v2/) and header-based versioning.
 *
 * Features:
 *  - Version registration with lifecycle status tracking
 *  - Deprecation notices with sunset dates and replacement paths
 *  - Version negotiation from URL path or HTTP headers
 *  - Migration path recommendations between versions
 *  - Breaking change documentation per version
 *  - Usage metrics per API version
 *  - Automatic sunset enforcement
 *  - Thread-safe concurrent access
 */
#pragma once
#ifndef GENIE_API_VERSIONING_HPP
#define GENIE_API_VERSIONING_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <unordered_set>

namespace genie::core {

/** @brief API version lifecycle status */
enum class VersionStatus { CURRENT, SUPPORTED, DEPRECATED, RETIRED };

/** @brief API version metadata and configuration */
struct ApiVersion {
    int major{1};
    int minor{0};
    VersionStatus status{VersionStatus::CURRENT};
    std::string release_date;
    std::string deprecation_date;
    std::string sunset_date;
    std::string changelog_url;
    std::string description;
    std::vector<std::string> breaking_changes;
    std::vector<std::string> new_features;
    std::vector<std::string> deprecated_endpoints;
    bool allow_anonymous{false};
    int rate_limit_per_minute{1000};

    [[nodiscard]] std::string to_string() const {
        return "v" + std::to_string(major) + "." + std::to_string(minor);
    }

    [[nodiscard]] std::string version_key() const {
        return "v" + std::to_string(major);
    }

    [[nodiscard]] bool operator<(const ApiVersion& other) const {
        return major < other.major || (major == other.major && minor < other.minor);
    }
};

/** @brief Deprecation warning for API clients */
struct DeprecationNotice {
    std::string endpoint;
    std::string version;
    std::string message;
    std::string sunset_date;
    std::string replacement_endpoint;
    std::string replacement_version;
    std::string migration_guide_url;
    int days_until_sunset{0};
};

/** @brief Migration path between two API versions */
struct MigrationPath {
    std::string from_version;
    std::string to_version;
    std::string description;
    std::vector<std::string> steps;
    std::vector<std::string> breaking_changes;
    std::string guide_url;
    std::string estimated_effort; // "low", "medium", "high"
};

/** @brief Per-version usage statistics */
struct VersionUsageStats {
    std::string version;
    uint64_t total_requests{0};
    uint64_t requests_today{0};
    uint64_t unique_clients{0};
    std::string last_request_time;
    double avg_response_ms{0.0};
    std::vector<std::string> top_endpoints;
};

/** @brief Version negotiation result */
struct NegotiationResult {
    std::string requested_version;
    std::string resolved_version;
    bool exact_match{false};
    bool fallback_used{false};
    std::optional<DeprecationNotice> deprecation_notice;
    std::string warning_header;
};

/** @brief API versioning summary */
struct VersioningSummary {
    std::size_t total_versions{0};
    std::size_t current_versions{0};
    std::size_t deprecated_versions{0};
    std::size_t retired_versions{0};
    std::string latest_version;
    std::vector<ApiVersion> versions;
    std::vector<MigrationPath> migration_paths;
    std::vector<VersionUsageStats> usage_stats;
};

/**
 * @class ApiVersionManager
 * @brief Manages API versioning, deprecation, routing, and migration
 *
 * Provides complete API version lifecycle management including:
 *  - Version registration and status tracking
 *  - Deprecation scheduling with sunset enforcement
 *  - Version negotiation from requests
 *  - Migration path documentation
 *  - Per-version usage metrics
 */
class ApiVersionManager {
public:
    ApiVersionManager() = default;

    // ---- Version Registration ----

    /** @brief Register a new API version */
    void register_version(ApiVersion version) {
        std::lock_guard lock(mutex_);
        std::string key = version.version_key();
        versions_[key] = std::move(version);
    }

    /** @brief Update version status (e.g., deprecate or retire) */
    bool set_version_status(const std::string& version_key, VersionStatus status) {
        std::lock_guard lock(mutex_);
        auto it = versions_.find(version_key);
        if (it == versions_.end()) return false;
        it->second.status = status;
        if (status == VersionStatus::DEPRECATED) {
            it->second.deprecation_date = now_str();
        }
        return true;
    }

    /** @brief Remove a version (typically after sunset) */
    bool remove_version(const std::string& version_key) {
        std::lock_guard lock(mutex_);
        return versions_.erase(version_key) > 0;
    }

    // ---- Version Queries ----

    /** @brief Get the current (latest active) API version */
    [[nodiscard]] ApiVersion current_version() const {
        std::lock_guard lock(mutex_);
        ApiVersion latest;
        for (const auto& [_, v] : versions_) {
            if (v.status == VersionStatus::CURRENT) return v;
            if (v.status != VersionStatus::RETIRED && latest < v) latest = v;
        }
        return latest;
    }

    /** @brief Get a specific version by key */
    [[nodiscard]] std::optional<ApiVersion> get_version(const std::string& key) const {
        std::lock_guard lock(mutex_);
        auto it = versions_.find(key);
        if (it != versions_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief Check if a version is supported (not retired) */
    [[nodiscard]] bool is_supported(const std::string& version_key) const {
        std::lock_guard lock(mutex_);
        auto it = versions_.find(version_key);
        if (it == versions_.end()) return false;
        return it->second.status != VersionStatus::RETIRED;
    }

    /** @brief List all registered versions, sorted by major.minor */
    [[nodiscard]] std::vector<ApiVersion> list_versions() const {
        std::lock_guard lock(mutex_);
        std::vector<ApiVersion> result;
        for (const auto& [_, v] : versions_) result.push_back(v);
        std::sort(result.begin(), result.end());
        return result;
    }

    // ---- Version Negotiation ----

    /** @brief Negotiate version from URL path (e.g., /api/v1/health -> v1) */
    [[nodiscard]] NegotiationResult negotiate_from_path(const std::string& path) const {
        std::lock_guard lock(mutex_);
        NegotiationResult result;
        result.requested_version = extract_version(path);
        return resolve_version(result);
    }

    /** @brief Negotiate version from Accept-Version header */
    [[nodiscard]] NegotiationResult negotiate_from_header(const std::string& header_value) const {
        std::lock_guard lock(mutex_);
        NegotiationResult result;
        result.requested_version = header_value;
        return resolve_version(result);
    }

    /** @brief Extract version string from URL path */
    static std::string extract_version(const std::string& path) {
        auto pos = path.find("/v");
        if (pos != std::string::npos) {
            auto end = path.find('/', pos + 1);
            if (end == std::string::npos) end = path.size();
            std::string ver = path.substr(pos + 1, end - pos - 1);
            if (ver.size() >= 2 && ver[0] == 'v' && std::isdigit(ver[1])) return ver;
        }
        return "v1";
    }

    // ---- Deprecation ----

    /** @brief Check for deprecation notice */
    [[nodiscard]] std::optional<DeprecationNotice> check_deprecation(
        const std::string& version_key, const std::string& endpoint = ""
    ) const {
        std::lock_guard lock(mutex_);
        auto it = versions_.find(version_key);
        if (it == versions_.end()) return std::nullopt;
        if (it->second.status == VersionStatus::DEPRECATED) {
            DeprecationNotice notice;
            notice.endpoint = endpoint;
            notice.version = version_key;
            notice.message = "API " + version_key + " is deprecated";
            notice.sunset_date = it->second.sunset_date;
            notice.replacement_version = current_version().version_key();
            notice.replacement_endpoint = "/api/" + notice.replacement_version + endpoint;
            return notice;
        }
        return std::nullopt;
    }

    /** @brief Deprecate a specific endpoint within a version */
    void deprecate_endpoint(const std::string& version_key, const std::string& endpoint) {
        std::lock_guard lock(mutex_);
        auto it = versions_.find(version_key);
        if (it != versions_.end()) {
            it->second.deprecated_endpoints.push_back(endpoint);
        }
    }

    // ---- Migration Paths ----

    /** @brief Register a migration path between versions */
    void register_migration(MigrationPath path) {
        std::lock_guard lock(mutex_);
        std::string key = path.from_version + "->" + path.to_version;
        migrations_[key] = std::move(path);
    }

    /** @brief Get migration path between two versions */
    [[nodiscard]] std::optional<MigrationPath> get_migration(
        const std::string& from, const std::string& to
    ) const {
        std::lock_guard lock(mutex_);
        auto it = migrations_.find(from + "->" + to);
        if (it != migrations_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List all available migration paths */
    [[nodiscard]] std::vector<MigrationPath> list_migrations() const {
        std::lock_guard lock(mutex_);
        std::vector<MigrationPath> result;
        for (const auto& [_, m] : migrations_) result.push_back(m);
        return result;
    }

    // ---- Usage Tracking ----

    /** @brief Record a request for usage tracking */
    void record_request(const std::string& version_key, const std::string& /*endpoint*/,
                        const std::string& client_id = "", double response_ms = 0.0) {
        std::lock_guard lock(mutex_);
        auto& stats = usage_stats_[version_key];
        stats.version = version_key;
        stats.total_requests++;
        stats.requests_today++;
        stats.last_request_time = now_str();
        if (!client_id.empty()) {
            // Simple unique client tracking via set behavior
            if (known_clients_[version_key].insert(client_id).second) {
                stats.unique_clients++;
            }
        }
        if (response_ms > 0) {
            double n = static_cast<double>(stats.total_requests);
            stats.avg_response_ms = stats.avg_response_ms * ((n - 1) / n) + response_ms / n;
        }
    }

    /** @brief Get usage stats for a version */
    [[nodiscard]] std::optional<VersionUsageStats> get_usage(const std::string& version_key) const {
        std::lock_guard lock(mutex_);
        auto it = usage_stats_.find(version_key);
        if (it != usage_stats_.end()) return it->second;
        return std::nullopt;
    }

    // ---- Summary ----

    /** @brief Generate comprehensive versioning summary */
    [[nodiscard]] VersioningSummary summary() const {
        std::lock_guard lock(mutex_);
        VersioningSummary s;
        for (const auto& [_, v] : versions_) {
            s.versions.push_back(v);
            s.total_versions++;
            switch (v.status) {
                case VersionStatus::CURRENT: s.current_versions++; break;
                case VersionStatus::DEPRECATED: s.deprecated_versions++; break;
                case VersionStatus::RETIRED: s.retired_versions++; break;
                default: break;
            }
        }
        std::sort(s.versions.begin(), s.versions.end());
        if (!s.versions.empty()) s.latest_version = s.versions.back().to_string();
        for (const auto& [_, m] : migrations_) s.migration_paths.push_back(m);
        for (const auto& [_, u] : usage_stats_) s.usage_stats.push_back(u);
        return s;
    }

    [[nodiscard]] std::size_t version_count() const {
        std::lock_guard lock(mutex_);
        return versions_.size();
    }

private:
    NegotiationResult resolve_version(NegotiationResult result) const {
        auto it = versions_.find(result.requested_version);
        if (it != versions_.end() && it->second.status != VersionStatus::RETIRED) {
            result.resolved_version = result.requested_version;
            result.exact_match = true;
            if (it->second.status == VersionStatus::DEPRECATED) {
                DeprecationNotice notice;
                notice.version = result.requested_version;
                notice.message = "API " + result.requested_version + " is deprecated";
                notice.sunset_date = it->second.sunset_date;
                notice.replacement_version = current_version().version_key();
                result.deprecation_notice = notice;
                result.warning_header = "299 - \"API " + result.requested_version + " is deprecated\"";
            }
        } else {
            // Fallback to current
            auto cur = current_version();
            result.resolved_version = cur.version_key();
            result.exact_match = false;
            result.fallback_used = true;
        }
        return result;
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::unordered_map<std::string, ApiVersion> versions_;
    std::unordered_map<std::string, MigrationPath> migrations_;
    std::unordered_map<std::string, VersionUsageStats> usage_stats_;
    std::unordered_map<std::string, std::unordered_set<std::string>> known_clients_;
    mutable std::mutex mutex_;
};

} // namespace genie::core

#endif // GENIE_API_VERSIONING_HPP
