/**
 * @file version.hpp
 * @brief Version information for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Version History:
 * - 5.5.11: Added a thin root CMakeLists.txt aggregator so the whole repository
 *           (server/ + client/ + docs/) opens as one CMake project in CLion via
 *           add_subdirectory(server); server/ still builds standalone. Fixed the
 *           docs install path (server/CMakeLists referenced a non-existent
 *           server/docs/; now ../docs/). Runtime architecture unchanged --
 *           client still talks to the server only over REST.
 * - 5.5.10: Repackaging + version-reference consistency pass. Normalized all
 *           distribution file timestamps to a single value (fixes a Ninja
 *           "manifest still dirty" reconfigure loop caused by configure-time
 *           inputs carrying a newer mtime than the rest of the tree). Swept all
 *           per-file @version tags, client cache-bust/version stamps, and doc
 *           distribution footers to the current release so every version
 *           reference matches VERSION.txt. No functional code changes vs 5.5.9.
 * - 5.5.9: Test/auth fixes. Convenience handle(method,path,body) overload now
 *          defaults Content-Type to application/json when a body is supplied
 *          without one (fixes 415 cascade in programmatic/test callers; wire
 *          path unaffected). Response cache no longer serves cached protected
 *          content to a token whose session has ended (post-logout/expiry now
 *          correctly returns 401). Removed hardcoded version literals: CLI
 *          version command, main.cpp self-test, and test banners/assertions
 *          now derive from VERSION_STRING (single source of truth).
 * - 5.3.1: PSON configuration (all parameters in config.pson), response caching
 *          wired into RestApi (ApiCache with per-endpoint TTL), request validation
 *          middleware (JSON body + Content-Type checks), structured error responses
 *          (ErrorHandler with trace IDs wired throughout), graceful shutdown with
 *          in-flight drain (GracefulShutdown integrated), GPU/K8s/Container stubs
 *          with documented C++20 implementation roadmap, directory restructure
 *          (server/ for CLion, client/ for WebStorm), re-crafted BACKGROUND.md,
 *          GitHub-ready README.md and .gitignore, all docs updated to v5.3.1.
 * - 4.13.0: Performance attribution (Brinson-Fachler, factor, FI), trade
 *           allocator (pro-rata/equal/cash-directed/round-robin), cash manager
 *           (sweeps, projections, drag analysis), benchmark tracker (TE, IR,
 *           active share, style drift), risk budgeter (allocation, efficiency,
 *           marginal contributions), corporate actions (dividend/split/merger/
 *           spinoff processing), approval workflow (multi-level chains), data
 *           quality monitor (validation, anomaly detection, quality scoring).
 * - 4.11.0: Position sizer (Kelly/fractional/vol-based), strategy backtester
 *           (event-driven simulation, Sharpe/Sortino/Calmar), trade journal (P&L
 *           attribution, grading), currency hedger (FX hedge optimization), fee
 *           calculator (mgmt/perf/custody), settlement engine (T+N, netting),
 *           peer comparison (percentile ranking), accrued interest (day count).
 * - 4.10.0: Rebalance optimizer (tax-aware, drift-band, turnover-constrained),
 *           correlation matrix (rolling pairwise, regime detection), liquidity
 *           scorer (bid-ask/volume/impact scoring), notification router (multi-
 *           channel dispatch), order book simulator, dividend tracker (yield/DRIP),
 *           margin calculator (Reg-T/portfolio/day-trading). 4 new + 4 wired.
 * - 4.9.0: Immutable audit trail (security), benchmark manager (performance),
 *          instrument master (assets), session manager (security), task scheduler
 *          (ops), report templates (reporting), stress scenarios (risk). Seven new
 *          modules totaling ~3,700 lines strengthening thin modules.
 * - 4.8.0: Completed 8-module enterprise suite: feature flags with rollout
 *          percentages, ETL data pipeline with transform chains, alert manager
 *          with threshold-based notification channels, backward-compatible API
 *          versioning, LRU/TTL cache manager with statistics. Wired all 8
 *          modules into genie.hpp (config_manager, metrics_collector, fx_engine
 *          already included; added feature_flags, data_pipeline, alert_manager,
 *          api_versioning, cache_manager).
 * - 4.7.0: Wash sale engine (IRS 30-day rule), tail risk (CVaR/ES/EVT/Cornish-
 *          Fisher), DAG workflow orchestration, Brinson-Fachler attribution,
 *          compliance calendar (SEC/FINRA/IRS deadlines), cross-system data
 *          reconciliation, smart order routing strategies, market data quality
 *          rules. 8 improvement modules totaling ~4,500 lines of new code.
 * - 4.6.0: Configuration management with hot-reload and validation, Prometheus-
 *          compatible metrics (counters/gauges/histograms/summaries), multi-currency
 *          FX engine with cross-rate triangulation, multi-factor risk model (Fama-
 *          French 5-factor + custom), API key lifecycle management, Transaction
 *          Cost Analysis (TCA) with broker scorecards, automated scheduled reports,
 *          system health dashboard with SLA tracking, data archival with tiered
 *          retention policies. 9 new modules totaling ~4,500 lines.
 * - 4.5.0: Structured error handling (Result<T,E>), JSON structured logging,
 *          time-series OHLCV store, portfolio snapshot versioning, market data
 *          normalizer (cross-provider), pre-trade compliance pipeline, per-broker
 *          order rate throttling, connection resilience with exponential backoff.
 *          8 improvement modules totaling ~4,000 lines of new code.
 * - 4.4.0: Cryptocurrency trading module (exchanges, wallets, staking, DeFi,
 *          bridge transfers, Travel Rule compliance). Event-driven message bus
 *          with pub/sub, dead letter queue, and replay. Database migration
 *          framework with schema builder, rollback, and seed data. OpenAPI 3.0
 *          specification generator with Swagger UI export. Architecture Decision
 *          Records (ADR) registry. Comprehensive compilation fix pass resolving
 *          struct redefinitions, template specializations, namespace conflicts.
 *          Cross-platform compatibility improvements for MinGW/GCC/Clang.
 * - 4.3.0: Version synchronization and documentation refresh across all modules.
 * - 4.2.0: Full client-server feature parity - new portfolio, benchmarks,
 *          alerts, compute/GPU, deployment, admin, NLQ, and export HTML pages.
 *          GPU compute abstraction for CUDA/Metal/Vulkan/OpenCL acceleration.
 *          Kubernetes and container deployment readiness (future C++20 impl).
 *          Re-crafted BACKGROUND.md with comprehensive TOC. All documentation
 *          verified, updated, and version-synchronized. Custom test framework
 *          enhancements. Cross-platform build improvements for Windows/Linux/macOS.
 * - 4.2.0: Complete client-server API coverage for all endpoints,
 *          GPU/K8s/Container future-ready architecture, re-crafted documentation,
 *          recommended improvements implemented (resilience, data validation,
 *          portfolio automation, API rate limiting, liquidity risk)
 * - 3.5.0: Full client-server feature parity - REST endpoints for all modules,
 *           new client pages (risk, whatif, market-feeds), ESG, ML Alpha, NLQ,
 *           FIX, Private Assets, Telemetry UI coverage, BACKGROUND.md rewrite
 * - 3.4.0: Resilience & Observability - Circuit breaker, rate limiting,
 *           telemetry, data export, input sanitization, request tracing
 * - 3.3.1: Bug fixes and Windows macro conflict resolution
 * - 3.0.0: Full client-server parity, GPU/K8s/Container architecture,
 *           expanded REST API (analytics, compliance, reporting, tax,
 *           security, operations, performance, compute endpoints),
 *           comprehensive documentation overhaul
 * - 2.24.0: TIER 5 Complete - Polish: UX, Security, Performance, Operations
 * - 2.23.0: TIER 4 Complete - Advanced Trading, Options Integration, Automation, Notifications
 * - 2.20.0: TIER 3 Complete - Enhanced Data Clients, All Brokers, Tax Tracking, Full Reporting
 * - 2.19.0: TIER 2 Complete - Enhanced Orders, Real Risk/Performance Analytics, Data Quality
 * - 2.18.0: TIER 1 Complete - Trading System, Broker Integration, Market Data, Position Sync
 */
