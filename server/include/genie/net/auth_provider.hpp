/**
 * @file auth_provider.hpp
 * @brief OAuth2/SSO authentication provider for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * JWT-like token validation, role-based access control, API key auth,
 * and session management with refresh tokens.
 */
#pragma once
#ifndef GENIE_NET_AUTH_PROVIDER_HPP
#define GENIE_NET_AUTH_PROVIDER_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include <chrono>
#include <mutex>
#include <optional>
#include <functional>
#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>

namespace genie::net {

enum class Role { Viewer, Trader, PortfolioManager, Admin, System };

inline std::string role_name(Role r) {
    switch (r) {
        case Role::Viewer: return "Viewer";
        case Role::Trader: return "Trader";
        case Role::PortfolioManager: return "PM";
        case Role::Admin: return "Admin";
        case Role::System: return "System";
        default: return "Unknown";
    }
}

inline Role role_from_string(const std::string& s) {
    if (s == "Admin") return Role::Admin;
    if (s == "PM" || s == "PortfolioManager") return Role::PortfolioManager;
    if (s == "Trader") return Role::Trader;
    if (s == "System") return Role::System;
    return Role::Viewer;
}

/** Permission scopes */
enum class Permission {
    PortfolioRead, PortfolioWrite, PortfolioDelete,
    OrderRead, OrderWrite, OrderCancel,
    RiskRead, MarketRead,
    AdminUsers, AdminConfig, AdminAudit
};

/** Role -> permissions mapping */
inline std::set<Permission> role_permissions(Role r) {
    std::set<Permission> perms;
    // Viewer
    perms.insert(Permission::PortfolioRead);
    perms.insert(Permission::RiskRead);
    perms.insert(Permission::MarketRead);
    if (r == Role::Viewer) return perms;

    // Trader adds order ops
    perms.insert(Permission::OrderRead);
    perms.insert(Permission::OrderWrite);
    perms.insert(Permission::OrderCancel);
    if (r == Role::Trader) return perms;

    // PM adds portfolio write
    perms.insert(Permission::PortfolioWrite);
    if (r == Role::PortfolioManager) return perms;

    // Admin/System gets everything
    perms.insert(Permission::PortfolioDelete);
    perms.insert(Permission::AdminUsers);
    perms.insert(Permission::AdminConfig);
    perms.insert(Permission::AdminAudit);
    return perms;
}

/** Authenticated user session */
struct AuthSession {
    std::string user_id;
    std::string username;
    Role role{Role::Viewer};
    std::string token;
    std::string refresh_token;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point expires;
    std::chrono::system_clock::time_point refresh_expires;
    std::set<Permission> permissions;
    bool active{true};
};

/** API key entry */
struct ApiKey {
    std::string key_id;
    std::string secret_hash;  // hashed
    std::string owner;
    Role role{Role::Viewer};
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point expires;
    bool active{true};
    std::set<std::string> allowed_ips;
};

/** User record */
struct UserRecord {
    std::string user_id;
    std::string username;
    std::string password_hash;
    Role role{Role::Viewer};
    bool active{true};
    std::string email;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point last_login;
    int failed_attempts{0};
    bool locked{false};
};

/** Authentication result */
struct AuthResult {
    bool success{false};
    std::string token;
    std::string refresh_token;
    std::string error;
    Role role{Role::Viewer};
    int expires_in{3600}; // seconds
};

/** Authentication provider */
class AuthProvider {
    std::map<std::string, UserRecord> users_;
    std::map<std::string, AuthSession> sessions_;    // token -> session
    std::map<std::string, AuthSession> refresh_map_; // refresh_token -> session
    std::map<std::string, ApiKey> api_keys_;
    mutable std::mutex mutex_;

    std::chrono::seconds token_ttl_{3600};       // 1 hour
    std::chrono::seconds refresh_ttl_{86400};    // 24 hours
    int max_failed_attempts_{5};

    std::string generate_token(const std::string& prefix) {
        // Generate 128-bit random hex token (unpredictable, non-sequential)
        static thread_local std::mt19937_64 engine{std::random_device{}()};
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream ss;
        ss << prefix << "-" << std::hex << std::setfill('0')
           << std::setw(16) << dist(engine)
           << std::setw(16) << dist(engine);
        return ss.str();
    }

    // Simple hash (not cryptographic - for emulation purposes)
    static std::string simple_hash(const std::string& input) {
        uint64_t h = 14695981039346656037ULL;
        for (char c : input) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
        std::ostringstream ss;
        ss << std::hex << h;
        return ss.str();
    }

public:
    AuthProvider(bool create_demo_users = false) {
        if (create_demo_users) {
            create_default_users();
        }
    }

    /** Create default demo users (admin/demo, trader/trade, viewer/view).
     *  Only call in development or demo mode. */
    void create_default_users() {
        register_user("admin", "demo", Role::Admin, "admin@genie.local");
        register_user("trader", "trade", Role::Trader, "trader@genie.local");
        register_user("viewer", "view", Role::Viewer, "viewer@genie.local");
    }

    /** Register a new user */
    bool register_user(const std::string& username, const std::string& password,
                       Role role = Role::Viewer, const std::string& email = "") {
        std::lock_guard lock(mutex_);
        if (users_.count(username)) return false;
        UserRecord u;
        u.user_id = "user-" + std::to_string(users_.size() + 1);
        u.username = username;
        u.password_hash = simple_hash(password);
        u.role = role;
        u.email = email;
        u.active = true;
        u.created = std::chrono::system_clock::now();
        users_[username] = u;
        return true;
    }

