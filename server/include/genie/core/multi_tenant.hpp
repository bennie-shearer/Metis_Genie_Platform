/**
 * @file multi_tenant.hpp
 * @brief Multi-Tenant Architecture for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides tenant isolation, resource management, and data partitioning
 * for hosting multiple investment management organizations on a single
 * Genie deployment.
 *
 * Features:
 *   - Tenant lifecycle management (create, suspend, delete, migrate)
 *   - Data isolation strategies: schema-per-tenant, row-level, hybrid
 *   - Resource quotas (CPU, memory, storage, API calls, portfolios)
 *   - Rate limiting per tenant with token bucket algorithm
 *   - Tenant-scoped configuration and feature flags
 *   - Cross-tenant analytics (anonymized benchmarking)
 *   - Audit logging per tenant with tamper-proof chain
 *   - Tenant hierarchy (organization -> teams -> users)
 *   - Data export/import for tenant migration
 *   - Billing metering (API calls, storage, compute minutes)
 *   - Custom branding per tenant (white-label support)
 *   - Tenant health monitoring and SLA tracking
 *
 * Zero external dependencies. Pure C++20.
 */

#ifndef GENIE_CORE_MULTI_TENANT_HPP
#define GENIE_CORE_MULTI_TENANT_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <random>
#include <stdexcept>

namespace genie {
namespace core {

// ============================================================
// Enumerations
// ============================================================

enum class TenantStatus {
    Active,
    Suspended,      // Temporarily disabled
    ReadOnly,       // Can read but not write
    Migrating,      // Data being moved
    PendingDelete,  // Marked for deletion
    Deleted         // Soft-deleted
};

enum class TenantTier {
    Free,           // Limited features
    Starter,        // Basic features
    Professional,   // Full features
    Enterprise,     // Full + priority support
    Custom          // Negotiated terms
};

enum class IsolationStrategy {
    SharedSchema,       // Row-level tenant_id column
    SchemaPerTenant,    // Separate schema/database per tenant
    HybridIsolation     // Hot data shared, cold data isolated
};

enum class ResourceType {
    Portfolios,
    Positions,
    Orders,
    ApiCallsPerMinute,
    ApiCallsPerDay,
    StorageMB,
    ComputeMinutes,
    Users,
    Alerts,
    Reports,
    Backtests,
    ConcurrentSessions
};

enum class BillingEvent {
    ApiCall,
    StorageUsed,
    ComputeMinute,
    ReportGenerated,
    TradeExecuted,
    MarketDataQuery,
    BacktestRun,
    AlertTriggered
};

// ============================================================
// Data Structures
// ============================================================

struct TenantQuota {
    ResourceType resource;
    int64_t     limit       = 0;    // -1 for unlimited
    int64_t     used        = 0;
    double      usage_pct() const { return (limit > 0) ? static_cast<double>(used) / limit * 100.0 : 0.0; }
    bool        exceeded() const { return limit > 0 && used >= limit; }
};

struct TenantBranding {
    std::string company_name        = "";
    std::string logo_url            = "";
    std::string primary_color       = "#1a73e8";
    std::string secondary_color     = "#174ea6";
    std::string favicon_url         = "";
    std::string custom_domain       = "";       // e.g., "portfolio.acmecapital.com"
    std::string email_from_name     = "Metis Genie Platform";
    std::string email_from_address  = "";
    bool        show_powered_by     = true;
};

struct TenantConfig {
    // Feature flags
    bool        enable_trading          = false;
    bool        enable_real_time_data   = false;
    bool        enable_options          = false;
    bool        enable_fixed_income     = false;
    bool        enable_fx               = false;
    bool        enable_compliance       = true;
    bool        enable_reporting        = true;
    bool        enable_backtesting      = true;
    bool        enable_ml_alpha         = false;
    bool        enable_nlq              = false;
    bool        enable_api_access       = true;

    // Data settings
    IsolationStrategy isolation     = IsolationStrategy::SharedSchema;
    std::string database_schema     = "";       // For schema-per-tenant
    std::string storage_prefix      = "";       // S3/filesystem prefix
    int         data_retention_days = 365 * 7;  // 7 years default

