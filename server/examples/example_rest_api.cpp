/**
 * @file example_rest_api.cpp
 * @brief REST API usage examples for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Demonstrates all REST API endpoints without a real HTTP server.
 * The RestApi class processes requests in-memory - no TCP/sockets needed.
 *
 * Build: g++ -std=c++20 -O2 -I include -o example_rest_api examples/example_rest_api.cpp -pthread
 * Run:   ./example_rest_api
 */
#include "../include/genie/genie.hpp"
#include <iostream>
#include <iomanip>

using namespace genie;

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '-') << "\n  " << title << "\n" << std::string(60, '-') << "\n";
}

void print_response(const std::string& label, const net::Response& res) {
    std::cout << "  " << label << " -> " << res.status
              << (res.ok() ? " OK" : " ERROR") << "\n"
              << "  Body: " << res.body.substr(0, 120)
              << (res.body.size() > 120 ? "..." : "") << "\n\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=')
              << "\n  Metis Genie Platform v" << VERSION_STRING << " - REST API Examples"
              << "\n" << std::string(60, '=') << "\n";

    // ---- Create and Configure API ----
    net::RestApi api;
    api.configure_defaults();

    std::cout << "\n  API configured with " << api.route_count() << " routes\n";
    std::cout << "  API version: " << net::RestApi::API_VERSION << "\n";

    // ---- 1. Health Check (no authentication required) ----
    print_header("1. Health Check (GET /api/v1/health)");
    auto health = api.handle("GET", "/api/v1/health");
    print_response("Health", health);

    // ---- 2. Authentication ----
    print_header("2. Authentication (POST /api/v1/auth/login)");

    // Successful login
    auto login_ok = api.handle("POST", "/api/v1/auth/login",
        "{\"username\":\"admin\",\"password\":\"demo\"}");
    print_response("Login (admin/demo)", login_ok);

    // Extract token for authenticated requests
    auto tpos = login_ok.body.find("genie-token-");
    std::string token = login_ok.body.substr(tpos, login_ok.body.find("\"", tpos) - tpos);
    std::cout << "  Token: " << token << "\n\n";

    // Failed login
    auto login_fail = api.handle("POST", "/api/v1/auth/login",
        "{\"username\":\"admin\",\"password\":\"wrong\"}");
    print_response("Login (bad password)", login_fail);

    // ---- 3. Authenticated Endpoints ----
    std::map<std::string, std::string> auth = {{"Authorization", "Bearer " + token}};

    print_header("3. System Status (GET /api/v1/status)");
    auto status = api.handle("GET", "/api/v1/status", "", auth);
    print_response("Status", status);

    print_header("4. Portfolios (GET /api/v1/portfolios)");
    auto portfolios = api.handle("GET", "/api/v1/portfolios", "", auth);
    print_response("Portfolios", portfolios);

    print_header("5. Positions (GET /api/v1/positions)");
    auto positions = api.handle("GET", "/api/v1/positions", "", auth);
    print_response("Positions", positions);

    print_header("6. Risk Metrics (GET /api/v1/risk)");
    auto risk = api.handle("GET", "/api/v1/risk", "", auth);
    print_response("Risk", risk);

    print_header("7. Market Data (GET /api/v1/market)");
    auto market = api.handle("GET", "/api/v1/market", "", auth);
    print_response("Market", market);

    print_header("8. Orders (GET /api/v1/orders)");
    auto orders = api.handle("GET", "/api/v1/orders", "", auth);
    print_response("Orders", orders);

    // ---- 4. Submit Order ----
    print_header("9. Submit Order (POST /api/v1/orders)");
    auto submit = api.handle("POST", "/api/v1/orders",
        "{\"symbol\":\"AAPL\",\"side\":\"Buy\",\"qty\":100,\"type\":\"Market\"}", auth);
    print_response("New Order", submit);

    // ---- 5. Error Handling ----
    print_header("10. Error Handling");

    // 401 - No token
    auto no_auth = api.handle("GET", "/api/v1/portfolios");
    print_response("No token -> 401", no_auth);

    // 404 - Bad endpoint
    auto not_found = api.handle("GET", "/api/v1/nonexistent");
    print_response("Bad endpoint -> 404", not_found);

    // 400 - Bad request
    auto bad_order = api.handle("POST", "/api/v1/orders", "{\"qty\":10}", auth);
    print_response("Missing fields -> 400", bad_order);

    // ---- 6. Logout ----
    print_header("11. Logout (POST /api/v1/auth/logout)");
    auto logout = api.handle("POST", "/api/v1/auth/logout", "", auth);
    print_response("Logout", logout);

    // Verify token is invalid after logout
    auto after_logout = api.handle("GET", "/api/v1/portfolios", "", auth);
    print_response("After logout -> 401", after_logout);

    // ---- Summary ----
    std::cout << std::string(60, '=')
              << "\n  All REST API examples completed successfully!"
              << "\n  Routes: " << api.route_count()
              << "  |  Logged requests: " << api.logger().count()
              << "\n" << std::string(60, '=') << "\n\n";

    return 0;
}
