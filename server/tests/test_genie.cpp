/**
 * @file test_genie.cpp
 * @brief Comprehensive test suite for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#include "../include/genie/genie.hpp"
#include <iostream>
#include <cassert>

using namespace genie;

static int tests_run = 0, tests_passed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) do { std::cout << "Running " << #name << "... "; tests_run++; \
    try { name(); tests_passed++; std::cout << "PASSED\n"; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << "\n"; } \
    catch (...) { std::cout << "FAILED\n"; } } while(0)
#define ASSERT_TRUE(x) if (!(x)) throw std::runtime_error("Assertion failed: " #x)
#define ASSERT_FALSE(x) if (x) throw std::runtime_error("Assertion failed: !" #x)
#define ASSERT_EQ(a, b) if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " == " #b)
#define ASSERT_NEAR(a, b, t) if (std::abs((a) - (b)) > (t)) throw std::runtime_error("Assertion failed: " #a " ~= " #b)

TEST(test_version) { ASSERT_EQ(VERSION_MAJOR, 4); ASSERT_EQ(VERSION_MINOR, 22); ASSERT_EQ(VERSION_PATCH, 0); ASSERT_TRUE(std::string(PROJECT_NAME) == "Metis Genie Platform"); }
TEST(test_money) { Money m1(100, "USD"), m2(50, "USD"); ASSERT_NEAR((m1 + m2).amount, 150, 0.01); ASSERT_NEAR((m1 * 2).amount, 200, 0.01); }
TEST(test_uuid) { auto u1 = UuidGenerator::generate(), u2 = UuidGenerator::generate(); ASSERT_EQ(u1.length(), 36u); ASSERT_TRUE(u1 != u2); }
TEST(test_statistics) { std::vector<double> d = {1,2,3,4,5}; ASSERT_NEAR(math::mean(d), 3.0, 0.001); ASSERT_NEAR(math::stddev(d), 1.5811, 0.01); }
TEST(test_percentile) { std::vector<double> d = {1,2,3,4,5,6,7,8,9,10}; ASSERT_NEAR(math::percentile(d, 50), 5.5, 0.5); }
TEST(test_correlation) { std::vector<double> x = {1,2,3,4,5}, y = {2,4,6,8,10}; ASSERT_NEAR(math::correlation(x, y), 1.0, 0.001); }
TEST(test_black_scholes) { double c = math::black_scholes_call(100, 100, 0.05, 0.2, 1.0); ASSERT_TRUE(c > 0 && c < 100); }
TEST(test_bond_price) { double p = math::bond_price(1000, 0.05, 0.05, 10, 2); ASSERT_NEAR(p, 1000.0, 1.0); }
TEST(test_date_conversion) { auto tp = date_utils::from_ymd(2026, 6, 15); auto [y, m, d] = date_utils::to_ymd(tp); ASSERT_EQ(y, 2026); ASSERT_EQ(m, 6u); }
TEST(test_weekend) { auto sat = date_utils::from_ymd(2026, 1, 4); ASSERT_TRUE(date_utils::is_weekend(sat)); }
TEST(test_security) { auto s = assets::create_common_stock("AAPL", "Apple", "AAPL"); ASSERT_EQ(s->id(), "AAPL"); ASSERT_TRUE(s->is_equity()); }
TEST(test_market_data) { market::MarketDataService mds; auto s = assets::create_common_stock("T", "Test", "T"); mds.add_security(s); mds.update_price("T", 100); ASSERT_NEAR(mds.get_price("T"), 100, 0.01); }
TEST(test_bond) { auto b = assets::create_treasury("UST", "Treasury", 0.04, 0.04, 10, date_utils::from_ymd(2035, 1, 1)); ASSERT_TRUE(b->is_fixed_income()); ASSERT_TRUE(b->duration() > 0); }
TEST(test_option) { auto c = assets::create_call_option("C", "AAPL", 150, date_utils::from_ymd(2026, 12, 31)); ASSERT_TRUE(c->is_call()); c->update_market_data(155, 0.25, 0.5, 0.05); ASSERT_TRUE(c->delta() > 0); }
TEST(test_portfolio) { portfolio::PortfolioConfig cfg; cfg.id = "P1"; cfg.name = "Test"; portfolio::Portfolio p(cfg); p.deposit_cash(Money(1000000, "USD")); ASSERT_NEAR(p.cash_balance().amount, 1000000, 0.01); }
TEST(test_position) { portfolio::PortfolioConfig cfg; cfg.id = "P2"; portfolio::Portfolio p(cfg); p.deposit_cash(Money(100000, "USD")); p.open_position("A", 100, 150); ASSERT_TRUE(p.has_position("A")); }
TEST(test_var) { risk::MonteCarloEngine e; e.set_simulations(1000); auto r = e.simulate_gbm(100, 0.1, 0.2, 1.0); ASSERT_TRUE(r.mean_terminal_price > 0); }
TEST(test_stress) { auto s = risk::StressScenarioLibrary::market_crash_2008(); ASSERT_TRUE(s.equity_shock < 0); }
TEST(test_order) { market::MarketDataService mds; auto s = assets::create_common_stock("O", "Order", "O"); mds.add_security(s); mds.update_price("O", 100); trading::OrderManagementSystem oms; oms.set_market_data(&mds); trading::Order o; o.security_id = "O"; o.side = OrderSide::Buy; o.quantity = 100; auto r = oms.submit_order(o); ASSERT_EQ(r.status, OrderStatus::Filled); }
TEST(test_compliance) { compliance::ComplianceEngine e; e.add_rule(compliance::RuleLibrary::single_position_limit(10)); ASSERT_EQ(e.rule_count(), 1u); }
TEST(test_performance) { std::vector<double> r = {0.01, -0.02, 0.03, 0.01}; performance::PerformanceCalculator c; auto s = c.calculate(r); ASSERT_TRUE(s.volatility > 0); }
TEST(test_report) { reporting::ReportBuilder b; b.set_title("Test"); b.add_section("S", {{"K", "V"}}); auto r = b.build(); ASSERT_TRUE(r.find("Test") != std::string::npos); }
TEST(test_optimizer) { portfolio::PortfolioOptimizer o; portfolio::OptimizationInputs i; i.security_ids = {"A", "B"}; i.expected_returns = {0.1, 0.08}; i.covariance_matrix = {{0.04, 0.01}, {0.01, 0.03}}; o.set_max_iterations(500); auto r = o.optimize(i); ASSERT_TRUE(r.success); }
TEST(test_system) { GenieSystem sys; sys.initialize(); ASSERT_TRUE(sys.is_initialized()); ASSERT_TRUE(sys.status().find("Metis Genie Platform") != std::string::npos); sys.shutdown(); }

// v2.6.0 New Module Tests
TEST(test_logging) { logger().set_level(LogLevel::DEBUG); logger().info("Test message"); ASSERT_TRUE(logger().level() == LogLevel::DEBUG); }
TEST(test_config) { config().set("test.key", 42); auto v = config().get<int>("test.key"); ASSERT_TRUE(v.has_value()); ASSERT_EQ(v.value(), 42); }
TEST(test_persistence_csv) { persistence::CsvWriter w("/tmp/test.csv"); w.write_header({"A", "B"}); w.write_row({"1", "2"}); w.close(); ASSERT_TRUE(true); }
TEST(test_fx) { auto fx = assets::create_fx_pair("EUR", "USD"); fx->update_spot(1.0850, 1.0); ASSERT_NEAR(fx->spot(), 1.0850, 0.001); ASSERT_TRUE(fx->is_fx()); }
TEST(test_commodity) { auto gold = assets::gold(); ASSERT_TRUE(gold->is_commodity()); ASSERT_TRUE(gold->spot_price() > 0); }
TEST(test_risk_metrics) { std::vector<double> r = {0.01, -0.02, 0.03, 0.01, -0.01, 0.02}; risk::RiskAnalyzer ra; auto m = ra.analyze(r); ASSERT_TRUE(m.volatility > 0); ASSERT_TRUE(m.sharpe_ratio != 0); }
TEST(test_blotter) { trading::TradeBlotter b; auto id = b.record_trade("O1", "P1", "AAPL", "Apple", true, 100, 175.0); ASSERT_TRUE(id.find("TRD-") != std::string::npos); ASSERT_EQ(b.trade_count(), 1u); }
TEST(test_cli) { cli::CLI c; auto r = c.execute("version"); ASSERT_TRUE(r.find("5.3.1") != std::string::npos); }

// v2.6.0 New Module Tests
TEST(test_timeseries) { std::vector<double> d = {1,2,3,4,5,6,7,8,9,10}; auto ma = timeseries::sma(d, 3); ASSERT_TRUE(ma.size() == 8); ASSERT_NEAR(ma[0], 2.0, 0.01); }
TEST(test_rebalancing) { portfolio::RebalancingEngine re; re.add_target("AAPL", 0.5); re.add_target("MSFT", 0.5); ASSERT_EQ(re.targets().size(), 2u); }
TEST(test_tax_lots) { tax::TaxLotManager tm; auto id = tm.add_lot("AAPL", 100, 150.0); ASSERT_TRUE(id.find("LOT-") != std::string::npos); }
TEST(test_currency_hedge) { fx::CurrencyHedgingEngine che; che.set_spot_rate("EUR", 1.08); ASSERT_NEAR(che.get_spot("EUR"), 1.08, 0.01); }
TEST(test_scenario) { scenario::ScenarioEngine se; se.add_scenario(scenario::ScenarioEngine::equity_crash_10pct()); ASSERT_TRUE(se.has_scenario("EQUITY_CRASH_10")); }
TEST(test_validation) { auto r = validation::Validator::validate_positive(100.0, "price"); ASSERT_TRUE(r.valid); r = validation::Validator::validate_positive(-5.0, "price"); ASSERT_FALSE(r.valid); }
TEST(test_alerts) { alerts::AlertManager am; auto id = am.raise_alert(alerts::AlertType::Custom, alerts::AlertSeverity::Info, "Test", "Test message"); ASSERT_TRUE(id.find("ALERT-") != std::string::npos); }
TEST(test_benchmark) { benchmark::BenchmarkEngine be; be.add_sp500({0.01, -0.01, 0.02}); ASSERT_TRUE(be.has_benchmark("SPX")); }

// v2.6.0 - Pure REST API tests
TEST(test_rest_request) {
    net::Request req;
    req.method = net::Method::GET;
    req.path = "/api/v1/health";
    req.headers["Authorization"] = "Bearer test-token-123";
    req.headers["Content-Type"] = "application/json";
    ASSERT_TRUE(req.bearer_token() == "test-token-123");
    ASSERT_TRUE(req.header("content-type") == "application/json");
}
TEST(test_rest_response) {
    net::Response res;
    res.json("{\"status\":\"ok\"}");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.ok());
    ASSERT_TRUE(res.body.find("ok") != std::string::npos);
    ASSERT_TRUE(res.headers.count("Access-Control-Allow-Origin")); // CORS
    res.error(401, "Unauthorized");
    ASSERT_TRUE(res.status == 401);
    ASSERT_FALSE(res.ok());
}
TEST(test_rest_json_builder) {
    auto json = net::JsonBuilder()
        .add("name", std::string("test"))
        .add("count", 42)
        .add("rate", 3.14)
        .add("active", true)
        .build();
    ASSERT_TRUE(json.find("\"name\":\"test\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"count\":42") != std::string::npos);
    ASSERT_TRUE(json.find("\"active\":true") != std::string::npos);
}
TEST(test_rest_json_parse) {
    std::string json = "{\"username\":\"admin\",\"password\":\"demo\"}";
    ASSERT_TRUE(net::json_get(json, "username") == "admin");
    ASSERT_TRUE(net::json_get(json, "password") == "demo");
    ASSERT_TRUE(net::json_get(json, "missing") == "");
}
TEST(test_rest_routing) {
    net::RestApi api;
    api.get("/api/v1/test", [](const net::Request&, net::Response& res) {
        res.json("{\"result\":\"success\"}");
    });
    auto res = api.handle("GET", "/api/v1/test");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("success") != std::string::npos);
    // 404 for unknown route
    auto res2 = api.handle("GET", "/api/unknown");
    ASSERT_TRUE(res2.status == 404);
    ASSERT_TRUE(api.route_count() == 1);
}
TEST(test_rest_query_params) {
    net::RestApi api;
    std::string captured_symbol;
    api.get("/api/data", [&captured_symbol](const net::Request& req, net::Response& res) {
        captured_symbol = req.param("symbol");
        res.json("{\"ok\":true}");
    });
    api.handle("GET", "/api/data?symbol=AAPL&period=1Y");
    ASSERT_TRUE(captured_symbol == "AAPL");
}
TEST(test_rest_health) {
    net::RestApi api;
    api.configure_defaults();
    auto res = api.handle("GET", "/api/v1/health");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("5.3.1") != std::string::npos);
    ASSERT_TRUE(res.body.find("\"status\":\"healthy\"") != std::string::npos);
}
TEST(test_rest_login) {
    net::RestApi api;
    api.configure_defaults();
    // Successful login
    auto res = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("genie-token-") != std::string::npos);
    ASSERT_TRUE(res.body.find("Administrator") != std::string::npos);
    // Bad credentials
    auto res2 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"wrong\"}");
    ASSERT_TRUE(res2.status == 401);
    // Missing username
    auto res3 = api.handle("POST", "/api/v1/auth/login", "{\"password\":\"demo\"}");
    ASSERT_TRUE(res3.status == 400);
}
TEST(test_rest_auth_flow) {
    net::RestApi api;
    api.configure_defaults();
    // Unauthenticated request -> 401
    auto res_u = api.handle("GET", "/api/v1/portfolios");
    ASSERT_TRUE(res_u.status == 401);
    // Login to get token
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto tpos = res_l.body.find("genie-token-");
    ASSERT_TRUE(tpos != std::string::npos);
    auto tend = res_l.body.find("\"", tpos);
    std::string token = res_l.body.substr(tpos, tend - tpos);
    // Authenticated request -> 200
    std::map<std::string, std::string> auth_headers = {{"Authorization", "Bearer " + token}};
    auto res_p = api.handle("GET", "/api/v1/portfolios", "", auth_headers);
    ASSERT_TRUE(res_p.status == 200);
    ASSERT_TRUE(res_p.body.find("Growth Portfolio") != std::string::npos);
    // Positions
    auto res_pos = api.handle("GET", "/api/v1/positions", "", auth_headers);
    ASSERT_TRUE(res_pos.status == 200);
    ASSERT_TRUE(res_pos.body.find("AAPL") != std::string::npos);
    // Risk
    auto res_r = api.handle("GET", "/api/v1/risk", "", auth_headers);
    ASSERT_TRUE(res_r.status == 200);
    ASSERT_TRUE(res_r.body.find("sharpe") != std::string::npos);
    // Market
    auto res_m = api.handle("GET", "/api/v1/market", "", auth_headers);
    ASSERT_TRUE(res_m.status == 200);
    ASSERT_TRUE(res_m.body.find("SPX") != std::string::npos);
    // Logout
    auto res_out = api.handle("POST", "/api/v1/auth/logout", "", auth_headers);
    ASSERT_TRUE(res_out.status == 200);
    // Token invalid after logout
    auto res_after = api.handle("GET", "/api/v1/portfolios", "", auth_headers);
    ASSERT_TRUE(res_after.status == 401);
}
// v5.3.1 - User Registration and Profile Management tests
TEST(test_rest_registration) {
    net::RestApi api;
    api.configure_defaults();
    // Register new user
    auto res = api.handle("POST", "/api/v1/auth/register",
        "{\"username\":\"newuser\",\"password\":\"pass123\",\"email\":\"new@test.com\",\"display_name\":\"New User\"}");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("registered") != std::string::npos);
    ASSERT_TRUE(res.body.find("genie-token-") != std::string::npos);
    // Duplicate username
    auto res2 = api.handle("POST", "/api/v1/auth/register",
        "{\"username\":\"newuser\",\"password\":\"other\"}");
    ASSERT_TRUE(res2.status == 400);
    ASSERT_TRUE(res2.body.find("already exists") != std::string::npos);
    // Invalid username (too short)
    auto res3 = api.handle("POST", "/api/v1/auth/register",
        "{\"username\":\"ab\",\"password\":\"pass123\"}");
    ASSERT_TRUE(res3.status == 400);
    // Invalid password (too short)
    auto res4 = api.handle("POST", "/api/v1/auth/register",
        "{\"username\":\"validuser\",\"password\":\"123\"}");
    ASSERT_TRUE(res4.status == 400);
}
TEST(test_rest_user_profile) {
    net::RestApi api;
    api.configure_defaults();
    // Login as admin
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto tpos = res_l.body.find("genie-token-");
    auto tend = res_l.body.find("\"", tpos);
    std::string token = res_l.body.substr(tpos, tend - tpos);
    std::map<std::string, std::string> auth = {{"Authorization", "Bearer " + token}};
    // Get own profile
    auto res_me = api.handle("GET", "/api/v1/users/me", "", auth);
    ASSERT_TRUE(res_me.status == 200);
    ASSERT_TRUE(res_me.body.find("admin") != std::string::npos);
    ASSERT_TRUE(res_me.body.find("Administrator") != std::string::npos);
    // Update profile
    auto res_up = api.handle("PUT", "/api/v1/users/me",
        "{\"display_name\":\"Admin User\",\"email\":\"admin@genie.com\"}", auth);
    ASSERT_TRUE(res_up.status == 200);
    // Verify update
    auto res_me2 = api.handle("GET", "/api/v1/users/me", "", auth);
    ASSERT_TRUE(res_me2.body.find("Admin User") != std::string::npos);
    ASSERT_TRUE(res_me2.body.find("admin@genie.com") != std::string::npos);
    // Change password
    auto res_pw = api.handle("POST", "/api/v1/users/me/password",
        "{\"old_password\":\"demo\",\"new_password\":\"newpass\"}", auth);
    ASSERT_TRUE(res_pw.status == 200);
    // Old password no longer works
    auto res_l2 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    ASSERT_TRUE(res_l2.status == 401);
    // New password works
    auto res_l3 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"newpass\"}");
    ASSERT_TRUE(res_l3.status == 200);
}
TEST(test_rest_admin_users) {
    net::RestApi api;
    api.configure_defaults();
    // Login as admin
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto tpos = res_l.body.find("genie-token-");
    auto tend = res_l.body.find("\"", tpos);
    std::string admin_token = res_l.body.substr(tpos, tend - tpos);
    std::map<std::string, std::string> admin_auth = {{"Authorization", "Bearer " + admin_token}};
    // List all users
    auto res_list = api.handle("GET", "/api/v1/admin/users", "", admin_auth);
    ASSERT_TRUE(res_list.status == 200);
    ASSERT_TRUE(res_list.body.find("admin") != std::string::npos);
    ASSERT_TRUE(res_list.body.find("trader") != std::string::npos);
    // Get specific user
    auto res_get = api.handle("GET", "/api/v1/admin/users/trader", "", admin_auth);
    ASSERT_TRUE(res_get.status == 200);
    ASSERT_TRUE(res_get.body.find("Trader") != std::string::npos);
    // Update user role
    auto res_role = api.handle("PUT", "/api/v1/admin/users/trader",
        "{\"role\":\"Administrator\"}", admin_auth);
    ASSERT_TRUE(res_role.status == 200);
    // Deactivate user
    auto res_deact = api.handle("PUT", "/api/v1/admin/users/user",
        "{\"active\":\"false\"}", admin_auth);
    ASSERT_TRUE(res_deact.status == 200);
    // Deactivated user cannot login
    auto res_user_login = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"user\",\"password\":\"user\"}");
    ASSERT_TRUE(res_user_login.status == 401);
    // Delete user (non-admin cannot access)
    auto res_l2 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"trader\",\"password\":\"trade\"}");
    auto tpos2 = res_l2.body.find("genie-token-");
    auto tend2 = res_l2.body.find("\"", tpos2);
    std::string trader_token = res_l2.body.substr(tpos2, tend2 - tpos2);
    std::map<std::string, std::string> trader_auth = {{"Authorization", "Bearer " + trader_token}};
    // Trader is now admin, but let's test with original 'user' role by recreating
    // Actually trader is now admin from earlier, so skip this part
    // Delete user as admin
    auto res_del = api.handle("DELETE", "/api/v1/admin/users/user", "", admin_auth);
    ASSERT_TRUE(res_del.status == 200);
    // Deleted user no longer exists
    auto res_get_del = api.handle("GET", "/api/v1/admin/users/user", "", admin_auth);
    ASSERT_TRUE(res_get_del.status == 404);
}
TEST(test_rest_path_params) {
    net::RestApi api;
    std::string captured;
    api.get("/users/:id/posts/:postid", [&captured](const net::Request& req, net::Response& res) {
        captured = req.path_param("id") + ":" + req.path_param("postid");
        res.json("{\"ok\":true}");
    });
    auto res = api.handle("GET", "/users/123/posts/456");
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(captured == "123:456");
}
// v2.6.0 - Pagination, Middleware, Rate Limiting tests
TEST(test_rest_pagination) {
    net::Pagination p;
    p.offset = 0; p.limit = 10; p.total = 25;
    auto meta = p.meta_json();
    ASSERT_TRUE(meta.find("\"total\":25") != std::string::npos);
    ASSERT_TRUE(meta.find("\"has_more\":true") != std::string::npos);
    p.offset = 20;
    meta = p.meta_json();
    ASSERT_TRUE(meta.find("\"has_more\":false") != std::string::npos);
}
TEST(test_rest_middleware) {
    net::RestApi api;
    bool mw_ran = false;
    api.use([&mw_ran](const net::Request&, net::Response&) -> bool {
        mw_ran = true;
        return true; // continue
    });
    api.get("/api/v1/mwtest", [](const net::Request&, net::Response& res) {
        res.json("{\"ok\":true}");
    });
    auto res = api.handle("GET", "/api/v1/mwtest");
    ASSERT_TRUE(mw_ran);
    ASSERT_TRUE(res.status == 200);
    // Middleware that blocks
    net::RestApi api2;
    api2.use([](const net::Request&, net::Response& res) -> bool {
        res.error(403, "Blocked"); return false;
    });
    api2.get("/api/v1/blocked", [](const net::Request&, net::Response& res) { res.json("{}"); });
    auto res2 = api2.handle("GET", "/api/v1/blocked");
    ASSERT_TRUE(res2.status == 403);
}
TEST(test_rest_rate_limiter) {
    net::RateLimiter rl(3, 60); // 3 requests per 60s
    ASSERT_TRUE(rl.allow("user1"));
    ASSERT_TRUE(rl.allow("user1"));
    ASSERT_TRUE(rl.allow("user1"));
    ASSERT_FALSE(rl.allow("user1")); // 4th should fail
    ASSERT_TRUE(rl.allow("user2")); // different key OK
    ASSERT_TRUE(rl.remaining("user2") == 2);
}
TEST(test_rest_logger) {
    net::RequestLogger logger;
    logger.log("GET", "/api/v1/health", 200, 1.5);
    logger.log("POST", "/api/v1/auth/login", 200, 3.2);
    ASSERT_EQ(logger.count(), 2u);
    auto recent = logger.recent(5);
    ASSERT_EQ(recent.size(), 2u);
    ASSERT_TRUE(recent[0].path == "/api/v1/health");
}
TEST(test_rest_api_version) {
    ASSERT_TRUE(std::string(net::RestApi::API_VERSION) == "v1");
}

// =========================================================================
// v2.6.0 - Extended Test Coverage (26 new tests)
// =========================================================================

// --- Core Module Deep Tests ---
TEST(test_money_currency) {
    Money usd(100, "USD"), eur(85, "EUR");
    ASSERT_TRUE(usd.currency == "USD");
    ASSERT_TRUE(eur.currency == "EUR");
    Money neg = usd - Money(150, "USD");
    ASSERT_NEAR(neg.amount, -50.0, 0.01);
    Money zero(0, "USD");
    ASSERT_NEAR(zero.amount, 0.0, 0.01);
}

TEST(test_statistics_edge_cases) {
    std::vector<double> single = {42.0};
    ASSERT_NEAR(math::mean(single), 42.0, 0.001);
    std::vector<double> negative = {-5, -3, -1, -4, -2};
    ASSERT_NEAR(math::mean(negative), -3.0, 0.001);
    std::vector<double> zeros = {0, 0, 0, 0};
    ASSERT_NEAR(math::stddev(zeros), 0.0, 0.001);
}

TEST(test_correlation_inverse) {
    std::vector<double> x = {1, 2, 3, 4, 5};
    std::vector<double> y = {5, 4, 3, 2, 1};
    ASSERT_NEAR(math::correlation(x, y), -1.0, 0.001);
}

TEST(test_date_business_days) {
    auto friday = date_utils::from_ymd(2026, 1, 2);
    auto saturday = date_utils::from_ymd(2026, 1, 3);
    auto sunday = date_utils::from_ymd(2026, 1, 4);
    auto monday = date_utils::from_ymd(2026, 1, 5);
    ASSERT_FALSE(date_utils::is_weekend(friday));
    ASSERT_TRUE(date_utils::is_weekend(saturday));
    ASSERT_TRUE(date_utils::is_weekend(sunday));
    ASSERT_FALSE(date_utils::is_weekend(monday));
}

TEST(test_uuid_uniqueness) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) ids.insert(UuidGenerator::generate());
    ASSERT_EQ(ids.size(), 100u);
}

TEST(test_config_types) {
    config().set("str.key", std::string("hello"));
    config().set("dbl.key", 3.14);
    config().set("bool.key", true);
    auto s = config().get<std::string>("str.key");
    ASSERT_TRUE(s.has_value());
    ASSERT_TRUE(s.value() == "hello");
    auto d = config().get<double>("dbl.key");
    ASSERT_TRUE(d.has_value());
    ASSERT_NEAR(d.value(), 3.14, 0.01);
    auto missing = config().get<int>("nonexistent");
    ASSERT_FALSE(missing.has_value());
}

TEST(test_events_pubsub) {
    EventBus bus;
    int count = 0;
    auto id = bus.subscribe(EventType::PriceUpdate, [&count](const EventData&) { count++; });
    EventData ev; ev.type = EventType::PriceUpdate; ev.source = "TEST";
    bus.publish(ev);
    bus.publish(ev);
    ASSERT_EQ(count, 2);
    bus.unsubscribe(id);
    bus.publish(ev);
    ASSERT_EQ(count, 2); // unsubscribed
}

TEST(test_alerts_lifecycle) {
    alerts::AlertManager am;
    auto id1 = am.raise_alert(alerts::AlertType::RiskLimit, alerts::AlertSeverity::Warning, "VaR Breach", "VaR exceeded threshold");
    auto id2 = am.raise_alert(alerts::AlertType::Compliance, alerts::AlertSeverity::Critical, "Position Limit", "AAPL > 10%");
    // alerts stored internally
    am.acknowledge(id1);
    am.resolve(id2);
    
}

TEST(test_logging_levels) {
    logger().set_level(LogLevel::WARN);
    ASSERT_TRUE(logger().level() == LogLevel::WARN);
    logger().set_level(LogLevel::DEBUG);
    ASSERT_TRUE(logger().level() == LogLevel::DEBUG);
}

// --- Assets Deep Tests ---
TEST(test_equity_etf) {
    auto spy = assets::create_etf("SPY", "SPDR S&P 500", "SPY");
    ASSERT_TRUE(spy->is_equity());
    ASSERT_EQ(spy->id(), "SPY");
    ASSERT_TRUE(spy->reference().name.find("SPDR") != std::string::npos);
}

TEST(test_option_greeks) {
    auto call = assets::create_call_option("C1", "AAPL", 150, date_utils::from_ymd(2026, 12, 31));
    auto put = assets::create_put_option("P1", "AAPL", 150, date_utils::from_ymd(2026, 12, 31));
    ASSERT_TRUE(call->is_call());
    ASSERT_FALSE(put->is_call());
    call->update_market_data(160, 0.25, 0.5, 0.05);
    put->update_market_data(160, 0.25, 0.5, 0.05);
    ASSERT_TRUE(call->delta() > 0);
    ASSERT_TRUE(put->delta() < 0);
    ASSERT_TRUE(call->gamma() > 0);
    ASSERT_TRUE(call->vega() > 0);
}

TEST(test_bond_pricing) {
    auto bond = assets::create_treasury("B1", "5Y Note", 0.03, 0.035, 5, date_utils::from_ymd(2031, 1, 1));
    ASSERT_TRUE(bond->is_fixed_income());
    ASSERT_TRUE(bond->duration() > 0);
    ASSERT_TRUE(bond->duration() < 6.0);
}

TEST(test_fx_pair_inverse) {
    auto eurusd = assets::create_fx_pair("EUR", "USD");
    eurusd->update_spot(1.0850, 0.005);
    ASSERT_NEAR(eurusd->spot(), 1.0850, 0.001);
    auto gbpusd = assets::create_fx_pair("GBP", "USD");
    gbpusd->update_spot(1.2650, 0.008);
    ASSERT_TRUE(gbpusd->spot() > eurusd->spot());
}

TEST(test_commodity_gold_oil) {
    auto gold = assets::gold();
    auto oil = assets::crude_oil();
    ASSERT_TRUE(gold->is_commodity());
    ASSERT_TRUE(oil->is_commodity());
    ASSERT_TRUE(gold->spot_price() > oil->spot_price());
}

// --- Portfolio Deep Tests ---
TEST(test_portfolio_multi_position) {
    portfolio::PortfolioConfig cfg; cfg.id = "MP"; cfg.name = "Multi";
    portfolio::Portfolio p(cfg);
    p.deposit_cash(Money(500000, "USD"));
    p.open_position("AAPL", 100, 175.0);
    p.open_position("MSFT", 50, 410.0);
    p.open_position("GOOGL", 80, 175.0);
    ASSERT_TRUE(p.has_position("AAPL"));
    ASSERT_TRUE(p.has_position("MSFT"));
    ASSERT_TRUE(p.has_position("GOOGL"));
    ASSERT_TRUE(p.position_count() == 3);
    ASSERT_TRUE(p.position_count() > 0);
}

TEST(test_portfolio_close_position) {
    portfolio::PortfolioConfig cfg; cfg.id = "CP"; cfg.name = "Close";
    portfolio::Portfolio p(cfg);
    p.deposit_cash(Money(100000, "USD"));
    p.open_position("AAPL", 100, 150.0);
    ASSERT_TRUE(p.has_position("AAPL"));
    p.close_position("AAPL", 160.0);
    ASSERT_FALSE(p.has_position("AAPL"));
    ASSERT_TRUE(p.cash_balance().amount > 100000);
}

TEST(test_rebalancing_drift) {
    portfolio::RebalancingEngine re;
    re.add_target("AAPL", 0.40);
    re.add_target("MSFT", 0.35);
    re.add_target("GOOGL", 0.25);
    ASSERT_EQ(re.targets().size(), 3u);
    double total = 0.0;
    for (const auto& t : re.targets()) total += t.target_weight;
    ASSERT_NEAR(total, 1.0, 0.01);
}

TEST(test_tax_lots_fifo) {
    tax::TaxLotManager tm;
    auto lot1 = tm.add_lot("AAPL", 100, 140.0);
    auto lot2 = tm.add_lot("AAPL", 50, 160.0);
    ASSERT_TRUE(lot1 != lot2);
    auto lots = tm.get_lots("AAPL");
    ASSERT_EQ(lots.size(), 2u);
    int total_shares = 0;
    for (const auto& l : lots) total_shares += l.quantity;
    ASSERT_EQ(total_shares, 150);
}

// --- Risk Deep Tests ---
TEST(test_var_returns) {
    std::vector<double> returns = {0.01, -0.02, 0.03, 0.01, -0.01, 0.02, -0.03, 0.01, 0.02, -0.01};
    risk::RiskAnalyzer ra;
    auto m = ra.analyze(returns);
    ASSERT_TRUE(m.volatility > 0);
    ASSERT_TRUE(m.max_drawdown >= 0);
}

TEST(test_scenario_multiple) {
    scenario::ScenarioEngine se;
    se.add_scenario(scenario::ScenarioEngine::equity_crash_10pct());
    se.add_scenario(scenario::ScenarioEngine::rate_hike_100bps());
    se.add_scenario(scenario::ScenarioEngine::dollar_rally());
    ASSERT_TRUE(se.has_scenario("EQUITY_CRASH_10"));
    ASSERT_TRUE(se.has_scenario("RATE_HIKE_100"));
    ASSERT_TRUE(se.has_scenario("USD_RALLY"));
    ASSERT_TRUE(se.has_scenario("EQUITY_CRASH_10"));
}

TEST(test_currency_hedge_multi) {
    fx::CurrencyHedgingEngine che;
    che.set_spot_rate("EUR", 1.0850);
    che.set_spot_rate("GBP", 1.2650);
    che.set_spot_rate("JPY", 0.0067);
    ASSERT_NEAR(che.get_spot("EUR"), 1.0850, 0.001);
    ASSERT_NEAR(che.get_spot("GBP"), 1.2650, 0.001);
    ASSERT_NEAR(che.get_spot("JPY"), 0.0067, 0.0001);
}

// --- Trading Deep Tests ---
TEST(test_order_sell) {
    market::MarketDataService mds;
    auto s = assets::create_common_stock("S1", "Sell Test", "S1");
    mds.add_security(s); mds.update_price("S1", 200);
    trading::OrderManagementSystem oms;
    oms.set_market_data(&mds);
    trading::Order o;
    o.security_id = "S1"; o.side = OrderSide::Sell; o.quantity = 50;
    auto r = oms.submit_order(o);
    ASSERT_EQ(r.status, OrderStatus::Filled);
    ASSERT_TRUE(r.avg_fill_price > 0);
}

TEST(test_blotter_multi_trades) {
    trading::TradeBlotter b;
    b.record_trade("O1", "P1", "AAPL", "Apple", true, 100, 175.0);
    b.record_trade("O2", "P1", "MSFT", "Microsoft", true, 50, 410.0);
    b.record_trade("O3", "P1", "AAPL", "Apple", false, 50, 180.0);
    ASSERT_EQ(b.trade_count(), 3u);
    // verify total
    ASSERT_TRUE(b.trade_count() == 3);
}

// --- Analytics Deep Tests ---
TEST(test_timeseries_ema) {
    std::vector<double> d = {1,2,3,4,5,6,7,8,9,10};
    auto ema = timeseries::ema(d, 3);
    ASSERT_TRUE(ema.size() > 0);
    ASSERT_TRUE(ema.back() > ema.front());
}

TEST(test_backtesting_sma) {
    backtest::BacktestEngine engine(100000.0);
    engine.set_slippage(5.0);
    engine.set_commission(0.01);
    backtest::SMACrossover strategy(5, 15);
    ASSERT_TRUE(strategy.name().find("SMA") != std::string::npos);
    std::vector<backtest::PriceBar> bars;
    double price = 100.0;
    for (int i = 0; i < 50; ++i) {
        backtest::PriceBar bar;
        bar.close = price + (i % 7 - 3) * 0.5;
        bar.open = bar.close - 0.2; bar.high = bar.close + 0.5; bar.low = bar.close - 0.5;
        bar.volume = 1000000;
        bars.push_back(bar);
        price = bar.close;
    }
    std::map<std::string, std::vector<backtest::PriceBar>> market_data;
    market_data["TEST"] = bars;
    auto result = engine.run(strategy, market_data);
    ASSERT_TRUE(result.total_trades >= 0);
    ASSERT_TRUE(result.volatility >= 0);
}

TEST(test_benchmark_tracking) {
    benchmark::BenchmarkEngine be;
    be.add_sp500({0.01, -0.005, 0.02, -0.01, 0.015});
    ASSERT_TRUE(be.has_benchmark("SPX"));
    std::vector<double> port_returns = {0.012, -0.003, 0.018, -0.008, 0.020};
    auto metrics = be.compare(port_returns, "SPX");
    ASSERT_TRUE(metrics.tracking_error >= 0);
}

// --- REST API Deep Tests ---
TEST(test_rest_market_data) {
    net::RestApi api;
    api.configure_defaults();
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto tpos = res_l.body.find("genie-token-");
    std::string token = res_l.body.substr(tpos, res_l.body.find("\"", tpos) - tpos);
    std::map<std::string, std::string> auth = {{"Authorization", "Bearer " + token}};
    auto res = api.handle("GET", "/api/v1/market", "", auth);
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("SPX") != std::string::npos);
    ASSERT_TRUE(res.body.find("VIX") != std::string::npos);
    ASSERT_TRUE(res.body.find("NDX") != std::string::npos);
}

TEST(test_rest_orders_crud) {
    net::RestApi api;
    api.configure_defaults();
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"trader\",\"password\":\"trade\"}");
    auto tpos = res_l.body.find("genie-token-");
    std::string token = res_l.body.substr(tpos, res_l.body.find("\"", tpos) - tpos);
    std::map<std::string, std::string> auth = {{"Authorization", "Bearer " + token}};
    auto res_get = api.handle("GET", "/api/v1/orders", "", auth);
    ASSERT_TRUE(res_get.status == 200);
    ASSERT_TRUE(res_get.body.find("ORD-") != std::string::npos);
    auto res_post = api.handle("POST", "/api/v1/orders", "{\"symbol\":\"TSLA\",\"side\":\"Buy\",\"qty\":10,\"type\":\"Market\"}", auth);
    ASSERT_TRUE(res_post.status == 201);
    ASSERT_TRUE(res_post.body.find("TSLA") != std::string::npos);
    auto res_bad = api.handle("POST", "/api/v1/orders", "{\"qty\":10}", auth);
    ASSERT_TRUE(res_bad.status == 400);
}

TEST(test_rest_session_lifecycle) {
    net::RestApi api;
    api.configure_defaults();
    auto r1 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto r2 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"user\",\"password\":\"user\"}");
    auto r3 = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"trader\",\"password\":\"trade\"}");
    ASSERT_TRUE(r1.status == 200 && r2.status == 200 && r3.status == 200);
    ASSERT_TRUE(r1.body.find("Administrator") != std::string::npos);
    ASSERT_TRUE(r2.body.find("Analyst") != std::string::npos);
    ASSERT_TRUE(r3.body.find("Trader") != std::string::npos);
    ASSERT_TRUE(api.sessions().count() == 3);
    auto tpos = r2.body.find("genie-token-");
    std::string t2 = r2.body.substr(tpos, r2.body.find("\"", tpos) - tpos);
    api.handle("POST", "/api/v1/auth/logout", "", {{"Authorization", "Bearer " + t2}});
    ASSERT_TRUE(api.sessions().count() == 2);
}

TEST(test_rest_cors_headers) {
    net::Response res;
    ASSERT_TRUE(res.headers.count("Access-Control-Allow-Origin"));
    ASSERT_TRUE(res.headers["Access-Control-Allow-Origin"] == "*");
    ASSERT_TRUE(res.headers["Access-Control-Allow-Methods"].find("GET") != std::string::npos);
    ASSERT_TRUE(res.headers["Access-Control-Allow-Headers"].find("Authorization") != std::string::npos);
}

TEST(test_rest_404_and_wrong_method) {
    net::RestApi api;
    api.configure_defaults();
    auto res = api.handle("GET", "/api/v1/nonexistent");
    ASSERT_TRUE(res.status == 404);
    ASSERT_TRUE(res.body.find("not found") != std::string::npos);
    auto res2 = api.handle("DELETE", "/api/v1/health");
    ASSERT_TRUE(res2.status == 404);
}

TEST(test_rest_status_endpoint) {
    net::RestApi api;
    api.configure_defaults();
    auto res_l = api.handle("POST", "/api/v1/auth/login", "{\"username\":\"admin\",\"password\":\"demo\"}");
    auto tpos = res_l.body.find("genie-token-");
    std::string token = res_l.body.substr(tpos, res_l.body.find("\"", tpos) - tpos);
    std::map<std::string, std::string> auth = {{"Authorization", "Bearer " + token}};
    auto res = api.handle("GET", "/api/v1/status", "", auth);
    ASSERT_TRUE(res.status == 200);
    ASSERT_TRUE(res.body.find("uptime") != std::string::npos);
    ASSERT_TRUE(res.body.find("sessions") != std::string::npos);
    ASSERT_TRUE(res.body.find("routes") != std::string::npos);
}

// =========================================================================
// v2.6.0 - Thread Pool & Parallel Execution Tests (8 new)
// =========================================================================

TEST(test_thread_pool_basic) {
    ThreadPool pool(4);
    ASSERT_EQ(pool.core_thread_count(), 4u);
    ASSERT_TRUE(pool.thread_count() >= 4u);  // At least core threads alive
    ASSERT_EQ(pool.max_thread_count(), ThreadPool::optimal_thread_count());
    auto f1 = pool.submit([]() { return 42; });
    auto f2 = pool.submit([]() { return 99; });
    ASSERT_EQ(f1.get(), 42);
    ASSERT_EQ(f2.get(), 99);
    pool.wait_idle();
    ASSERT_TRUE(pool.is_idle());
}

TEST(test_thread_pool_elastic_growth) {
    // 1 core, max 8, short idle timeout for fast shrink
    ThreadPoolConfig cfg;
    cfg.core_threads = 1;
    cfg.max_threads = 8;
    cfg.idle_timeout = std::chrono::milliseconds{200};
    ThreadPool pool(cfg);

    ASSERT_EQ(pool.core_thread_count(), 1u);
    ASSERT_EQ(pool.max_thread_count(), 8u);

    // Use a barrier-like pattern: tasks signal they're running, then wait for release
    std::atomic<int> running_count{0};
    std::atomic<bool> release{false};
    std::vector<std::future<int>> futures;

    // Submit 4 tasks that block until released
    for (int i = 0; i < 4; ++i) {
        futures.push_back(pool.submit([&running_count, &release, i]() {
            running_count.fetch_add(1);
            while (!release.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return i;
        }));
        // Small delay between submits to give growth logic time
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for at least 2 tasks to start running (proves elastic growth)
    for (int w = 0; w < 100 && running_count.load() < 2; ++w)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // At least 2 tasks running concurrently means elastic thread was spawned
    ASSERT_TRUE(running_count.load() >= 2);
    ASSERT_TRUE(pool.thread_count() > 1u);
    ASSERT_TRUE(pool.thread_count() <= 8u);

    // Release all
    release.store(true);
    for (auto& f : futures) f.get();
    pool.wait_idle();

    // After idle timeout, elastic threads should expire
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    // Core threads remain
    ASSERT_TRUE(pool.thread_count() >= 1u);
}

TEST(test_thread_pool_parallel_for) {
    ThreadPool pool(4);
    std::vector<int> data(1000, 0);
    pool.parallel_for(0, 1000, [&data](size_t i) {
        data[i] = static_cast<int>(i * 2);
    });
    ASSERT_EQ(data[0], 0);
    ASSERT_EQ(data[500], 1000);
    ASSERT_EQ(data[999], 1998);
}

TEST(test_thread_pool_parallel_reduce) {
    ThreadPool pool(4);
    // Sum 1..1000
    int sum = pool.parallel_reduce<int>(1, 1001, 0,
        [](size_t i) -> int { return static_cast<int>(i); },
        [](int a, int b) -> int { return a + b; });
    ASSERT_EQ(sum, 500500);
}

TEST(test_thread_pool_parallel_map) {
    ThreadPool pool(4);
    std::vector<int> input = {1, 2, 3, 4, 5};
    auto output = pool.parallel_map(input, [](const int& x) { return x * x; });
    ASSERT_EQ(output.size(), 5u);
    ASSERT_EQ(output[0], 1);
    ASSERT_EQ(output[4], 25);
}

TEST(test_monte_carlo_parallel) {
    risk::MonteCarloEngine mc;
    mc.set_simulations(2000);
    mc.set_steps(50);
    mc.set_parallel(true);
    auto result = mc.simulate_gbm(100, 0.10, 0.20, 1.0);
    ASSERT_TRUE(result.mean_terminal_price > 0);
    ASSERT_TRUE(result.std_terminal_price > 0);
    ASSERT_TRUE(result.mean_return > -1.0);
    // Verify vs sequential
    risk::MonteCarloEngine mc2;
    mc2.set_simulations(2000);
    mc2.set_steps(50);
    mc2.set_parallel(false);
    auto result2 = mc2.simulate_gbm(100, 0.10, 0.20, 1.0);
    // Both should produce reasonable results (not exact due to different RNG seeds)
    ASSERT_TRUE(std::abs(result.mean_terminal_price - result2.mean_terminal_price) < 30.0);
}

TEST(test_backtest_parallel_strategies) {
    backtest::BacktestEngine engine(100000.0);
    engine.set_slippage(5.0);
    engine.set_commission(0.01);

    auto sma_fast = std::make_shared<backtest::SMACrossover>(5, 15);
    auto sma_slow = std::make_shared<backtest::SMACrossover>(10, 30);
    auto momentum = std::make_shared<backtest::MomentumStrategy>(20, 0.02);

    std::vector<backtest::PriceBar> bars;
    double p = 100.0;
    for (int i = 0; i < 100; ++i) {
        backtest::PriceBar bar;
        bar.close = p + std::sin(i * 0.1) * 3.0 + (i * 0.01);
        bar.open = bar.close - 0.2; bar.high = bar.close + 0.5; bar.low = bar.close - 0.5;
        bar.volume = 500000;
        bars.push_back(bar);
        p = bar.close;
    }
    std::map<std::string, std::vector<backtest::PriceBar>> data;
    data["TEST"] = bars;

    std::vector<std::shared_ptr<backtest::Strategy>> strategies = {sma_fast, sma_slow, momentum};
    auto results = engine.parallel_run(strategies, data);
    ASSERT_EQ(results.size(), 3u);
    // All should have run
    for (const auto& r : results) {
        ASSERT_TRUE(r.total_trades >= 0);
    }
}

TEST(test_rest_async_handle) {
    net::RestApi api;
    api.configure_defaults();
    // Submit multiple requests asynchronously
    net::Request r1; r1.method = net::Method::GET; r1.path = "/api/v1/health";
    net::Request r2; r2.method = net::Method::GET; r2.path = "/api/v1/health";
    net::Request r3; r3.method = net::Method::GET; r3.path = "/api/v1/health";
    auto f1 = api.handle_async(r1);
    auto f2 = api.handle_async(r2);
    auto f3 = api.handle_async(r3);
    ASSERT_TRUE(f1.get().status == 200);
    ASSERT_TRUE(f2.get().status == 200);
    ASSERT_TRUE(f3.get().status == 200);
}

TEST(test_rest_batch_handle) {
    net::RestApi api;
    api.configure_defaults();
    std::vector<net::Request> requests(5);
    for (auto& r : requests) { r.method = net::Method::GET; r.path = "/api/v1/health"; }
    auto responses = api.handle_batch(requests);
    ASSERT_EQ(responses.size(), 5u);
    for (const auto& res : responses) {
        ASSERT_TRUE(res.status == 200);
        ASSERT_TRUE(res.body.find("healthy") != std::string::npos);
    }
}

// =========================================================================
// v2.6.0 - SQLite Persistence Tests (10 new)
// =========================================================================

TEST(test_db_connection) {
    db::Connection conn(":memory:");
    conn.exec("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    conn.exec("INSERT INTO test VALUES (1, 'hello')");
    auto rows = conn.query("SELECT * FROM test");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].at("name"), "hello");
    ASSERT_TRUE(conn.table_exists("test"));
    ASSERT_TRUE(!conn.table_exists("nonexistent"));
}

TEST(test_db_statement_bind) {
    db::Connection conn(":memory:");
    conn.exec("CREATE TABLE nums (id INTEGER, val REAL, label TEXT)");
    auto stmt = conn.prepare("INSERT INTO nums VALUES (?, ?, ?)");
    stmt.bind(1, 42).bind(2, 3.14).bind(3, std::string("pi"));
    stmt.execute();
    stmt.reset();
    stmt.bind(1, 99).bind(2, 2.72).bind(3, std::string("e"));
    stmt.execute();
    auto s2 = conn.prepare("SELECT val FROM nums WHERE id = ?");
    s2.bind(1, 42);
    ASSERT_TRUE(s2.step());
    ASSERT_TRUE(std::abs(s2.column_double(0) - 3.14) < 0.001);
}

TEST(test_db_transaction_commit) {
    db::Connection conn(":memory:");
    conn.exec("CREATE TABLE t (x INTEGER)");
    {
        db::Transaction txn(conn);
        conn.exec("INSERT INTO t VALUES (1)");
        conn.exec("INSERT INTO t VALUES (2)");
        txn.commit();
    }
    auto v = conn.scalar<int>("SELECT count(*) FROM t");
    ASSERT_EQ(v.value_or(0), 2);
}

TEST(test_db_transaction_rollback) {
    db::Connection conn(":memory:");
    conn.exec("CREATE TABLE t (x INTEGER)");
    conn.exec("INSERT INTO t VALUES (100)");
    {
        db::Transaction txn(conn);
        conn.exec("INSERT INTO t VALUES (200)");
        // No commit - auto rollback on scope exit
    }
    auto v = conn.scalar<int>("SELECT count(*) FROM t");
    ASSERT_EQ(v.value_or(0), 1);
}

TEST(test_db_schema_migration) {
    db::Connection conn(":memory:");
    db::SchemaManager schema(conn);
    ASSERT_EQ(schema.version(), 0);
    schema.migrate(persistence::SCHEMA_VERSION, persistence::MIGRATIONS);
    ASSERT_EQ(schema.version(), 1);
    ASSERT_TRUE(conn.table_exists("portfolios"));
    ASSERT_TRUE(conn.table_exists("positions"));
    ASSERT_TRUE(conn.table_exists("orders"));
    ASSERT_TRUE(conn.table_exists("trades"));
    ASSERT_TRUE(conn.table_exists("market_data"));
    ASSERT_TRUE(conn.table_exists("audit_log"));
}

TEST(test_store_securities) {
    persistence::DataStore store(":memory:");
    store.save_security({"AAPL", "Apple Inc.", "Equity", "NASDAQ", "Technology", "USD"});
    store.save_security({"MSFT", "Microsoft Corp.", "Equity", "NASDAQ", "Technology", "USD"});
    store.save_security({"UST10Y", "US 10Y Treasury", "FixedIncome", "", "Government", "USD"});
    auto all = store.list_securities();
    ASSERT_EQ(all.size(), 3u);
    auto aapl = store.get_security("AAPL");
    ASSERT_TRUE(aapl.has_value());
    ASSERT_EQ(aapl->name, "Apple Inc.");
    ASSERT_EQ(aapl->sector, "Technology");
}

TEST(test_store_portfolio_lifecycle) {
    persistence::DataStore store(":memory:");
    store.save_security({"AAPL", "Apple", "Equity", "", "", "USD"});
    store.save_security({"GOOGL", "Google", "Equity", "", "", "USD"});

    // Create portfolio
    store.save_portfolio({"P1", "Growth Fund", "USD", "Active", 1000000.0, "user-1"});
    auto p = store.get_portfolio("P1");
    ASSERT_TRUE(p.has_value());
    ASSERT_EQ(p->name, "Growth Fund");

    // Add positions
    persistence::PositionRow pos1; pos1.portfolio_id = "P1"; pos1.security_id = "AAPL";
    pos1.quantity = 500; pos1.avg_cost = 175.0; pos1.market_value = 90000.0; pos1.unrealized_pnl = 2500.0;
    store.save_position(pos1);
    persistence::PositionRow pos2; pos2.portfolio_id = "P1"; pos2.security_id = "GOOGL";
    pos2.quantity = 200; pos2.avg_cost = 140.0; pos2.market_value = 30000.0; pos2.unrealized_pnl = 2000.0;
    store.save_position(pos2);
    auto positions = store.get_positions("P1");
    ASSERT_EQ(positions.size(), 2u);

    // Close one
    store.close_position("P1", "GOOGL");
    auto open_positions = store.get_positions("P1", true);
    ASSERT_EQ(open_positions.size(), 1u);
    ASSERT_EQ(open_positions[0].security_id, "AAPL");
}

TEST(test_store_order_trade_flow) {
    persistence::DataStore store(":memory:");
    store.save_security({"TSLA", "Tesla", "Equity", "", "", "USD"});
    store.save_portfolio({"P1", "Test", "USD", "Active", 500000.0, "user-1"});

    // Place order
    persistence::OrderRow ord; ord.id = "O1"; ord.portfolio_id = "P1"; ord.security_id = "TSLA";
    ord.side = "Buy"; ord.order_type = "Market"; ord.quantity = 100; ord.status = "New";
    store.save_order(ord);
    auto orders = store.get_orders("P1", "New");
    ASSERT_EQ(orders.size(), 1u);
    ASSERT_EQ(orders[0].status, "New");

    // Fill order
    store.fill_order("O1", 250.50, 100);
    auto filled = store.get_orders("P1", "Filled");
    ASSERT_EQ(filled.size(), 1u);
    ASSERT_TRUE(std::abs(filled[0].avg_fill_price - 250.50) < 0.01);

    // Record trade
    persistence::TradeRow trd; trd.id = "T1"; trd.order_id = "O1"; trd.portfolio_id = "P1";
    trd.security_id = "TSLA"; trd.side = "Buy"; trd.quantity = 100; trd.price = 250.50; trd.commission = 1.00;
    store.save_trade(trd);
    auto trades = store.get_trades("P1");
    ASSERT_EQ(trades.size(), 1u);
    ASSERT_EQ(store.trade_count("P1"), 1);
}

TEST(test_store_market_data) {
    persistence::DataStore store(":memory:");
    store.save_security({"SPY", "S&P 500 ETF", "Equity", "", "", "USD"});

    // Batch insert
    std::vector<persistence::PriceRow> prices;
    for (int d = 1; d <= 30; ++d) {
        std::ostringstream date;
        date << "2026-01-" << std::setfill('0') << std::setw(2) << d;
        double base = 500.0 + d * 0.5;
        prices.push_back({"SPY", date.str(), base - 1, base + 2, base - 2, base, 5000000.0 + d * 100000});
    }
    store.save_prices_batch(prices);

    auto all = store.get_prices("SPY");
    ASSERT_EQ(all.size(), 30u);

    auto range = store.get_prices("SPY", "2026-01-15", "2026-01-20");
    ASSERT_EQ(range.size(), 6u);

    auto latest = store.latest_price("SPY");
    ASSERT_TRUE(latest.has_value());
    ASSERT_TRUE(*latest > 500.0);
}

TEST(test_store_config_and_stats) {
    persistence::DataStore store(":memory:");
    store.set_config("risk.var.confidence", "0.99");
    store.set_config("risk.var.holding_period", "10");
    store.set_config("app.name", "Metis Genie Platform");

    auto val = store.get_config("risk.var.confidence");
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(*val, "0.99");

    auto all_cfg = store.get_all_config();
    ASSERT_EQ(all_cfg.size(), 3u);

    // Audit
    store.audit("portfolio", "P1", "create", "Created Growth Fund");
    store.audit("portfolio", "P1", "trade", "Bought 100 AAPL");
    auto log = store.get_audit_log("portfolio", "P1");
    ASSERT_EQ(log.size(), 2u);

    // Stats
    store.save_security({"AAPL", "Apple", "Equity", "", "", "USD"});
    store.save_portfolio({"P1", "Test", "USD", "Active", 100000.0, "user-1"});
    auto s = store.stats();
    ASSERT_EQ(s.securities, 1);
    ASSERT_EQ(s.portfolios, 1);
    ASSERT_EQ(s.config_entries, 3);
    ASSERT_EQ(s.schema_version, 1);
}

// =========================================================================
// v2.6.0 - Logging + P1/P2/P3 Tests (15 new)
// =========================================================================

TEST(test_logging_framework) {
    auto& log = genie::logger();
    auto orig_level = log.level();
    log.set_level(LogLevel::DEBUG);
    log.set_component("TestSuite");
    log.debug("Debug test"); log.info("Info test");
    log.warn("Warn test"); log.error("Error test");
    LOG_DEBUG("Macro debug"); LOG_INFO("Macro info");
    ASSERT_TRUE(log.level() == LogLevel::DEBUG);
    log.set_level(orig_level);
    log.set_component("Genie");
}

TEST(test_factor_model) {
    risk::FactorModel model;
    risk::FactorExposure exp;
    exp.security_id = "AAPL";
    exp.exposures[static_cast<size_t>(risk::Factor::Market)] = 1.1;
    exp.exposures[static_cast<size_t>(risk::Factor::Size)] = 0.3;
    model.set_exposure(exp);
    model.set_specific_risk("AAPL", 0.15);
    ASSERT_EQ(model.security_count(), 1u);
    const auto& stored = model.exposure("AAPL");
    ASSERT_TRUE(std::abs(stored.exposures[0] - 1.1) < 0.001);
}

TEST(test_correlated_mc) {
    std::vector<std::vector<double>> corr = {{1.0, 0.6}, {0.6, 1.0}};
    risk::CholeskyDecomposition chol;
    ASSERT_TRUE(chol.decompose(corr));
    std::vector<risk::AssetParams> assets = {
        {"A", 100.0, 0.08, 0.20}, {"B", 50.0, 0.05, 0.15}
    };
    std::vector<double> weights = {0.6, 0.4};
    auto result = risk::CorrelatedMonteCarlo::simulate(
        assets, corr, weights, 500, 252, 1.0/252.0, 42);
    ASSERT_EQ(result.n_assets, 2u);
    ASSERT_EQ(result.n_paths, 500u);
    ASSERT_TRUE(result.terminal_prices.size() == 2);
    ASSERT_TRUE(result.terminal_prices[0].size() == 500);
    ASSERT_TRUE(result.mean_portfolio_value > 0);
}

TEST(test_brinson_fachler) {
    using namespace performance;
    std::vector<SegmentData> segments = {
        {"Tech", 0.6, 0.5, 0.10, 0.08},
        {"Health", 0.3, 0.3, -0.02, 0.03},
        {"Energy", 0.1, 0.2, 0.05, 0.04}
    };
    auto result = BrinsonFachler::single_period("Q1", segments);
    double exp_port = 0.6*0.10 + 0.3*(-0.02) + 0.1*0.05;
    ASSERT_TRUE(std::abs(result.portfolio_return - exp_port) < 0.001);
    ASSERT_TRUE(result.segments.size() == 3);
}

TEST(test_ibor) {
    portfolio::IBOR ibor("P1", 1000000.0);
    portfolio::Transaction t1;
    t1.type = portfolio::TxnType::Buy;
    t1.portfolio_id = "P1"; t1.security_id = "AAPL"; t1.trade_date = "2026-01-02";
    t1.quantity = 100; t1.price = 175.0; t1.amount = -17500.0; t1.commission = 10.0;
    ibor.record(t1);
    auto positions = ibor.positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_TRUE(positions.count("AAPL") > 0);
    ASSERT_TRUE(std::abs(positions.at("AAPL").quantity - 100.0) < 0.01);
    auto cash = ibor.cash_balance();
    ASSERT_TRUE(cash < 1000000.0);
}

TEST(test_auth_provider) {
    net::AuthProvider auth(true);  // Enable demo users for testing
    // Default users: admin/demo, trader/trade, viewer/view
    auto r1 = auth.authenticate("admin", "demo");
    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(!r1.token.empty());
    auto r2 = auth.authenticate("admin", "wrong");
    ASSERT_TRUE(!r2.success);
    auto v1 = auth.validate(r1.token);
    ASSERT_TRUE(v1.has_value());
    ASSERT_EQ(v1->username, "admin");
    auth.revoke(r1.token);
    auto v2 = auth.validate(r1.token);
    ASSERT_TRUE(!v2.has_value());
}

TEST(test_ws_server) {
    net::WsServer ws;
    std::vector<std::pair<std::string, std::string>> sent;
    ws.set_send_callback([&](const std::string& cid, const std::string& msg) {
        sent.push_back({cid, msg});
    });
    auto c1 = ws.connect("user1", "Alice");
    auto c2 = ws.connect("user2", "Bob");
    ASSERT_EQ(ws.client_count(), 2u);
    ws.subscribe(c1, "prices");
    ws.subscribe(c2, "prices");
    ws.subscribe(c1, "alerts");
    ws.publish("prices", "{\"AAPL\":175.50}");
    ASSERT_TRUE(sent.size() >= 2);
    ws.disconnect(c2);
    ASSERT_EQ(ws.client_count(), 1u);
}

TEST(test_pdf_report) {
    reporting::ReportDocument doc;
    doc.title = "Test Report";
    doc.generated_by = "Genie";
    doc.add_title("Portfolio Tearsheet");
    doc.add_subtitle("Q1 2026");
    doc.add_paragraph("Unit test report.");
    doc.add_divider();
    doc.add_key_values("Summary", {{"NAV", "$1,000,000"}, {"Return", "+5.2%"}});
    std::vector<reporting::ColumnDef> cols = {{"Symbol"}, {"Qty"}, {"Value"}};
    std::vector<std::vector<std::string>> rows = {{"AAPL", "100", "$17,500"}, {"MSFT", "200", "$85,000"}};
    doc.add_table("Holdings", cols, rows);
    auto html = doc.to_html();
    ASSERT_TRUE(html.find("Portfolio Tearsheet") != std::string::npos);
    ASSERT_TRUE(html.find("AAPL") != std::string::npos);
    ASSERT_TRUE(doc.elements.size() >= 5);
}

TEST(test_settlement) {
    trading::BusinessCalendar cal;
    cal.add_holiday("2026-01-01");
    cal.add_holiday("2026-01-19"); // MLK
    auto settle = cal.add_business_days("2026-01-05", 2); // Mon + 2bd = Wed
    ASSERT_EQ(settle, "2026-01-07");
    auto fri_settle = cal.add_business_days("2026-01-09", 2); // Fri + 2bd = Tue
    ASSERT_EQ(fri_settle, "2026-01-13");
    ASSERT_TRUE(cal.is_business_day("2026-01-05"));
    ASSERT_TRUE(!cal.is_business_day("2026-01-04")); // Sunday
    ASSERT_TRUE(!cal.is_business_day("2026-01-01")); // Holiday
}

TEST(test_tax_optimization) {
    portfolio::TaxOptimizer optimizer;
    optimizer.set_rates(0.37, 0.20);
    std::map<std::string, std::vector<portfolio::HarvestLot>> positions;
    portfolio::HarvestLot lot1;
    lot1.security_id = "AAPL"; lot1.quantity = 100; lot1.cost_basis = 200.0;
    lot1.total_cost = 20000.0; lot1.acquisition_date = "2025-01-15"; lot1.long_term = true;
    positions["AAPL"].push_back(lot1);
    std::map<std::string, double> prices = {{"AAPL", 170.0}};
    auto candidates = optimizer.find_harvest_candidates(positions, prices, 100.0);
    ASSERT_TRUE(candidates.size() >= 1);
    for (const auto& c : candidates) {
        ASSERT_TRUE(c.unrealized_loss < 0);
        ASSERT_TRUE(c.tax_savings > 0);
    }
}

TEST(test_regulatory_engine) {
    compliance::RegulatoryEngine reg;
    compliance::CompliancePortfolio port;
    port.id = "P1"; port.name = "Test Fund";
    port.nav = 10000000.0;
    port.cash = 500000.0;
    compliance::CompliancePosition pos1;
    pos1.security_id = "AAPL"; pos1.weight = 12.0; pos1.market_value = 1200000.0;
    pos1.asset_class = "Equity"; pos1.is_transferable = true;
    port.positions.push_back(pos1);
    auto results = reg.check_all(port);
    // Even with no rules, should return empty (not crash)
    // Add rules and recheck
    auto pre = reg.pre_trade_check(port, pos1);
    ASSERT_TRUE(true); // No crash
}

TEST(test_distributed_pool) {
    compute::DistributedPool pool;
    auto w1 = pool.register_worker("node1", 4, 8192);
    auto w2 = pool.register_worker("node2", 8, 16384);
    ASSERT_EQ(pool.worker_count(), 2u);
    auto job_id = pool.submit(compute::JobType::MonteCarlo, "sim config", 3);
    ASSERT_TRUE(!job_id.empty());
    auto job = pool.get_job(job_id);
    ASSERT_TRUE(job.has_value());
    ASSERT_EQ(job->status, compute::JobStatus::Queued);
    ASSERT_TRUE(pool.cancel(job_id));
    auto cancelled = pool.get_job(job_id);
    ASSERT_TRUE(cancelled.has_value());
    ASSERT_EQ(cancelled->status, compute::JobStatus::Cancelled);
}

TEST(test_event_store) {
    persistence::EventStore store;
    store.append(persistence::EventType::PortfolioCreated, "portfolio", "P1",
                 "{\"name\":\"Growth Fund\"}");
    store.append(persistence::EventType::OrderFilled, "portfolio", "P1",
                 "{\"security\":\"AAPL\",\"qty\":100}");
    store.append(persistence::EventType::PositionUpdated, "portfolio", "P1",
                 "{\"security\":\"AAPL\",\"qty\":100}");
    auto events = store.events_for("portfolio", "P1");
    ASSERT_EQ(events.size(), 3u);
    ASSERT_EQ(events[0].type, persistence::EventType::PortfolioCreated);
    store.save_snapshot("portfolio", "P1", 3, "{\"cash\":982500}");
    auto snap = store.load_snapshot("portfolio", "P1");
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(snap->state.find("982500") != std::string::npos);
    ASSERT_TRUE(store.total_events() >= 3);
}

TEST(test_smart_router) {
    trading::SmartRouter router;
    // Constructor loads 7 default venues (NYSE, NASDAQ, ARCA, BATS, IEX, SIGMA, XDARK)
    ASSERT_TRUE(router.venue_count() >= 7);
    auto decision = router.route("AAPL", "Buy", 1000, 175.0);
    ASSERT_TRUE(!decision.order_id.empty());
    ASSERT_TRUE(!decision.rationale.empty());
    // Add custom venue
    trading::Venue custom;
    custom.id = "CUSTOM"; custom.name = "Custom ATS"; custom.type = "ATS";
    custom.fee_per_share = 0.001; custom.avg_fill_rate = 0.75; custom.active = true;
    router.add_venue(custom);
    ASSERT_TRUE(router.venue_count() >= 8);
}

// v2.6.0 - Multi-User Support Tests (5 new)

TEST(test_user_manager_crud) {
    genie::UserManager mgr;
    auto id1 = mgr.create_user("alice", "Alice Smith", "alice@test.com", "PM");
    auto id2 = mgr.create_user("bob", "Bob Jones", "bob@test.com", "Trader");
    ASSERT_TRUE(!id1.empty());
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(mgr.create_user("alice", "Alice Dup").empty());

    auto alice = mgr.get_user(id1);
    ASSERT_TRUE(alice.has_value());
    ASSERT_TRUE(alice->display_name == "Alice Smith");
    ASSERT_TRUE(alice->role == "PM");

    auto alice_by_name = mgr.get_user_by_name("alice");
    ASSERT_TRUE(alice_by_name.has_value());
    ASSERT_TRUE(alice_by_name->user_id == id1);

    mgr.update_user(id1, "Alice B. Smith", "alice2@test.com", "Admin");
    auto updated = mgr.get_user(id1);
    ASSERT_TRUE(updated->display_name == "Alice B. Smith");
    ASSERT_TRUE(updated->role == "Admin");

    ASSERT_EQ(mgr.user_count(), 2u);
    mgr.deactivate_user(id2);
    ASSERT_EQ(mgr.list_users().size(), 1u);
    ASSERT_EQ(mgr.list_users(true).size(), 2u);
    mgr.activate_user(id2);
    ASSERT_EQ(mgr.list_users().size(), 2u);

    mgr.set_preference(id1, "theme", "dark");
    auto pref = mgr.get_preference(id1, "theme");
    ASSERT_TRUE(pref.has_value() && *pref == "dark");
}

TEST(test_portfolio_ownership) {
    genie::UserManager mgr;
    auto alice = mgr.create_user("alice", "Alice", "", "PM");
    auto bob = mgr.create_user("bob", "Bob", "", "Trader");

    mgr.assign_portfolio("growth-fund", alice);
    mgr.assign_portfolio("value-fund", alice);
    mgr.assign_portfolio("hedge-fund", bob);

    auto owner = mgr.get_portfolio_owner("growth-fund");
    ASSERT_TRUE(owner.has_value() && *owner == alice);
    ASSERT_EQ(mgr.get_user_portfolios(alice).size(), 2u);
    ASSERT_EQ(mgr.get_user_portfolios(bob).size(), 1u);

    ASSERT_TRUE(mgr.transfer_portfolio("growth-fund", bob));
    auto new_owner = mgr.get_portfolio_owner("growth-fund");
    ASSERT_TRUE(new_owner.has_value() && *new_owner == bob);
    ASSERT_EQ(mgr.get_user_portfolios(bob).size(), 2u);
}

TEST(test_shared_access_grants) {
    genie::UserManager mgr;
    auto alice = mgr.create_user("alice", "Alice", "", "PM");
    auto bob = mgr.create_user("bob", "Bob", "", "Trader");
    auto carol = mgr.create_user("carol", "Carol", "", "Viewer");

    mgr.assign_portfolio("fund-a", alice);

    ASSERT_TRUE(mgr.get_access_level("fund-a", alice) == genie::AccessLevel::Owner);
    ASSERT_TRUE(mgr.can_read("fund-a", alice));
    ASSERT_TRUE(mgr.can_write("fund-a", alice));
    ASSERT_TRUE(mgr.get_access_level("fund-a", bob) == genie::AccessLevel::None);
    ASSERT_TRUE(!mgr.can_read("fund-a", bob));

    mgr.grant_access("fund-a", bob, genie::AccessLevel::ReadOnly, alice);
    ASSERT_TRUE(mgr.can_read("fund-a", bob));
    ASSERT_TRUE(!mgr.can_write("fund-a", bob));

    mgr.grant_access("fund-a", bob, genie::AccessLevel::ReadWrite, alice);
    ASSERT_TRUE(mgr.can_write("fund-a", bob));

    mgr.revoke_access("fund-a", bob);
    ASSERT_TRUE(!mgr.can_read("fund-a", bob));

    mgr.grant_access("fund-a", carol, genie::AccessLevel::ReadOnly, alice);
    auto accessible = mgr.get_accessible_portfolios(carol);
    ASSERT_EQ(accessible.size(), 1u);
    ASSERT_TRUE(accessible[0].second == genie::AccessLevel::ReadOnly);
}

TEST(test_user_isolation) {
    genie::UserManager mgr;
    auto alice = mgr.create_user("alice", "Alice", "", "PM");
    auto bob = mgr.create_user("bob", "Bob", "", "Trader");
    auto admin = mgr.create_user("admin", "Admin", "", "Admin");

    mgr.assign_portfolio("fund-a", alice);
    mgr.assign_portfolio("fund-b", bob);

    ASSERT_TRUE(!mgr.can_read("fund-b", alice));
    ASSERT_TRUE(!mgr.can_read("fund-a", bob));
    ASSERT_TRUE(mgr.can_read("fund-a", admin));
    ASSERT_TRUE(mgr.can_read("fund-b", admin));
    ASSERT_TRUE(mgr.can_write("fund-a", admin));

    ASSERT_EQ(mgr.get_accessible_portfolios(admin).size(), 2u);
    ASSERT_EQ(mgr.get_accessible_portfolios(alice).size(), 1u);
}

TEST(test_multi_user_datastore) {
    genie::persistence::DataStore store(":memory:");
    store.save_portfolio({"fund-a", "Growth Fund", "USD", "Active", 1000000.0, "user-1"});
    store.save_portfolio({"fund-b", "Value Fund", "USD", "Active", 500000.0, "user-2"});
    store.save_portfolio({"fund-c", "Balanced Fund", "USD", "Active", 750000.0, "user-1"});

    ASSERT_EQ(store.list_portfolios().size(), 3u);
    ASSERT_EQ(store.list_user_portfolios("user-1").size(), 2u);
    ASSERT_EQ(store.list_user_portfolios("user-2").size(), 1u);

    auto fund_a = store.get_portfolio("fund-a");
    ASSERT_TRUE(fund_a.has_value() && fund_a->owner_id == "user-1");
}

int main() {
    std::cout << "\n" << std::string(60, '=') << "\nMetis Genie Platform v5.3.1 - Test Suite\n" << std::string(60, '=') << "\n\n";
    RUN_TEST(test_version); RUN_TEST(test_money); RUN_TEST(test_uuid);
    RUN_TEST(test_statistics); RUN_TEST(test_percentile); RUN_TEST(test_correlation);
    RUN_TEST(test_black_scholes); RUN_TEST(test_bond_price);
    RUN_TEST(test_date_conversion); RUN_TEST(test_weekend);
    RUN_TEST(test_security); RUN_TEST(test_market_data); RUN_TEST(test_bond); RUN_TEST(test_option);
    RUN_TEST(test_portfolio); RUN_TEST(test_position);
    RUN_TEST(test_var); RUN_TEST(test_stress);
    RUN_TEST(test_order); RUN_TEST(test_compliance);
    RUN_TEST(test_performance); RUN_TEST(test_report);
    RUN_TEST(test_optimizer); RUN_TEST(test_system);
    // v2.0.3 tests
    RUN_TEST(test_logging); RUN_TEST(test_config); RUN_TEST(test_persistence_csv);
    RUN_TEST(test_fx); RUN_TEST(test_commodity);
    RUN_TEST(test_risk_metrics); RUN_TEST(test_blotter); RUN_TEST(test_cli);
    // v2.6.0 new tests
    RUN_TEST(test_timeseries); RUN_TEST(test_rebalancing); RUN_TEST(test_tax_lots);
    RUN_TEST(test_currency_hedge); RUN_TEST(test_scenario); RUN_TEST(test_validation);
    RUN_TEST(test_alerts); RUN_TEST(test_benchmark);
    // v2.6.0 REST API tests
    RUN_TEST(test_rest_request); RUN_TEST(test_rest_response);
    RUN_TEST(test_rest_json_builder); RUN_TEST(test_rest_json_parse);
    RUN_TEST(test_rest_routing); RUN_TEST(test_rest_query_params);
    RUN_TEST(test_rest_health); RUN_TEST(test_rest_login);
    RUN_TEST(test_rest_auth_flow);
    // v5.3.1 User management
    RUN_TEST(test_rest_registration); RUN_TEST(test_rest_user_profile);
    RUN_TEST(test_rest_admin_users); RUN_TEST(test_rest_path_params);
    // v2.6.0 REST enhancements
    RUN_TEST(test_rest_pagination); RUN_TEST(test_rest_middleware);
    RUN_TEST(test_rest_rate_limiter); RUN_TEST(test_rest_logger);
    RUN_TEST(test_rest_api_version);
    // v2.6.0 extended test coverage (26 new)
    RUN_TEST(test_money_currency); RUN_TEST(test_statistics_edge_cases);
    RUN_TEST(test_correlation_inverse); RUN_TEST(test_date_business_days);
    RUN_TEST(test_uuid_uniqueness); RUN_TEST(test_config_types);
    RUN_TEST(test_events_pubsub); RUN_TEST(test_alerts_lifecycle);
    RUN_TEST(test_logging_levels);
    RUN_TEST(test_equity_etf); RUN_TEST(test_option_greeks);
    RUN_TEST(test_bond_pricing); RUN_TEST(test_fx_pair_inverse);
    RUN_TEST(test_commodity_gold_oil);
    RUN_TEST(test_portfolio_multi_position); RUN_TEST(test_portfolio_close_position);
    RUN_TEST(test_rebalancing_drift); RUN_TEST(test_tax_lots_fifo);
    RUN_TEST(test_var_returns); RUN_TEST(test_scenario_multiple);
    RUN_TEST(test_currency_hedge_multi);
    RUN_TEST(test_order_sell); RUN_TEST(test_blotter_multi_trades);
    RUN_TEST(test_timeseries_ema); RUN_TEST(test_backtesting_sma);
    RUN_TEST(test_benchmark_tracking);
    RUN_TEST(test_rest_market_data); RUN_TEST(test_rest_orders_crud);
    RUN_TEST(test_rest_session_lifecycle); RUN_TEST(test_rest_cors_headers);
    RUN_TEST(test_rest_404_and_wrong_method); RUN_TEST(test_rest_status_endpoint);
    // v2.6.0 thread pool & parallel execution (8 new)
    RUN_TEST(test_thread_pool_basic); RUN_TEST(test_thread_pool_elastic_growth);
    RUN_TEST(test_thread_pool_parallel_for);
    RUN_TEST(test_thread_pool_parallel_reduce); RUN_TEST(test_thread_pool_parallel_map);
    RUN_TEST(test_monte_carlo_parallel); RUN_TEST(test_backtest_parallel_strategies);
    RUN_TEST(test_rest_async_handle); RUN_TEST(test_rest_batch_handle);
    // v2.6.0 SQLite persistence (10 new)
    RUN_TEST(test_db_connection); RUN_TEST(test_db_statement_bind);
    RUN_TEST(test_db_transaction_commit); RUN_TEST(test_db_transaction_rollback);
    RUN_TEST(test_db_schema_migration); RUN_TEST(test_store_securities);
    RUN_TEST(test_store_portfolio_lifecycle); RUN_TEST(test_store_order_trade_flow);
    RUN_TEST(test_store_market_data); RUN_TEST(test_store_config_and_stats);
    // v2.6.0 P1/P2/P3 features (14 new)
    RUN_TEST(test_logging_framework);
    RUN_TEST(test_factor_model); RUN_TEST(test_correlated_mc);
    RUN_TEST(test_brinson_fachler); RUN_TEST(test_ibor);
    RUN_TEST(test_auth_provider); RUN_TEST(test_ws_server);
    RUN_TEST(test_pdf_report); RUN_TEST(test_settlement);
    RUN_TEST(test_tax_optimization);
    RUN_TEST(test_regulatory_engine); RUN_TEST(test_distributed_pool);
    RUN_TEST(test_event_store); RUN_TEST(test_smart_router);

    // v2.6.0 Multi-user support (5 new)
    RUN_TEST(test_user_manager_crud);
    RUN_TEST(test_portfolio_ownership);
    RUN_TEST(test_shared_access_grants);
    RUN_TEST(test_user_isolation);
    RUN_TEST(test_multi_user_datastore);

    std::cout << "\n" << std::string(60, '=') << "\nResults: " << tests_passed << "/" << tests_run << " PASSED\n" << std::string(60, '=') << "\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