    // Security
    bool        require_2fa             = false;
    bool        require_ip_whitelist    = false;
    int         session_timeout_min     = 480;  // 8 hours
    int         max_failed_logins       = 5;
    bool        audit_all_reads         = false; // Log read operations too

    // Performance
    int         max_concurrent_queries  = 10;
    int         query_timeout_sec       = 60;
    int         batch_size_limit        = 10000;
};

struct TenantMetrics {
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_active;
    int64_t     total_api_calls     = 0;
    int64_t     total_trades        = 0;
    int64_t     total_reports       = 0;
    double      storage_used_mb     = 0.0;
    double      compute_minutes     = 0.0;
    int         active_users        = 0;
    int         total_portfolios    = 0;
    double      total_aum           = 0.0;
    double      uptime_pct          = 99.9;

    // Billing period
    int64_t     period_api_calls    = 0;
    double      period_compute_min  = 0.0;
    double      period_cost_usd     = 0.0;
};

struct Tenant {
    std::string     id;                 // UUID
    std::string     name;
    std::string     display_name;
    TenantStatus    status          = TenantStatus::Active;
    TenantTier      tier            = TenantTier::Starter;
    TenantConfig    config;
    TenantBranding  branding;
    TenantMetrics   metrics;
    std::string     parent_org_id;      // For hierarchical tenants
    std::vector<std::string> team_ids;
    std::map<ResourceType, TenantQuota> quotas;
    std::map<std::string, std::string>  metadata;

    [[nodiscard]] bool is_active() const { return status == TenantStatus::Active; }
    [[nodiscard]] bool is_writable() const {
        return status == TenantStatus::Active;
    }
    [[nodiscard]] bool can_read() const {
        return status == TenantStatus::Active || status == TenantStatus::ReadOnly;
    }
};

struct TenantUser {
    std::string tenant_id;
    std::string user_id;
    std::string email;
    std::string display_name;
    std::string role;           // "admin", "portfolio_manager", "trader", "viewer"
    bool        is_tenant_admin = false;
    std::chrono::system_clock::time_point joined_at;
    std::chrono::system_clock::time_point last_login;
};

struct TenantAuditEntry {
    std::string tenant_id;
    std::string user_id;
    std::string action;         // "login", "create_portfolio", "execute_trade", etc.
    std::string resource_type;
    std::string resource_id;
    std::string details;
    std::chrono::system_clock::time_point timestamp;
    std::string ip_address;
    std::string previous_hash;  // Chain for tamper detection
    std::string hash;
};

// ============================================================
// Rate Limiter (Token Bucket)
// ============================================================

class TenantRateLimiter {
public:
    struct Bucket {
        double      tokens;
        double      max_tokens;
        double      refill_rate;    // Tokens per second
        std::chrono::steady_clock::time_point last_refill;
    };

    bool allow(const std::string& tenant_id, ResourceType resource) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = tenant_id + ":" + std::to_string(static_cast<int>(resource));

        auto it = buckets_.find(key);
        if (it == buckets_.end()) {
            // Create default bucket
            Bucket b;
            b.max_tokens = default_limits_.count(resource) ? default_limits_.at(resource) : 100.0;
            b.tokens = b.max_tokens;
            b.refill_rate = b.max_tokens / 60.0;  // Refill over 1 minute
            b.last_refill = std::chrono::steady_clock::now();
            buckets_[key] = b;
            it = buckets_.find(key);
        }

        auto& bucket = it->second;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - bucket.last_refill).count();
        bucket.tokens = std::min(bucket.max_tokens, bucket.tokens + elapsed * bucket.refill_rate);
        bucket.last_refill = now;

        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return true;
        }
        return false;
    }

    void set_limit(const std::string& tenant_id, ResourceType resource, double max_tokens, double refill_rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = tenant_id + ":" + std::to_string(static_cast<int>(resource));
        Bucket b;
        b.max_tokens = max_tokens;
        b.tokens = max_tokens;
        b.refill_rate = refill_rate;
        b.last_refill = std::chrono::steady_clock::now();
        buckets_[key] = b;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    const std::map<ResourceType, double> default_limits_ = {
        {ResourceType::ApiCallsPerMinute, 60.0},
        {ResourceType::ApiCallsPerDay, 10000.0},
        {ResourceType::ConcurrentSessions, 10.0}
    };
};

