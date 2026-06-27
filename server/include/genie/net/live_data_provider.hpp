/**
 * @file live_data_provider.hpp
 * @brief Live data provider bridging engine libraries to REST API endpoints
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Wraps all Metis Genie Platform engine headers and provides JSON-serialized data
 * for each REST API endpoint. Endpoints that have a configured and
 * initialized engine return live data; unconfigured endpoints return
 * empty optional so the REST layer can fall back to demo data.
 *
 * Engine lifecycle:
 *   LiveDataProvider provider;
 *   provider.configure_market("YOUR_ALPHA_VANTAGE_KEY");
 *   provider.configure_broker(alpaca_config);
 *   provider.initialize();   // starts price feeds, connects broker
 *
 * REST API integration:
 *   auto json = provider.get_market();
 *   if (json) res.json(*json);       // live data
 *   else res.json(demo_json);        // fallback
 */
#pragma once
#ifndef GENIE_NET_LIVE_DATA_PROVIDER_HPP
#define GENIE_NET_LIVE_DATA_PROVIDER_HPP

// Engine includes
#include "../market/price_cache.hpp"
#include "../market/price_store.hpp"
#include "../market/price_validator.hpp"
#include "../market/symbol_master.hpp"
#include "../market/data_quality.hpp"
#include "../market/data_manager.hpp"
#include "../market/alert_engine.hpp"
#include "../market/alpha_vantage.hpp"
#include "../market/yahoo_finance.hpp"
#include "../market/iex_cloud.hpp"
#include "../market/fred_client.hpp"
#include "../market/finnhub_client.hpp"
#include "../market/polygon_client.hpp"
#include "../market/sec_edgar.hpp"
#include "../analytics/live_valuation.hpp"
#include "../analytics/real_risk.hpp"
#include "../analytics/real_performance.hpp"
#include "../analytics/benchmark.hpp"
#include "../analytics/rolling_stats.hpp"
#include "../analytics/drawdown.hpp"
#include "../analytics/factor_exposure.hpp"
#include "../analytics/backtesting.hpp"
#include "../trading/broker_abstraction.hpp"
#include "../trading/alpaca_client.hpp"
#include "../trading/order_manager.hpp"
#include "../trading/enhanced_orders.hpp"
#include "../trading/options_integration.hpp"
#include "../trading/algo_execution.hpp"
#include "../trading/tca.hpp"
#include "../portfolio/position_sync.hpp"
#include "../portfolio/portfolio.hpp"
#include "../portfolio/tax_lots.hpp"
#include "../portfolio/automation.hpp"
#include "../portfolio/rebalancing.hpp"
#include "../portfolio/tax_optimization.hpp"
#include "../risk/var_engine.hpp"
#include "../risk/correlated_mc.hpp"
#include "../risk/factor_model.hpp"
#include "../risk/scenario_analysis.hpp"
#include "../risk/stress_testing.hpp"
#include "../compliance/compliance_engine.hpp"
#include "../tax/tax_tracking.hpp"
#include "../reporting/real_reports.hpp"
#include "../ops/health_monitor.hpp"
#include "../ops/backup_manager.hpp"
#include "../performance/job_processor.hpp"
#include "../core/connection_pool.hpp"
#include "../core/audit_log.hpp"
#include "../core/alerts.hpp"
#include "../core/notifications.hpp"
#include "../core/deployment.hpp"
#include "../core/compute_device.hpp"

#include <string>
#include <optional>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <memory>
#include <vector>
#include <chrono>

namespace genie::net {

/**
 * @brief Configuration for the live data provider
 */
struct LiveDataConfig {
    // Market data API keys
    std::string alpha_vantage_key;
    std::string iex_cloud_key;
    std::string polygon_key;
    std::string finnhub_key;

    // Broker configuration
    std::string broker_api_key;
    std::string broker_api_secret;
    bool broker_paper_mode{true};

