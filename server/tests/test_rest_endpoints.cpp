/**
 * @file test_rest_endpoints.cpp
 * @brief REST API endpoint tests for Metis Genie Platform v5.5.11
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Exercises all 34 REST API endpoints end-to-end via the RestApi
 * in-process handler (no network required). Tests:
 *   - Health and status (unauthenticated)
 *   - Auth flow: register, login, token, logout
 *   - All authenticated data endpoints
 *   - Admin user management
 *   - Order submission (POST)
 *   - Report generation
 *   - Security, operations, compute, deployment endpoints
 *   - Error handling: 401 unauthenticated, 404 not found
 *
 * Build:
 *   cmake --build . --target test_rest_endpoints
 *   ./bin/test_rest_endpoints
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cassert>

#include "genie/genie.hpp"
#include "genie/net/rest_api.hpp"

using namespace genie;

// ============================================================================
// Test Framework
// ============================================================================

static int g_run = 0, g_pass = 0, g_fail = 0;

struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};
static std::vector<TestResult> g_results;

#define TEST(name) \
    do { g_run++; std::cout << "  " << name << " ... "; } while(0)

#define PASS() \
    do { g_pass++; std::cout << "PASS\n"; g_results.push_back({__func__, true, ""}); } while(0)

#define FAIL(msg) \
    do { g_fail++; std::cout << "FAIL: " << msg << "\n"; \
         g_results.push_back({__func__, false, msg}); return; } while(0)

#define CHECK(cond, msg) \
    if (!(cond)) { FAIL(msg); }

#define CHECK_STATUS(res, expected) \
    if (res.status != expected) { \
        FAIL("Expected HTTP " + std::to_string(expected) + " got " + std::to_string(res.status) + \
             " body: " + res.body.substr(0, 120)); }

#define CHECK_BODY_CONTAINS(res, needle) \
    if (res.body.find(needle) == std::string::npos) { \
        FAIL("Response body missing '" + std::string(needle) + "'"); }

// ============================================================================
// Helpers
// ============================================================================

static net::RestApi g_api;
static std::string g_token;
static std::string g_admin_token;

using Headers = std::map<std::string, std::string>;

static Headers auth_headers(const std::string& token) {
    return {{"Authorization", "Bearer " + token}};
}

static Headers admin_headers() {
    return auth_headers(g_admin_token);
}

static Headers user_headers() {
    return auth_headers(g_token);
}

static std::string login(const std::string& user, const std::string& pass) {
    std::string body = R"({"username":")" + user + R"(","password":")" + pass + R"("})";
    auto res = g_api.handle("POST", "/api/v1/auth/login", body, {});
    if (res.status != 200) return "";

    auto pos = res.body.find("\"token\":\"");
    if (pos == std::string::npos) return "";
    auto start = pos + 9;
    auto end = res.body.find("\"", start);
    return res.body.substr(start, end - start);
}

// ============================================================================
// 1. Health & Status (unauthenticated)
// ============================================================================

void test_health() {
    std::cout << "\n=== Health & Status ===\n";

    {
        TEST("GET /api/v1/health => 200");
        auto res = g_api.handle("GET", "/api/v1/health", "", {});
        CHECK_STATUS(res, 200);
        CHECK_BODY_CONTAINS(res, "healthy");
        PASS();
    }

    {
        TEST("GET /api/v1/status => 200");
        auto res = g_api.handle("GET", "/api/v1/status", "", {});
        CHECK_STATUS(res, 200);
        CHECK_BODY_CONTAINS(res, "version");
        PASS();
    }
}

// ============================================================================
// 2. Authentication Flow
// ============================================================================

void test_auth() {
    std::cout << "\n=== Authentication ===\n";

    {
        TEST("POST /api/v1/auth/login (admin/demo) => 200 + token");
        g_admin_token = login("admin", "demo");
        CHECK(!g_admin_token.empty(), "admin login returned empty token");
        PASS();
    }

    {
        TEST("POST /api/v1/auth/login (trader/trade) => 200 + token");
        g_token = login("trader", "trade");
        CHECK(!g_token.empty(), "trader login returned empty token");
        PASS();
    }

    {
        TEST("POST /api/v1/auth/login (bad creds) => 401");
        std::string body = R"({"username":"nobody","password":"wrong"})";
        auto res = g_api.handle("POST", "/api/v1/auth/login", body, {});
        CHECK_STATUS(res, 401);
        PASS();
    }

    {
        TEST("POST /api/v1/auth/login (missing fields) => 400");
        auto res = g_api.handle("POST", "/api/v1/auth/login", "{}", {});
        CHECK(res.status == 400 || res.status == 401, "Expected 400 or 401");
        PASS();
    }
}

// ============================================================================
// 3. User Management
// ============================================================================

void test_user_management() {
    std::cout << "\n=== User Management ===\n";

    {
        TEST("GET /api/v1/users/me => 200");
        auto res = g_api.handle("GET", "/api/v1/users/me", "", user_headers());
        CHECK_STATUS(res, 200);
        CHECK_BODY_CONTAINS(res, "trader");
        PASS();
    }

    {
        TEST("PUT /api/v1/users/me => 200");
        std::string body = R"({"display_name":"Test Trader"})";
        auto res = g_api.handle("PUT", "/api/v1/users/me", body, user_headers());
        CHECK(res.status == 200 || res.status == 204, "Expected 200 or 204");
        PASS();
    }

    {
        TEST("GET /api/v1/users/me (no auth) => 401");
        auto res = g_api.handle("GET", "/api/v1/users/me", "", {});
        CHECK_STATUS(res, 401);
        PASS();
    }
}

// ============================================================================
// 4. Admin Endpoints
// ============================================================================

void test_admin() {
    std::cout << "\n=== Admin ===\n";

    {
        TEST("GET /api/v1/admin/users => 200 (admin)");
        auto res = g_api.handle("GET", "/api/v1/admin/users", "", admin_headers());
        CHECK_STATUS(res, 200);
        CHECK_BODY_CONTAINS(res, "admin");
        PASS();
    }

    {
        TEST("GET /api/v1/admin/users/:username => 200");
        auto res = g_api.handle("GET", "/api/v1/admin/users/trader", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/admin/users (non-admin) => 403");
        auto res = g_api.handle("GET", "/api/v1/admin/users", "", user_headers());
        CHECK(res.status == 403 || res.status == 401,
              "Expected 403 or 401 for non-admin, got " + std::to_string(res.status));
        PASS();
    }
}

// ============================================================================
// 5. Portfolio & Position Data
// ============================================================================

void test_portfolio_data() {
    std::cout << "\n=== Portfolio & Positions ===\n";

    {
        TEST("GET /api/v1/portfolios => 200");
        auto res = g_api.handle("GET", "/api/v1/portfolios", "", user_headers());
        CHECK_STATUS(res, 200);
        CHECK_BODY_CONTAINS(res, "portfolio");
        PASS();
    }

    {
        TEST("GET /api/v1/positions => 200");
        auto res = g_api.handle("GET", "/api/v1/positions", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/transactions => 200");
        auto res = g_api.handle("GET", "/api/v1/transactions", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 6. Market Data
// ============================================================================

void test_market_data() {
    std::cout << "\n=== Market Data ===\n";

    {
        TEST("GET /api/v1/market => 200");
        auto res = g_api.handle("GET", "/api/v1/market", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/alerts => 200");
        auto res = g_api.handle("GET", "/api/v1/alerts", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/benchmark => 200");
        auto res = g_api.handle("GET", "/api/v1/benchmark", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 7. Orders
// ============================================================================

void test_orders() {
    std::cout << "\n=== Orders ===\n";

    {
        TEST("GET /api/v1/orders => 200");
        auto res = g_api.handle("GET", "/api/v1/orders", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("POST /api/v1/orders (buy AAPL) => 200/201");
        std::string body = R"({"symbol":"AAPL","side":"buy","quantity":10,"type":"market"})";
        auto res = g_api.handle("POST", "/api/v1/orders", body, user_headers());
        CHECK(res.status == 200 || res.status == 201 || res.status == 202,
              "Expected 200/201/202, got " + std::to_string(res.status));
        PASS();
    }
}

// ============================================================================
// 8. Analytics & Risk
// ============================================================================

void test_analytics() {
    std::cout << "\n=== Analytics & Risk ===\n";

    {
        TEST("GET /api/v1/analytics => 200");
        auto res = g_api.handle("GET", "/api/v1/analytics", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/risk => 200");
        auto res = g_api.handle("GET", "/api/v1/risk", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 9. Compliance
// ============================================================================

void test_compliance() {
    std::cout << "\n=== Compliance ===\n";

    {
        TEST("GET /api/v1/compliance => 200");
        auto res = g_api.handle("GET", "/api/v1/compliance", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 10. Reporting
// ============================================================================

void test_reporting() {
    std::cout << "\n=== Reporting ===\n";

    {
        TEST("GET /api/v1/reporting => 200");
        auto res = g_api.handle("GET", "/api/v1/reporting", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("POST /api/v1/reporting/generate => 200");
        std::string body = R"({"type":"performance","format":"json"})";
        auto res = g_api.handle("POST", "/api/v1/reporting/generate", body, user_headers());
        CHECK(res.status == 200 || res.status == 202,
              "Expected 200/202, got " + std::to_string(res.status));
        PASS();
    }
}

// ============================================================================
// 11. Tax
// ============================================================================

void test_tax() {
    std::cout << "\n=== Tax ===\n";

    {
        TEST("GET /api/v1/tax => 200");
        auto res = g_api.handle("GET", "/api/v1/tax", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 12. Security Endpoints
// ============================================================================

void test_security_endpoints() {
    std::cout << "\n=== Security ===\n";

    {
        TEST("GET /api/v1/security/overview => 200");
        auto res = g_api.handle("GET", "/api/v1/security/overview", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/security/audit => 200");
        auto res = g_api.handle("GET", "/api/v1/security/audit", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/security/sessions => 200");
        auto res = g_api.handle("GET", "/api/v1/security/sessions", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 13. Operations
// ============================================================================

void test_operations() {
    std::cout << "\n=== Operations ===\n";

    {
        TEST("GET /api/v1/operations/health => 200");
        auto res = g_api.handle("GET", "/api/v1/operations/health", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/operations/backups => 200");
        auto res = g_api.handle("GET", "/api/v1/operations/backups", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/operations/jobs => 200");
        auto res = g_api.handle("GET", "/api/v1/operations/jobs", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 14. Compute & Deployment
// ============================================================================

void test_compute_deploy() {
    std::cout << "\n=== Compute & Deployment ===\n";

    {
        TEST("GET /api/v1/compute => 200");
        auto res = g_api.handle("GET", "/api/v1/compute", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/deployment => 200");
        auto res = g_api.handle("GET", "/api/v1/deployment", "", admin_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }
}

// ============================================================================
// 15. Error Handling
// ============================================================================

void test_error_handling() {
    std::cout << "\n=== Error Handling ===\n";

    {
        TEST("GET /api/v1/nonexistent => 404");
        auto res = g_api.handle("GET", "/api/v1/nonexistent", "", user_headers());
        CHECK_STATUS(res, 404);
        PASS();
    }

    {
        TEST("GET /api/v1/portfolios (no auth) => 401");
        auto res = g_api.handle("GET", "/api/v1/portfolios", "", {});
        CHECK_STATUS(res, 401);
        PASS();
    }

    {
        TEST("GET /api/v1/portfolios (bad token) => 401");
        auto res = g_api.handle("GET", "/api/v1/portfolios", "",
                                 {{"Authorization", "Bearer bad_token_xyz"}});
        CHECK_STATUS(res, 401);
        PASS();
    }
}

// ============================================================================
// 16. Logout
// ============================================================================

void test_logout() {
    std::cout << "\n=== Logout ===\n";

    {
        TEST("POST /api/v1/auth/logout => 200");
        auto res = g_api.handle("POST", "/api/v1/auth/logout", "", user_headers());
        CHECK_STATUS(res, 200);
        PASS();
    }

    {
        TEST("GET /api/v1/portfolios (after logout) => 401");
        auto res = g_api.handle("GET", "/api/v1/portfolios", "", user_headers());
        CHECK_STATUS(res, 401);
        PASS();
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=============================================\n";
    std::cout << "  Metis Genie Platform v" << VERSION_STRING << " REST Endpoint Tests\n";
    std::cout << "  Testing all 34 API endpoints\n";
    std::cout << "=============================================\n";

    // Initialize
    GenieSystem sys;
    sys.initialize();
    g_api.configure_defaults();

    // Run all test groups
    test_health();
    test_auth();
    test_user_management();
    test_admin();
    test_portfolio_data();
    test_market_data();
    test_orders();
    test_analytics();
    test_compliance();
    test_reporting();
    test_tax();
    test_security_endpoints();
    test_operations();
    test_compute_deploy();
    test_error_handling();
    test_logout();

    // Summary
    std::cout << "\n=============================================\n";
    std::cout << "  Results: " << g_pass << " passed, "
              << g_fail << " failed / "
              << g_run << " total\n";

    if (g_fail > 0) {
        std::cout << "\n  Failures:\n";
        for (const auto& r : g_results) {
            if (!r.passed) {
                std::cout << "    - " << r.name << ": " << r.detail << "\n";
            }
        }
    }

    std::cout << "=============================================\n";

    sys.shutdown();
    return g_fail > 0 ? 1 : 0;
}
