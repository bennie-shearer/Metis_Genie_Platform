/**
 * @file example_tier2_analytics.cpp
 * @brief Comprehensive example demonstrating TIER 2 prototype functionality
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * This example demonstrates the TIER 2 prototype transformation features:
 * - Enhanced order management with validation and stop-loss monitoring
 * - Real risk analytics (VaR, correlation, beta, volatility)
 * - Real performance metrics (TWR, Sharpe, attribution)
 * - Data quality management (spike detection, corporate actions)
 * 
 * Build:
 *   g++ -std=c++20 -I../include -o example_tier2 example_tier2_analytics.cpp \
 *       -lsqlite3 -lpthread
 */

#include <genie/genie.hpp>
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>

using namespace genie;
using namespace genie::trading;
using namespace genie::market;
using namespace genie::analytics;

// ============================================================================
// Helper Functions
// ============================================================================

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(70, '=') << "\n\n";
}

void print_section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n\n";
}

// Generate synthetic price data for demonstration
std::vector<PriceBar> generate_sample_prices(
    const std::string& symbol,
    int days,
    double start_price,
    double volatility,
    double drift = 0.0001) {
    
    std::vector<PriceBar> bars;
    bars.reserve(days);
    
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::normal_distribution<double> dist(0, 1);
    
    double price = start_price;
    
    for (int i = 0; i < days; ++i) {
        PriceBar bar;
        
        // Generate date
        int year = 2024 + i / 252;
        int day_of_year = i % 252;
        int month = 1 + day_of_year / 21;
        if (month > 12) month = 12;
        int day = 1 + day_of_year % 21;
        
        std::ostringstream date_oss;
        date_oss << year << "-" << std::setfill('0') << std::setw(2) << month
                 << "-" << std::setw(2) << day;
        bar.date = date_oss.str();
        
        // Generate OHLC
        double daily_return = drift + volatility * dist(rng);
        double intraday_vol = volatility * std::abs(dist(rng)) * 0.5;
        
        bar.open = price;
        bar.high = price * (1 + intraday_vol);
        bar.low = price * (1 - intraday_vol);
        price *= (1 + daily_return);
        bar.close = price;
        bar.adjusted_close = price;
        bar.volume = 1000000 + static_cast<int64_t>(std::abs(dist(rng)) * 500000);
        
        bars.push_back(bar);
    }
    
    return bars;
}

// ============================================================================
// Demo 1: Enhanced Order Management
// ============================================================================