    /** Authenticate with username/password */
    AuthResult authenticate(const std::string& username, const std::string& password) {
        std::lock_guard lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end())
            return {false, "", "", "Invalid credentials"};

        auto& user = it->second;
        if (user.locked)
            return {false, "", "", "Account locked"};
        if (!user.active)
            return {false, "", "", "Account disabled"};

        if (user.password_hash != simple_hash(password)) {
            user.failed_attempts++;
            if (user.failed_attempts >= max_failed_attempts_)
                user.locked = true;
            return {false, "", "", "Invalid credentials"};
        }

        // Success
        user.failed_attempts = 0;
        user.last_login = std::chrono::system_clock::now();

        AuthSession session;
        session.user_id = user.user_id;
        session.username = username;
        session.role = user.role;
        session.token = generate_token("genie-token");
        session.refresh_token = generate_token("genie-refresh");
        session.created = std::chrono::system_clock::now();
        session.expires = session.created + token_ttl_;
        session.refresh_expires = session.created + refresh_ttl_;
        session.permissions = role_permissions(user.role);
        session.active = true;

        sessions_[session.token] = session;
        refresh_map_[session.refresh_token] = session;

        return {true, session.token, session.refresh_token, "",
                user.role, static_cast<int>(token_ttl_.count())};
    }

    /** Validate a token and return the session */
    std::optional<AuthSession> validate(const std::string& token) {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return std::nullopt;
        if (!it->second.active) return std::nullopt;
        if (std::chrono::system_clock::now() > it->second.expires) {
            it->second.active = false;
            return std::nullopt;
        }
        return it->second;
    }

    /** Check if a session has a specific permission */
    bool has_permission(const std::string& token, Permission perm) {
        auto session = validate(token);
        if (!session) return false;
        return session->permissions.count(perm) > 0;
    }

    /** Refresh a token */
    AuthResult refresh(const std::string& refresh_token) {
        std::lock_guard lock(mutex_);
        auto it = refresh_map_.find(refresh_token);
        if (it == refresh_map_.end())
            return {false, "", "", "Invalid refresh token"};
        if (std::chrono::system_clock::now() > it->second.refresh_expires)
            return {false, "", "", "Refresh token expired"};

        // Invalidate old session
        sessions_.erase(it->second.token);

        // Create new session
        auto& old = it->second;
        AuthSession session;
        session.user_id = old.user_id;
        session.username = old.username;
        session.role = old.role;
        session.token = generate_token("genie-token");
        session.refresh_token = generate_token("genie-refresh");
        session.created = std::chrono::system_clock::now();
        session.expires = session.created + token_ttl_;
        session.refresh_expires = session.created + refresh_ttl_;
        session.permissions = role_permissions(old.role);
        session.active = true;

        sessions_[session.token] = session;
        refresh_map_.erase(it);
        refresh_map_[session.refresh_token] = session;

        return {true, session.token, session.refresh_token, "",
                session.role, static_cast<int>(token_ttl_.count())};
    }

    /** Revoke a session */
    void revoke(const std::string& token) {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(token);
        if (it != sessions_.end()) {
            it->second.active = false;
            refresh_map_.erase(it->second.refresh_token);
        }
    }

    /** Authenticate with API key */
    std::optional<AuthSession> validate_api_key(const std::string& key_id, const std::string& secret) {
        std::lock_guard lock(mutex_);
        auto it = api_keys_.find(key_id);
        if (it == api_keys_.end() || !it->second.active) return std::nullopt;
        if (it->second.secret_hash != simple_hash(secret)) return std::nullopt;
        if (std::chrono::system_clock::now() > it->second.expires) return std::nullopt;

        AuthSession session;
        session.user_id = it->second.owner;
        session.username = "api:" + key_id;
        session.role = it->second.role;
        session.permissions = role_permissions(it->second.role);
        session.active = true;
        return session;
    }

    /** Create an API key */
    std::pair<std::string, std::string> create_api_key(
            const std::string& owner, Role role = Role::Viewer,
            int expires_days = 365) {
        std::lock_guard lock(mutex_);
        std::string key_id = generate_token("genie-key");
        std::string secret = generate_token("genie-secret");
        ApiKey ak;
        ak.key_id = key_id;
        ak.secret_hash = simple_hash(secret);
        ak.owner = owner;
        ak.role = role;
        ak.created = std::chrono::system_clock::now();
        ak.expires = ak.created + std::chrono::hours(24 * expires_days);
        ak.active = true;
        api_keys_[key_id] = ak;
        return {key_id, secret};
    }

    [[nodiscard]] size_t active_sessions() const {
        std::lock_guard lock(mutex_);
        size_t count = 0;
        for (const auto& [k, s] : sessions_) if (s.active) count++;
        return count;
    }

    [[nodiscard]] size_t user_count() const { std::lock_guard lock(mutex_); return users_.size(); }
    [[nodiscard]] size_t api_key_count() const { std::lock_guard lock(mutex_); return api_keys_.size(); }
};

} // namespace genie::net

#endif // GENIE_NET_AUTH_PROVIDER_HPP
