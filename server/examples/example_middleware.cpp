/**
 * @file example_middleware.cpp
 * @brief REST API middleware, rate limiting, and logging examples
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Demonstrates: Custom middleware, request logging, rate limiting,
 * CORS configuration, pagination, and API versioning.
 *
 * Build: g++ -std=c++20 -O2 -I include -o example_middleware examples/example_middleware.cpp -pthread
 * Run:   ./example_middleware
 */
#include "../include/genie/genie.hpp"
#include <iostream>
#include <iomanip>

using namespace genie;

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '-') << "\n  " << title << "\n" << std::string(60, '-') << "\n";
}

int main() {
    std::cout << "\n" << std::string(60, '=')
              << "\n  Metis Genie Platform v" << VERSION_STRING << " - Middleware & Infrastructure"
              << "\n" << std::string(60, '=') << "\n";

    // ---- 1. Custom Middleware ----
    print_header("1. Custom Middleware Pipeline");
    {
        net::RestApi api;
        int mw_count = 0;

        // Middleware 1: Request counter
        api.use([&mw_count](const net::Request&, net::Response&) -> bool {
            mw_count++;
            return true; // continue to next middleware
        });

        // Middleware 2: Custom header injection
        api.use([](const net::Request&, net::Response& res) -> bool {
            res.headers["X-Powered-By"] = "Metis Genie Platform";
            return true;
        });

        api.get("/api/v1/demo", [](const net::Request&, net::Response& res) {
            res.json("{\"message\":\"Hello from middleware demo\"}");
        });

        auto r1 = api.handle("GET", "/api/v1/demo");
        auto r2 = api.handle("GET", "/api/v1/demo");
        auto r3 = api.handle("GET", "/api/v1/demo");

        std::cout << "  Requests processed: " << mw_count << "\n"
                  << "  Custom header: " << r1.headers["X-Powered-By"] << "\n"
                  << "  Response: " << r1.body << "\n";
    }

    // ---- 2. Blocking Middleware ----
    print_header("2. Blocking Middleware (IP Ban / Maintenance Mode)");
    {
        net::RestApi api;
        bool maintenance_mode = true;

        api.use([&maintenance_mode](const net::Request&, net::Response& res) -> bool {
            if (maintenance_mode) {
                res.error(503, "Server is under maintenance");
                return false; // stop pipeline
            }
            return true;
        });

        api.get("/api/v1/data", [](const net::Request&, net::Response& res) {
            res.json("{\"data\":\"secret\"}");
        });

        auto blocked = api.handle("GET", "/api/v1/data");
        std::cout << "  Maintenance mode ON: " << blocked.status << " " << blocked.body << "\n";

        maintenance_mode = false;
        auto allowed = api.handle("GET", "/api/v1/data");
        std::cout << "  Maintenance mode OFF: " << allowed.status << " " << allowed.body << "\n";
    }

    // ---- 3. Rate Limiting ----
    print_header("3. Rate Limiting");
    {
        net::RateLimiter rl(5, 60); // 5 requests per 60 seconds
        std::cout << "  Config: 5 req/min per key\n\n";

        for (int i = 1; i <= 7; ++i) {
            bool ok = rl.allow("user-alice");
            std::cout << "  Request " << i << ": "
                      << (ok ? "ALLOWED" : "RATE LIMITED")
                      << " (remaining: " << rl.remaining("user-alice") << ")\n";
        }

        std::cout << "\n  Different user (bob): "
                  << (rl.allow("user-bob") ? "ALLOWED" : "LIMITED")
                  << " (remaining: " << rl.remaining("user-bob") << ")\n";
    }

    // ---- 4. Request Logging ----
    print_header("4. Request Logging");
    {
        net::RestApi api;
        api.configure_defaults();

        // Make several requests
        api.handle("GET", "/api/v1/health");
        api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
        api.handle("GET", "/api/v1/health");
        api.handle("POST", "/api/v1/auth/login", "{\"username\":\"bad\",\"password\":\"bad\"}");

        std::cout << "  Total logged requests: " << api.logger().count() << "\n\n";
        auto recent = api.logger().recent(10);
        for (const auto& log : recent) {
            std::cout << "  " << log.method << " " << log.path
                      << " -> " << log.status
                      << " (" << std::fixed << std::setprecision(3) << log.duration_ms << "ms)\n";
        }
    }

    // ---- 5. Pagination ----
    print_header("5. Pagination");
    {
        // Simulate paginated data
        auto show_page = [](int offset, int limit, int total) {
            net::Pagination p;
            p.offset = offset; p.limit = limit; p.total = total;
            std::cout << "  Page: offset=" << p.offset << " limit=" << p.limit
                      << " total=" << p.total << "\n"
                      << "    Meta: " << p.meta_json() << "\n";
        };

        std::cout << "  Dataset: 47 records, page size 10\n\n";
        show_page(0, 10, 47);   // Page 1
        show_page(10, 10, 47);  // Page 2
        show_page(40, 10, 47);  // Last page
    }

    // ---- 6. Pagination from Request ----
    print_header("6. Pagination from HTTP Query Params");
    {
        net::Request req;
        req.method = net::Method::GET;
        req.path = "/api/v1/positions";
        req.query_params["offset"] = "20";
        req.query_params["limit"] = "15";

        auto page = net::Pagination::from_request(req, 100);
        std::cout << "  Query: ?offset=20&limit=15 (100 total)\n"
                  << "  Parsed: offset=" << page.offset << " limit=" << page.limit << "\n"
                  << "  Meta: " << page.meta_json() << "\n";

        // Edge case: offset beyond total
        req.query_params["offset"] = "200";
        auto edge = net::Pagination::from_request(req, 100);
        std::cout << "\n  Edge case: ?offset=200&limit=15 (100 total)\n"
                  << "  Clamped: offset=" << edge.offset << " limit=" << edge.limit << "\n";
    }

    // ---- 7. JSON Builder ----
    print_header("7. JSON Builder Patterns");
    {
        // Simple object
        auto simple = net::JsonBuilder()
            .add("name", std::string("Growth Fund"))
            .add("aum", 1250000.50)
            .add("active", true)
            .add("positions", 7)
            .build();
        std::cout << "  Simple: " << simple << "\n";

        // Nested with raw JSON
        auto nested = net::JsonBuilder()
            .add("status", std::string("ok"))
            .add_raw("metrics", "{\"sharpe\":1.85,\"volatility\":12.5}")
            .add_raw("holdings", "[\"AAPL\",\"MSFT\",\"NVDA\"]")
            .build();
        std::cout << "  Nested: " << nested << "\n";
    }

    // ---- 8. Session Management ----
    print_header("8. Session Store");
    {
        net::SessionStore store;

        auto t1 = store.create("alice", "Administrator");
        auto t2 = store.create("bob", "Analyst");
        auto t3 = store.create("carol", "Trader");

        std::cout << "  Active sessions: " << store.count() << "\n\n";

        auto s1 = store.validate(t1);
        if (s1) std::cout << "  Token 1: " << s1->first << " (" << s1->second << ")\n";

        auto s2 = store.validate(t2);
        if (s2) std::cout << "  Token 2: " << s2->first << " (" << s2->second << ")\n";

        store.destroy(t2);
        std::cout << "\n  After destroying bob's session: " << store.count() << " active\n";

        auto invalid = store.validate(t2);
        std::cout << "  Bob's token valid? " << (invalid.has_value() ? "YES" : "NO") << "\n";
    }

    // ---- 9. Credential Store ----
    print_header("9. Credential Store");
    {
        net::CredentialStore creds;
        creds.add_user("admin", "secret123", "Administrator");
        creds.add_user("viewer", "readonly", "ReadOnly");

        auto r1 = creds.authenticate("admin", "secret123");
        std::cout << "  admin/secret123: " << (r1 ? *r1 : "FAILED") << "\n";

        auto r2 = creds.authenticate("admin", "wrong");
        std::cout << "  admin/wrong: " << (r2 ? *r2 : "FAILED") << "\n";

        auto r3 = creds.authenticate("viewer", "readonly");
        std::cout << "  viewer/readonly: " << (r3 ? *r3 : "FAILED") << "\n";

        std::cout << "  Total users: " << creds.user_count() << "\n";
    }

    // ---- 10. API Version ----
    print_header("10. API Versioning");
    {
        std::cout << "  Current API Version: " << net::RestApi::API_VERSION << "\n"
                  << "  Endpoint pattern: /api/" << net::RestApi::API_VERSION << "/<resource>\n"
                  << "  Example: /api/v1/health, /api/v1/portfolios\n\n"
                  << "  Future: /api/v2/ can be added without breaking v1 clients\n";
    }

    // ---- Summary ----
    std::cout << "\n" << std::string(60, '=')
              << "\n  All middleware & infrastructure examples completed!"
              << "\n  Metis Genie Platform v" << VERSION_STRING
              << "\n" << std::string(60, '=') << "\n\n";

    return 0;
}