void demo_order_management() {
    print_header("Demo 1: Enhanced Order Management");
    
    // Order Validation
    print_section("Order Validation");
    
    trading::ValidationConfig val_config;
    val_config.max_limit_deviation_pct = 10.0;
    val_config.max_position_concentration = 0.25;
    val_config.max_order_size = 10000;
    
    OrderValidator validator(val_config);
    
    // Test order request
    OrderRequest request;
    request.symbol = "AAPL";
    request.side = trading::OrderSide::Buy;
    request.type = trading::OrderType::Limit;
    request.qty = 100;
    request.limit_price = 180.0;
    
    auto validation = validator.validate(
        request,
        185.0,      // market price
        50000.0,    // buying power
        0,          // current position
        100000.0    // portfolio value
    );
    
    std::cout << validation.format() << "\n";
    
    // Order Preview
    print_section("Order Preview with Fee Estimates");
    
    EnhancedFeeStructure fees;
    fees.commission_per_share = 0.005;
    fees.sec_fee_rate = 0.0000278;
    fees.taf_fee_rate = 0.000166;
    
    OrderPreviewGenerator previewer(fees, val_config);
    
    auto preview = previewer.preview(
        request,
        185.0,      // market price
        50000.0,    // buying power
        0,          // current position
        100000.0,   // portfolio value
        5000000     // avg daily volume
    );
    
    std::cout << preview.format() << "\n";
    
    // Stop Loss Monitoring
    print_section("Stop Loss Monitoring");
    
    EnhancedStopLossMonitor stop_monitor;
    
    // Add a stop loss
    auto stop_id = stop_monitor.add_stop_loss("AAPL", 100, 175.0);
    std::cout << "Created stop loss order: " << stop_id << "\n";
    std::cout << "  Symbol: AAPL\n";
    std::cout << "  Quantity: 100\n";
    std::cout << "  Stop Price: $175.00\n\n";
    
    // Add a trailing stop
    auto trail_id = stop_monitor.add_trailing_stop("MSFT", 50, 400.0, 0, 5.0);
    std::cout << "Created trailing stop: " << trail_id << "\n";
    std::cout << "  Symbol: MSFT\n";
    std::cout << "  Trail: 5%\n\n";
    
    // Simulate price updates
    std::cout << "Simulating price updates...\n";
    
    auto triggers = stop_monitor.check_price("AAPL", 176.0);
    std::cout << "  AAPL @ $176.00: " << triggers.size() << " triggers\n";
    
    triggers = stop_monitor.check_price("AAPL", 174.50);
    std::cout << "  AAPL @ $174.50: " << triggers.size() << " triggers\n";
    
    if (!triggers.empty()) {
        std::cout << "  STOP TRIGGERED! " << triggers[0].symbol 
                  << " at $" << std::fixed << std::setprecision(2) 
                  << triggers[0].trigger_price << "\n";
    }
    
    std::cout << "\nActive stops: " << stop_monitor.active_stop_count() << "\n";
    
    // Partial Fill Tracking
    print_section("Partial Fill Tracking");
    
    PartialFillTracker tracker;
    
    // Track an order
    BrokerOrder order;
    order.id = "ORD-001";
    order.symbol = "GOOGL";
    order.side = trading::OrderSide::Buy;
    order.type = trading::OrderType::Limit;
    order.qty = 100;
    order.filled_qty = 0;
    order.status = trading::OrderStatus::New;
    
    tracker.track_order(order);
    
    // Record partial fills
    EnhancedFill fill1;
    fill1.id = "FILL-001";
    fill1.order_id = "ORD-001";
    fill1.qty = 30;
    fill1.price = 175.50;
    fill1.commission = 0.15;
    
    tracker.record_fill("ORD-001", fill1);
    
    EnhancedFill fill2;
    fill2.id = "FILL-002";
    fill2.order_id = "ORD-001";
    fill2.qty = 70;
    fill2.price = 175.75;
    fill2.commission = 0.35;
    
    tracker.record_fill("ORD-001", fill2);
    
    auto tracked = tracker.get_order("ORD-001");
    if (tracked) {
        std::cout << "Order " << tracked->id << ":\n";
        std::cout << "  Total Qty: " << tracked->total_qty << "\n";
        std::cout << "  Filled Qty: " << tracked->filled_qty << "\n";
        std::cout << "  Avg Fill Price: $" << tracked->avg_fill_price << "\n";
        std::cout << "  Total Commission: $" << tracked->total_commission << "\n";
        std::cout << "  Fill Count: " << tracked->fills.size() << "\n";
        std::cout << "  Status: " << order_status_to_string(tracked->status) << "\n";
    }
}

// ============================================================================
// Demo 2: Real Risk Analytics
// ============================================================================

void demo_risk_analytics() {
    print_header("Demo 2: Real Risk Analytics");
    
    // Generate sample data
    auto aapl_bars = generate_sample_prices("AAPL", 504, 150.0, 0.02);  // 2 years
    auto msft_bars = generate_sample_prices("MSFT", 504, 350.0, 0.018);
    auto spy_bars = generate_sample_prices("SPY", 504, 450.0, 0.012);   // Benchmark
    
    // Calculate returns
    auto aapl_returns = ReturnCalculator::calculate_returns(aapl_bars);
    auto msft_returns = ReturnCalculator::calculate_returns(msft_bars);
    auto spy_returns = ReturnCalculator::calculate_returns(spy_bars);
    
    // VaR Calculation
    print_section("Value at Risk (VaR)");
    
    auto var_hist = VaRCalculator::calculate(aapl_returns, 0.95, 1, genie::analytics::VaRMethod::Historical);
    std::cout << "AAPL Historical VaR:\n" << var_hist.format();
    
    auto var_param = VaRCalculator::calculate(aapl_returns, 0.95, 1, genie::analytics::VaRMethod::Parametric);
    std::cout << "\nParametric VaR: " << std::fixed << std::setprecision(2) 
              << (var_param.var * 100) << "%\n";
    
    auto var_cf = VaRCalculator::calculate(aapl_returns, 0.95, 1, genie::analytics::VaRMethod::CornishFisher);
    std::cout << "Cornish-Fisher VaR: " << (var_cf.var * 100) << "%\n";
    
    // Portfolio VaR
    print_section("Portfolio VaR");
    
    std::map<std::string, std::vector<double>> asset_returns;
    asset_returns["AAPL"] = aapl_returns;
    asset_returns["MSFT"] = msft_returns;
    
    std::map<std::string, double> weights;
    weights["AAPL"] = 0.6;
    weights["MSFT"] = 0.4;
    
    auto port_var = VaRCalculator::calculate_portfolio(asset_returns, weights, 0.95, 10);
    std::cout << "Portfolio 10-day VaR (95%):\n" << port_var.format();
    
    // Correlation Matrix
    print_section("Correlation Matrix");
    
    std::map<std::string, std::vector<double>> all_returns;
    all_returns["AAPL"] = aapl_returns;
    all_returns["MSFT"] = msft_returns;
    all_returns["SPY"] = spy_returns;
    
    auto corr_matrix = CorrelationCalculator::build_matrix(all_returns);
    std::cout << corr_matrix.format();
    
    // Beta Calculation
    print_section("Beta vs SPY Benchmark");
    
    auto aapl_beta = BetaCalculator::calculate(aapl_returns, spy_returns, "AAPL", "SPY");
    std::cout << aapl_beta.format();
    
    auto msft_beta = BetaCalculator::calculate(msft_returns, spy_returns, "MSFT", "SPY");
    std::cout << msft_beta.format();
    
    // Volatility
    print_section("Volatility Analysis");
    
    auto aapl_vol = VolatilityCalculator::calculate_from_ohlc(aapl_bars);
    std::cout << "AAPL " << aapl_vol.format();
    
    // EWMA Volatility
    double ewma_vol = VolatilityCalculator::ewma_volatility(aapl_returns, 0.94);
    std::cout << "  EWMA Volatility: " << std::fixed << std::setprecision(2) 
              << (ewma_vol * 100) << "%\n";
    
    // Drawdown
    print_section("Drawdown Analysis");
    
    auto drawdown = DrawdownTracker::analyze(aapl_bars);
    std::cout << drawdown.format();
    
    // Comprehensive Risk Report
    print_section("Comprehensive Risk Report");
    
    auto risk_report = generate_risk_report(aapl_bars, spy_returns, "AAPL", "SPY");
    std::cout << risk_report.format();
}