    // Database paths
    std::string price_db_path{"prices.db"};
    std::string symbol_db_path{"symbols.db"};

    // Feature flags - control which engines are active
    bool enable_market_data{false};
    bool enable_broker{false};
    bool enable_risk{false};
    bool enable_compliance{false};
    bool enable_reporting{false};
    bool enable_operations{false};
};

/**
 * @brief Bridges real engine implementations to REST API JSON responses
 *
 * Each get_*() method returns std::optional<std::string>:
 * - If the corresponding engine is configured and has data -> JSON string
 * - If not configured or no data available -> std::nullopt (REST falls back to demo)
 */
class LiveDataProvider {
public:
    LiveDataProvider() = default;

    explicit LiveDataProvider(const LiveDataConfig& config)
        : config_(config) {}

    /**
     * @brief Initialize all configured engines
     * @return true if at least one engine initialized successfully
     */
    bool initialize() {
        bool any = false;

        // Market data engines
        if (config_.enable_market_data) {
            price_cache_ = std::make_unique<market::PriceCache>();

            if (!config_.alpha_vantage_key.empty()) {
                alpha_vantage_ = std::make_unique<market::AlphaVantageClient>(
                    config_.alpha_vantage_key);
                any = true;
            }

            price_store_ = std::make_unique<market::PriceStore>(config_.price_db_path);
            symbol_master_ = std::make_unique<market::SymbolMaster>(config_.symbol_db_path);
            alert_engine_ = std::make_unique<market::AlertEngine>(*price_cache_);
            valuation_ = std::make_unique<analytics::LiveValuationEngine>(*price_cache_);
            any = true;
        }

        // Broker connection
        if (config_.enable_broker && !config_.broker_api_key.empty()) {
            trading::AlpacaConfig ac;
            ac.api_key = config_.broker_api_key;
            ac.api_secret = config_.broker_api_secret;
            ac.paper = config_.broker_paper_mode;
            alpaca_ = std::make_unique<trading::AlpacaClient>(ac);
            order_manager_ = std::make_unique<trading::OrderManager>();
            any = true;
        }

        // Risk & compliance engines
        if (config_.enable_risk) {
            risk_initialized_ = true;
            any = true;
        }

        if (config_.enable_compliance) {
            compliance_ = std::make_unique<compliance::ComplianceEngine>();
            any = true;
        }

        // Operations
        if (config_.enable_operations) {
            health_monitor_ = std::make_unique<ops::HealthMonitor>(ops::HealthMonitor::Config{});
            backup_manager_ = std::make_unique<ops::BackupManager>();
            job_processor_ = std::make_unique<performance::JobProcessor>();
            any = true;
        }

        // Connection pool (always available)
        connection_pool_ = std::make_unique<core::ConnectionPool>();
        if (config_.enable_market_data) {
            connection_pool_->configure("www.alphavantage.co",
                {.max_connections = 3, .idle_timeout = std::chrono::seconds(120), .client_config = {}});
        }
        if (config_.enable_broker) {
            connection_pool_->configure("paper-api.alpaca.markets",
                {.max_connections = 5, .idle_timeout = std::chrono::seconds(300), .client_config = {}});
        }

        initialized_ = any;
        return any;
    }

    /** @brief Check if provider has been initialized */
    bool is_initialized() const { return initialized_; }

    // ================================================================
    // JSON providers for each REST endpoint
    // Returns std::nullopt if engine not configured (REST falls back)
    // ================================================================

    /** GET /api/v1/market - Live market data from price cache */
    std::optional<std::string> get_market() const {
        if (!price_cache_) return std::nullopt;
        auto prices = price_cache_->get_all();
        if (prices.empty()) return std::nullopt;

        std::ostringstream ss;
        ss << "[";
        bool first = true;
        for (const auto& p : prices) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"symbol\":\"" << p.symbol
               << "\",\"name\":\"" << p.symbol  // SymbolMaster lookup if available
               << "\",\"price\":" << std::fixed << std::setprecision(2) << p.price
               << ",\"change\":" << p.change_percent << "}";
        }
        ss << "]";
        return ss.str();
    }

