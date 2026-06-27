/**
 * @file test_tier3.cpp
 * @brief Comprehensive test suite for TIER 3 components
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Tests for:
 * - Enhanced Data Clients (IEX, FRED, Polygon, Finnhub, SEC EDGAR)
 * - Additional Brokers (IBKR, TDA, Webull)
 * - Tax Tracking (FIFO, Wash Sales, Form 8949)
 * - Reporting (Statements, Performance, Tax, Risk)
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <cmath>

#include "genie/core/version.hpp"

// Enhanced Data Clients
#include "genie/market/iex_cloud.hpp"
#include "genie/market/fred_client.hpp"
#include "genie/market/polygon_client.hpp"
#include "genie/market/finnhub_client.hpp"
#include "genie/market/sec_edgar.hpp"

// Broker Integrations
#include "genie/trading/broker_abstraction.hpp"
#include "genie/trading/ibkr_client.hpp"
#include "genie/trading/tda_client.hpp"
#include "genie/trading/webull_client.hpp"

// Tax Tracking
#include "genie/tax/tax_tracking.hpp"

// Reporting
#include "genie/reporting/reporting_extended.hpp"

using namespace genie;

// ============================================================================
// Test Utilities
// ============================================================================

int tests_passed = 0;
int tests_failed = 0;

void test_assert(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "  [PASS] " << test_name << std::endl;
        tests_passed++;
    } else {
        std::cout << "  [FAIL] " << test_name << std::endl;
        tests_failed++;
    }
}

void test_section(const std::string& name) {
    std::cout << "\n=== " << name << " ===" << std::endl;
}

// ============================================================================
// Enhanced Data Client Tests
// ============================================================================

void test_iex_cloud_structures() {
    test_section("IEX Cloud Structures");
    
    // Test IEXQuote structure
    market::IEXQuote quote;
    quote.symbol = "AAPL";
    quote.company_name = "Apple Inc.";
    quote.latest_price = 175.50;
    quote.change = 2.50;
    quote.change_percent = 0.0145;
    quote.volume = 65000000;
    quote.pe_ratio = 28.5;
    
    test_assert(quote.symbol == "AAPL", "IEXQuote symbol");
    test_assert(quote.latest_price == 175.50, "IEXQuote price");
    test_assert(std::abs(quote.change_percent - 0.0145) < 0.0001, "IEXQuote change percent");
    
    // Test IEXHistoricalPrice
    market::IEXHistoricalPrice hist;
    hist.date = "2025-01-15";
    hist.open = 170.0;
    hist.high = 176.0;
    hist.low = 169.5;
    hist.close = 175.5;
    hist.volume = 80000000;
    
    test_assert(hist.date == "2025-01-15", "IEXHistoricalPrice date");
    test_assert(hist.close == 175.5, "IEXHistoricalPrice close");
    
    // Test IEXCompany
    market::IEXCompany company;
    company.symbol = "AAPL";
    company.company_name = "Apple Inc.";
    company.industry = "Consumer Electronics";
    company.sector = "Technology";
    company.employees = 164000;
    
    test_assert(company.sector == "Technology", "IEXCompany sector");
    test_assert(company.employees == 164000, "IEXCompany employees");
    
    // Test IEXStats
    market::IEXStats stats;
    stats.market_cap = 2750000000000.0;
    stats.week52_high = 200.0;
    stats.week52_low = 140.0;
    stats.pe_ratio = 28.5;
    stats.beta = 1.2;
    
    test_assert(stats.market_cap > 2e12, "IEXStats market cap");
    test_assert(std::abs(stats.beta - 1.2) < 0.01, "IEXStats beta");
    
    // Test IEXNews
    market::IEXNews news;
    news.datetime = "2025-01-15T10:30:00";
    news.headline = "Apple Reports Strong Q4 Results";
    news.source = "Reuters";
    news.url = "https://reuters.com/article/123";
    news.related = "AAPL";
    
    test_assert(news.source == "Reuters", "IEXNews source");
    test_assert(news.related == "AAPL", "IEXNews related");
    
    // Test IEXEarnings
    market::IEXEarnings earnings;
    earnings.actual_eps = 1.53;
    earnings.consensus_eps = 1.48;
    earnings.eps_surprise = 0.05;
    earnings.eps_surprise_pct = 0.0338;
    earnings.fiscal_period = "Q4 2024";
    
    test_assert(std::abs(earnings.eps_surprise - 0.05) < 0.001, "IEXEarnings surprise");
    test_assert(earnings.fiscal_period == "Q4 2024", "IEXEarnings period");
    
    std::cout << "  IEX Cloud structures validated" << std::endl;
}

void test_fred_client_structures() {
    test_section("FRED Client Structures");
    
    // Test FREDSeries
    market::FREDSeries series;
    series.id = "UNRATE";
    series.title = "Unemployment Rate";
    series.units = "Percent";
    series.frequency = "Monthly";
    series.seasonal_adjustment = "Seasonally Adjusted";
    
    test_assert(series.id == "UNRATE", "FREDSeries id");
    test_assert(series.units == "Percent", "FREDSeries units");
    
    // Test FREDObservation
    market::FREDObservation obs;
    obs.date = "2025-01-01";
    obs.value = 3.8;
    
    test_assert(obs.date == "2025-01-01", "FREDObservation date");
    test_assert(std::abs(obs.value - 3.8) < 0.01, "FREDObservation value");
    
    // Test common indicators namespace
    test_assert(std::string(market::indicators::GDP) == "GDP", "CommonIndicators GDP");
    test_assert(std::string(market::indicators::UNRATE) == "UNRATE", "CommonIndicators UNRATE");
    test_assert(std::string(market::indicators::FEDFUNDS) == "FEDFUNDS", "CommonIndicators FEDFUNDS");
    test_assert(std::string(market::indicators::CPIAUCSL) == "CPIAUCSL", "CommonIndicators CPI");
    test_assert(std::string(market::indicators::DGS10) == "DGS10", "CommonIndicators DGS10");
    
    std::cout << "  FRED structures validated" << std::endl;
}

void test_polygon_client_structures() {
    test_section("Polygon.io Structures");
    
    // Test PolygonBar
    market::PolygonBar bar;
    bar.timestamp = 1705334400000;  // Unix ms
    bar.open = 175.0;
    bar.high = 178.5;
    bar.low = 174.5;
    bar.close = 177.25;
    bar.volume = 45000000;
    bar.vwap = 176.80;
    bar.transactions = 125000;
    
    test_assert(bar.vwap == 176.80, "PolygonBar VWAP");
    test_assert(bar.transactions == 125000, "PolygonBar transactions");
    
    // Test PolygonSnapshot
    market::PolygonSnapshot snap;
    snap.ticker = "AAPL";
    snap.change = 2.75;
    snap.change_percent = 1.58;
    snap.last_timestamp = 1705340000000;
    
    test_assert(snap.ticker == "AAPL", "PolygonSnapshot ticker");
    test_assert(std::abs(snap.change_percent - 1.58) < 0.01, "PolygonSnapshot change pct");
    
    // Test PolygonTrade
    market::PolygonTrade trade;
    trade.symbol = "AAPL";
    trade.price = 177.50;
    trade.size = 100;
    trade.timestamp = 1705340100000;
    trade.exchange = 4;  // NYSE
    
    test_assert(trade.size == 100, "PolygonTrade size");
    
    // Test PolygonQuote
    market::PolygonQuote pquote;
    pquote.symbol = "AAPL";
    pquote.bid = 177.45;
    pquote.ask = 177.50;
    pquote.bid_size = 500;
    pquote.ask_size = 300;
    
    double spread = pquote.ask - pquote.bid;
    test_assert(std::abs(spread - 0.05) < 0.001, "PolygonQuote spread");
    
    std::cout << "  Polygon.io structures validated" << std::endl;
}

void test_finnhub_structures() {
    test_section("Finnhub Structures");
    
    // Test FinnhubNews
    market::FinnhubNews news;
    news.id = 123456;
    news.headline = "Apple announces new product line";
    news.source = "Bloomberg";
    news.datetime = 1705340000;
    news.summary = "Apple Inc. today announced...";
    news.url = "https://bloomberg.com/news/123";
    news.related = "AAPL";
    
    test_assert(news.source == "Bloomberg", "FinnhubNews source");
    
    // Test FinnhubSentiment
    market::FinnhubSentiment sentiment;
    sentiment.symbol = "AAPL";
    sentiment.reddit_mention = 150;
    sentiment.twitter_mention = 2500;
    sentiment.reddit_positive_mention = 1800;
    sentiment.reddit_negative_mention = 400;
    
    double total = sentiment.reddit_positive_mention + sentiment.reddit_negative_mention;
    double positive_ratio = sentiment.reddit_positive_mention / total;
    test_assert(positive_ratio > 0.8, "FinnhubSentiment positive ratio");
    
    // Test FinnhubInsiderTransaction
    market::FinnhubInsiderTransaction insider;
    insider.symbol = "AAPL";
    insider.name = "Tim Cook";
    insider.transaction_code = "S-Sale";
    insider.share = 50000;
    insider.transaction_price = 175.00;
    
    double value = insider.share * insider.transaction_price;
    test_assert(value == 8750000.0, "FinnhubInsiderTransaction value");
    
    // Test FinnhubEarnings
    market::FinnhubEarnings fearn;
    fearn.symbol = "AAPL";
    fearn.eps_actual = 1.53;
    fearn.eps_estimate = 1.48;
    fearn.revenue_actual = 119.58e9;
    fearn.revenue_estimate = 117.5e9;
    
    double eps_surprise = fearn.eps_actual - fearn.eps_estimate;
    test_assert(std::abs(eps_surprise - 0.05) < 0.001, "FinnhubEarnings EPS surprise");
    
    // Test FinnhubRecommendation
    market::FinnhubRecommendation rec;
    rec.period = "2026-02-01";
    rec.strong_buy = 15;
    rec.buy = 20;
    rec.hold = 8;
    rec.sell = 2;
    rec.strong_sell = 0;
    
    int total_recs = rec.strong_buy + rec.buy + rec.hold + rec.sell + rec.strong_sell;
    test_assert(total_recs == 45, "FinnhubRecommendation total");
    
    std::cout << "  Finnhub structures validated" << std::endl;
}

void test_sec_edgar_structures() {
    test_section("SEC EDGAR Structures");
    
    // Test EDGARFiling
    market::EDGARFiling filing;
    filing.accession_number = "0001193125-25-012345";
    filing.form_type_str = "10-K";
    filing.filing_date = "2025-01-15";
    filing.report_date = "2024-12-31";
    filing.company_name = "Apple Inc.";
    filing.cik = "320193";
    filing.primary_document = "aapl-20241231.htm";
    filing.is_xbrl = true;
    
    test_assert(filing.form_type_str == "10-K", "EDGARFiling form");
    test_assert(filing.is_xbrl, "EDGARFiling has XBRL");
    
    // Test EDGARCompany
    market::EDGARCompany company;
    company.cik = "320193";
    company.name = "APPLE INC";
    company.ticker = "AAPL";
    company.state = "CA";
    company.sic = "3571";
    company.sic_description = "Electronic Computers";
    company.fiscal_year_end = "0930";
    
    test_assert(company.ticker == "AAPL", "EDGARCompany ticker");
    test_assert(company.sic == "3571", "EDGARCompany SIC");
    
    // Test EDGARInsiderTransaction
    market::EDGARInsiderTransaction itrans;
    itrans.owner_name = "Cook Timothy D";
    itrans.officer_title = "CEO";
    itrans.transaction_code = "S";
    itrans.shares = 50000;
    itrans.price_per_share = 175.00;
    itrans.transaction_date = "2025-01-10";
    itrans.ownership_type = "D";
    
    double trans_value = itrans.shares * itrans.price_per_share;
    test_assert(trans_value == 8750000.0, "EDGARInsiderTransaction value");
    
    // Test EDGAR13FHolding
    market::EDGAR13FHolding holding;
    holding.name_of_issuer = "APPLE INC";
    holding.cusip = "037833100";
    holding.value = 25000000000;  // $25B
    holding.shares = 140000000;
    holding.investment_discretion = "SOLE";
    
    test_assert(holding.shares == 140000000, "EDGAR13FHolding shares");
    
    std::cout << "  SEC EDGAR structures validated" << std::endl;
}

// ============================================================================
// Broker Integration Tests
// ============================================================================

void test_broker_abstraction() {
    test_section("Broker Abstraction");
    
    // Test BrokerId enum
    test_assert(trading::broker_id_to_string(trading::BrokerId::Alpaca) == "Alpaca",
                "BrokerId Alpaca");
    test_assert(trading::broker_id_to_string(trading::BrokerId::InteractiveBrokers) == "Interactive Brokers",
                "BrokerId IBKR");
    test_assert(trading::broker_id_to_string(trading::BrokerId::TDAmeritrade) == "TD Ameritrade",
                "BrokerId TDA");
    test_assert(trading::broker_id_to_string(trading::BrokerId::Tradier) == "Tradier",
                "BrokerId Tradier");
    test_assert(trading::broker_id_to_string(trading::BrokerId::Webull) == "Webull",
                "BrokerId Webull");
    
    // Test UnifiedAccount
    trading::UnifiedAccount account;
    account.id = "ACC123";
    account.broker = trading::BrokerId::Alpaca;
    account.type = trading::AccountType::Margin;
    account.cash = 50000.0;
    account.buying_power = 100000.0;
    account.portfolio_value = 150000.0;
    
    test_assert(account.cash == 50000.0, "UnifiedAccount cash");
    test_assert(account.buying_power == 100000.0, "UnifiedAccount buying power");
    
    // Test UnifiedPosition
    trading::UnifiedPosition position;
    position.symbol = "AAPL";
    position.broker = trading::BrokerId::Alpaca;
    position.qty = 100;
    position.avg_entry_price = 150.00;
    position.current_price = 175.00;
    position.market_value = 17500.00;
    position.cost_basis = 15000.00;
    position.unrealized_pl = 2500.00;
    
    test_assert(position.qty == 100, "UnifiedPosition qty");
    test_assert(position.unrealized_pl == 2500.00, "UnifiedPosition P/L");
    
    // Test UnifiedOrder
    trading::UnifiedOrder order;
    order.id = "ORD123";
    order.symbol = "AAPL";
    order.side = trading::OrderSide::Buy;
    order.type = trading::OrderType::Limit;
    order.time_in_force = trading::TimeInForce::Day;
    order.qty = 100;
    order.limit_price = 175.00;
    order.status = trading::OrderStatus::New;
    
    test_assert(order.side == trading::OrderSide::Buy, "UnifiedOrder side");
    test_assert(order.type == trading::OrderType::Limit, "UnifiedOrder type");
    
    // Test available brokers
    auto brokers = trading::get_available_brokers();
    test_assert(brokers.size() == 6, "Available brokers count");
    
    std::cout << "  Broker abstraction validated" << std::endl;
}

void test_ibkr_structures() {
    test_section("Interactive Brokers Structures");
    
    // Test IBKRConfig
    trading::IBKRConfig config;
    config.gateway_host = "localhost";
    config.gateway_port = 5000;
    config.ssl = true;
    config.paper = true;
    
    std::string base_url = config.base_url();
    test_assert(base_url.find("https://localhost:5000") != std::string::npos, "IBKRConfig base_url");
    
    // Test IBKRContract
    trading::IBKRContract contract;
    contract.con_id = 265598;
    contract.symbol = "AAPL";
    contract.sec_type = "STK";
    contract.exchange = "SMART";
    contract.currency = "USD";
    
    test_assert(contract.is_stock(), "IBKRContract is_stock");
    test_assert(!contract.is_option(), "IBKRContract not option");
    
    // Test IBKRAccountInfo
    trading::IBKRAccountInfo acct_info;
    acct_info.account_id = "U1234567";
    acct_info.net_liquidation = 500000.0;
    acct_info.buying_power = 1000000.0;
    acct_info.total_cash = 100000.0;
    acct_info.maintenance_margin = 150000.0;
    
    test_assert(acct_info.account_id == "U1234567", "IBKRAccountInfo id");
    test_assert(acct_info.net_liquidation == 500000.0, "IBKRAccountInfo NLV");
    
    std::cout << "  IBKR structures validated" << std::endl;
}

void test_tda_structures() {
    test_section("TD Ameritrade Structures");
    
    // Test TDAConfig
    trading::TDAConfig config;
    config.client_id = "TEST_CLIENT_ID";
    config.redirect_uri = "https://localhost:8080/callback";
    
    test_assert(config.is_valid(), "TDAConfig is_valid");
    
    std::string auth_url = config.get_auth_url("test_state");
    test_assert(auth_url.find("auth.tdameritrade.com") != std::string::npos, "TDAConfig auth_url");
    test_assert(auth_url.find("response_type=code") != std::string::npos, "TDAConfig auth_url params");
    
    // Test TDATokens
    trading::TDATokens tokens;
    tokens.access_token = "test_access_token";
    tokens.refresh_token = "test_refresh_token";
    tokens.expires_in = 1800;
    tokens.access_expiry = std::chrono::system_clock::now() + std::chrono::seconds(1800);
    tokens.refresh_expiry = std::chrono::system_clock::now() + std::chrono::seconds(7776000);
    
    test_assert(!tokens.is_access_expired(), "TDATokens not expired");
    test_assert(tokens.is_valid(), "TDATokens is_valid");
    
    // Test TDAPosition to_unified conversion
    trading::TDAPosition pos;
    pos.symbol = "MSFT";
    pos.long_quantity = 50;
    pos.short_quantity = 0;
    pos.average_price = 350.00;
    pos.market_value = 18500.00;
    pos.current_day_profit_loss = 1000.00;
    
    auto unified = pos.to_unified();
    test_assert(unified.symbol == "MSFT", "TDAPosition to_unified symbol");
    test_assert(unified.qty == 50, "TDAPosition to_unified qty");
    test_assert(unified.broker == trading::BrokerId::TDAmeritrade, "TDAPosition to_unified broker");
    
    std::cout << "  TDA structures validated" << std::endl;
}

void test_webull_structures() {
    test_section("Webull Structures");
    
    // Test WebullConfig
    trading::WebullConfig config;
    config.device_id = "test_device_id_12345";
    config.access_token = "test_access_token";
    config.token_expiry = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + std::chrono::hours(24));
    
    test_assert(config.is_authenticated(), "WebullConfig is_authenticated");
    
    // Test device ID generation
    std::string generated_id = config.generate_device_id();
    test_assert(generated_id.length() == 32, "WebullConfig generated device ID length");
    
    // Test WebullAccount
    trading::WebullAccount acct;
    acct.account_id = "WB123456";
    acct.account_type = "2";  // Margin
    acct.net_liquidation = 75000.0;
    acct.buying_power = 150000.0;
    acct.total_cash = 25000.0;
    
    auto unified_acct = acct.to_unified();
    test_assert(unified_acct.type == trading::AccountType::Margin, "WebullAccount to_unified type");
    test_assert(unified_acct.broker == trading::BrokerId::Webull, "WebullAccount to_unified broker");
    
    // Test WebullPosition
    trading::WebullPosition wpos;
    wpos.symbol = "TSLA";
    wpos.quantity = 25;
    wpos.cost_price = 200.00;
    wpos.last_price = 250.00;
    wpos.unrealized_profit = 1250.00;
    wpos.unrealized_profit_rate = 0.25;
    
    auto wpos_unified = wpos.to_unified();
    test_assert(wpos_unified.symbol == "TSLA", "WebullPosition to_unified symbol");
    test_assert(wpos_unified.unrealized_pl == 1250.00, "WebullPosition to_unified P/L");
    
    // Test WebullOrder
    trading::WebullOrder worder;
    worder.order_id = "WO12345";
    worder.symbol = "NVDA";
    worder.action = "BUY";
    worder.order_type = "LMT";
    worder.time_in_force = "DAY";
    worder.total_quantity = 10;
    worder.limit_price = 500.00;
    worder.status = "Working";
    
    auto worder_unified = worder.to_unified();
    test_assert(worder_unified.side == trading::OrderSide::Buy, "WebullOrder to_unified side");
    test_assert(worder_unified.type == trading::OrderType::Limit, "WebullOrder to_unified type");
    test_assert(worder_unified.status == trading::OrderStatus::New, "WebullOrder to_unified status");
    
    std::cout << "  Webull structures validated" << std::endl;
}

// ============================================================================
// Tax Tracking Tests
// ============================================================================

void test_tax_lot_inventory() {
    test_section("Tax Lot Inventory");
    
    tax::TaxLotInventory inventory("AAPL");
    
    // Add some lots
    inventory.add_lot(100, 150.00, "2024-01-15");
    inventory.add_lot(50, 160.00, "2024-03-20");
    inventory.add_lot(75, 155.00, "2024-06-10");
    
    test_assert(inventory.total_shares() == 225, "Total shares");
    test_assert(std::abs(inventory.total_cost_basis() - 34625.0) < 0.01, "Total cost basis");
    test_assert(std::abs(inventory.average_cost() - 153.89) < 0.01, "Average cost");
    
    auto open_lots = inventory.get_open_lots();
    test_assert(open_lots.size() == 3, "Open lots count");
    
    // Test FIFO removal
    auto gains = inventory.remove_shares(100, 175.00, "2025-01-15", 
                                         tax::CostBasisMethod::FIFO);
    
    test_assert(gains.size() == 1, "FIFO removal gains count");
    test_assert(gains[0].shares == 100, "FIFO removal shares");
    test_assert(gains[0].cost_basis == 15000.00, "FIFO removal cost basis");
    test_assert(gains[0].proceeds == 17500.00, "FIFO removal proceeds");
    test_assert(gains[0].gain_loss == 2500.00, "FIFO removal gain");
    test_assert(gains[0].holding_period == tax::HoldingPeriod::LongTerm, "FIFO holding period");
    
    test_assert(inventory.total_shares() == 125, "Remaining shares after FIFO");
    
    std::cout << "  Tax lot inventory validated" << std::endl;
}

void test_wash_sale_detection() {
    test_section("Wash Sale Detection");
    
    // Create a sale that resulted in a loss
    tax::RealizedGainLoss sale;
    sale.symbol = "AAPL";
    sale.sale_date = "2025-01-15";
    sale.shares = 100;
    sale.cost_basis = 18000.00;
    sale.proceeds = 15000.00;
    sale.gain_loss = -3000.00;
    
    // Create tax lot records - one within 30-day window
    std::vector<tax::TaxLotRecord> lots;
    
    tax::TaxLotRecord lot1;
    lot1.id = "LOT1";
    lot1.symbol = "AAPL";
    lot1.acquisition_date = "2025-01-20";  // Within 30 days after sale
    lot1.shares = 50;
    lot1.cost_per_share = 155.00;
    lot1.is_open = true;
    lots.push_back(lot1);
    
    tax::TaxLotRecord lot2;
    lot2.id = "LOT2";
    lot2.symbol = "AAPL";
    lot2.acquisition_date = "2024-06-01";  // Not within window
    lot2.shares = 100;
    lot2.cost_per_share = 160.00;
    lot2.is_open = true;
    lots.push_back(lot2);
    
    auto result = tax::WashSaleDetector::check_wash_sale(sale, lots);
    
    test_assert(result.is_wash_sale, "Wash sale detected");
    test_assert(result.replacement_lot_id == "LOT1", "Wash sale lot identified");
    test_assert(result.disallowed_loss == 1500.00, "Disallowed loss (50 shares * $30/share)");
    
    std::cout << "  Wash sale detection validated" << std::endl;
}

void test_tax_tracker() {
    test_section("Tax Tracker");
    
    tax::TaxTracker tracker;
    tracker.set_default_method(tax::CostBasisMethod::FIFO);
    
    // Record purchases
    tracker.record_purchase("MSFT", 100, 300.00, "2024-01-10");
    tracker.record_purchase("MSFT", 50, 320.00, "2024-04-15");
    tracker.record_purchase("AAPL", 200, 150.00, "2024-02-20");
    
    // Record sales
    tracker.record_sale("MSFT", 75, 350.00, "2025-01-15");
    tracker.record_sale("AAPL", 100, 140.00, "2025-01-20");  // Loss
    
    auto symbols = tracker.get_symbols();
    test_assert(symbols.size() == 2, "Tracked symbols");
    
    // Get realized gains for tax year
    auto realized = tracker.get_all_realized("2025-01-01", "2025-12-31");
    test_assert(realized.size() == 2, "Realized transactions");
    
    // Get tax summary
    auto summary = tracker.get_summary(2025);
    test_assert(summary.total_transactions() == 2, "Summary total transactions");
    
    // MSFT: 75 shares * ($350 - $300) = $3750 gain (long-term)
    // AAPL: 100 shares * ($140 - $150) = -$1000 loss (short-term, < 1 year)
    test_assert(summary.long_term_gains > 0, "Long-term gains");
    test_assert(summary.short_term_losses < 0, "Short-term loss");
    
    std::cout << "  Tax tracker validated" << std::endl;
}

void test_form_8949_generation() {
    test_section("Form 8949 Generation");
    
    std::vector<tax::RealizedGainLoss> realized;
    
    // Short-term gain
    tax::RealizedGainLoss st_gain;
    st_gain.symbol = "AAPL";
    st_gain.acquisition_date = "2025-03-01";
    st_gain.sale_date = "2025-06-15";
    st_gain.shares = 100;
    st_gain.cost_basis = 15000.00;
    st_gain.proceeds = 17500.00;
    st_gain.gain_loss = 2500.00;
    st_gain.holding_period = tax::HoldingPeriod::ShortTerm;
    st_gain.wash_sale_type = tax::WashSaleType::None;
    realized.push_back(st_gain);
    
    // Long-term gain
    tax::RealizedGainLoss lt_gain;
    lt_gain.symbol = "MSFT";
    lt_gain.acquisition_date = "2023-01-15";
    lt_gain.sale_date = "2025-02-20";
    lt_gain.shares = 50;
    lt_gain.cost_basis = 12500.00;
    lt_gain.proceeds = 17500.00;
    lt_gain.gain_loss = 5000.00;
    lt_gain.holding_period = tax::HoldingPeriod::LongTerm;
    lt_gain.wash_sale_type = tax::WashSaleType::None;
    realized.push_back(lt_gain);
    
    // Short-term loss with wash sale
    tax::RealizedGainLoss st_loss;
    st_loss.symbol = "NVDA";
    st_loss.acquisition_date = "2025-04-01";
    st_loss.sale_date = "2025-07-10";
    st_loss.shares = 25;
    st_loss.cost_basis = 15000.00;
    st_loss.proceeds = 12000.00;
    st_loss.gain_loss = -3000.00;
    st_loss.holding_period = tax::HoldingPeriod::ShortTerm;
    st_loss.wash_sale_type = tax::WashSaleType::Partial;
    st_loss.wash_sale_disallowed = 1500.00;
    st_loss.adjustment_code = "W";
    st_loss.adjustment_amount = 1500.00;
    realized.push_back(st_loss);
    
    auto form_lines = tax::generate_form_8949(realized, true);
    
    // Should have 3 lines
    test_assert(form_lines.size() == 3, "Form 8949 line count");
    
    // Check box assignments
    int box_a_count = 0, box_b_count = 0, box_d_count = 0;
    for (const auto& line : form_lines) {
        if (line.form_box == 'A') box_a_count++;
        else if (line.form_box == 'B') box_b_count++;
        else if (line.form_box == 'D') box_d_count++;
    }
    
    test_assert(box_a_count == 1, "Box A (short-term, basis reported) count");
    test_assert(box_b_count == 1, "Box B (short-term, adjustment needed) count");
    test_assert(box_d_count == 1, "Box D (long-term, basis reported) count");
    
    std::cout << "  Form 8949 generation validated" << std::endl;
}

// ============================================================================
// Reporting Tests
// ============================================================================

void test_report_utilities() {
    test_section("Report Utilities");
    
    // Test currency formatting
    std::string curr = reporting::fmt_currency(12345.67);
    test_assert(curr == "$12345.67", "Currency formatting");
    
    std::string neg_curr = reporting::fmt_currency(-1234.56);
    test_assert(neg_curr.find("-") != std::string::npos, "Negative currency");
    
    // Test percent formatting
    std::string pct = reporting::fmt_percent(15.75);
    test_assert(pct == "15.75%", "Percent formatting");
    
    // Test number formatting
    std::string num = reporting::fmt_number(3.14159, 3);
    test_assert(num == "3.142", "Number formatting");
    
    std::cout << "  Report utilities validated" << std::endl;
}

void test_account_statement_generation() {
    test_section("Account Statement Generation");
    
    // Build test data
    reporting::AccountStatementData data;
    
    data.account.id = "ACC123";
    data.account.name = "Test Account";
    data.account.broker_account_id = "TB-123456";
    data.account.portfolio_value = 250000.0;
    data.account.cash = 50000.0;
    data.account.buying_power = 100000.0;
    data.account.margin_used = 25000.0;
    
    data.beginning_value = 200000.0;
    data.ending_value = 250000.0;
    data.deposits = 20000.0;
    data.withdrawals = 5000.0;
    data.dividends = 1500.0;
    data.interest = 250.0;
    data.fees = 150.0;
    data.realized_gain_loss = 15000.0;
    data.unrealized_gain_loss = 18400.0;
    
    // Add positions
    trading::UnifiedPosition pos1;
    pos1.symbol = "AAPL";
    pos1.qty = 100;
    pos1.avg_entry_price = 150.0;
    pos1.current_price = 175.0;
    pos1.market_value = 17500.0;
    pos1.unrealized_pl = 2500.0;
    pos1.unrealized_pl_pct = 16.67;
    data.positions.push_back(pos1);
    
    trading::UnifiedPosition pos2;
    pos2.symbol = "MSFT";
    pos2.qty = 50;
    pos2.avg_entry_price = 300.0;
    pos2.current_price = 350.0;
    pos2.market_value = 17500.0;
    pos2.unrealized_pl = 2500.0;
    pos2.unrealized_pl_pct = 16.67;
    data.positions.push_back(pos2);
    
    // Generate report
    reporting::ExtendedReportConfig config;
    config.period_start = "2025-01-01";
    config.period_end = "2025-01-31";
    
    std::string html = reporting::generate_account_statement_html(data, config);
    
    // Verify HTML content
    test_assert(html.find("<!DOCTYPE html>") != std::string::npos, "HTML doctype");
    test_assert(html.find("Account Statement") != std::string::npos, "Report title");
    test_assert(html.find("AAPL") != std::string::npos, "Position symbol in report");
    test_assert(html.find("$250,000") != std::string::npos || 
                html.find("$250000") != std::string::npos, "Portfolio value in report");
    test_assert(html.find("Period Activity") != std::string::npos, "Period activity section");
    test_assert(html.find("Current Holdings") != std::string::npos, "Holdings section");
    
    std::cout << "  Account statement generation validated" << std::endl;
}

void test_performance_report_generation() {
    test_section("Performance Report Generation");
    
    reporting::PerformanceReportData data;
    
    data.total_return_pct = 18.5;
    data.annualized_return = 22.3;
    data.ytd_return = 15.2;
    data.mtd_return = 2.1;
    
    data.sharpe_ratio = 1.85;
    data.sortino_ratio = 2.15;
    data.calmar_ratio = 3.2;
    data.information_ratio = 0.95;
    
    data.volatility = 14.5;
    data.max_drawdown = -8.5;
    data.beta = 1.15;
    data.alpha = 4.2;
    data.tracking_error = 3.8;
    
    data.win_rate = 62.5;
    data.total_trades = 150;
    data.winning_trades = 94;
    data.losing_trades = 56;
    data.avg_win = 850.0;
    data.avg_loss = -420.0;
    data.profit_factor = 2.15;
    
    data.benchmark_name = "S&P 500";
    data.benchmark_return = 12.5;
    
    // Monthly returns
    data.monthly_returns["2025-01"] = 3.2;
    data.monthly_returns["2025-02"] = -1.5;
    data.monthly_returns["2025-03"] = 4.8;
    
    reporting::ExtendedReportConfig config;
    config.period_start = "2024-01-01";
    config.period_end = "2025-03-31";
    
    std::string html = reporting::generate_performance_report_html(data, config);
    
    test_assert(html.find("Performance Report") != std::string::npos, "Report title");
    test_assert(html.find("18.5%") != std::string::npos || 
                html.find("18.50%") != std::string::npos, "Total return");
    test_assert(html.find("Sharpe Ratio") != std::string::npos, "Sharpe ratio label");
    test_assert(html.find("1.85") != std::string::npos, "Sharpe ratio value");
    test_assert(html.find("Monthly Returns") != std::string::npos, "Monthly returns section");
    
    std::cout << "  Performance report generation validated" << std::endl;
}

void test_tax_report_generation() {
    test_section("Tax Report Generation");
    
    std::vector<tax::Form8949Line> lines;
    
    // Short-term gain (Box A)
    tax::Form8949Line line1;
    line1.description = "100 SH AAPL";
    line1.date_acquired = "2025-03-01";
    line1.date_sold = "2025-06-15";
    line1.proceeds = 17500.0;
    line1.cost_basis = 15000.0;
    line1.gain_loss = 2500.0;
    line1.is_short_term = true;
    line1.reported_to_irs = true;
    line1.form_box = 'A';
    lines.push_back(line1);
    
    // Long-term gain (Box D)
    tax::Form8949Line line2;
    line2.description = "50 SH MSFT";
    line2.date_acquired = "2023-01-15";
    line2.date_sold = "2025-02-20";
    line2.proceeds = 17500.0;
    line2.cost_basis = 12500.0;
    line2.gain_loss = 5000.0;
    line2.is_short_term = false;
    line2.reported_to_irs = true;
    line2.form_box = 'D';
    lines.push_back(line2);
    
    reporting::ExtendedReportConfig config;
    
    std::string html = reporting::generate_tax_report_html(lines, 2025, config);
    
    test_assert(html.find("Capital Gains and Losses") != std::string::npos, "Tax report title");
    test_assert(html.find("Box A") != std::string::npos, "Box A section");
    test_assert(html.find("Box D") != std::string::npos, "Box D section");
    test_assert(html.find("Short-Term") != std::string::npos, "Short-term label");
    test_assert(html.find("Long-Term") != std::string::npos, "Long-term label");
    test_assert(html.find("$2,500") != std::string::npos || 
                html.find("$2500") != std::string::npos, "Short-term gain amount");
    
    // Test CSV generation
    std::string csv = reporting::generate_tax_report_csv(lines);
    test_assert(csv.find("Description,Date Acquired") != std::string::npos, "CSV headers");
    test_assert(csv.find("AAPL") != std::string::npos, "CSV contains AAPL");
    test_assert(csv.find("Short") != std::string::npos, "CSV contains term");
    
    std::cout << "  Tax report generation validated" << std::endl;
}

void test_risk_report_generation() {
    test_section("Risk Report Generation");
    
    reporting::RiskReportData data;
    
    data.var_95_1d = 12500.0;
    data.var_99_1d = 18750.0;
    data.var_95_10d = 39528.0;  // sqrt(10) * 1-day VaR
    data.cvar_95 = 15625.0;
    
    // Position risks
    reporting::RiskReportData::PositionRisk pr1;
    pr1.symbol = "AAPL";
    pr1.market_value = 100000.0;
    pr1.weight = 40.0;
    pr1.volatility = 25.0;
    pr1.beta = 1.2;
    pr1.var_contribution = 5000.0;
    pr1.marginal_var = 125.0;
    data.position_risks.push_back(pr1);
    
    reporting::RiskReportData::PositionRisk pr2;
    pr2.symbol = "MSFT";
    pr2.market_value = 75000.0;
    pr2.weight = 30.0;
    pr2.volatility = 22.0;
    pr2.beta = 1.1;
    pr2.var_contribution = 3750.0;
    pr2.marginal_var = 110.0;
    data.position_risks.push_back(pr2);
    
    data.top_5_concentration = 70.0;
    data.herfindahl_index = 0.22;
    
    // Stress tests
    reporting::RiskReportData::StressScenario st1;
    st1.name = "2008 Financial Crisis";
    st1.impact = -125000.0;
    st1.description = "Market decline of 50%";
    data.stress_tests.push_back(st1);
    
    reporting::RiskReportData::StressScenario st2;
    st2.name = "COVID March 2020";
    st2.impact = -85000.0;
    st2.description = "Rapid 34% decline";
    data.stress_tests.push_back(st2);
    
    // Risk limits
    reporting::RiskReportData::RiskLimit rl1;
    rl1.metric = "Max Position Size";
    rl1.current = 40.0;
    rl1.limit = 50.0;
    rl1.breached = false;
    data.risk_limits.push_back(rl1);
    
    reporting::RiskReportData::RiskLimit rl2;
    rl2.metric = "Daily VaR Limit";
    rl2.current = 12500.0;
    rl2.limit = 10000.0;
    rl2.breached = true;
    data.risk_limits.push_back(rl2);
    
    reporting::ExtendedReportConfig config;
    
    std::string html = reporting::generate_risk_report_html(data, config);
    
    test_assert(html.find("Risk Report") != std::string::npos, "Risk report title");
    test_assert(html.find("Value at Risk") != std::string::npos, "VaR section");
    test_assert(html.find("$12,500") != std::string::npos || 
                html.find("$12500") != std::string::npos, "1-day VaR value");
    test_assert(html.find("Position Risk") != std::string::npos, "Position risk section");
    test_assert(html.find("Stress Test") != std::string::npos, "Stress test section");
    test_assert(html.find("BREACHED") != std::string::npos, "Breached limit indicator");
    
    std::cout << "  Risk report generation validated" << std::endl;
}

void test_html_builder() {
    test_section("Enhanced HTML Builder");
    
    reporting::EnhancedHTMLBuilder builder;
    
    builder.start_document("Test Report");
    
    reporting::ExtendedReportConfig config;
    config.title = "Test Report";
    config.account_name = "Test Account";
    config.period_start = "2025-01-01";
    config.period_end = "2025-01-31";
    
    builder.add_header(config);
    
    builder.start_section("Test Section");
    builder.start_metric_grid();
    builder.add_metric("Metric 1", "$1,000", "positive");
    builder.add_metric("Metric 2", "-$500", "negative");
    builder.end_metric_grid();
    
    builder.start_table({"Column 1", "Column 2", "Column 3"});
    builder.add_table_row({"Row 1", "$100", "10%"}, {false, true, true});
    builder.add_table_row({"Row 2", "-$50", "-5%"}, {false, true, true});
    builder.end_table();
    
    builder.add_summary_box("This is a summary box with important information.");
    builder.end_section();
    
    builder.add_footer("Test Footer");
    builder.end_document();
    
    std::string html = builder.build();
    
    test_assert(html.find("<!DOCTYPE html>") != std::string::npos, "HTML doctype");
    test_assert(html.find("Test Report") != std::string::npos, "Document title");
    test_assert(html.find("metric-grid") != std::string::npos, "Metric grid class");
    test_assert(html.find("summary-box") != std::string::npos, "Summary box class");
    test_assert(html.find("Test Footer") != std::string::npos, "Footer content");
    
    std::cout << "  HTML builder validated" << std::endl;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "================================================================" << std::endl;
    std::cout << "      TIER 3 COMPREHENSIVE TEST SUITE - Metis Genie Platform v" << VERSION_STRING << "    " << std::endl;
    std::cout << "================================================================" << std::endl;
    
    // Enhanced Data Clients
    test_iex_cloud_structures();
    test_fred_client_structures();
    test_polygon_client_structures();
    test_finnhub_structures();
    test_sec_edgar_structures();
    
    // Broker Integrations
    test_broker_abstraction();
    test_ibkr_structures();
    test_tda_structures();
    test_webull_structures();
    
    // Tax Tracking
    test_tax_lot_inventory();
    test_wash_sale_detection();
    test_tax_tracker();
    test_form_8949_generation();
    
    // Reporting
    test_report_utilities();
    test_account_statement_generation();
    test_performance_report_generation();
    test_tax_report_generation();
    test_risk_report_generation();
    test_html_builder();
    
    // Summary
    std::cout << "\n================================================================" << std::endl;
    std::cout << "                        TEST SUMMARY                           " << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "  Tests Passed: " << tests_passed << std::endl;
    std::cout << "  Tests Failed: " << tests_failed << std::endl;
    std::cout << "  Total Tests:  " << (tests_passed + tests_failed) << std::endl;
    std::cout << "================================================================\n" << std::endl;
    
    return tests_failed == 0 ? 0 : 1;
}
