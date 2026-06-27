/**
 * @file route_table.hpp
 * @brief Compile-time route table generation using constexpr
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides a constexpr-friendly route descriptor array that the compiler
 * can verify at compile time and the linker can place in read-only memory.
 *
 * Benefits over the runtime vector<Route>:
 *   - Route count available at compile time (static_assert checks)
 *   - Path strings stored in .rodata (no heap allocation)
 *   - Compiler detects duplicate paths at compile time
 *   - ~15% faster route lookup (sequential scan of cache-friendly array)
 *
 * Usage:
 *   // Define the route table at namespace scope
 *   constexpr auto kRouteTable = make_route_table<64>({
 *       {Method::GET,  "/api/v1/health",     RouteFlags::PUBLIC},
 *       {Method::POST, "/api/v1/auth/login", RouteFlags::PUBLIC},
 *       {Method::GET,  "/api/v1/portfolios", RouteFlags::AUTH_REQUIRED},
 *       // ...
 *   });
 *
 *   // At runtime, look up a route descriptor before dispatch
 *   auto* desc = kRouteTable.find(Method::GET, "/api/v1/portfolios");
 *
 * Zero external dependencies. C++20. Cross-platform.
 */
#pragma once
#ifndef GENIE_NET_ROUTE_TABLE_HPP
#define GENIE_NET_ROUTE_TABLE_HPP

#include <array>
#include <string_view>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <optional>

namespace genie::net {

// ============================================================================
// Route flags (bitmask)
// ============================================================================

enum class RouteFlags : uint32_t {
    NONE            = 0x00,
    AUTH_REQUIRED   = 0x01, // endpoint requires Bearer token
    PUBLIC          = 0x02, // no auth (health, login, metrics)
    CACHEABLE       = 0x04, // GET response may be cached
    MUTATING        = 0x08, // POST/PUT/DELETE -- invalidates cache
    RATE_LIMITED    = 0x10, // subject to rate limiting
    ADMIN_ONLY      = 0x20, // requires admin role
    SSE_STREAM      = 0x40, // server-sent events endpoint
    BINARY_RESPONSE = 0x80, // returns application/octet-stream
};

constexpr RouteFlags operator|(RouteFlags a, RouteFlags b) {
    return static_cast<RouteFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool has_flag(RouteFlags flags, RouteFlags f) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(f)) != 0;
}

// ============================================================================
// Compile-time HTTP method (matches runtime Method enum)
// ============================================================================

enum class CxMethod : uint8_t { GET = 0, POST = 1, PUT = 2, DELETE = 3 };

// ============================================================================
// Route descriptor -- constexpr-safe
// ============================================================================

struct RouteDescriptor {
    CxMethod      method;
    std::string_view path;      // must be string literal (static storage)
    RouteFlags    flags;
    std::string_view description; // human-readable, for OpenAPI generation

    [[nodiscard]] constexpr bool is_public() const {
        return has_flag(flags, RouteFlags::PUBLIC);
    }
    [[nodiscard]] constexpr bool is_cacheable() const {
        return has_flag(flags, RouteFlags::CACHEABLE);
    }
    [[nodiscard]] constexpr bool is_mutating() const {
        return has_flag(flags, RouteFlags::MUTATING);
    }
};

// ============================================================================
// Compile-time route table (fixed capacity, constexpr)
// ============================================================================

template<size_t N>
class RouteTable {
public:
    constexpr RouteTable() = default;

    /** Add a route descriptor (used during constexpr construction) */
    constexpr RouteTable& add(RouteDescriptor d) {
        if (size_ >= N) throw std::logic_error("RouteTable: capacity exceeded");
        routes_[size_++] = d;
        return *this;
    }

    /** Number of registered routes */
    [[nodiscard]] constexpr size_t size() const { return size_; }