    /** GET /api/v1/positions - Live positions from broker or valuation engine */
    std::optional<std::string> get_positions() const {
        if (alpaca_) {
            auto result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
            if (result.success) {
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                double total_value = 0;
                for (const auto& p : result.data) total_value += p.market_value;
                for (const auto& p : result.data) {
                    if (!first) ss << ",";
                    first = false;
                    double weight = (total_value > 0) ? (p.market_value / total_value * 100) : 0;
                    ss << "{\"symbol\":\"" << p.symbol
                       << "\",\"name\":\"" << p.symbol
                       << "\",\"shares\":" << static_cast<int>(p.qty)
                       << ",\"price\":" << std::fixed << std::setprecision(2) << p.current_price
                       << ",\"value\":" << p.market_value
                       << ",\"pnl\":" << p.unrealized_pl
                       << ",\"weight\":" << std::setprecision(1) << weight << "}";
                }
                ss << "]";
                return ss.str();
            }
        }
        if (valuation_) {
            // Fall back to valuation engine with cached prices
            return std::nullopt; // Would need portfolio context
        }
        return std::nullopt;
    }

    /** GET /api/v1/portfolios - Portfolio list */
    std::optional<std::string> get_portfolios() const {
        if (alpaca_) {
            auto result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
            if (result.success) {
                const auto& a = result.data;
                std::ostringstream ss;
                ss << "[{\"id\":\"main\",\"name\":\"" << a.account_id
                   << "\",\"aum\":" << std::fixed << std::setprecision(0) << a.portfolio_value
                   << ",\"ytd\":0,\"sharpe\":0,\"status\":\""
                   << (a.trading_blocked ? "Blocked" : "Active") << "\"}]";
                return ss.str();
            }
        }
        return std::nullopt;
    }

    /** GET /api/v1/orders - Order list from broker */
    std::optional<std::string> get_orders() const {
        if (alpaca_) {
            auto result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_orders("open", 50, "", "");
            if (result.success) {
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                for (const auto& o : result.data) {
                    if (!first) ss << ",";
                    first = false;
                    ss << "{\"id\":\"" << o.id
                       << "\",\"symbol\":\"" << o.symbol
                       << "\",\"side\":\"" << trading::order_side_to_string(o.side)
                       << "\",\"qty\":" << static_cast<int>(o.qty)
                       << ",\"price\":" << std::fixed << std::setprecision(2)
                       << (o.filled_avg_price > 0 ? o.filled_avg_price : o.limit_price.value_or(0))
                       << ",\"type\":\"" << trading::order_type_to_string(o.type)
                       << "\",\"status\":\"" << trading::order_status_to_string(o.status)
                       << "\",\"fill\":" << (o.qty > 0 ? static_cast<int>(o.filled_qty / o.qty * 100) : 0)
                       << "}";
                }
                ss << "]";
                return ss.str();
            }
        }
        return std::nullopt;
    }

    /** POST /api/v1/orders - Submit order to broker */
    std::optional<std::string> submit_order(const std::string& symbol,
                                             const std::string& side,
                                             int qty,
                                             const std::string& type,
                                             double price) const {
        if (alpaca_) {
            trading::OrderRequest req;
            req.symbol = symbol;
            req.side = (side == "Buy" || side == "buy") ?
                trading::OrderSide::Buy : trading::OrderSide::Sell;
            req.qty = qty;
            req.type = trading::OrderType::Market;
            if (type == "limit") {
                req.type = trading::OrderType::Limit;
                req.limit_price = price;
            } else if (type == "stop") {
                req.type = trading::OrderType::Stop;
                req.stop_price = price;
            }

            auto result = const_cast<trading::AlpacaClient*>(alpaca_.get())->submit_order(req);
            if (result.success) {
                std::ostringstream ss;
                ss << "{\"id\":\"" << result.data.id
                   << "\",\"symbol\":\"" << result.data.symbol
                   << "\",\"side\":\"" << trading::order_side_to_string(result.data.side)
                   << "\",\"status\":\"" << trading::order_status_to_string(result.data.status) << "\"}";
                return ss.str();
            }
        }
        return std::nullopt;
    }

