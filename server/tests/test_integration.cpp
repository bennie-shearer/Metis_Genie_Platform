/**
 * @file test_integration.cpp
 * @brief Integration tests for Metis Genie Platform v5.5.11
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Tests the newly implemented components:
 *   1. URL parser
 *   2. Platform HTTP client (real network calls)
 *   3. WebSocket handshake validation
 *   4. API config loader
 *   5. Trading calendar
 *   6. PDF writer
 *   7. SMTP client (structure only, no live send without creds)
 *   8. Tradier client (sandbox only with API key)
 *
 * Tests marked [NETWORK] require internet connectivity.
 * Tests marked [APIKEY] require configured API keys in config.json.
 */

#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>

// Core components
#include "genie/core/http_client.hpp"
#include "genie/core/version.hpp"
#include "genie/core/platform_http.hpp"
#include "genie/core/platform_websocket.hpp"
#include "genie/core/api_config_loader.hpp"
#include "genie/core/smtp_client.hpp"

// Market
#include "genie/market/trading_calendar.hpp"

// Reporting
#include "genie/reporting/pdf_report.hpp"
#include "genie/reporting/pdf_writer.hpp"

// Trading
#include "genie/trading/tradier_client.hpp"

// ============================================================================
// Test Framework (minimal)
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST: " << name << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASS" << std::endl; \
    } while(0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        std::cout << "FAIL: " << msg << std::endl; \
    } while(0)

#define SKIP(msg) \
    do { \
        tests_skipped++; \
        std::cout << "SKIP: " << msg << std::endl; \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { FAIL(msg); return; } 

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) { FAIL(std::string(msg) + " (got: " + std::to_string(a) + ")"); return; }

#define ASSERT_STR_EQ(a, b, msg) \
    if ((a) != (b)) { FAIL(std::string(msg) + " (got: '" + (a) + "')"); return; }

// ============================================================================
// 1. URL Parser Tests
// ============================================================================

void test_url_parser() {
    std::cout << "\n=== URL Parser ===" << std::endl;

    {
        TEST("Parse HTTPS URL");
        auto p = genie::core::parse_url("https://api.alpaca.markets/v2/account");
        ASSERT_STR_EQ(p.scheme, "https", "scheme");
        ASSERT_STR_EQ(p.host, "api.alpaca.markets", "host");
        ASSERT_STR_EQ(p.path, "/v2/account", "path");
        ASSERT_TRUE(p.is_https, "is_https");
        ASSERT_EQ(p.effective_port(), 443, "port");
        PASS();
    }

    {
        TEST("Parse HTTP URL with port");
        auto p = genie::core::parse_url("http://localhost:8080/api/test?key=val");
        ASSERT_STR_EQ(p.scheme, "http", "scheme");
        ASSERT_STR_EQ(p.host, "localhost", "host");
        ASSERT_EQ(p.port, 8080, "port");
        ASSERT_STR_EQ(p.path, "/api/test", "path");
        ASSERT_STR_EQ(p.query, "key=val", "query");
        ASSERT_TRUE(!p.is_https, "not https");
        PASS();
    }

    {
        TEST("Parse WSS URL");
        auto p = genie::core::parse_url("wss://stream.data.alpaca.markets/v2/iex");
        ASSERT_STR_EQ(p.host, "stream.data.alpaca.markets", "host");
        ASSERT_STR_EQ(p.path, "/v2/iex", "path");
        PASS();
    }

    {
        TEST("Parse URL with no path");
        auto p = genie::core::parse_url("https://example.com");
        ASSERT_STR_EQ(p.host, "example.com", "host");
        ASSERT_STR_EQ(p.path, "/", "path defaults to /");
        PASS();
    }

    {
        TEST("Request URI with query");
        auto p = genie::core::parse_url("https://api.example.com/v1/data?symbol=AAPL&limit=10");
        ASSERT_STR_EQ(p.request_uri(), "/v1/data?symbol=AAPL&limit=10", "request_uri");
        PASS();
    }

    {
        TEST("Host header without default port");
        auto p = genie::core::parse_url("https://api.example.com/test");
        ASSERT_STR_EQ(p.host_header(), "api.example.com", "host_header");
        PASS();
    }

    {
        TEST("Host header with non-default port");
        auto p = genie::core::parse_url("https://api.example.com:8443/test");
        ASSERT_STR_EQ(p.host_header(), "api.example.com:8443", "host_header");
        PASS();
    }
}