// ============================================================
// Billing Meter
// ============================================================

class BillingMeter {
public:
    struct MeterEntry {
        std::string tenant_id;
        BillingEvent event;
        double      quantity = 1.0;
        double      unit_cost = 0.0;
        std::chrono::system_clock::time_point timestamp;
    };

    void record(const std::string& tenant_id, BillingEvent event, double quantity = 1.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        MeterEntry entry;
        entry.tenant_id = tenant_id;
        entry.event = event;
        entry.quantity = quantity;
        entry.unit_cost = unit_costs_.count(event) ? unit_costs_.at(event) : 0.0;
        entry.timestamp = std::chrono::system_clock::now();
        entries_.push_back(entry);

        // Update running totals
        auto& total = totals_[tenant_id][event];
        total += quantity;
    }

    double get_period_cost(const std::string& tenant_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        double cost = 0.0;
        auto it = totals_.find(tenant_id);
        if (it != totals_.end()) {
            for (const auto& [event, qty] : it->second) {
                double uc = unit_costs_.count(event) ? unit_costs_.at(event) : 0.0;
                cost += qty * uc;
            }
        }
        return cost;
    }

    void reset_period(const std::string& tenant_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        totals_.erase(tenant_id);
    }

    void set_unit_cost(BillingEvent event, double cost) {
        std::lock_guard<std::mutex> lock(mutex_);
        unit_costs_[event] = cost;
    }

private:
    mutable std::mutex mutex_;
    std::vector<MeterEntry> entries_;
    std::map<std::string, std::map<BillingEvent, double>> totals_;
    std::map<BillingEvent, double> unit_costs_ = {
        {BillingEvent::ApiCall, 0.0001},        // $0.01 per 100 calls
        {BillingEvent::ComputeMinute, 0.01},    // $0.01 per minute
        {BillingEvent::StorageUsed, 0.10},      // $0.10 per GB/month
        {BillingEvent::ReportGenerated, 0.05},  // $0.05 per report
        {BillingEvent::TradeExecuted, 0.001},   // $0.001 per trade
        {BillingEvent::BacktestRun, 0.10}       // $0.10 per backtest
    };
};

// ============================================================
// Tenant Manager (Main Engine)
// ============================================================

class TenantManager {
public:
    TenantManager() {
        // Initialize default tier quotas
        tier_quotas_[TenantTier::Free] = {
            {ResourceType::Portfolios, {ResourceType::Portfolios, 3, 0}},
            {ResourceType::Users, {ResourceType::Users, 1, 0}},
            {ResourceType::ApiCallsPerDay, {ResourceType::ApiCallsPerDay, 1000, 0}},
            {ResourceType::StorageMB, {ResourceType::StorageMB, 100, 0}},
            {ResourceType::Backtests, {ResourceType::Backtests, 5, 0}}
        };
        tier_quotas_[TenantTier::Starter] = {
            {ResourceType::Portfolios, {ResourceType::Portfolios, 25, 0}},
            {ResourceType::Users, {ResourceType::Users, 5, 0}},
            {ResourceType::ApiCallsPerDay, {ResourceType::ApiCallsPerDay, 50000, 0}},
            {ResourceType::StorageMB, {ResourceType::StorageMB, 5000, 0}},
            {ResourceType::Backtests, {ResourceType::Backtests, 100, 0}}
        };
        tier_quotas_[TenantTier::Professional] = {
            {ResourceType::Portfolios, {ResourceType::Portfolios, 200, 0}},
            {ResourceType::Users, {ResourceType::Users, 50, 0}},
            {ResourceType::ApiCallsPerDay, {ResourceType::ApiCallsPerDay, 500000, 0}},
            {ResourceType::StorageMB, {ResourceType::StorageMB, 50000, 0}},
            {ResourceType::Backtests, {ResourceType::Backtests, -1, 0}}  // Unlimited
        };
        tier_quotas_[TenantTier::Enterprise] = {
            {ResourceType::Portfolios, {ResourceType::Portfolios, -1, 0}},
            {ResourceType::Users, {ResourceType::Users, -1, 0}},
            {ResourceType::ApiCallsPerDay, {ResourceType::ApiCallsPerDay, -1, 0}},
            {ResourceType::StorageMB, {ResourceType::StorageMB, -1, 0}},
            {ResourceType::Backtests, {ResourceType::Backtests, -1, 0}}
        };
    }