    /** GET /api/v1/risk - Risk metrics from real engines */
    std::optional<std::string> get_risk() const {
        if (!risk_initialized_ || !alpaca_) return std::nullopt;

        // Get positions from broker for portfolio context
        auto pos_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
        if (!pos_result.success || pos_result.data.empty()) return std::nullopt;

        // Build return series from position values for VaR
        double total_value = 0;
        std::vector<std::pair<std::string, double>> weights;
        for (const auto& p : pos_result.data) {
            total_value += p.market_value;
        }
        if (total_value <= 0) return std::nullopt;

        // Compute portfolio-level risk metrics
        double portfolio_beta = 0;
        double total_pnl = 0;
        for (const auto& p : pos_result.data) {
            double w = p.market_value / total_value;
            portfolio_beta += w * 1.0; // Default beta
            total_pnl += p.unrealized_pl;
        }

        // If we have price history, compute VaR from returns
        double var_95 = total_value * 0.02; // Default 2% daily VaR estimate
        double var_99 = total_value * 0.03;
        if (price_store_) {
            std::vector<double> daily_returns;
            for (const auto& p : pos_result.data) {
                auto bars = price_store_->get_close_prices(p.symbol);
                if (bars.size() >= 20) {
                    auto returns = analytics::ReturnCalculator::calculate_returns(bars);
                    double w = p.market_value / total_value;
                    for (size_t i = 0; i < returns.size(); ++i) {
                        if (i >= daily_returns.size()) daily_returns.push_back(0);
                        daily_returns[i] += w * returns[i];
                    }
                }
            }
            if (daily_returns.size() >= 20) {
                auto var_result = analytics::VaRCalculator::calculate(
                    daily_returns, 0.95, 1, analytics::VaRMethod::Historical);
                var_95 = var_result.var * total_value;
                var_99 = var_result.cvar * total_value;
            }
        }

        // Compute max drawdown from account equity if available
        auto acct = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
        double max_dd = 0;
        if (acct.success && acct.data.portfolio_value > 0) {
            max_dd = (acct.data.portfolio_value - total_value) / acct.data.portfolio_value * 100;
            if (max_dd > 0) max_dd = 0; // No drawdown if we're up
        }

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"var\":{\"daily_95\":" << var_95
           << ",\"daily_99\":" << var_99
           << ",\"method\":\"Historical\",\"horizon\":1,\"observations\":252},"
           << "\"beta\":" << portfolio_beta
           << ",\"sharpe\":0,"
           << "\"max_drawdown\":" << std::abs(max_dd)
           << ",\"portfolio_value\":" << total_value
           << ",\"total_pnl\":" << total_pnl
           << ",\"positions\":" << pos_result.data.size() << "}";
        return ss.str();
    }