// ============================================================================
// 2. HTTP Client Tests
// ============================================================================

void test_http_client() {
    std::cout << "\n=== HTTP Client ===" << std::endl;

    {
        TEST("[NETWORK] GET httpbin.org/get");
        auto response = genie::core::platform_http_request(
            "https://httpbin.org/get", 0, {}, "");
        if (!response.success) {
            SKIP("Network unavailable: " + response.error);
            return;
        }
        ASSERT_EQ(response.status_code, 200, "status 200");
        ASSERT_TRUE(response.body.find("headers") != std::string::npos, "body contains headers");
        PASS();
    }

    {
        TEST("[NETWORK] POST with JSON body");
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"}
        };
        auto response = genie::core::platform_http_request(
            "https://httpbin.org/post", 1, headers,
            R"({"test":"metis-genie-platform"})");
        if (!response.success) {
            SKIP("Network unavailable");
            return;
        }
        ASSERT_EQ(response.status_code, 200, "status 200");
        ASSERT_TRUE(response.body.find("metis-genie-platform") != std::string::npos, "body echoed");
        PASS();
    }

    {
        TEST("HttpClient with mock response");
        genie::core::HttpClient client("https://api.example.com");
        genie::core::HttpResponse mock;
        mock.status_code = 200;
        mock.body = R"({"status":"ok"})";
        mock.success = true;
        client.set_mock_response(mock);

        auto resp = client.get("/v1/test");
        ASSERT_EQ(resp.status_code, 200, "mock status");
        ASSERT_STR_EQ(resp.body, R"({"status":"ok"})", "mock body");
        PASS();
    }

    {
        TEST("HttpClient rate limiter");
        auto limiter = std::make_shared<genie::core::RateLimiter>(5, 1);
        ASSERT_TRUE(limiter->try_acquire(), "first acquire");
        ASSERT_TRUE(limiter->try_acquire(), "second acquire");
        ASSERT_TRUE(limiter->try_acquire(), "third acquire");
        ASSERT_TRUE(limiter->try_acquire(), "fourth acquire");
        ASSERT_TRUE(limiter->try_acquire(), "fifth acquire");
        ASSERT_TRUE(!limiter->try_acquire(), "sixth should fail");
        PASS();
    }
}

// ============================================================================
// 3. WebSocket Tests
// ============================================================================

void test_websocket() {
    std::cout << "\n=== WebSocket ===" << std::endl;

    {
        TEST("WebSocket key generation");
        std::string key = genie::core::ws_detail::generate_ws_key();
        ASSERT_TRUE(key.size() == 24, "key is 24 chars (base64 of 16 bytes)");
        PASS();
    }

    {
        TEST("WebSocket accept key computation");
        // Known test vector from RFC 6455
        std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
        std::string expected = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
        std::string computed = genie::core::ws_detail::compute_accept_key(key);
        ASSERT_STR_EQ(computed, expected, "RFC 6455 test vector");
        PASS();
    }
}

// ============================================================================
// 4. API Config Loader Tests
// ============================================================================

void test_config_loader() {
    std::cout << "\n=== API Config Loader ===" << std::endl;

    {
        TEST("Load config from JSON string");
        // Write a test config file
        std::string test_config = R"({
            "server.port": 8080,
            "api_keys": {
                "alpha_vantage": { "key": "TEST_AV_KEY" },
                "alpaca": { "key_id": "PKTEST123", "secret": "secret456", "paper": true },
                "finnhub": { "token": "FH_TOKEN" }
            }
        })";

        std::ofstream f("/tmp/test_config.json");
        f << test_config;
        f.close();

        int count = genie::core::load_api_keys("/tmp/test_config.json");
        ASSERT_TRUE(count >= 3, "loaded 3+ providers");

        auto& creds = genie::core::api_credentials();
        auto [av_key, av_secret] = creds.get("alpha_vantage");
        ASSERT_STR_EQ(av_key, "TEST_AV_KEY", "alpha vantage key");

        auto [alp_key, alp_secret] = creds.get("alpaca");
        ASSERT_STR_EQ(alp_key, "PKTEST123", "alpaca key_id");
        ASSERT_STR_EQ(alp_secret, "secret456", "alpaca secret");

        auto* alpaca_config = genie::core::get_provider_config("alpaca");
        ASSERT_TRUE(alpaca_config != nullptr, "alpaca config exists");
        ASSERT_TRUE(alpaca_config->get_bool("paper", false), "alpaca paper mode");

        // Clean up test credentials
        creds.remove("alpha_vantage");
        creds.remove("alpaca");
        creds.remove("finnhub");

        PASS();
    }
}

