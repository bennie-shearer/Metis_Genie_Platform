/**
 * @file user_manager.hpp
 * @brief Multi-user management for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides user lifecycle management, portfolio ownership, and tenant
 * isolation for multi-user deployment:
 *   - User CRUD with profile data
 *   - Portfolio ownership and access control
 *   - Shared portfolio support (read/write grants)
 *   - User-scoped queries for data isolation
 *   - Admin operations (list users, transfer ownership)
 *
 * Design: all user data is stored in-memory with optional SQLite
 * persistence via the DataStore. Thread-safe for concurrent access.
 *
 * Future: PostgreSQL backend enables horizontal scaling; Kubernetes
 * deployment adds pod-level isolation.
 */
#pragma once
#ifndef GENIE_CORE_USER_MANAGER_HPP
#define GENIE_CORE_USER_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <functional>

namespace genie {

// =========================================================================
// Access grant for shared portfolios
// =========================================================================

enum class AccessLevel { None, ReadOnly, ReadWrite, Owner };

inline std::string access_level_string(AccessLevel level) {
    switch (level) {
        case AccessLevel::None:      return "None";
        case AccessLevel::ReadOnly:  return "ReadOnly";
        case AccessLevel::ReadWrite: return "ReadWrite";
        case AccessLevel::Owner:     return "Owner";
    }
    return "Unknown";
}

// =========================================================================
// User profile
// =========================================================================

struct UserProfile {
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string email;
    std::string role;              // Viewer, Trader, PM, Admin
    bool active{true};
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point last_login;
    std::map<std::string, std::string> preferences;  // user settings
};

// =========================================================================
// Portfolio access grant
// =========================================================================

struct PortfolioGrant {
    std::string portfolio_id;
    std::string user_id;
    AccessLevel level{AccessLevel::None};
    std::string granted_by;
    std::chrono::system_clock::time_point granted_at;
};

// =========================================================================
// User manager - multi-user operations
// =========================================================================

class UserManager {
    std::map<std::string, UserProfile> users_;                 // user_id -> profile
    std::map<std::string, std::string> username_index_;        // username -> user_id
    std::map<std::string, std::string> portfolio_owners_;      // portfolio_id -> owner user_id
    std::map<std::string, std::vector<PortfolioGrant>> grants_;// portfolio_id -> grants
    mutable std::mutex mutex_;
    int next_user_id_{1};

    [[nodiscard]] std::string generate_user_id() {
        return "user-" + std::to_string(next_user_id_++);
    }

public:
    UserManager() = default;

    // =====================================================================
    // User CRUD
    // =====================================================================

    /** Create a new user. Returns user_id on success, empty on duplicate username. */
    [[nodiscard]] std::string create_user(const std::string& username,
                                          const std::string& display_name,
                                          const std::string& email = "",
                                          const std::string& role = "Viewer") {
        std::lock_guard lock(mutex_);
        if (username_index_.count(username)) {
            return "";  // duplicate
        }
        UserProfile profile;
        profile.user_id = generate_user_id();
        profile.username = username;
        profile.display_name = display_name;
        profile.email = email;
        profile.role = role;
        profile.active = true;
        profile.created = std::chrono::system_clock::now();
        profile.last_login = profile.created;

        username_index_[username] = profile.user_id;
        users_[profile.user_id] = std::move(profile);
        return users_.rbegin()->second.user_id;
    }

    /** Get user by user_id */
    [[nodiscard]] std::optional<UserProfile> get_user(const std::string& user_id) const {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /** Get user by username */
    [[nodiscard]] std::optional<UserProfile> get_user_by_name(const std::string& username) const {
        std::lock_guard lock(mutex_);
        auto idx = username_index_.find(username);
        if (idx == username_index_.end()) {
            return std::nullopt;
        }
        auto it = users_.find(idx->second);
        if (it == users_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /** List all active users (Admin operation) */
    [[nodiscard]] std::vector<UserProfile> list_users(bool include_inactive = false) const {
        std::lock_guard lock(mutex_);
        std::vector<UserProfile> result;
        for (const auto& [id, profile] : users_) {
            if (include_inactive || profile.active) {
                result.push_back(profile);
            }
        }
        return result;
    }

    /** Update user profile fields */
    bool update_user(const std::string& user_id,
                     const std::string& display_name = "",
                     const std::string& email = "",
                     const std::string& role = "") {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return false;
        }
        if (!display_name.empty()) {
            it->second.display_name = display_name;
        }
        if (!email.empty()) {
            it->second.email = email;
        }
        if (!role.empty()) {
            it->second.role = role;
        }
        return true;
    }

    /** Deactivate user (soft delete) */
    bool deactivate_user(const std::string& user_id) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return false;
        }
        it->second.active = false;
        return true;
    }