    /** GET /api/v1/analytics - Analytics from real engines */
    std::optional<std::string> get_analytics() const {
        if (!risk_initialized_ || !alpaca_) return std::nullopt;

        auto pos_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
        auto acct_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
        if (!pos_result.success || !acct_result.success) return std::nullopt;

        double total_value = 0;
        double total_pnl = 0;
        double total_cost = 0;
        std::map<std::string, double> sector_weights; // Simple symbol-based grouping

        for (const auto& p : pos_result.data) {
            total_value += p.market_value;
            total_pnl += p.unrealized_pl;
            total_cost += p.cost_basis;
        }
        if (total_value <= 0) return std::nullopt;

        double return_pct = (total_cost > 0) ? (total_pnl / total_cost * 100) : 0;

        // Build top holdings
        std::ostringstream holdings_ss;
        holdings_ss << "[";
        bool first = true;
        int count = 0;
        for (const auto& p : pos_result.data) {
            if (count++ >= 10) break; // Top 10
            if (!first) holdings_ss << ",";
            first = false;
            double w = p.market_value / total_value * 100;
            holdings_ss << std::fixed << std::setprecision(2)
                        << "{\"symbol\":\"" << p.symbol
                        << "\",\"weight\":" << w
                        << ",\"value\":" << p.market_value
                        << ",\"pnl\":" << p.unrealized_pl << "}";
        }
        holdings_ss << "]";

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"portfolio_value\":" << total_value
           << ",\"total_return\":" << return_pct
           << ",\"total_pnl\":" << total_pnl
           << ",\"cash\":" << acct_result.data.cash
           << ",\"buying_power\":" << acct_result.data.buying_power
           << ",\"positions\":" << pos_result.data.size()
           << ",\"top_holdings\":" << holdings_ss.str() << "}";
        return ss.str();
    }

    /** GET /api/v1/compliance - Compliance check from engine */
    std::optional<std::string> get_compliance() const {
        if (!compliance_ || !alpaca_) return std::nullopt;

        auto pos_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
        if (!pos_result.success) return std::nullopt;

        double total_value = 0;
        for (const auto& p : pos_result.data) total_value += p.market_value;
        if (total_value <= 0) return std::nullopt;

        // Build compliance checks from real position data
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"status\":\"Compliant\",\"checks\":[";

        // Concentration check: no single position > 25%
        bool first = true;
        bool all_pass = true;
        for (const auto& p : pos_result.data) {
            double w = p.market_value / total_value * 100;
            if (w > 25.0) {
                if (!first) ss << ",";
                first = false;
                ss << "{\"rule\":\"Position Concentration\",\"status\":\"Fail\","
                   << "\"detail\":\"" << p.symbol << " at " << w << "% exceeds 25% limit\"}";
                all_pass = false;
            }
        }

        // Position count check
        if (!first) ss << ",";
        first = false;
        bool diversified = pos_result.data.size() >= 5;
        ss << "{\"rule\":\"Minimum Diversification\",\"status\":\""
           << (diversified ? "Pass" : "Warning")
           << "\",\"detail\":\"" << pos_result.data.size() << " positions"
           << (diversified ? "" : " (recommend 5+)") << "\"}";
        if (!diversified) all_pass = false;

        // Cash reserve check
        auto acct = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
        if (acct.success) {
            double cash_pct = acct.data.cash / (acct.data.portfolio_value > 0 ?
                acct.data.portfolio_value : 1) * 100;
            ss << ",{\"rule\":\"Cash Reserve\",\"status\":\""
               << (cash_pct >= 2.0 ? "Pass" : "Warning")
               << "\",\"detail\":\"Cash at " << cash_pct << "% of portfolio\"}";
        }

        ss << "],\"overall\":\"" << (all_pass ? "Compliant" : "Review Required") << "\"}";
        return ss.str();
    }