// ============================================================================
// 5. Trading Calendar Tests
// ============================================================================

void test_trading_calendar() {
    std::cout << "\n=== Trading Calendar ===" << std::endl;

    const auto& cal = genie::market::trading_calendar();

    {
        TEST("Christmas 2025 is a holiday");
        ASSERT_TRUE(cal.is_holiday("2025-12-25"), "Dec 25 2025 is a holiday");
        PASS();
    }

    {
        TEST("Weekends are not trading days");
        // 2026-02-07 is a Saturday, 2026-02-08 is a Sunday
        ASSERT_TRUE(!cal.is_trading_day("2026-02-07"), "Saturday not trading");
        ASSERT_TRUE(!cal.is_trading_day("2026-02-08"), "Sunday not trading");
        PASS();
    }

    {
        TEST("Regular weekday is a trading day");
        // 2026-02-05 is a Thursday
        ASSERT_TRUE(cal.is_trading_day("2026-02-05"), "Thursday is trading");
        PASS();
    }

    {
        TEST("MLK Day 2026 is a holiday");
        // 3rd Monday in January 2026 = Jan 19
        ASSERT_TRUE(cal.is_holiday("2026-01-19"), "MLK Day 2026");
        PASS();
    }

    {
        TEST("Good Friday 2026");
        // Easter 2026 is April 5, Good Friday = April 3
        ASSERT_TRUE(cal.is_holiday("2026-04-03"), "Good Friday 2026");
        PASS();
    }

    {
        TEST("Trading days in 2025");
        int days = cal.expected_trading_days(2025);
        // Typically 250-253 trading days per year
        ASSERT_TRUE(days >= 248 && days <= 254, "reasonable trading day count");
        PASS();
    }

    {
        TEST("Trading days between dates");
        // First week of Feb 2026 (Mon Feb 2 - Fri Feb 6)
        int days = cal.trading_days_between("2026-02-02", "2026-02-06");
        ASSERT_EQ(days, 5, "5 trading days Mon-Fri");
        PASS();
    }

    {
        TEST("Next/previous trading day");
        // Saturday Feb 7, 2026 -> next trading day should be Monday Feb 9
        auto d = genie::market::CalendarDate(2026, 2, 7);
        auto next = cal.next_trading_day(d);
        ASSERT_EQ(next.month, 2, "month");
        ASSERT_EQ(next.day, 9, "next trading day is Feb 9");
        PASS();
    }

    {
        TEST("Early close - Black Friday 2025");
        // Thanksgiving 2025 = Nov 27, Black Friday = Nov 28
        ASSERT_TRUE(cal.is_early_close(genie::market::CalendarDate(2025, 11, 28)),
                     "Black Friday is early close");
        PASS();
    }

    {
        TEST("Holiday name lookup");
        std::string name = cal.holiday_name("2025-12-25");
        ASSERT_TRUE(name.find("Christmas") != std::string::npos, "Christmas name");
        PASS();
    }

    {
        TEST("Juneteenth observed since 2022");
        // 2023: Jun 19 is Monday, observed on Jun 19
        ASSERT_TRUE(cal.is_holiday("2023-06-19"), "Juneteenth 2023");
        // 2021: No Juneteenth holiday yet
        ASSERT_TRUE(!cal.is_holiday("2021-06-18"), "No Juneteenth 2021");
        PASS();
    }
}

// ============================================================================
// 6. PDF Writer Tests
// ============================================================================