// ============================================================================
// Demo 3: Real Performance Analytics
// ============================================================================

void demo_performance_analytics() {
    print_header("Demo 3: Real Performance Analytics");
    
    // Generate sample NAV series with some cash flows
    std::vector<double> navs;
    std::vector<std::string> dates;
    std::vector<double> cash_flows;
    
    double nav = 100000;  // Starting NAV
    std::mt19937 rng(123);
    std::normal_distribution<double> dist(0.0003, 0.01);
    
    for (int i = 0; i < 504; ++i) {  // 2 years
        int year = 2024 + i / 252;
        int day_of_year = i % 252;
        int month = 1 + day_of_year / 21;
        if (month > 12) month = 12;
        int day = 1 + day_of_year % 21;
        
        std::ostringstream date_oss;
        date_oss << year << "-" << std::setfill('0') << std::setw(2) << month
                 << "-" << std::setw(2) << day;
        dates.push_back(date_oss.str());
        
        // Simulate cash flow on first of each quarter
        double cf = 0;
        if (i > 0 && i % 63 == 0) {
            cf = 5000;  // Quarterly contribution
        }
        cash_flows.push_back(cf);
        
        nav = nav * (1 + dist(rng)) + cf;
        navs.push_back(nav);
    }
    
    // TWR Calculation
    print_section("Time-Weighted Return (TWR)");
    
    auto twr = TWRCalculator::calculate(navs, dates, cash_flows);
    std::cout << twr.format();
    
    // Generate benchmark returns
    auto spy_bars = generate_sample_prices("SPY", 504, 450.0, 0.012);
    auto benchmark_returns = ReturnCalculator::calculate_returns(spy_bars);
    
    // Portfolio returns
    std::vector<double> portfolio_returns;
    for (size_t i = 1; i < navs.size(); ++i) {
        if (navs[i-1] > 0) {
            double cf = (i < cash_flows.size()) ? cash_flows[i] : 0;
            portfolio_returns.push_back((navs[i] - cf - navs[i-1]) / navs[i-1]);
        }
    }
    
    // Benchmark Comparison
    print_section("Benchmark Comparison vs SPY");
    
    auto benchmark = BenchmarkCalculator::compare(portfolio_returns, benchmark_returns, "SPY");
    std::cout << benchmark.format();
    
    // Risk-Adjusted Metrics
    print_section("Risk-Adjusted Metrics");
    
    auto risk_metrics = RiskAdjustedCalculator::calculate(
        portfolio_returns,
        0.02,   // Risk-free rate
        benchmark.beta,
        DrawdownTracker::analyze(navs, dates).max_drawdown
    );
    std::cout << risk_metrics.format();
    
    // Rolling Returns
    print_section("Rolling Returns");
    
    auto rolling = RollingReturnsCalculator::calculate(portfolio_returns, dates, 63);  // Quarterly
    std::cout << rolling.format();
    
    // Multi-period rolling
    auto multi_rolling = RollingReturnsCalculator::calculate_multiple(
        portfolio_returns, dates, {21, 63, 126, 252});
    
    std::cout << "\nRolling Return Summary:\n";
    for (const auto& [window, rr] : multi_rolling) {
        std::cout << "  " << std::setw(3) << window << "-day: "
                  << "avg " << std::fixed << std::setprecision(1) 
                  << (rr.avg_return * 100) << "%, "
                  << "best " << (rr.best_return * 100) << "%, "
                  << "worst " << (rr.worst_return * 100) << "%\n";
    }
    
    // Performance Attribution
    print_section("Performance Attribution (Brinson-Fachler)");
    
    // Simulate sector weights and returns
    std::map<std::string, double> port_weights = {
        {"Technology", 0.35}, {"Healthcare", 0.20}, {"Finance", 0.15},
        {"Consumer", 0.15}, {"Industrial", 0.10}, {"Other", 0.05}
    };
    
    std::map<std::string, double> bench_weights = {
        {"Technology", 0.28}, {"Healthcare", 0.15}, {"Finance", 0.20},
        {"Consumer", 0.12}, {"Industrial", 0.15}, {"Other", 0.10}
    };
    
    std::map<std::string, double> port_returns = {
        {"Technology", 0.12}, {"Healthcare", 0.08}, {"Finance", 0.06},
        {"Consumer", 0.10}, {"Industrial", 0.04}, {"Other", 0.02}
    };
    
    std::map<std::string, double> bench_returns = {
        {"Technology", 0.10}, {"Healthcare", 0.07}, {"Finance", 0.08},
        {"Consumer", 0.09}, {"Industrial", 0.05}, {"Other", 0.03}
    };
    
    auto attribution = AttributionCalculator::calculate_brinson(
        port_weights, bench_weights, port_returns, bench_returns);
    std::cout << attribution.format();
}

