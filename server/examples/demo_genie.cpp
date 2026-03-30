/**
 * @file demo_genie.cpp
 * @brief Demonstration of Metis Genie Platform Emulator capabilities
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#include "../include/genie/genie.hpp"
#include <iostream>
#include <iomanip>

using namespace genie;

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n" << title << "\n" << std::string(60, '=') << "\n";
}

void demo_phase1_foundation() {
    print_header("Phase 1: Foundation - Market Data & Portfolios");
    
    GenieSystem sys;
    sys.initialize();
    std::cout << sys.status() << "\n";
    
    // Create securities
    auto aapl = assets::create_common_stock("AAPL", "Apple Inc", "AAPL", "NASDAQ");
    auto msft = assets::create_common_stock("MSFT", "Microsoft Corp", "MSFT", "NASDAQ");
    auto googl = assets::create_common_stock("GOOGL", "Alphabet Inc", "GOOGL", "NASDAQ");
    auto spy = assets::create_etf("SPY", "SPDR S&P 500 ETF", "SPY");
    auto ust10y = assets::create_treasury("UST10Y", "10-Year Treasury", 0.04, 0.042, 10, date_utils::from_ymd(2035, 1, 1));
    
    sys.add_security(aapl); sys.add_security(msft); sys.add_security(googl);
    sys.add_security(spy); sys.add_security(ust10y);
    
    // Generate historical data
    sys.market_data().generate_synthetic_history("AAPL", 252, 175.0, 0.15, 0.25);
    sys.market_data().generate_synthetic_history("MSFT", 252, 380.0, 0.12, 0.22);
    sys.market_data().generate_synthetic_history("GOOGL", 252, 140.0, 0.10, 0.28);
    sys.market_data().generate_synthetic_history("SPY", 252, 480.0, 0.10, 0.15);
    sys.update_price("UST10Y", ust10y->bond_data().theoretical_price());
    
    std::cout << "Securities loaded: " << sys.market_data().store()->security_count() << "\n";
    std::cout << "AAPL Price: $" << std::fixed << std::setprecision(2) << sys.market_data().get_price("AAPL") << "\n";
    std::cout << "MSFT Price: $" << sys.market_data().get_price("MSFT") << "\n";
    std::cout << "UST10Y Price: $" << sys.market_data().get_price("UST10Y") << "\n";
    
    // Create portfolio
    auto port = sys.create_portfolio("GROWTH_FUND", "Growth Equity Fund", "USD");
    port->deposit_cash(Money(10000000, "USD"));
    
    port->open_position("AAPL", 10000, sys.market_data().get_price("AAPL"));
    port->open_position("MSFT", 5000, sys.market_data().get_price("MSFT"));
    port->open_position("GOOGL", 8000, sys.market_data().get_price("GOOGL"));
    port->open_position("SPY", 2000, sys.market_data().get_price("SPY"));
    port->open_position("UST10Y", 500, sys.market_data().get_price("UST10Y"));
    port->update_market_values();
    
    std::cout << "\nPortfolio: " << port->name() << "\n";
    std::cout << "NAV: " << port->nav().to_string() << "\n";
    std::cout << "Cash: " << port->cash_balance().to_string() << "\n";
    std::cout << "Positions: " << port->position_count() << "\n";
    std::cout << "Leverage: " << std::setprecision(2) << port->leverage() << "x\n";
    
    auto weights = port->get_weights();
    std::cout << "\nPosition Weights:\n";
    for (const auto& [id, w] : weights) std::cout << "  " << id << ": " << std::setprecision(1) << (w * 100) << "%\n";
    
    sys.shutdown();
}

void demo_phase2_risk() {
    print_header("Phase 2: Risk Analytics - VaR & Stress Testing");
    
    GenieSystem sys;
    sys.initialize();
    
    auto aapl = assets::create_common_stock("AAPL", "Apple", "AAPL");
    auto msft = assets::create_common_stock("MSFT", "Microsoft", "MSFT");
    sys.add_security(aapl); sys.add_security(msft);
    sys.market_data().generate_synthetic_history("AAPL", 252, 175.0, 0.15, 0.25);
    sys.market_data().generate_synthetic_history("MSFT", 252, 380.0, 0.12, 0.22);
    
    auto port = sys.create_portfolio("RISK_TEST", "Risk Test Portfolio");
    port->deposit_cash(Money(1000000, "USD"));
    port->open_position("AAPL", 2000, sys.market_data().get_price("AAPL"));
    port->open_position("MSFT", 1000, sys.market_data().get_price("MSFT"));
    port->update_market_values();
    
    // VaR Calculations
    std::cout << "Portfolio NAV: " << port->nav().to_string() << "\n\n";
    
    risk::VaRConfig cfg;
    cfg.confidence_level = 0.95;
    
    cfg.method = VaRMethod::Parametric;
    sys.var_engine().set_config(cfg);
    auto var_p = sys.var_engine().calculate_var(*port);
    std::cout << "Parametric VaR (95%): $" << std::fixed << std::setprecision(0) << var_p.var << " (" << std::setprecision(2) << var_p.var_percent << "%)\n";
    
    cfg.method = VaRMethod::Historical;
    sys.var_engine().set_config(cfg);
    auto var_h = sys.var_engine().calculate_var(*port);
    std::cout << "Historical VaR (95%): $" << std::setprecision(0) << var_h.var << " (" << std::setprecision(2) << var_h.var_percent << "%)\n";
    
    cfg.method = VaRMethod::MonteCarlo;
    cfg.monte_carlo_simulations = 10000;
    sys.var_engine().set_config(cfg);
    auto var_mc = sys.var_engine().calculate_var(*port);
    std::cout << "Monte Carlo VaR (95%): $" << std::setprecision(0) << var_mc.var << " (" << std::setprecision(2) << var_mc.var_percent << "%)\n";
    std::cout << "CVaR (Expected Shortfall): $" << std::setprecision(0) << var_mc.cvar << "\n";
    
    // Stress Testing
    std::cout << "\nStress Test Results:\n";
    auto stress_results = sys.run_stress_tests(*port);
    for (const auto& r : stress_results) {
        std::cout << "  " << std::setw(25) << std::left << r.scenario_name << ": $" << std::setw(10) << std::right << std::setprecision(0) << r.absolute_pnl << " (" << std::setprecision(1) << r.percentage_pnl << "%)\n";
    }
    
    // Monte Carlo Simulation
    std::cout << "\nMonte Carlo GBM Simulation (AAPL):\n";
    sys.monte_carlo().set_simulations(10000);
    auto mc_result = sys.run_monte_carlo(sys.market_data().get_price("AAPL"), 0.10, 0.25, 1.0);
    std::cout << "  Mean Terminal: $" << std::setprecision(2) << mc_result.mean_terminal_price << "\n";
    std::cout << "  Std Dev: $" << mc_result.std_terminal_price << "\n";
    std::cout << "  Expected Return: " << std::setprecision(1) << (mc_result.mean_return * 100) << "%\n";
    
    sys.shutdown();
}

void demo_phase3_optimization() {
    print_header("Phase 3: Portfolio Optimization");
    
    GenieSystem sys;
    sys.initialize();
    
    portfolio::OptimizationInputs inputs;
    inputs.security_ids = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};
    inputs.expected_returns = {0.12, 0.10, 0.14, 0.11, 0.15};
    inputs.covariance_matrix = {
        {0.0625, 0.020, 0.025, 0.018, 0.022},
        {0.020, 0.0484, 0.018, 0.015, 0.020},
        {0.025, 0.018, 0.0784, 0.020, 0.028},
        {0.018, 0.015, 0.020, 0.0529, 0.018},
        {0.022, 0.020, 0.028, 0.018, 0.0900}
    };
    inputs.risk_free_rate = 0.05;
    inputs.constraints.max_weight = 0.40;
    
    std::cout << "Max Sharpe Portfolio:\n";
    inputs.objective = OptimizationObjective::MaxSharpe;
    sys.optimizer().set_max_iterations(5000);
    auto sharpe = sys.optimize_portfolio(inputs);
    std::cout << "  Expected Return: " << std::setprecision(1) << (sharpe.expected_return * 100) << "%\n";
    std::cout << "  Volatility: " << (sharpe.expected_volatility * 100) << "%\n";
    std::cout << "  Sharpe Ratio: " << std::setprecision(2) << sharpe.sharpe_ratio << "\n";
    std::cout << "  Weights: ";
    for (const auto& [id, w] : sharpe.weights) std::cout << id << "=" << std::setprecision(0) << (w * 100) << "% ";
    std::cout << "\n";
    
    std::cout << "\nMin Variance Portfolio:\n";
    inputs.objective = OptimizationObjective::MinVariance;
    auto minvar = sys.optimize_portfolio(inputs);
    std::cout << "  Expected Return: " << std::setprecision(1) << (minvar.expected_return * 100) << "%\n";
    std::cout << "  Volatility: " << (minvar.expected_volatility * 100) << "%\n";
    std::cout << "  Sharpe Ratio: " << std::setprecision(2) << minvar.sharpe_ratio << "\n";
    
    std::cout << "\nRisk Parity Portfolio:\n";
    inputs.objective = OptimizationObjective::RiskParity;
    auto rp = sys.optimize_portfolio(inputs);
    std::cout << "  Expected Return: " << std::setprecision(1) << (rp.expected_return * 100) << "%\n";
    std::cout << "  Volatility: " << (rp.expected_volatility * 100) << "%\n";
    std::cout << "  Weights: ";
    for (const auto& [id, w] : rp.weights) std::cout << id << "=" << std::setprecision(0) << (w * 100) << "% ";
    std::cout << "\n";
    
    sys.shutdown();
}

void demo_phase4_trading() {
    print_header("Phase 4: Trading & Compliance");
    
    GenieSystem sys;
    sys.initialize();
    
    auto aapl = assets::create_common_stock("AAPL", "Apple", "AAPL");
    sys.add_security(aapl);
    sys.update_price("AAPL", 175.0);
    
    // Order Management
    std::cout << "Order Management System:\n";
    trading::Order buy_order;
    buy_order.portfolio_id = "TRADE_TEST";
    buy_order.security_id = "AAPL";
    buy_order.side = OrderSide::Buy;
    buy_order.type = OrderType::Market;
    buy_order.quantity = 1000;
    
    auto result = sys.oms().submit_order(buy_order);
    std::cout << "  Order ID: " << result.id.substr(0, 8) << "...\n";
    std::cout << "  Status: " << (result.status == OrderStatus::Filled ? "Filled" : "Pending") << "\n";
    std::cout << "  Fill Price: $" << std::fixed << std::setprecision(2) << result.avg_fill_price << "\n";
    
    std::cout << "\nAudit Trail (" << sys.oms().audit().size() << " entries):\n";
    for (const auto& entry : sys.oms().audit().get_entries()) {
        std::cout << "  " << entry.event_type << ": " << entry.description << "\n";
    }
    
    // Compliance
    auto port = sys.create_portfolio("COMP_TEST", "Compliance Test");
    port->deposit_cash(Money(100000, "USD"));
    port->open_position("AAPL", 500, 175.0);
    port->update_market_values();
    
    std::cout << "\nCompliance Check:\n";
    auto report = sys.check_compliance(*port);
    std::cout << "  Total Rules: " << report.total << "\n";
    std::cout << "  Compliant: " << report.compliant << "\n";
    std::cout << "  Warnings: " << report.warnings << "\n";
    std::cout << "  Breaches: " << report.breaches << "\n";
    std::cout << "  Compliance Rate: " << std::setprecision(0) << report.compliance_rate() << "%\n";
    
    sys.shutdown();
}

void demo_phase5_performance() {
    print_header("Phase 5: Performance & Reporting");
    
    GenieSystem sys;
    sys.initialize();
    
    // Generate returns
    std::vector<double> port_returns, bench_returns;
    RandomGenerator gen(42);
    for (int i = 0; i < 252; ++i) {
        port_returns.push_back(gen.normal(0.0004, 0.012));
        bench_returns.push_back(gen.normal(0.0003, 0.010));
    }
    
    auto stats = sys.calculate_performance(port_returns, bench_returns);
    
    std::cout << "Performance Statistics:\n";
    std::cout << "  Total Return: " << std::setprecision(1) << (stats.total_return * 100) << "%\n";
    std::cout << "  Annualized Return: " << (stats.annualized_return * 100) << "%\n";
    std::cout << "  Annualized Volatility: " << (stats.annualized_volatility * 100) << "%\n";
    std::cout << "  Sharpe Ratio: " << std::setprecision(2) << stats.sharpe_ratio << "\n";
    std::cout << "  Sortino Ratio: " << stats.sortino_ratio << "\n";
    std::cout << "  Max Drawdown: " << std::setprecision(1) << (stats.max_drawdown * 100) << "%\n";
    std::cout << "  Alpha: " << (stats.alpha * 100) << "%\n";
    std::cout << "  Beta: " << std::setprecision(2) << stats.beta << "\n";
    std::cout << "  Win Rate: " << std::setprecision(0) << stats.win_rate << "%\n";
    
    // Brinson Attribution
    std::map<std::string, double> pw = {{"Tech", 0.45}, {"Finance", 0.25}, {"Healthcare", 0.30}};
    std::map<std::string, double> bw = {{"Tech", 0.35}, {"Finance", 0.35}, {"Healthcare", 0.30}};
    std::map<std::string, double> pr = {{"Tech", 0.18}, {"Finance", 0.08}, {"Healthcare", 0.12}};
    std::map<std::string, double> br = {{"Tech", 0.15}, {"Finance", 0.10}, {"Healthcare", 0.11}};
    
    auto attr = sys.attribution().calculate_brinson(pw, bw, pr, br);
    std::cout << "\nBrinson Attribution:\n";
    std::cout << "  Allocation Effect: " << std::setprecision(2) << (attr.total_allocation * 100) << "%\n";
    std::cout << "  Selection Effect: " << (attr.total_selection * 100) << "%\n";
    std::cout << "  Interaction Effect: " << (attr.total_interaction * 100) << "%\n";
    std::cout << "  Total Active Return: " << (attr.total_active * 100) << "%\n";
    
    // Report Generation
    auto aapl = assets::create_common_stock("AAPL", "Apple", "AAPL");
    sys.add_security(aapl);
    sys.market_data().generate_synthetic_history("AAPL", 252, 175, 0.15, 0.25);
    
    auto port = sys.create_portfolio("REPORT_TEST", "Report Test");
    port->deposit_cash(Money(1000000, "USD"));
    port->open_position("AAPL", 2000, sys.market_data().get_price("AAPL"));
    port->update_market_values();
    
    std::cout << "\n--- Generated Report ---\n";
    reporting::ReportBuilder builder;
    builder.set_title("Metis Genie Platform Portfolio Report v3.0.2")
           .add_portfolio_overview(*port)
           .add_performance_summary(stats);
    std::cout << builder.build();
    
    sys.shutdown();
}

int main() {
    std::cout << "\n" << std::string(70, '*') << "\n";
    std::cout << "        METIS GENIE EMULATOR v3.0.2 - DEMONSTRATION\n";
    std::cout << std::string(70, '*') << "\n";
    std::cout << "Investment Management Platform Emulation\n";
    std::cout << "Cross-Platform: Windows | Linux | macOS\n";
    std::cout << genie::version_full() << "\n";
    
    demo_phase1_foundation();
    demo_phase2_risk();
    demo_phase3_optimization();
    demo_phase4_trading();
    demo_phase5_performance();
    
    print_header("Demonstration Complete");
    std::cout << "All 5 phases demonstrated successfully!\n";
    std::cout << "Metis Genie Platform Emulator v3.0.2 - Ready for Production\n\n";
    
    return 0;
}