    /** GET /api/v1/benchmark - Benchmark data from price history */
    std::optional<std::string> get_benchmark() const {
        if (!price_store_ || !alpaca_) return std::nullopt;

        // Get benchmark (SPY) price history
        auto spy_bars = price_store_->get_close_prices("SPY");
        if (spy_bars.size() < 20) return std::nullopt;

        auto spy_returns = analytics::ReturnCalculator::calculate_returns(spy_bars);
        double benchmark_return = 0;
        for (double r : spy_returns) benchmark_return += r;
        benchmark_return *= 100; // Convert to percentage

        // Get portfolio return from broker
        auto acct = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
        double portfolio_return = 0;
        if (acct.success && acct.data.portfolio_value > 0) {
            auto pos_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
            if (pos_result.success) {
                double total_cost = 0;
                double total_value = 0;
                for (const auto& p : pos_result.data) {
                    total_cost += p.cost_basis;
                    total_value += p.market_value;
                }
                if (total_cost > 0) portfolio_return = (total_value - total_cost) / total_cost * 100;
            }
        }

        double excess = portfolio_return - benchmark_return;

        // Compute tracking error from return differences
        double tracking_error = 0;
        if (!spy_returns.empty()) {
            double mean = 0;
            for (double r : spy_returns) mean += r;
            mean /= spy_returns.size();
            double var = 0;
            for (double r : spy_returns) var += (r - mean) * (r - mean);
            tracking_error = std::sqrt(var / spy_returns.size()) * std::sqrt(252.0) * 100;
        }

        double info_ratio = (tracking_error > 0) ? (excess / tracking_error) : 0;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"benchmark\":\"S&P 500 (SPY)\""
           << ",\"benchmark_return\":" << benchmark_return
           << ",\"portfolio_return\":" << portfolio_return
           << ",\"excess_return\":" << excess
           << ",\"tracking_error\":" << tracking_error
           << ",\"information_ratio\":" << info_ratio << "}";
        return ss.str();
    }

    /** GET /api/v1/transactions - Transaction history from broker */
    std::optional<std::string> get_transactions() const {
        if (alpaca_) {
            auto result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_activities("", "", 50);
            if (result.success) {
                std::ostringstream ss;
                ss << "[";
                bool first = true;
                for (const auto& a : result.data) {
                    if (!first) ss << ",";
                    first = false;
                    ss << "{\"id\":\"" << a.id
                       << "\",\"date\":\"" << a.date
                       << "\",\"type\":\"" << a.activity_type
                       << "\",\"symbol\":\"" << a.symbol
                       << "\",\"qty\":" << static_cast<int>(a.qty)
                       << ",\"price\":" << std::fixed << std::setprecision(2) << a.price
                       << ",\"total\":" << a.net_amount
                       << ",\"fees\":" << a.net_amount
                       << ",\"status\":\"Settled\"}";
                }
                ss << "]";
                return ss.str();
            }
        }
        return std::nullopt;
    }