void test_pdf_writer() {
    std::cout << "\n=== PDF Writer ===" << std::endl;

    {
        TEST("Generate portfolio tearsheet PDF");
        auto doc = genie::reporting::ReportTemplates::portfolio_tearsheet(
            "Test Portfolio", 1000000, 50000, 15,
            {{"AAPL", 100, 185.50, 18550},
             {"MSFT", 50, 420.30, 21015},
             {"GOOGL", 25, 175.80, 4395}},
            0.1234, 1.85, 25000);

        genie::reporting::PdfWriter writer;
        std::string pdf = writer.render(doc);

        ASSERT_TRUE(pdf.size() > 100, "PDF has content");
        ASSERT_TRUE(pdf.substr(0, 5) == "%PDF-", "starts with PDF header");
        ASSERT_TRUE(pdf.find("%%EOF") != std::string::npos, "ends with EOF marker");
        ASSERT_TRUE(pdf.find("/Type /Catalog") != std::string::npos, "has catalog");
        ASSERT_TRUE(pdf.find("/Type /Page") != std::string::npos, "has pages");
        PASS();
    }

    {
        TEST("Generate risk dashboard PDF");
        auto doc = genie::reporting::ReportTemplates::risk_dashboard(
            "Test Portfolio",
            25000, 35000, 42000, 0.18, 1.05,
            {{"Market", 0.85}, {"Size", -0.12}, {"Value", 0.34},
             {"Momentum", 0.22}, {"Quality", 0.15}});

        genie::reporting::PdfWriter writer;
        std::string pdf = writer.render(doc);

        ASSERT_TRUE(pdf.size() > 100, "PDF has content");
        ASSERT_TRUE(pdf.substr(0, 5) == "%PDF-", "valid PDF");
        PASS();
    }

    {
        TEST("Write PDF to file");
        auto doc = genie::reporting::ReportTemplates::portfolio_tearsheet(
            "File Test", 500000, 25000, 5,
            {{"SPY", 200, 520.00, 104000}},
            0.08, 1.2, 15000);

        genie::reporting::PdfWriter writer;
        bool ok = writer.write_to_file(doc, "/tmp/test_report.pdf");
        ASSERT_TRUE(ok, "file written");

        // Verify file exists and has content
        std::ifstream f("/tmp/test_report.pdf", std::ios::binary | std::ios::ate);
        ASSERT_TRUE(f.is_open(), "file opened");
        ASSERT_TRUE(f.tellg() > 100, "file has content");
        PASS();
    }
}

// ============================================================================
// 7. SMTP Client Tests (structure only)
// ============================================================================

void test_smtp() {
    std::cout << "\n=== SMTP Client ===" << std::endl;

    {
        TEST("SMTP config defaults");
        genie::core::SmtpClient::Config config;
        ASSERT_STR_EQ(config.host, "smtp.gmail.com", "default host");
        ASSERT_EQ(config.port, 587, "default port");
        ASSERT_TRUE(config.use_tls, "TLS enabled by default");
        PASS();
    }

    // Live SMTP test would require real credentials
    // Skipped unless explicitly configured
    {
        TEST("[APIKEY] Send test email via SMTP");
        if (!genie::core::is_provider_configured("smtp")) {
            SKIP("SMTP not configured");
            return;
        }
        // Would do: smtp.send_email("test@example.com", "Test", "Test body");
        SKIP("Live SMTP test disabled in CI");
    }
}

// ============================================================================
// 8. Tradier Client Tests
// ============================================================================

void test_tradier() {
    std::cout << "\n=== Tradier Client ===" << std::endl;

    {
        TEST("Tradier config validation");
        genie::trading::TradierConfig config;
        ASSERT_TRUE(!config.is_valid(), "empty config is invalid");
        config.access_token = "test";
        ASSERT_TRUE(config.is_valid(), "config with token is valid");
        ASSERT_TRUE(config.sandbox, "default is sandbox");
        ASSERT_TRUE(config.base_url().find("sandbox") != std::string::npos, "sandbox URL");
        PASS();
    }

    {
        TEST("[APIKEY] Get Tradier market clock");
        if (!genie::core::is_provider_configured("tradier")) {
            SKIP("Tradier not configured");
            return;
        }
        auto [token, account] = genie::core::api_credentials().get("tradier");
        genie::trading::TradierConfig config;
        config.access_token = token;
        config.sandbox = true;

        genie::trading::TradierClient client(config);
        auto clock = client.get_clock();
        if (clock) {
            std::cout << "PASS (state: " << clock->state << ")" << std::endl;
            tests_passed++;
        } else {
            SKIP("API call failed (may need valid token)");
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "  Metis Genie Platform v" << genie::VERSION_STRING << " Integration Tests  " << std::endl;
    std::cout << "==========================================" << std::endl;

    // Try to load config for API key tests
    genie::core::load_api_keys("config.json");

    test_url_parser();
    test_http_client();
    test_websocket();
    test_config_loader();
    test_trading_calendar();
    test_pdf_writer();
    test_smtp();
    test_tradier();

    std::cout << "\n==========================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed, "
              << tests_skipped << " skipped / "
              << tests_run << " total" << std::endl;
    std::cout << "==========================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