#pragma once
#ifndef GENIE_VERSION_HPP
#define GENIE_VERSION_HPP
#include <string>
#include <string_view>

namespace genie {

inline constexpr int VERSION_MAJOR = 5;
inline constexpr int VERSION_MINOR = 5;
inline constexpr int VERSION_PATCH = 11;
inline constexpr std::string_view VERSION_STRING = "5.5.11";
inline constexpr std::string_view VERSION_FULL = "Metis Genie Platform v5.5.11";
inline constexpr std::string_view PROJECT_NAME = "Metis Genie Platform";

#if defined(_WIN32) || defined(_WIN64)
    inline constexpr std::string_view PLATFORM = "Windows";
    #define GENIE_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    inline constexpr std::string_view PLATFORM = "macOS";
    #define GENIE_PLATFORM_MACOS 1
#elif defined(__linux__)
    inline constexpr std::string_view PLATFORM = "Linux";
    #define GENIE_PLATFORM_LINUX 1
#else
    inline constexpr std::string_view PLATFORM = "Unknown";
    #define GENIE_PLATFORM_UNKNOWN 1
#endif

#if defined(__clang__)
    inline constexpr std::string_view COMPILER = "Clang";
#elif defined(__GNUC__)
    inline constexpr std::string_view COMPILER = "GCC";
#elif defined(_MSC_VER)
    inline constexpr std::string_view COMPILER = "MSVC";
#else
    inline constexpr std::string_view COMPILER = "Unknown";
#endif

inline std::string version_full() {
    return std::string(VERSION_FULL) + " [" + std::string(PLATFORM) + "/" + std::string(COMPILER) + "]";
}

} // namespace genie

#endif // GENIE_VERSION_HPP