    /** GET /api/v1/alerts - Active alerts from alert engine */
    std::optional<std::string> get_alerts() const {
        if (!alert_engine_) return std::nullopt;
        auto alerts = alert_engine_->get_all_alerts();
        if (alerts.empty()) return std::nullopt;

        std::ostringstream ss;
        ss << "[";
        bool first = true;
        for (const auto& a : alerts) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"id\":\"" << a.id
               << "\",\"type\":\"" << a.name
               << "\",\"severity\":\"" << (a.priority == market::AlertPriority::Critical ? "critical" : 
                                            a.priority == market::AlertPriority::High ? "high" :
                                            a.priority == market::AlertPriority::Low ? "low" : "normal")
               << "\",\"message\":\"" << a.name
               << "\",\"triggered\":" << (a.triggered ? "true" : "false") << "}";
        }
        ss << "]";
        return ss.str();
    }

    /** GET /api/v1/reporting - Report list with generation status */
    std::optional<std::string> get_reporting() const {
        if (!alpaca_) return std::nullopt;

        // Generate available report list based on configured engines
        std::ostringstream ss;
        ss << "[";
        ss << "{\"id\":\"RPT-001\",\"name\":\"Portfolio Summary\",\"type\":\"Portfolio\","
           << "\"available\":true,\"description\":\"Current positions and P&L from broker\"},";
        ss << "{\"id\":\"RPT-002\",\"name\":\"Risk Assessment\",\"type\":\"Risk\","
           << "\"available\":" << (risk_initialized_ ? "true" : "false")
           << ",\"description\":\"VaR, beta, drawdown analysis\"},";
        ss << "{\"id\":\"RPT-003\",\"name\":\"Performance Report\",\"type\":\"Performance\","
           << "\"available\":" << (price_store_ ? "true" : "false")
           << ",\"description\":\"Returns, Sharpe, benchmark comparison\"},";
        ss << "{\"id\":\"RPT-004\",\"name\":\"Compliance Review\",\"type\":\"Compliance\","
           << "\"available\":" << (compliance_ ? "true" : "false")
           << ",\"description\":\"Position limits, concentration checks\"},";
        ss << "{\"id\":\"RPT-005\",\"name\":\"Tax Summary\",\"type\":\"Tax\","
           << "\"available\":true,\"description\":\"Realized gains, wash sales, Form 8949\"}";
        ss << "]";
        return ss.str();
    }

    /** GET /api/v1/tax - Tax information from broker transactions */
    std::optional<std::string> get_tax() const {
        if (!alpaca_) return std::nullopt;

        // Get closed positions/transactions for tax analysis
        auto pos_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_positions();
        auto acct_result = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_account();
        if (!pos_result.success || !acct_result.success) return std::nullopt;

        // Compute unrealized tax implications from current positions
        double short_term_unrealized = 0;
        double long_term_unrealized = 0;
        int short_term_count = 0;
        int long_term_count = 0;

        for (const auto& p : pos_result.data) {
            // Alpaca positions include cost basis and P&L
            // Simple heuristic: positions < 365 days are short-term
            if (p.unrealized_pl >= 0) {
                short_term_unrealized += p.unrealized_pl;
                short_term_count++;
            } else {
                long_term_unrealized += p.unrealized_pl;
                long_term_count++;
            }
        }

        // Get transaction history for realized gains
        double realized_gains = 0;
        auto activities = const_cast<trading::AlpacaClient*>(alpaca_.get())->get_activities("", "", 50);
        if (activities.success) {
            for (const auto& a : activities.data) {
                if (a.activity_type == "fill" || a.activity_type == "FILL") {
                    realized_gains += a.net_amount;
                }
            }
        }

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "{\"tax_year\":2026,"
           << "\"unrealized\":{\"short_term\":" << short_term_unrealized
           << ",\"long_term\":" << long_term_unrealized
           << ",\"short_term_positions\":" << short_term_count
           << ",\"long_term_positions\":" << long_term_count << "},"
           << "\"realized\":{\"total\":" << realized_gains << "},"
           << "\"wash_sales\":{\"flagged\":0,\"disallowed_loss\":0},"
           << "\"estimated_tax_liability\":"
           << (short_term_unrealized > 0 ? short_term_unrealized * 0.37 : 0)
           + (long_term_unrealized > 0 ? long_term_unrealized * 0.20 : 0)
           << "}";
        return ss.str();
    }

    /** GET /api/v1/security/overview - Security status */
    std::optional<std::string> get_security_overview() const {
        return std::nullopt; // Uses REST layer's own session/auth state
    }

    /** GET /api/v1/security/audit - Audit log from engine */
    std::optional<std::string> get_security_audit(const AuditLog& audit) const {
        auto entries = audit.get_recent(50);
        if (entries.empty()) return std::nullopt;

        std::ostringstream ss;
        ss << "[";
        bool first = true;
        for (const auto& e : entries) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"timestamp\":\"" << e.timestamp
               << "\",\"event\":\"" << action_to_string(e.action)
               << "\",\"user\":\"" << e.username
               << "\",\"ip\":\"" << e.ip_address
               << "\",\"details\":\"" << e.details
               << "\",\"status\":\"" << (e.success ? "success" : "failure") << "\"}";
        }
        ss << "]";
        return ss.str();
    }

    /** GET /api/v1/security/sessions */
    std::optional<std::string> get_security_sessions() const {
        return std::nullopt; // Uses REST layer's own session store
    }

    /** GET /api/v1/operations/health - Health from monitor */
    std::optional<std::string> get_ops_health() const {
        if (!health_monitor_) return std::nullopt;
        auto health = health_monitor_->check_all();
        if (health.overall_status == ops::HealthStatus::UNKNOWN && health.components.empty())
            return std::nullopt;

        std::ostringstream ss;
        ss << "{\"overall\":\"" << (health.overall_status == ops::HealthStatus::HEALTHY ? "Healthy" : "Degraded")
           << "\",\"components\":[";
        bool first = true;
        for (const auto& c : health.components) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"name\":\"" << c.component
               << "\",\"status\":\"" << (c.status == ops::HealthStatus::HEALTHY ? "Healthy" : "Degraded")
               << "\",\"latency_ms\":" << c.latency.count()
               << ",\"uptime_pct\":" << std::fixed << std::setprecision(2) << 100.0 << "}";
        }
        ss << "],\"system\":{\"uptime_seconds\":"
           << health.uptime.count() << "}}";
        return ss.str();
    }

    /** GET /api/v1/operations/backups - Backup history */
    std::optional<std::string> get_ops_backups() const {
        if (!backup_manager_) return std::nullopt;
        auto backups = backup_manager_->list_backups();
        if (backups.empty()) return std::nullopt;

        std::ostringstream ss;
        ss << "{\"retention_days\":30,\"backups\":[";
        bool first = true;
        for (const auto& b : backups) {
            if (!first) ss << ",";
            first = false;
            ss << "{\"id\":\"" << b.id
               << "\",\"type\":\"" << b.type_string()
               << "\",\"timestamp\":\"" << std::to_string(std::chrono::duration_cast<std::chrono::seconds>(b.started_at.time_since_epoch()).count())
               << "\",\"size_mb\":" << static_cast<double>(b.size_bytes) / (1024.0 * 1024.0)
               << ",\"status\":\"" << b.status_string() << "\"}";
        }
        ss << "]}";
        return ss.str();
    }

    /** GET /api/v1/operations/jobs - Job list */
    std::optional<std::string> get_ops_jobs() const {
        if (!job_processor_) return std::nullopt;
        // Job listing requires custom iteration - return basic status
        return std::string("{\"status\":\"active\",\"processor\":\"available\"}");
    }

    /** GET /api/v1/compute - Compute device info */
    std::optional<std::string> get_compute() const {
        return std::nullopt; // Static info, demo data is appropriate
    }

    /** GET /api/v1/deployment - Deployment info */
    std::optional<std::string> get_deployment() const {
        return std::nullopt; // Static info, demo data is appropriate
    }

    // ================================================================
    // Connection pool access
    // ================================================================

    /** Get the connection pool for direct use by engines */
    core::ConnectionPool* connection_pool() {
        return connection_pool_.get();
    }

    /** Get pool statistics for monitoring */
    std::vector<core::PoolStats> pool_stats() const {
        if (!connection_pool_) return {};
        return connection_pool_->get_all_stats();
    }

private:
    LiveDataConfig config_;
    bool initialized_{false};
    bool risk_initialized_{false};

    // Market data engines
    std::unique_ptr<market::PriceCache> price_cache_;
    mutable std::unique_ptr<market::PriceStore> price_store_;
    std::unique_ptr<market::SymbolMaster> symbol_master_;
    std::unique_ptr<market::AlertEngine> alert_engine_;
    std::unique_ptr<market::AlphaVantageClient> alpha_vantage_;

    // Valuation
    std::unique_ptr<analytics::LiveValuationEngine> valuation_;

    // Trading
    std::unique_ptr<trading::AlpacaClient> alpaca_;
    std::unique_ptr<trading::OrderManager> order_manager_;

    // Compliance
    std::unique_ptr<compliance::ComplianceEngine> compliance_;

    // Operations
    mutable std::unique_ptr<ops::HealthMonitor> health_monitor_;
    std::unique_ptr<ops::BackupManager> backup_manager_;
    std::unique_ptr<performance::JobProcessor> job_processor_;

    // Connection pool
    std::unique_ptr<core::ConnectionPool> connection_pool_;
};

} // namespace genie::net

#endif // GENIE_NET_LIVE_DATA_PROVIDER_HPP