// ============================================================================
// Demo 4: Data Quality Management
// ============================================================================

void demo_data_quality() {
    print_header("Demo 4: Data Quality Management");
    
    // Generate sample data with some anomalies
    auto bars = generate_sample_prices("TEST", 252, 100.0, 0.02);
    
    // Inject some spikes
    if (bars.size() > 50) {
        bars[50].close *= 1.30;  // 30% spike
        bars[50].high = bars[50].close * 1.05;
    }
    if (bars.size() > 150) {
        bars[150].close *= 0.75;  // 25% drop
        bars[150].low = bars[150].close * 0.95;
    }
    
    // Spike Detection
    print_section("Price Spike Detection");
    
    SpikeConfig spike_config;
    spike_config.max_daily_change_pct = 15.0;
    spike_config.zscore_threshold = 3.5;
    
    SpikeDetector detector(spike_config);
    auto spike_result = detector.detect("TEST", bars, false);
    
    std::cout << spike_result.format() << "\n";
    
    if (!spike_result.spikes.empty()) {
        std::cout << "Detected Spikes:\n";
        for (const auto& spike : spike_result.spikes) {
            std::cout << "  " << spike.date << ": " << std::fixed << std::setprecision(2)
                      << spike.change_pct << "% change"
                      << " (z-score: " << std::setprecision(1) << spike.zscore << ")"
                      << " - " << spike.reason << "\n";
        }
    }
    
    // Corporate Action Engine
    print_section("Corporate Action Handling");
    
    CorporateActionEngine action_engine;
    
    // Add a stock split
    DataQualityCorporateAction split;
    split.symbol = "TEST";
    split.date = "2024-06-15";
    split.type = CorporateActionType::Split;
    split.split_from = 1;
    split.split_to = 4;  // 4:1 split
    
    action_engine.add_action(split);
    std::cout << "Added 4:1 stock split on 2024-06-15\n";
    
    // Add dividends
    DataQualityCorporateAction div1;
    div1.symbol = "TEST";
    div1.date = "2024-03-15";
    div1.type = CorporateActionType::CashDividend;
    div1.dividend_amount = 0.50;
    
    DataQualityCorporateAction div2;
    div2.symbol = "TEST";
    div2.date = "2024-09-15";
    div2.type = CorporateActionType::CashDividend;
    div2.dividend_amount = 0.55;
    
    action_engine.add_action(div1);
    action_engine.add_action(div2);
    std::cout << "Added quarterly dividends: $0.50, $0.55\n\n";
    
    // Get split factor
    double factor = action_engine.get_split_factor("TEST", "2024-01-01");
    std::cout << "Split factor for 2024-01-01: " << factor << "\n";
    
    factor = action_engine.get_split_factor("TEST", "2024-07-01");
    std::cout << "Split factor for 2024-07-01: " << factor << "\n";
    
    // Total dividends
    double total_divs = action_engine.get_total_dividends("TEST", "2024-01-01", "2024-12-31");
    std::cout << "Total dividends 2024: $" << std::fixed << std::setprecision(2) 
              << total_divs << "\n";
    
    // Price Adjustment
    print_section("Price Adjustment for Corporate Actions");
    
    PriceAdjuster adjuster(action_engine);
    
    std::cout << "Adjusting prices for splits and dividends...\n";
    auto adjusted_bars = adjuster.adjust("TEST", bars, AdjustmentType::Full);
    
    std::cout << "Original bars: " << bars.size() << "\n";
    std::cout << "Adjusted bars: " << adjusted_bars.size() << "\n";
    
    if (!bars.empty() && !adjusted_bars.empty()) {
        std::cout << "\nFirst bar (original): $" << bars.front().close << "\n";
        std::cout << "First bar (adjusted): $" << adjusted_bars.front().close << "\n";
    }
    
    // Data Source Failover
    print_section("Data Source Failover");
    
    FailoverConfig failover_config;
    failover_config.max_consecutive_failures = 3;
    failover_config.source_priority = {"yahoo", "alpha_vantage", "iex"};
    
    FailoverManager failover(failover_config);
    
    // Simulate requests
    std::cout << "Initial source: " << failover.get_source() << "\n";
    
    failover.record_success("yahoo");
    failover.record_success("yahoo");
    std::cout << "After 2 successes on yahoo: " << failover.get_source() << "\n";
    
    // Simulate failures
    failover.record_failure("yahoo", "Rate limited");
    failover.record_failure("yahoo", "Rate limited");
    failover.record_failure("yahoo", "Rate limited");
    
    std::cout << "After 3 consecutive failures: " << failover.get_source() << "\n";
    
    auto all_status = failover.get_all_status();
    std::cout << "\nSource Status:\n";
    for (const auto& [name, status] : all_status) {
        std::cout << "  " << name << ": "
                  << (status.healthy ? "healthy" : "unhealthy")
                  << ", success rate: " << std::fixed << std::setprecision(0)
                  << (status.success_rate() * 100) << "%"
                  << ", failures: " << status.consecutive_failures << "\n";
    }
    
    // Gap Detection
    print_section("Gap Detection and Filling");
    
    // Create bars with a gap
    std::vector<PriceBar> bars_with_gap;
    for (int i = 0; i < 20; ++i) {
        if (i >= 8 && i <= 12) continue;  // Skip 5 days
        
        PriceBar bar;
        std::ostringstream oss;
        oss << "2024-01-" << std::setfill('0') << std::setw(2) << (i + 1);
        bar.date = oss.str();
        bar.close = 100.0 + i * 0.5;
        bar.open = bar.close - 0.1;
        bar.high = bar.close + 0.2;
        bar.low = bar.close - 0.2;
        bars_with_gap.push_back(bar);
    }
    
    GapHandler gap_handler;
    auto gaps = gap_handler.detect_gaps("TEST", bars_with_gap, 2);
    
    std::cout << "Detected " << gaps.size() << " gap(s):\n";
    for (const auto& gap : gaps) {
        std::cout << "  " << gap.start_date << " to " << gap.end_date
                  << " (" << gap.missing_days << " missing days)\n";
    }
    
    auto filled = gap_handler.fill_gaps(bars_with_gap, GapFillMethod::LinearInterp, 10);
    std::cout << "\nBars before fill: " << bars_with_gap.size() << "\n";
    std::cout << "Bars after fill: " << filled.size() << "\n";
    
    // Data Quality Report
    print_section("Data Quality Assessment");
    
    auto quality = assess_quality("TEST", bars, spike_config, &action_engine);
    std::cout << quality.format();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "  Metis Genie Platform v" << genie::VERSION_STRING << "\n";
    std::cout << "  TIER 2 Prototype Demonstration\n";
    std::cout << "\n";
    
    try {
        demo_order_management();
        demo_risk_analytics();
        demo_performance_analytics();
        demo_data_quality();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    print_header("Demo Complete");
    std::cout << "All TIER 2 features demonstrated successfully!\n\n";
    
    return 0;
}