    /** Reactivate user */
    bool activate_user(const std::string& user_id) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return false;
        }
        it->second.active = true;
        return true;
    }

    /** Record login timestamp */
    void record_login(const std::string& user_id) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it != users_.end()) {
            it->second.last_login = std::chrono::system_clock::now();
        }
    }

    /** Set user preference */
    void set_preference(const std::string& user_id,
                        const std::string& key, const std::string& value) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it != users_.end()) {
            it->second.preferences[key] = value;
        }
    }

    /** Get user preference */
    [[nodiscard]] std::optional<std::string> get_preference(
            const std::string& user_id, const std::string& key) const {
        std::lock_guard lock(mutex_);
        auto it = users_.find(user_id);
        if (it == users_.end()) {
            return std::nullopt;
        }
        auto pref = it->second.preferences.find(key);
        if (pref == it->second.preferences.end()) {
            return std::nullopt;
        }
        return pref->second;
    }

    /** Get user count */
    [[nodiscard]] size_t user_count() const {
        std::lock_guard lock(mutex_);
        return users_.size();
    }

    // =====================================================================
    // Portfolio ownership
    // =====================================================================

    /** Register portfolio ownership */
    void assign_portfolio(const std::string& portfolio_id, const std::string& owner_id) {
        std::lock_guard lock(mutex_);
        portfolio_owners_[portfolio_id] = owner_id;
    }

    /** Get portfolio owner */
    [[nodiscard]] std::optional<std::string> get_portfolio_owner(
            const std::string& portfolio_id) const {
        std::lock_guard lock(mutex_);
        auto it = portfolio_owners_.find(portfolio_id);
        if (it == portfolio_owners_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /** List portfolios owned by a user */
    [[nodiscard]] std::vector<std::string> get_user_portfolios(const std::string& user_id) const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [pid, oid] : portfolio_owners_) {
            if (oid == user_id) {
                result.push_back(pid);
            }
        }
        return result;
    }

    /** Transfer portfolio ownership (Admin operation) */
    bool transfer_portfolio(const std::string& portfolio_id,
                            const std::string& new_owner_id) {
        std::lock_guard lock(mutex_);
        if (!users_.count(new_owner_id)) {
            return false;
        }
        portfolio_owners_[portfolio_id] = new_owner_id;
        return true;
    }

    // =====================================================================
    // Shared access grants
    // =====================================================================

    /** Grant access to a portfolio for another user */
    bool grant_access(const std::string& portfolio_id,
                      const std::string& user_id,
                      AccessLevel level,
                      const std::string& granted_by) {
        std::lock_guard lock(mutex_);
        if (!users_.count(user_id)) {
            return false;
        }
        // Remove existing grant for this user on this portfolio
        auto& glist = grants_[portfolio_id];
        glist.erase(std::remove_if(glist.begin(), glist.end(),
            [&](const PortfolioGrant& g) { return g.user_id == user_id; }),
            glist.end());

        if (level != AccessLevel::None) {
            PortfolioGrant grant;
            grant.portfolio_id = portfolio_id;
            grant.user_id = user_id;
            grant.level = level;
            grant.granted_by = granted_by;
            grant.granted_at = std::chrono::system_clock::now();
            glist.push_back(grant);
        }
        return true;
    }

    /** Revoke access grant */
    bool revoke_access(const std::string& portfolio_id, const std::string& user_id) {
        return grant_access(portfolio_id, user_id, AccessLevel::None, "");
    }

    /** Get effective access level for a user on a portfolio */
    [[nodiscard]] AccessLevel get_access_level(const std::string& portfolio_id,
                                                const std::string& user_id) const {
        std::lock_guard lock(mutex_);

        // Owner has full access
        auto owner_it = portfolio_owners_.find(portfolio_id);
        if (owner_it != portfolio_owners_.end() && owner_it->second == user_id) {
            return AccessLevel::Owner;
        }

        // Check for Admin role (full access to everything)
        auto user_it = users_.find(user_id);
        if (user_it != users_.end() && user_it->second.role == "Admin") {
            return AccessLevel::ReadWrite;
        }

        // Check explicit grants
        auto grant_it = grants_.find(portfolio_id);
        if (grant_it != grants_.end()) {
            for (const auto& g : grant_it->second) {
                if (g.user_id == user_id) {
                    return g.level;
                }
            }
        }

        return AccessLevel::None;
    }

    /** List all portfolios a user can access (owned + granted) */
    [[nodiscard]] std::vector<std::pair<std::string, AccessLevel>>
    get_accessible_portfolios(const std::string& user_id) const {
        std::lock_guard lock(mutex_);
        std::vector<std::pair<std::string, AccessLevel>> result;

        // Check if admin
        bool is_admin = false;
        auto user_it = users_.find(user_id);
        if (user_it != users_.end() && user_it->second.role == "Admin") {
            is_admin = true;
        }

        // Owned portfolios
        for (const auto& [pid, oid] : portfolio_owners_) {
            if (oid == user_id) {
                result.emplace_back(pid, AccessLevel::Owner);
            } else if (is_admin) {
                result.emplace_back(pid, AccessLevel::ReadWrite);
            }
        }

        // Granted portfolios (skip if already in result)
        if (!is_admin) {
            for (const auto& [pid, glist] : grants_) {
                for (const auto& g : glist) {
                    if (g.user_id == user_id && g.level != AccessLevel::None) {
                        bool already = false;
                        for (const auto& [rid, rl] : result) {
                            if (rid == pid) { already = true; break; }
                        }
                        if (!already) {
                            result.emplace_back(pid, g.level);
                        }
                    }
                }
            }
        }

        return result;
    }

    /** List grants on a specific portfolio */
    [[nodiscard]] std::vector<PortfolioGrant> get_portfolio_grants(
            const std::string& portfolio_id) const {
        std::lock_guard lock(mutex_);
        auto it = grants_.find(portfolio_id);
        if (it == grants_.end()) {
            return {};
        }
        return it->second;
    }

    /** Check if user can read a portfolio */
    [[nodiscard]] bool can_read(const std::string& portfolio_id,
                                 const std::string& user_id) const {
        auto level = get_access_level(portfolio_id, user_id);
        return level != AccessLevel::None;
    }

    /** Check if user can modify a portfolio */
    [[nodiscard]] bool can_write(const std::string& portfolio_id,
                                  const std::string& user_id) const {
        auto level = get_access_level(portfolio_id, user_id);
        return level == AccessLevel::ReadWrite || level == AccessLevel::Owner;
    }

    // =====================================================================
    // Serialization (for persistence / API responses)
    // =====================================================================

    /** Serialize user list to JSON */
    [[nodiscard]] std::string users_to_json() const {
        std::lock_guard lock(mutex_);
        std::ostringstream ss;
        ss << "[";
        bool first = true;
        for (const auto& [id, u] : users_) {
            if (!u.active) continue;
            if (!first) ss << ",";
            first = false;
            ss << "{\"user_id\":\"" << u.user_id
               << "\",\"username\":\"" << u.username
               << "\",\"display_name\":\"" << u.display_name
               << "\",\"email\":\"" << u.email
               << "\",\"role\":\"" << u.role
               << "\",\"active\":" << (u.active ? "true" : "false")
               << ",\"portfolios\":" << count_portfolios_unlocked(u.user_id)
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    /** Serialize user profile to JSON */
    [[nodiscard]] static std::string user_to_json(const UserProfile& u) {
        std::ostringstream ss;
        ss << "{\"user_id\":\"" << u.user_id
           << "\",\"username\":\"" << u.username
           << "\",\"display_name\":\"" << u.display_name
           << "\",\"email\":\"" << u.email
           << "\",\"role\":\"" << u.role
           << "\",\"active\":" << (u.active ? "true" : "false")
           << "}";
        return ss.str();
    }

private:
    [[nodiscard]] int count_portfolios_unlocked(const std::string& user_id) const {
        int count = 0;
        for (const auto& [pid, oid] : portfolio_owners_) {
            if (oid == user_id) ++count;
        }
        return count;
    }
};

} // namespace genie

#endif // GENIE_CORE_USER_MANAGER_HPP
