/**
 * @file genie.hpp
 * @brief Main header for Metis Genie Platform Emulator
 * @version 5.5.11
 * 
 * Comprehensive investment management platform emulation.
 * Zero external dependencies - pure C++20 implementation.
 * Cross-platform: Windows, Linux, macOS
 * 
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_HPP
#define GENIE_HPP

// Core modules
#include "core/version.hpp"
#include "core/types.hpp"
#include "core/math_utils.hpp"
#include "core/date_utils.hpp"
#include "core/events.hpp"
#include "core/random.hpp"
#include "core/logging.hpp"
#include "core/config.hpp"
#include "core/persistence.hpp"
#include "core/cli.hpp"
#include "core/validation.hpp"
#include "core/alerts.hpp"
#include "core/thread_pool.hpp"
#include "core/database.hpp"
#include "core/distributed_pool.hpp"
#include "core/user_manager.hpp"

// Persistence
#include "persistence/data_store.hpp"
#include "persistence/event_store.hpp"

// Risk (P1)
#include "risk/factor_model.hpp"
#include "risk/correlated_mc.hpp"

// Performance (P1)
#include "performance/multi_period_attribution.hpp"

// Portfolio (P1/P2)
#include "portfolio/ibor.hpp"
#include "portfolio/tax_optimization.hpp"

// Net (P2)
#include "net/auth_provider.hpp"
#include "net/ws_server.hpp"

// Reporting (P2)
#include "reporting/pdf_report.hpp"

// Trading (P2/P3)
#include "trading/settlement.hpp"
#include "trading/smart_router.hpp"

// Compliance (P3)
#include "compliance/regulatory.hpp"

// Market data
#include "market/security.hpp"
#include "market/market_data.hpp"
#include "market/trading_calendar.hpp"

// Core extensions
#include "core/api_config_loader.hpp"
#include "core/smtp_client.hpp"
#include "core/connection_pool.hpp"

// Reporting extensions
#include "reporting/pdf_writer.hpp"

// Trading extensions
#include "trading/tradier_client.hpp"

// Asset classes
#include "assets/equity.hpp"
#include "assets/fixed_income.hpp"
#include "assets/derivatives.hpp"
#include "assets/fx_commodity.hpp"

// Portfolio management
#include "portfolio/position.hpp"
#include "portfolio/portfolio.hpp"
#include "portfolio/portfolio_optimizer.hpp"
#include "portfolio/rebalancing.hpp"
#include "portfolio/tax_lots.hpp"

// Risk analytics
#include "risk/var_engine.hpp"
#include "risk/stress_testing.hpp"
#include "risk/risk_metrics.hpp"
#include "risk/currency_hedging.hpp"
#include "risk/scenario_analysis.hpp"

// Trading
#include "trading/order_management.hpp"
#include "trading/trade_blotter.hpp"

// Other modules
#include "compliance/compliance_engine.hpp"
#include "performance/performance_engine.hpp"
#include "reporting/reporting.hpp"
#include "analytics/backtesting.hpp"
#include "analytics/benchmark.hpp"
#include "analytics/timeseries.hpp"

// Network / REST API
#include "net/rest_api.hpp"
#include "net/sse_server.hpp"
#include "net/prometheus_endpoint.hpp"
#include "net/route_table.hpp"
#include "net/response_compression.hpp"
#include "net/http2_server.hpp"
#include "net/wasm_client.hpp"
#include "core/file_watcher.hpp"
#include "core/binary_serializer.hpp"
#include "ops/k8s_client.hpp"
#include "trading/fix_engine_v2.hpp"
#include "net/live_data_provider.hpp"
// NOTE: net/http_server.hpp is NOT included here to avoid pulling in
// platform socket headers transitively. Only main.cpp includes it directly.
// NOTE: core/pg_database.hpp requires libpq -- include explicitly when needed.
// NOTE: market/feed_handler.hpp uses platform sockets -- include explicitly.

// --- P0 Foundation (v3.2.0) ---
#include "trading/fix_engine.hpp"
#include "analytics/benchmark_builder.hpp"

// Prototype Transformation - TIER 1 (Trading System)
#include "trading/broker_interface.hpp"
#include "trading/trading_system.hpp"
#include "market/price_cache.hpp"
#include "market/price_store.hpp"

// Prototype Transformation - TIER 2 (Analytics & Data Quality)
#include "trading/enhanced_orders.hpp"
#include "analytics/real_risk.hpp"
#include "analytics/real_performance.hpp"
#include "market/data_quality.hpp"

// Prototype Transformation - TIER 3 (Enhanced Data)
#include "market/iex_cloud.hpp"
#include "market/fred_client.hpp"
#include "market/polygon_client.hpp"
#include "market/finnhub_client.hpp"
#include "market/sec_edgar.hpp"

// Prototype Transformation - TIER 3 (Multi-Broker)
#include "trading/broker_abstraction.hpp"

// Prototype Transformation - TIER 3 (Tax Tracking)
#include "tax/tax_tracking.hpp"

// Prototype Transformation - TIER 3 (Real Reports)
#include "reporting/real_reports.hpp"

// --- P4 Aspirational: Advanced Platform Features (v3.2.0) ---
#include "risk/gpu_monte_carlo.hpp"          // GPU-accelerated Monte Carlo VaR
#include "analytics/ml_alpha.hpp"            // ML-driven alpha signal generation
#include "core/nlq_engine.hpp"               // Natural Language Query engine
#include "core/multi_tenant.hpp"             // Multi-tenant isolation & routing
#include "market/bloomberg_adapter.hpp"      // Bloomberg B-PIPE/BLPAPI adapter
#include "market/reuters_adapter.hpp"        // Reuters/Refinitiv Elektron adapter
#include "compliance/esg_scoring.hpp"        // ESG scoring & integration
#include "assets/private_assets.hpp"         // Private equity/RE/infra engine
#include "trading/fix_certification.hpp"     // FIX protocol certification suite
#include "ux/whatif_scenario.hpp"            // What-if scenario builder
#include "ops/kubernetes_deploy.hpp"        // Kubernetes deployment & HPA scaling

// --- P5 Resilience & Observability (v3.4.0, v3.5.0) ---
#include "core/circuit_breaker.hpp"         // Circuit breaker pattern for external calls
#include "net/rate_limiter.hpp"             // Token bucket API rate limiting
#include "ops/telemetry.hpp"               // Prometheus-compatible metrics export
#include "core/data_export.hpp"            // CSV/JSON/XML data export framework
#include "core/input_sanitizer.hpp"        // SQL injection & XSS prevention
#include "core/request_context.hpp"        // Correlation ID request tracing

// --- v4.4.0 New Features ---
#include "trading/crypto_trading.hpp"      // Cryptocurrency trading & DeFi
#include "core/message_bus.hpp"            // Event-driven message bus
#include "persistence/db_migrations.hpp"   // Database migration framework
#include "net/openapi_export.hpp"          // OpenAPI 3.0 documentation export
#include "core/adr_registry.hpp"           // Architecture Decision Records

// --- v4.5.0 Improvements ---
#include "core/result.hpp"                 // Result<T,E> error handling
#include "core/json_logger.hpp"            // Structured JSON logging
#include "persistence/timeseries_store.hpp" // Time-series OHLCV storage
#include "portfolio/portfolio_snapshot.hpp" // Portfolio versioning & snapshots
#include "market/data_normalizer.hpp"      // Cross-provider data normalization
#include "compliance/pretrade_compliance.hpp" // Pre-trade compliance pipeline
#include "trading/order_throttle.hpp"      // Per-broker order rate throttling
#include "net/connection_resilience.hpp"   // Connection retry & circuit breaker
#include "net/error_handler.hpp"           // Structured error responses with trace IDs
#include "net/response_cache.hpp"          // API response cache with per-endpoint TTL
#include "net/static_server.hpp"           // Static file server for client HTML
#include "net/request_trace.hpp"         // Request ID generation and tracing
#include "core/config_validator.hpp"     // Configuration validation

// --- v4.6.0 Improvements ---
#include "core/config_manager.hpp"         // Configuration management & hot-reload
#include "core/metrics_collector.hpp"      // Prometheus-compatible metrics
#include "market/fx_engine.hpp"            // Multi-currency FX conversion
#include "analytics/factor_model.hpp"      // Multi-factor risk model
#include "security/api_key_manager.hpp"    // API key lifecycle management
#include "trading/execution_analytics.hpp" // Transaction Cost Analysis (TCA)
#include "reporting/scheduled_reports.hpp" // Automated scheduled reporting
#include "ops/health_dashboard.hpp"        // System health dashboard
#include "persistence/data_archiver.hpp"   // Data archival & lifecycle

// --- v4.7.0 Improvements ---
#include "tax/wash_sale_engine.hpp"          // IRS wash sale detection & adjustment
#include "risk/tail_risk.hpp"                // CVaR/Expected Shortfall/EVT analysis
#include "core/workflow_engine.hpp"          // DAG-based task orchestration
#include "portfolio/brinson_attribution.hpp" // Brinson-Fachler performance attribution
#include "compliance/compliance_calendar.hpp" // Regulatory deadline tracking
#include "persistence/data_reconciliation.hpp" // Cross-system data reconciliation
#include "trading/routing_strategy.hpp"      // Smart order routing strategies
#include "market/data_quality_rules.hpp"     // Market data quality validation

// --- v4.8.0 Completions: 8-Module Suite ---
#include "core/feature_flags.hpp"            // Feature flag toggle w/ rollout %
#include "core/data_pipeline.hpp"            // ETL stages w/ transform chains
#include "core/alert_manager.hpp"            // Threshold-based alerting & channels
#include "core/api_versioning.hpp"           // Backward-compatible API routing
#include "core/cache_manager.hpp"            // LRU/TTL cache w/ statistics

// --- v4.9.0 Improvements ---
#include "security/audit_trail.hpp"          // Immutable tamper-evident audit logging
#include "performance/benchmark_manager.hpp" // Index tracking & tracking error
#include "assets/instrument_master.hpp"      // Security master w/ identifiers
#include "security/session_manager.hpp"      // User session lifecycle & JWT tokens
#include "ops/task_scheduler.hpp"            // Cron-like task scheduling
#include "reporting/report_templates.hpp"    // Configurable report templates
#include "risk/stress_scenarios.hpp"         // Pre-built crisis stress scenarios

// --- v4.10.0 Improvements ---
#include "portfolio/rebalance_optimizer.hpp" // Target-weight rebalancing w/ tax awareness
#include "analytics/correlation_matrix.hpp"  // Rolling pairwise correlation & regime detection
#include "risk/liquidity_scorer.hpp"         // Multi-factor liquidity scoring & impact
#include "core/notification_router.hpp"      // Multi-channel notification dispatch
#include "trading/order_book_simulator.hpp"  // Level-2 order book depth-of-market
#include "portfolio/dividend_tracker.hpp"    // Ex-date tracking, yield, DRIP modeling
#include "risk/margin_calculator.hpp"        // Reg-T/portfolio margin & call detection

// --- v4.11.0 Improvements ---
#include "trading/position_sizer.hpp"       // Kelly/fractional/volatility position sizing
#include "analytics/strategy_backtester.hpp" // Historical strategy simulation engine
#include "trading/trade_journal.hpp"        // Trade logging with P&L attribution
#include "market/currency_hedger.hpp"       // FX hedge ratio & rolling forwards
#include "portfolio/fee_calculator.hpp"     // Management/performance/custody fees
#include "trading/settlement_engine.hpp"    // T+N settlement cycles & netting
#include "performance/peer_comparison.hpp"  // Percentile ranking & universe analysis
#include "assets/accrued_interest.hpp"      // Day count conventions & clean/dirty pricing

// --- v4.12.0 Improvements ---
#include "performance/performance_attribution.hpp" // Brinson-Fachler return decomposition
#include "trading/trade_allocator.hpp"      // Block trade allocation to sub-accounts
#include "portfolio/cash_manager.hpp"       // Cash position tracking, sweeps, projections
#include "performance/benchmark_tracker.hpp" // Benchmark return tracking & tracking error
#include "risk/risk_budgeter.hpp"           // Risk budget allocation & monitoring
#include "ops/corporate_actions.hpp"        // Dividend/split/merger/spinoff processing
#include "ops/approval_workflow.hpp"        // Multi-level approval with escalation
#include "persistence/data_quality_monitor.hpp" // Data validation & anomaly detection

namespace genie {

class GenieSystem {
private:
    market::MarketDataService market_data_;
    portfolio::PortfolioManager portfolio_manager_;
    portfolio::RebalancingEngine rebalancing_engine_;
    tax::TaxLotManager tax_lot_manager_;
    trading::OrderManagementSystem oms_;
    trading::TradeBlotter blotter_;
    compliance::ComplianceEngine compliance_engine_;
    risk::VaREngine var_engine_;
    risk::StressTestEngine stress_engine_;
    risk::MonteCarloEngine monte_carlo_engine_;
    risk::FactorRiskModel factor_model_;
    risk::CorrelationAnalyzer correlation_analyzer_;
    risk::RiskAnalyzer risk_analyzer_;
    risk::ExposureAnalyzer exposure_analyzer_;
    risk::LiquidityAnalyzer liquidity_analyzer_;
    performance::AttributionEngine attribution_engine_;
    portfolio::PortfolioOptimizer optimizer_;
    portfolio::PortfolioRebalancer rebalancer_;
    EventBus event_bus_;
    bool initialized_{false};

public:
    GenieSystem() = default;

    void initialize() {
        portfolio_manager_.set_market_data(&market_data_);
        oms_.set_market_data(&market_data_);
        oms_.set_portfolio_manager(&portfolio_manager_);
        oms_.set_event_bus(&event_bus_);
        var_engine_.set_market_data(&market_data_);
        stress_engine_.set_market_data(&market_data_);
        correlation_analyzer_.set_market_data(&market_data_);
        compliance_engine_.set_event_bus(&event_bus_);
        factor_model_ = risk::FactorRiskModel::create_equity_model();
        compliance_engine_.add_rule(compliance::RuleLibrary::single_position_limit(10.0));
        compliance_engine_.add_rule(compliance::RuleLibrary::max_leverage(1.5));
        compliance_engine_.add_rule(compliance::RuleLibrary::minimum_cash(2.0));
        initialized_ = true;
        EventData e(EventType::SystemStartup); e.source = "GenieSystem"; event_bus_.publish(e);
    }

    void shutdown() {
        EventData e(EventType::SystemShutdown); e.source = "GenieSystem"; event_bus_.publish(e);
        initialized_ = false;
    }

    market::MarketDataService& market_data() { return market_data_; }
    portfolio::PortfolioManager& portfolios() { return portfolio_manager_; }
    trading::OrderManagementSystem& oms() { return oms_; }
    compliance::ComplianceEngine& compliance() { return compliance_engine_; }
    risk::VaREngine& var_engine() { return var_engine_; }
    risk::StressTestEngine& stress_engine() { return stress_engine_; }
    risk::MonteCarloEngine& monte_carlo() { return monte_carlo_engine_; }
    risk::FactorRiskModel& factor_model() { return factor_model_; }
    risk::CorrelationAnalyzer& correlation_analyzer() { return correlation_analyzer_; }
    performance::AttributionEngine& attribution() { return attribution_engine_; }
    portfolio::PortfolioOptimizer& optimizer() { return optimizer_; }
    portfolio::PortfolioRebalancer& rebalancer() { return rebalancer_; }
    EventBus& events() { return event_bus_; }

    std::shared_ptr<portfolio::Portfolio> create_portfolio(const PortfolioId& id, const std::string& name, const Currency& ccy = "USD") {
        portfolio::PortfolioConfig cfg; cfg.id = id; cfg.name = name; cfg.base_currency = ccy;
        cfg.inception_date = std::chrono::system_clock::now();
        return portfolio_manager_.create_portfolio(cfg);
    }

    void add_security(market::SecurityPtr sec) { market_data_.add_security(std::move(sec)); }
    void update_price(const SecurityId& id, Price p) { market_data_.update_price(id, p); }

    risk::VaRResult calculate_var(const portfolio::Portfolio& p, VaRMethod m = VaRMethod::Historical) {
        risk::VaRConfig cfg; cfg.method = m; var_engine_.set_config(cfg); return var_engine_.calculate_var(p);
    }

    risk::SimulationResult run_monte_carlo(double price, double drift, double vol, double T = 1.0) {
        return monte_carlo_engine_.simulate_gbm(price, drift, vol, T);
    }

    std::vector<risk::StressTestResult> run_stress_tests(const portfolio::Portfolio& p) {
        return stress_engine_.run_all_scenarios(p);
    }

    compliance::ComplianceReport check_compliance(const portfolio::Portfolio& p) {
        return compliance_engine_.check_portfolio(p);
    }

    performance::PerformanceStats calculate_performance(const std::vector<double>& r, const std::vector<double>& b = {}) {
        return attribution_engine_.performance().calculate(r, b);
    }

    portfolio::OptimizationResult optimize_portfolio(const portfolio::OptimizationInputs& in) {
        return optimizer_.optimize(in);
    }

    [[nodiscard]] bool is_initialized() const { return initialized_; }

    [[nodiscard]] std::string status() const {
        std::ostringstream s;
        s << std::string(VERSION_FULL) << "\n"
          << "Status: " << (initialized_ ? "Running" : "Not Initialized") << "\n"
          << "Platform: " << std::string(PLATFORM) << "/" << std::string(COMPILER) << "\n"
          << "Securities: " << market_data_.store()->security_count() << "\n"
          << "Portfolios: " << portfolio_manager_.portfolio_count() << "\n"
          << "Total AUM: $" << std::fixed << std::setprecision(2) << portfolio_manager_.total_aum().amount << "\n";
        return s.str();
    }
};

inline GenieSystem& system() { static GenieSystem instance; return instance; }

} // namespace genie

#endif // GENIE_HPP