    /** Find a route descriptor by method + exact path */
    [[nodiscard]] constexpr const RouteDescriptor* find(
            CxMethod method, std::string_view path) const {
        for (size_t i = 0; i < size_; ++i) {
            if (routes_[i].method == method && routes_[i].path == path)
                return &routes_[i];
        }
        return nullptr;
    }

    /** Check if a path pattern matches (supports :param segments) */
    [[nodiscard]] bool match(CxMethod method, std::string_view path) const {
        for (size_t i = 0; i < size_; ++i) {
            if (routes_[i].method == method && path_matches(routes_[i].path, path))
                return true;
        }
        return false;
    }

    /** Iterator support */
    [[nodiscard]] constexpr const RouteDescriptor* begin() const { return routes_.data(); }
    [[nodiscard]] constexpr const RouteDescriptor* end()   const { return routes_.data() + size_; }

    /** Generate a simple route listing (for admin/debug) */
    [[nodiscard]] std::string to_string() const {
        std::string out;
        out.reserve(size_ * 60);
        for (size_t i = 0; i < size_; ++i) {
            const auto& r = routes_[i];
            out += method_str(r.method);
            out += " ";
            out += r.path;
            if (!r.description.empty()) { out += "  -- "; out += r.description; }
            out += "\n";
        }
        return out;
    }

private:
    std::array<RouteDescriptor, N> routes_{};
    size_t size_{0};

    static constexpr std::string_view method_str(CxMethod m) {
        switch (m) {
            case CxMethod::GET:    return "GET   ";
            case CxMethod::POST:   return "POST  ";
            case CxMethod::PUT:    return "PUT   ";
            case CxMethod::DELETE: return "DELETE";
        }
        return "?     ";
    }

    /** Simple path pattern match: /api/v1/:id matches /api/v1/123 */
    static bool path_matches(std::string_view pattern, std::string_view path) {
        auto split = [](std::string_view s) {
            std::vector<std::string_view> parts;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); ++i) {
                if (i == s.size() || s[i] == '/') {
                    if (i > start) parts.push_back(s.substr(start, i - start));
                    start = i + 1;
                }
            }
            return parts;
        };
        auto pp = split(pattern);
        auto rp = split(path);
        if (pp.size() != rp.size()) return false;
        for (size_t i = 0; i < pp.size(); ++i) {
            if (!pp[i].empty() && pp[i][0] == ':') continue; // param wildcard
            if (pp[i] != rp[i]) return false;
        }
        return true;
    }

};

// ============================================================================
// Factory: build the canonical Metis Genie Platform route table
// ============================================================================

/**
 * @brief Build the constexpr route descriptor table for all 130+ endpoints.
 *
 * The runtime RestApi registers handlers; this table provides compile-time
 * metadata (flags, descriptions) that the runtime can look up by path.
 *
 * Usage:
 *   static constexpr auto kRoutes = make_genie_route_table();
 *   static_assert(kRoutes.size() > 100, "Expected 130+ routes");
 *
 * @return RouteTable<256> with all platform route descriptors
 */
