/**
 * @file example_portfolio.cpp
 * @brief Portfolio management, risk analysis, and trading examples
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Demonstrates: Portfolio creation, position management, risk metrics,
 * options pricing, backtesting, scenario analysis, compliance monitoring.
 *
 * Build: g++ -std=c++20 -O2 -I include -o example_portfolio examples/example_portfolio.cpp -pthread
 * Run:   ./example_portfolio
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
              << "\n  Metis Genie Platform v" << VERSION_STRING << " - Portfolio Management Example"
              << "\n" << std::string(60, '=') << "\n";

    // ---- 1. System Initialization ----
    print_header("1. System Initialization");
    GenieSystem sys;
    sys.initialize();
    std::cout << "  " << sys.status() << "\n";

    // ---- 2. Create Securities ----
    print_header("2. Securities Universe");
    auto aapl = assets::create_common_stock("AAPL", "Apple Inc", "AAPL", "NASDAQ");
    auto msft = assets::create_common_stock("MSFT", "Microsoft Corp", "MSFT", "NASDAQ");
    auto googl = assets::create_common_stock("GOOGL", "Alphabet Inc", "GOOGL", "NASDAQ");
    auto nvda = assets::create_common_stock("NVDA", "NVIDIA Corp", "NVDA", "NASDAQ");
    auto spy = assets::create_etf("SPY", "SPDR S&P 500", "SPY");
    auto ust = assets::create_treasury("UST10Y", "10-Year Treasury", 0.04, 0.042, 10,
                                        date_utils::from_ymd(2035, 1, 1));
    auto gold = assets::gold();
    auto eurusd = assets::create_fx_pair("EUR", "USD");
    eurusd->update_spot(1.0850, 0.005);

    std::cout << "  Created 8 securities:\n"
              << "    Equities: AAPL, MSFT, GOOGL, NVDA\n"
              << "    ETF: SPY\n"
              << "    Fixed Income: UST10Y (duration=" << std::fixed << std::setprecision(2)
              << ust->duration() << "y)\n"
              << "    Commodity: Gold ($" << gold->spot_price() << ")\n"
              << "    FX: EUR/USD " << eurusd->spot() << "\n";

    // ---- 3. Market Data ----
    print_header("3. Market Data Service");
    market::MarketDataService mds;
    mds.add_security(aapl); mds.update_price("AAPL", 178.25);
    mds.add_security(msft); mds.update_price("MSFT", 415.80);
    mds.add_security(googl); mds.update_price("GOOGL", 175.50);
    mds.add_security(nvda); mds.update_price("NVDA", 875.30);
    mds.add_security(spy); mds.update_price("SPY", 584.20);

    std::cout << "  AAPL: $" << mds.get_price("AAPL") << "\n"
              << "  MSFT: $" << mds.get_price("MSFT") << "\n"
              << "  NVDA: $" << mds.get_price("NVDA") << "\n";

    // ---- 4. Portfolio Construction ----
    print_header("4. Portfolio Construction");
    portfolio::PortfolioConfig cfg;
    cfg.id = "GROWTH-001"; cfg.name = "Growth Fund";
    portfolio::Portfolio port(cfg);
    port.deposit_cash(Money(1000000, "USD"));

    port.open_position("AAPL", 200, 178.25);
    port.open_position("MSFT", 100, 415.80);
    port.open_position("GOOGL", 150, 175.50);
    port.open_position("NVDA", 50, 875.30);

    std::cout << "  Portfolio: " << cfg.name << " (" << cfg.id << ")\n"
              << "  Initial Cash: $1,000,000\n"
              << "  Positions: " << port.position_count() << "\n"
              << "  Cash Remaining: $" << std::fixed << std::setprecision(2)
              << port.cash_balance().amount << "\n";

    // ---- 5. Options Pricing ----
    print_header("5. Options Greeks");
    auto call = assets::create_call_option("AAPL-C-200", "AAPL", 200.0, date_utils::from_ymd(2026, 12, 31));
    auto put = assets::create_put_option("AAPL-P-150", "AAPL", 150.0, date_utils::from_ymd(2026, 12, 31));

    call->update_market_data(178.25, 0.28, 0.92, 0.05);
    put->update_market_data(178.25, 0.28, 0.92, 0.05);

    std::cout << "  AAPL Call (strike=$200):\n"
              << "    Delta: " << std::setprecision(4) << call->delta() << "\n"
              << "    Gamma: " << call->gamma() << "\n"
              << "    Vega:  " << call->vega() << "\n\n"
              << "  AAPL Put (strike=$150):\n"
              << "    Delta: " << put->delta() << "\n"
              << "    Gamma: " << put->gamma() << "\n";

    // ---- 6. Risk Analysis ----
    print_header("6. Risk Metrics");
    std::vector<double> daily_returns = {
        0.012, -0.008, 0.015, -0.003, 0.007, -0.011, 0.009, 0.002,
        -0.005, 0.018, -0.014, 0.006, -0.001, 0.013, -0.009, 0.004,
        0.011, -0.007, 0.003, -0.016, 0.008, -0.002, 0.010, -0.006
    };
    risk::RiskAnalyzer ra;
    auto metrics = ra.analyze(daily_returns);
    std::cout << "  Annualized Volatility: " << std::setprecision(2) << (metrics.volatility * 100) << "%\n"
              << "  Sharpe Ratio: " << metrics.sharpe_ratio << "\n"
              << "  Sortino Ratio: " << metrics.sortino_ratio << "\n"
              << "  Max Drawdown: " << (metrics.max_drawdown * 100) << "%\n"
              << "  Calmar Ratio: " << metrics.calmar_ratio << "\n";

    // ---- 7. Monte Carlo VaR ----
    print_header("7. Monte Carlo Simulation");
    risk::MonteCarloEngine mc;
    mc.set_simulations(5000);
    auto sim = mc.simulate_gbm(100, 0.10, 0.20, 1.0);
    std::cout << "  Simulations: 5,000\n"
              << "  Mean Terminal Price: $" << std::setprecision(2) << sim.mean_terminal_price << "\n"
              << "  Std Terminal Price: $" << sim.std_terminal_price << "\n"
              << "  Mean Return: " << (sim.mean_return * 100) << "%\n";

    // ---- 8. Stress Testing ----
    print_header("8. Stress Scenarios");
    auto crash = risk::StressScenarioLibrary::market_crash_2008();
    auto covid = risk::StressScenarioLibrary::interest_rate_rise();
    std::cout << "  2008 Financial Crisis:\n"
              << "    Equity Shock: " << (crash.equity_shock * 100) << "%\n"
              << "    Rate Shock: " << (crash.rate_shock * 100) << "bp\n\n"
              << "  Interest Rate Shock +200bp:\n"
              << "    Equity Shock: " << (covid.equity_shock * 100) << "%\n"
              << "    Rate Shock: " << (covid.rate_shock * 100) << "bp\n";

    // ---- 9. Scenario Analysis ----
    print_header("9. Scenario Analysis");
    scenario::ScenarioEngine se;
    se.add_scenario(scenario::ScenarioEngine::equity_crash_10pct());
    se.add_scenario(scenario::ScenarioEngine::rate_hike_100bps());
    se.add_scenario(scenario::ScenarioEngine::stagflation());
    std::cout << "  Registered Scenarios:\n"
              << "    Equity Crash 10%: " << (se.has_scenario("EQUITY_CRASH_10") ? "YES" : "NO") << "\n"
              << "    Rate Hike 100bp: " << (se.has_scenario("RATE_HIKE_100") ? "YES" : "NO") << "\n"
              << "    Stagflation: " << (se.has_scenario("STAGFLATION") ? "YES" : "NO") << "\n";

    // ---- 10. Currency Hedging ----
    print_header("10. Currency Hedging");
    fx::CurrencyHedgingEngine hedge;
    hedge.set_spot_rate("EUR", 1.0850);
    hedge.set_spot_rate("GBP", 1.2650);
    hedge.set_spot_rate("JPY", 0.0067);
    std::cout << "  FX Spot Rates:\n"
              << "    EUR/USD: " << std::setprecision(4) << hedge.get_spot("EUR") << "\n"
              << "    GBP/USD: " << hedge.get_spot("GBP") << "\n"
              << "    JPY/USD: " << hedge.get_spot("JPY") << "\n";

    // ---- 11. Compliance ----
    print_header("11. Compliance Rules");
    compliance::ComplianceEngine ce;
    ce.add_rule(compliance::RuleLibrary::single_position_limit(10));
    std::cout << "  Rules loaded: " << ce.rule_count() << "\n"
              << "    Single Position Limit: 10%\n";

    // ---- 12. Order Management ----
    print_header("12. Order Management & Execution");
    trading::OrderManagementSystem oms;
    oms.set_market_data(&mds);

    trading::Order buy_aapl;
    buy_aapl.security_id = "AAPL"; buy_aapl.side = OrderSide::Buy;
    buy_aapl.quantity = 50; buy_aapl.type = OrderType::Market;
    auto result = oms.submit_order(buy_aapl);
    std::cout << "  Buy 50 AAPL (Market): " << (result.status == OrderStatus::Filled ? "FILLED" : "PENDING")
              << " @ $" << std::setprecision(2) << result.avg_fill_price << "\n";

    trading::Order sell_msft;
    sell_msft.security_id = "MSFT"; sell_msft.side = OrderSide::Sell;
    sell_msft.quantity = 25; sell_msft.type = OrderType::Market;
    auto result2 = oms.submit_order(sell_msft);
    std::cout << "  Sell 25 MSFT (Market): " << (result2.status == OrderStatus::Filled ? "FILLED" : "PENDING")
              << " @ $" << result2.avg_fill_price << "\n";

    // ---- 13. Trade Blotter ----
    print_header("13. Trade Blotter");
    trading::TradeBlotter blotter;
    blotter.record_trade("ORD-001", "GROWTH-001", "AAPL", "Apple Inc", true, 50, 178.30);
    blotter.record_trade("ORD-002", "GROWTH-001", "MSFT", "Microsoft", false, 25, 415.90);
    std::cout << "  Recorded Trades: " << blotter.trade_count() << "\n";

    // ---- 14. Backtesting ----
    print_header("14. Strategy Backtesting");
    backtest::BacktestEngine bt_engine(100000.0);
    bt_engine.set_slippage(5.0);
    bt_engine.set_commission(0.01);

    backtest::SMACrossover sma_strategy(10, 30);
    std::cout << "  Strategy: " << sma_strategy.name() << "\n";

    std::vector<backtest::PriceBar> price_bars;
    double p = 100.0;
    for (int i = 0; i < 200; ++i) {
        backtest::PriceBar bar;
        bar.close = p + std::sin(i * 0.1) * 5.0 + (i * 0.02);
        bar.open = bar.close - 0.3; bar.high = bar.close + 0.8; bar.low = bar.close - 0.8;
        bar.volume = 500000 + (i % 10) * 50000;
        price_bars.push_back(bar);
        p = bar.close;
    }

    std::map<std::string, std::vector<backtest::PriceBar>> bt_data;
    bt_data["AAPL"] = price_bars;
    auto bt_result = bt_engine.run(sma_strategy, bt_data);

    std::cout << "  Results (200 bars):\n"
              << "    Total Return: " << std::setprecision(2) << (bt_result.total_return * 100) << "%\n"
              << "    Total Trades: " << bt_result.total_trades << "\n"
              << "    Winning Trades: " << bt_result.winning_trades << "\n"
              << "    Max Drawdown: " << (bt_result.max_drawdown * 100) << "%\n"
              << "    Sharpe Ratio: " << bt_result.sharpe_ratio << "\n";

    // ---- 15. Benchmark Comparison ----
    print_header("15. Benchmark Comparison");
    benchmark::BenchmarkEngine bm;
    std::vector<double> spx_returns = {0.01, -0.005, 0.02, -0.01, 0.015, -0.008, 0.012, 0.003};
    bm.add_sp500(spx_returns);

    std::vector<double> port_returns = {0.012, -0.003, 0.018, -0.008, 0.020, -0.005, 0.015, 0.005};
    auto relative = bm.compare(port_returns, "SPX");
    std::cout << "  Tracking Error: " << (relative.tracking_error * 100) << "%\n"
              << "  Active Return: " << (relative.active_return * 100) << "%\n"
              << "  Information Ratio: " << relative.information_ratio << "\n";

    // ---- 16. Performance Attribution ----
    print_header("16. Performance Calculation");
    performance::PerformanceCalculator perf;
    auto stats = perf.calculate(daily_returns);
    std::cout << "  Annualized Return: " << (stats.annualized_return * 100) << "%\n"
              << "  Volatility: " << (stats.volatility * 100) << "%\n"
              << "  Sharpe: " << stats.sharpe_ratio << "\n";

    // ---- 17. Events ----
    print_header("17. Event System");
    EventBus bus;
    int event_count = 0;
    bus.subscribe(EventType::PriceUpdate, [&event_count](const EventData&) { event_count++; });
    bus.subscribe(EventType::TradeExecuted, [&event_count](const EventData&) { event_count++; });

    EventData price_ev; price_ev.type = EventType::PriceUpdate; price_ev.source = "AAPL";
    EventData trade_ev; trade_ev.type = EventType::TradeExecuted; trade_ev.source = "OMS";
    bus.publish(price_ev);
    bus.publish(trade_ev);
    std::cout << "  Events published: 2\n"
              << "  Events handled: " << event_count << "\n";

    // ---- 18. Alerts ----
    print_header("18. Alert Management");
    alerts::AlertManager am;
    auto a1 = am.raise_alert(alerts::AlertType::RiskLimit, alerts::AlertSeverity::Warning,
                              "VaR Breach", "Portfolio VaR exceeds 95% threshold");
    auto a2 = am.raise_alert(alerts::AlertType::PriceThreshold, alerts::AlertSeverity::Info,
                              "Price Target Hit", "NVDA reached $875");
    am.acknowledge(a1);
    am.resolve(a2);
    std::cout << "  Alert 1 (VaR Breach): Acknowledged\n"
              << "  Alert 2 (Price Target): Resolved\n";

    // ---- 19. Tax Lots ----
    print_header("19. Tax Lot Tracking");
    tax::TaxLotManager tlm;
    tlm.add_lot("AAPL", 100, 140.00);
    tlm.add_lot("AAPL", 100, 165.00);
    tlm.add_lot("MSFT", 50, 380.00);
    auto aapl_lots = tlm.get_lots("AAPL");
    std::cout << "  AAPL Tax Lots: " << aapl_lots.size() << "\n";
    for (const auto& lot : aapl_lots) {
        std::cout << "    " << lot.lot_id << ": " << lot.quantity << " shares @ $"
                  << std::setprecision(2) << lot.cost_basis << "\n";
    }

    // ---- 20. Reporting ----
    print_header("20. Report Generation");
    reporting::ReportBuilder rb;
    rb.set_title("Growth Fund - Monthly Report");
    rb.add_section("Overview", {{"AUM", "$1,000,000"}, {"Positions", "4"}, {"Cash", "$145,685"}});
    rb.add_section("Performance", {{"MTD", "+2.3%"}, {"YTD", "+18.5%"}, {"Sharpe", "1.85"}});
    auto report = rb.build();
    std::cout << "  Generated report (" << report.size() << " chars)\n"
              << "  Preview: " << report.substr(0, 100) << "...\n";

    // ---- Summary ----
    std::cout << "\n" << std::string(60, '=')
              << "\n  All 20 examples completed successfully!"
              << "\n  Metis Genie Platform v" << VERSION_STRING << " - Ready for Production"
              << "\n" << std::string(60, '=') << "\n\n";

    sys.shutdown();
    return 0;
}