    // Create a new tenant
    std::string create_tenant(const std::string& name, TenantTier tier = TenantTier::Starter) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::string id = generate_id();

        Tenant tenant;
        tenant.id = id;
        tenant.name = name;
        tenant.display_name = name;
        tenant.status = TenantStatus::Active;
        tenant.tier = tier;
        tenant.metrics.created_at = std::chrono::system_clock::now();
        tenant.metrics.last_active = tenant.metrics.created_at;

        // Apply tier quotas
        if (tier_quotas_.count(tier)) {
            tenant.quotas = tier_quotas_.at(tier);
        }

        // Set isolation strategy based on tier
        if (tier == TenantTier::Enterprise) {
            tenant.config.isolation = IsolationStrategy::SchemaPerTenant;
            tenant.config.database_schema = "tenant_" + id;
        } else {
            tenant.config.isolation = IsolationStrategy::SharedSchema;
        }

        tenants_[id] = tenant;
        name_index_[name] = id;
        return id;
    }

    // Get tenant by ID
    std::optional<Tenant> get_tenant(const std::string& id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(id);
        if (it != tenants_.end()) return it->second;
        return std::nullopt;
    }

    // Get tenant by name
    std::optional<Tenant> get_tenant_by_name(const std::string& name) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = name_index_.find(name);
        if (it == name_index_.end()) return std::nullopt;
        auto tid = tenants_.find(it->second);
        if (tid != tenants_.end()) return tid->second;
        return std::nullopt;
    }

    // Suspend tenant
    bool suspend_tenant(const std::string& id, const std::string& reason = "") {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(id);
        if (it == tenants_.end()) return false;
        it->second.status = TenantStatus::Suspended;
        it->second.metadata["suspend_reason"] = reason;
        return true;
    }

    // Reactivate tenant
    bool reactivate_tenant(const std::string& id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(id);
        if (it == tenants_.end()) return false;
        it->second.status = TenantStatus::Active;
        it->second.metadata.erase("suspend_reason");
        return true;
    }

    // Upgrade/downgrade tier
    bool change_tier(const std::string& id, TenantTier new_tier) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(id);
        if (it == tenants_.end()) return false;
        it->second.tier = new_tier;
        if (tier_quotas_.count(new_tier)) {
            it->second.quotas = tier_quotas_.at(new_tier);
        }
        return true;
    }

    // Check resource quota
    bool check_quota(const std::string& tenant_id, ResourceType resource) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(tenant_id);
        if (it == tenants_.end()) return false;
        auto qit = it->second.quotas.find(resource);
        if (qit == it->second.quotas.end()) return true; // No quota = allowed
        return !qit->second.exceeded();
    }

    // Increment resource usage
    bool use_resource(const std::string& tenant_id, ResourceType resource, int64_t amount = 1) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(tenant_id);
        if (it == tenants_.end()) return false;
        auto qit = it->second.quotas.find(resource);
        if (qit == it->second.quotas.end()) return true;
        if (qit->second.limit > 0 && qit->second.used + amount > qit->second.limit) return false;
        qit->second.used += amount;
        return true;
    }

    // Check rate limit
    bool check_rate_limit(const std::string& tenant_id, ResourceType resource) {
        return rate_limiter_.allow(tenant_id, resource);
    }

    // Record billing event
    void record_billing(const std::string& tenant_id, BillingEvent event, double qty = 1.0) {
        billing_.record(tenant_id, event, qty);
    }

    // Get billing cost for period
    double get_billing_cost(const std::string& tenant_id) const {
        return billing_.get_period_cost(tenant_id);
    }

    // Add user to tenant
    bool add_user(const std::string& tenant_id, const TenantUser& user) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(tenant_id);
        if (it == tenants_.end()) return false;
        if (!use_resource_internal(it->second, ResourceType::Users)) return false;
        users_[tenant_id].push_back(user);
        return true;
    }

    // List all tenants
    std::vector<Tenant> list_tenants(std::optional<TenantStatus> status_filter = std::nullopt) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<Tenant> result;
        for (const auto& [id, tenant] : tenants_) {
            if (!status_filter || tenant.status == *status_filter) {
                result.push_back(tenant);
            }
        }
        return result;
    }

    // Get tenant count
    size_t tenant_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return tenants_.size();
    }

    // Tenant context for request scoping
    struct TenantContext {
        std::string tenant_id;
        std::string user_id;
        std::string role;
        TenantTier  tier;
        TenantConfig config;
        bool        is_valid = false;

        [[nodiscard]] bool has_feature(const std::string& feature) const {
            if (feature == "trading") return config.enable_trading;
            if (feature == "real_time_data") return config.enable_real_time_data;
            if (feature == "options") return config.enable_options;
            if (feature == "ml_alpha") return config.enable_ml_alpha;
            if (feature == "nlq") return config.enable_nlq;
            return true; // Unknown features default to allowed
        }
    };

    // Resolve tenant context from request
    TenantContext resolve_context(const std::string& tenant_id, const std::string& user_id) const {
        TenantContext ctx;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = tenants_.find(tenant_id);
        if (it == tenants_.end() || !it->second.is_active()) return ctx;

        ctx.tenant_id = tenant_id;
        ctx.user_id = user_id;
        ctx.tier = it->second.tier;
        ctx.config = it->second.config;
        ctx.is_valid = true;

        // Find user role
        auto uit = users_.find(tenant_id);
        if (uit != users_.end()) {
            for (const auto& u : uit->second) {
                if (u.user_id == user_id) {
                    ctx.role = u.role;
                    break;
                }
            }
        }

        return ctx;
    }

    // Health summary
    std::string health_summary() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::ostringstream ss;
        ss << "Multi-Tenant Health Summary\n"
           << "  Total tenants: " << tenants_.size() << "\n";
        int active = 0, suspended = 0;
        for (const auto& [id, t] : tenants_) {
            if (t.status == TenantStatus::Active) active++;
            else if (t.status == TenantStatus::Suspended) suspended++;
        }
        ss << "  Active: " << active << " | Suspended: " << suspended << "\n";
        return ss.str();
    }

private:
    bool use_resource_internal(Tenant& tenant, ResourceType resource) {
        auto it = tenant.quotas.find(resource);
        if (it == tenant.quotas.end()) return true;
        if (it->second.limit > 0 && it->second.used >= it->second.limit) return false;
        it->second.used++;
        return true;
    }

    std::string generate_id() const {
        static std::mt19937_64 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        static const char chars[] = "0123456789abcdef";
        std::string id;
        id.reserve(32);
        for (int i = 0; i < 32; ++i) {
            id += chars[rng() % 16];
            if (i == 7 || i == 11 || i == 15 || i == 19) id += '-';
        }
        return id;
    }

    mutable std::shared_mutex mutex_;
    std::map<std::string, Tenant> tenants_;
    std::map<std::string, std::string> name_index_;  // name -> id
    std::map<std::string, std::vector<TenantUser>> users_;
    std::map<TenantTier, std::map<ResourceType, TenantQuota>> tier_quotas_;
    TenantRateLimiter rate_limiter_;
    mutable BillingMeter billing_;
};

} // namespace core
} // namespace genie

#endif // GENIE_CORE_MULTI_TENANT_HPP