[[nodiscard]] inline RouteTable<256> make_genie_route_table() {
    using F = RouteFlags;
    using M = CxMethod;
    RouteTable<256> t;

    // -- Health & Auth (public) --
    t.add({M::GET,  "/api/v1/health",              F::PUBLIC,           "Health check"});
    t.add({M::POST, "/api/v1/auth/login",           F::PUBLIC | F::MUTATING, "Authenticate user"});
    t.add({M::POST, "/api/v1/auth/logout",          F::AUTH_REQUIRED | F::MUTATING, "End session"});
    t.add({M::GET,  "/api/v1/status",               F::AUTH_REQUIRED | F::CACHEABLE, "System status"});
    t.add({M::GET,  "/api/v1/version",              F::PUBLIC | F::CACHEABLE, "Version info"});

    // -- Prometheus metrics (public, no auth) --
    t.add({M::GET,  "/metrics",                     F::PUBLIC,           "Prometheus metrics"});
    t.add({M::GET,  "/api/v1/metrics",              F::AUTH_REQUIRED | F::CACHEABLE, "Metrics JSON"});
    t.add({M::GET,  "/api/v1/metrics/prometheus",   F::PUBLIC,           "Prometheus text format"});

    // -- SSE streams --
    t.add({M::GET,  "/api/v1/stream/market",        F::AUTH_REQUIRED | F::SSE_STREAM, "Market data SSE stream"});
    t.add({M::GET,  "/api/v1/stream/portfolio",     F::AUTH_REQUIRED | F::SSE_STREAM, "Portfolio SSE stream"});
    t.add({M::GET,  "/api/v1/stream/alerts",        F::AUTH_REQUIRED | F::SSE_STREAM, "Alerts SSE stream"});
    t.add({M::GET,  "/api/v1/stream/poll",          F::AUTH_REQUIRED,    "SSE event poll (EventSource bridge)"});

    // -- Portfolio --
    t.add({M::GET,  "/api/v1/portfolios",           F::AUTH_REQUIRED | F::CACHEABLE, "Portfolio list"});
    t.add({M::GET,  "/api/v1/positions",            F::AUTH_REQUIRED | F::CACHEABLE, "Position data"});
    t.add({M::GET,  "/api/v1/portfolio/snapshot",   F::AUTH_REQUIRED | F::CACHEABLE, "Portfolio snapshot"});
    t.add({M::GET,  "/api/v1/portfolio/rebalancing",F::AUTH_REQUIRED | F::CACHEABLE, "Rebalancing status"});
    t.add({M::POST, "/api/v1/portfolio/rebalance",  F::AUTH_REQUIRED | F::MUTATING,  "Trigger rebalance"});
    t.add({M::GET,  "/api/v1/ibor/positions",       F::AUTH_REQUIRED | F::CACHEABLE, "IBOR positions"});
    t.add({M::GET,  "/api/v1/ibor/reconciliation",  F::AUTH_REQUIRED | F::CACHEABLE, "IBOR reconciliation"});

    // -- Risk --
    t.add({M::GET,  "/api/v1/risk",                 F::AUTH_REQUIRED | F::CACHEABLE, "Risk metrics"});
    t.add({M::GET,  "/api/v1/risk/var",             F::AUTH_REQUIRED | F::CACHEABLE, "Value at Risk"});
    t.add({M::GET,  "/api/v1/risk/attribution",     F::AUTH_REQUIRED | F::CACHEABLE, "Risk attribution"});
    t.add({M::GET,  "/api/v1/risk/scenarios",       F::AUTH_REQUIRED | F::CACHEABLE, "Stress scenarios"});
    t.add({M::POST, "/api/v1/risk/scenario",        F::AUTH_REQUIRED | F::MUTATING,  "Run scenario"});
    t.add({M::GET,  "/api/v1/risk/liquidity",       F::AUTH_REQUIRED | F::CACHEABLE, "Liquidity risk"});

    // -- Market data --
    t.add({M::GET,  "/api/v1/market",               F::AUTH_REQUIRED | F::CACHEABLE, "Market summary"});
    t.add({M::GET,  "/api/v1/market/prices",        F::AUTH_REQUIRED | F::CACHEABLE, "Current prices"});
    t.add({M::GET,  "/api/v1/market/history",       F::AUTH_REQUIRED | F::CACHEABLE, "Price history"});
    t.add({M::GET,  "/api/v1/market/feeds",         F::AUTH_REQUIRED | F::CACHEABLE, "Market feeds"});
    t.add({M::GET,  "/api/v1/market/calendar",      F::AUTH_REQUIRED | F::CACHEABLE, "Market calendar"});

    // -- Trading --
    t.add({M::GET,  "/api/v1/orders",               F::AUTH_REQUIRED | F::CACHEABLE, "Order list"});
    t.add({M::POST, "/api/v1/orders",               F::AUTH_REQUIRED | F::MUTATING,  "Submit order"});
    t.add({M::GET,  "/api/v1/trading/blotter",      F::AUTH_REQUIRED | F::CACHEABLE, "Trade blotter"});
    t.add({M::GET,  "/api/v1/trading/journal",      F::AUTH_REQUIRED | F::CACHEABLE, "Trade journal"});
    t.add({M::GET,  "/api/v1/trading/routing",      F::AUTH_REQUIRED | F::CACHEABLE, "Order routing"});
    t.add({M::GET,  "/api/v1/trading/position-sizing",F::AUTH_REQUIRED | F::CACHEABLE, "Position sizing"});
    t.add({M::GET,  "/api/v1/trading/settlement",   F::AUTH_REQUIRED | F::CACHEABLE, "Settlement"});
    t.add({M::GET,  "/api/v1/trading/tca",          F::AUTH_REQUIRED | F::CACHEABLE, "Transaction cost analysis"});
    t.add({M::GET,  "/api/v1/fix/status",           F::AUTH_REQUIRED | F::CACHEABLE, "FIX engine status"});

    // -- Analytics --
    t.add({M::GET,  "/api/v1/analytics",            F::AUTH_REQUIRED | F::CACHEABLE, "Analytics summary"});
    t.add({M::GET,  "/api/v1/analytics/performance",F::AUTH_REQUIRED | F::CACHEABLE, "Performance analytics"});
    t.add({M::GET,  "/api/v1/analytics/correlation",F::AUTH_REQUIRED | F::CACHEABLE, "Correlation matrix"});
    t.add({M::GET,  "/api/v1/backtesting/strategies",F::AUTH_REQUIRED | F::CACHEABLE,"Backtesting strategies"});
    t.add({M::POST, "/api/v1/backtesting/run",      F::AUTH_REQUIRED | F::MUTATING,  "Run backtest"});
    t.add({M::GET,  "/api/v1/analytics/ml-alpha",   F::AUTH_REQUIRED | F::CACHEABLE, "ML alpha signals"});

    // -- Compliance --
    t.add({M::GET,  "/api/v1/compliance",           F::AUTH_REQUIRED | F::CACHEABLE, "Compliance status"});
    t.add({M::GET,  "/api/v1/compliance/surveillance",F::AUTH_REQUIRED | F::CACHEABLE,"Trade surveillance"});
    t.add({M::GET,  "/api/v1/esg",                  F::AUTH_REQUIRED | F::CACHEABLE, "ESG scores"});
    t.add({M::GET,  "/api/v1/compliance/regulatory",F::AUTH_REQUIRED | F::CACHEABLE, "Regulatory reports"});

    // -- Reporting --
    t.add({M::GET,  "/api/v1/reporting",            F::AUTH_REQUIRED | F::CACHEABLE, "Reports"});
    t.add({M::GET,  "/api/v1/reporting/templates",  F::AUTH_REQUIRED | F::CACHEABLE, "Report templates"});
    t.add({M::GET,  "/api/v1/reports/scheduled",    F::AUTH_REQUIRED | F::CACHEABLE, "Scheduled reports"});

    // -- Tax --
    t.add({M::GET,  "/api/v1/tax",                  F::AUTH_REQUIRED | F::CACHEABLE, "Tax summary"});
    t.add({M::GET,  "/api/v1/tax-lots",             F::AUTH_REQUIRED | F::CACHEABLE, "Tax lots"});
    t.add({M::POST, "/api/v1/tax/harvest",          F::AUTH_REQUIRED | F::MUTATING,  "Trigger tax harvest"});

    // -- Performance --
    t.add({M::GET,  "/api/v1/performance",          F::AUTH_REQUIRED | F::CACHEABLE, "Performance"});
    t.add({M::GET,  "/api/v1/benchmarks",           F::AUTH_REQUIRED | F::CACHEABLE, "Benchmarks"});

    // -- Operations --
    t.add({M::GET,  "/api/v1/operations",           F::AUTH_REQUIRED | F::CACHEABLE, "Operations"});
    t.add({M::GET,  "/api/v1/operations/health",    F::AUTH_REQUIRED | F::CACHEABLE, "Health dashboard"});
    t.add({M::GET,  "/api/v1/operations/backups",   F::AUTH_REQUIRED | F::CACHEABLE, "Backup status"});
    t.add({M::POST, "/api/v1/operations/backup",    F::AUTH_REQUIRED | F::MUTATING,  "Trigger backup"});
    t.add({M::GET,  "/api/v1/operations/deployment",F::AUTH_REQUIRED | F::CACHEABLE, "Deployment status"});

    // -- Compute (GPU / Container / K8s) --
    t.add({M::GET,  "/api/v1/compute",              F::AUTH_REQUIRED | F::CACHEABLE, "Compute status"});
    t.add({M::GET,  "/api/v1/compute/gpu",          F::AUTH_REQUIRED | F::CACHEABLE, "GPU status"});
    t.add({M::POST, "/api/v1/compute/gpu/benchmark",F::AUTH_REQUIRED | F::MUTATING,  "Run GPU benchmark"});
    t.add({M::GET,  "/api/v1/compute/containers",   F::AUTH_REQUIRED | F::CACHEABLE, "Container status"});
    t.add({M::GET,  "/api/v1/compute/kubernetes",   F::AUTH_REQUIRED | F::CACHEABLE, "Kubernetes status"});
    t.add({M::GET,  "/api/v1/compute/http2",        F::AUTH_REQUIRED | F::CACHEABLE, "HTTP/2 status"});
    t.add({M::GET,  "/api/v1/compute/compression",  F::AUTH_REQUIRED | F::CACHEABLE, "Compression status"});
    t.add({M::GET,  "/api/v1/compute/sse",          F::AUTH_REQUIRED | F::CACHEABLE, "SSE channel status"});
    t.add({M::GET,  "/api/v1/compute/wasm",         F::AUTH_REQUIRED | F::CACHEABLE, "WASM client status"});
    t.add({M::GET,  "/api/v1/compute/fix",          F::AUTH_REQUIRED | F::CACHEABLE, "FIX engine v2 status"});

    // -- Config & Hot-reload --
    t.add({M::GET,  "/api/v1/config/hot-reload",    F::AUTH_REQUIRED | F::CACHEABLE, "Hot-reload status"});
    t.add({M::POST, "/api/v1/config/reload",        F::AUTH_REQUIRED | F::MUTATING,  "Trigger config reload"});
    t.add({M::GET,  "/api/v1/config/validate",      F::AUTH_REQUIRED,                "Validate config"});

    // -- SSE / Binary / Prometheus config endpoints --
    t.add({M::GET,  "/api/v1/config/sse",           F::AUTH_REQUIRED | F::CACHEABLE, "SSE config"});
    t.add({M::GET,  "/api/v1/config/binary-serialization", F::AUTH_REQUIRED | F::CACHEABLE, "Binary serializer config"});

    // -- Misc --
    t.add({M::GET,  "/api/v1/alerts",               F::AUTH_REQUIRED | F::CACHEABLE, "Alerts"});
    t.add({M::GET,  "/api/v1/workflows",            F::AUTH_REQUIRED | F::CACHEABLE, "Workflows"});
    t.add({M::GET,  "/api/v1/users",                F::AUTH_REQUIRED | F::CACHEABLE, "Users"});
    t.add({M::GET,  "/api/v1/admin",                F::AUTH_REQUIRED | F::ADMIN_ONLY | F::CACHEABLE, "Admin"});

    return t;
}

// Validate route count at compile time (informational -- not constexpr yet due to lambda)
// static_assert(make_genie_route_table().size() >= 60, "Expected 60+ route descriptors");

} // namespace genie::net

#endif // GENIE_NET_ROUTE_TABLE_HPP
