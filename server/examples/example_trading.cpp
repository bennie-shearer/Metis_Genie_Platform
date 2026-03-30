/**
 * @file example_trading.cpp
 * @brief Comprehensive example demonstrating Metis Genie Platform prototype trading
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * This example demonstrates the complete TIER 1 prototype functionality:
 * - Connecting to Alpaca paper trading
 * - Fetching real market data
 * - Managing positions
 * - Submitting and tracking orders
 * - Real-time portfolio valuation
 * 
 * Prerequisites:
 * - Alpaca paper trading account (free at https://alpaca.markets)
 * - Set environment variables:
 *   - APCA_API_KEY_ID
 *   - APCA_API_SECRET_KEY
 * - Optional: ALPHAVANTAGE_API_KEY for additional data
 * 
 * Build:
 *   g++ -std=c++20 -I../include -o example_trading example_trading.cpp \
 *       -lsqlite3 -lpthread
 * 
 * Run:
 *   export APCA_API_KEY_ID="your-api-key"
 *   export APCA_API_SECRET_KEY="your-secret-key"
 *   ./example_trading
 */

#include <genie/genie.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

using namespace genie;
using namespace genie::trading;
using namespace genie::market;
using namespace genie::analytics;

// ============================================================================
// Helper Functions
// ============================================================================

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void print_section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n";
}

std::string format_money(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "$" << amount;
    return oss.str();
}

std::string format_pct(double pct) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << (pct * 100) << "%";
    return oss.str();
}

// ============================================================================
// Demo Functions
// ============================================================================

/**
 * @brief Demo 1: System initialization and status
 */
void demo_system_init(TradingSystem& system) {
    print_header("Demo 1: System Initialization");
    
    // Display system status
    auto status = system.get_status();
    std::cout << format_system_status(status) << "\n";
    
    // Get account details
    auto account = system.get_account();
    if (account) {
        std::cout << "Account Details:\n";
        std::cout << "  Account ID: " << account->account_id << "\n";
        std::cout << "  Status: " << account->status << "\n";
        std::cout << "  Paper Trading: " << (account->is_paper ? "Yes" : "No") << "\n";
        std::cout << "  Cash: " << format_money(account->cash) << "\n";
        std::cout << "  Buying Power: " << format_money(account->buying_power) << "\n";
        std::cout << "  Portfolio Value: " << format_money(account->portfolio_value) << "\n";
        std::cout << "  Day Trades: " << account->daytrade_count << "\n";
    }
}

/**
 * @brief Demo 2: Market data retrieval
 */
void demo_market_data(TradingSystem& system) {
    print_header("Demo 2: Market Data");
    
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "TSLA"};
    
    print_section("Real-Time Quotes");
    
    std::cout << std::left << std::setw(8) << "Symbol"
              << std::right << std::setw(10) << "Price"
              << std::setw(10) << "Change"
              << std::setw(10) << "Change%"
              << std::setw(12) << "Volume"
              << "\n";
    std::cout << std::string(50, '-') << "\n";
    
    for (const auto& symbol : symbols) {
        auto quote = system.get_quote(symbol);
        if (quote) {
            std::cout << std::left << std::setw(8) << quote->symbol
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
                      << quote->price
                      << std::setw(10) << quote->change
                      << std::setw(9) << quote->change_percent << "%"
                      << std::setw(12) << quote->volume
                      << "\n";
        } else {
            std::cout << std::left << std::setw(8) << symbol
                      << " (quote unavailable)\n";
        }
    }
    
    // Historical data example
    print_section("Historical Data (AAPL last 5 days)");
    
    auto history = system.get_historical_prices("AAPL");
    int count = 0;
    
    std::cout << std::left << std::setw(12) << "Date"
              << std::right << std::setw(10) << "Open"
              << std::setw(10) << "High"
              << std::setw(10) << "Low"
              << std::setw(10) << "Close"
              << std::setw(12) << "Volume"
              << "\n";
    std::cout << std::string(64, '-') << "\n";
    
    for (const auto& bar : history) {
        if (count++ >= 5) break;
        
        std::cout << std::left << std::setw(12) << bar.date
                  << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
                  << bar.open
                  << std::setw(10) << bar.high
                  << std::setw(10) << bar.low
                  << std::setw(10) << bar.close
                  << std::setw(12) << bar.volume
                  << "\n";
    }
}

/**
 * @brief Demo 3: Position management
 */
void demo_positions(TradingSystem& system) {
    print_header("Demo 3: Current Positions");
    
    auto positions = system.get_positions();
    
    if (positions.empty()) {
        std::cout << "No open positions.\n";
        return;
    }
    
    std::cout << std::left << std::setw(8) << "Symbol"
              << std::right << std::setw(10) << "Qty"
              << std::setw(12) << "Avg Cost"
              << std::setw(12) << "Current"
              << std::setw(14) << "Market Val"
              << std::setw(12) << "P&L"
              << std::setw(10) << "P&L%"
              << "\n";
    std::cout << std::string(78, '-') << "\n";
    
    double total_value = 0;
    double total_pnl = 0;
    
    for (const auto& pos : positions) {
        std::cout << std::left << std::setw(8) << pos.symbol
                  << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
                  << pos.qty
                  << std::setw(12) << pos.avg_entry_price
                  << std::setw(12) << pos.current_price
                  << std::setw(14) << pos.market_value
                  << std::setw(12) << pos.unrealized_pl
                  << std::setw(9) << (pos.unrealized_plpc * 100) << "%"
                  << "\n";
        
        total_value += pos.market_value;
        total_pnl += pos.unrealized_pl;
    }
    
    std::cout << std::string(78, '-') << "\n";
    std::cout << std::left << std::setw(8) << "TOTAL"
              << std::right << std::setw(10) << ""
              << std::setw(12) << ""
              << std::setw(12) << ""
              << std::setw(14) << total_value
              << std::setw(12) << total_pnl
              << "\n";
}

/**
 * @brief Demo 4: Order management
 */
void demo_orders(TradingSystem& system) {
    print_header("Demo 4: Order Management");
    
    // Show open orders
    print_section("Open Orders");
    
    auto orders = system.get_open_orders();
    
    if (orders.empty()) {
        std::cout << "No open orders.\n";
    } else {
        std::cout << std::left << std::setw(12) << "Order ID"
                  << std::setw(8) << "Symbol"
                  << std::setw(8) << "Side"
                  << std::setw(8) << "Type"
                  << std::right << std::setw(10) << "Qty"
                  << std::setw(10) << "Limit"
                  << std::setw(12) << "Status"
                  << "\n";
        std::cout << std::string(68, '-') << "\n";
        
        for (const auto& order : orders) {
            std::cout << std::left << std::setw(12) << order.id.substr(0, 10)
                      << std::setw(8) << order.symbol
                      << std::setw(8) << order_side_to_string(order.side)
                      << std::setw(8) << order_type_to_string(order.type)
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                      << order.qty
                      << std::setw(10) << (order.limit_price ? *order.limit_price : 0)
                      << std::setw(12) << order_status_to_string(order.status)
                      << "\n";
        }
    }
    
    // Demo: Submit a limit order (if we have buying power)
    print_section("Submit Demo Order");
    
    auto buying_power = system.get_buying_power();
    std::cout << "Available buying power: " << format_money(buying_power) << "\n";
    
    if (buying_power > 100) {
        // Submit a limit buy order well below market price (won't fill)
        auto quote = system.get_quote("AAPL");
        if (quote && quote->price > 0) {
            double limit_price = quote->price * 0.95;  // 5% below market
            
            std::cout << "Submitting limit buy order:\n";
            std::cout << "  Symbol: AAPL\n";
            std::cout << "  Quantity: 1\n";
            std::cout << "  Limit Price: " << format_money(limit_price) 
                      << " (current: " << format_money(quote->price) << ")\n";
            
            auto order = system.buy_limit("AAPL", 1, limit_price);
            
            if (order) {
                std::cout << "Order submitted successfully!\n";
                std::cout << "  Order ID: " << order->id << "\n";
                std::cout << "  Status: " << order_status_to_string(order->status) << "\n";
                
                // Wait a moment and check status
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                auto updated = system.get_order(order->id);
                if (updated) {
                    std::cout << "  Updated Status: " << order_status_to_string(updated->status) << "\n";
                }
                
                // Cancel the order
                std::cout << "Canceling order...\n";
                if (system.cancel_order(order->id)) {
                    std::cout << "Order canceled successfully.\n";
                }
            } else {
                std::cout << "Order submission failed: " << system.last_error() << "\n";
            }
        }
    } else {
        std::cout << "Insufficient buying power for demo order.\n";
    }
}

/**
 * @brief Demo 5: Portfolio valuation
 */
void demo_valuation(TradingSystem& system) {
    print_header("Demo 5: Portfolio Valuation");
    
    auto valuation = system.get_valuation();
    
    std::cout << "NAV: " << format_money(valuation.nav) << "\n";
    std::cout << "  Market Value: " << format_money(valuation.total_market_value) << "\n";
    std::cout << "  Cash: " << format_money(valuation.cash) << "\n";
    std::cout << "\n";
    
    std::cout << "P&L:\n";
    std::cout << "  Unrealized: " << format_money(valuation.total_unrealized_pl)
              << " (" << format_pct(valuation.total_unrealized_pl_pct) << ")\n";
    std::cout << "  Day P&L: " << format_money(valuation.day_pl)
              << " (" << format_pct(valuation.day_pl_pct) << ")\n";
    std::cout << "\n";
    
    std::cout << "Positions: " << valuation.position_count << "\n";
    
    if (!valuation.best_performer.empty()) {
        std::cout << "Best Performer: " << valuation.best_performer 
                  << " (" << format_pct(valuation.best_performer_pct) << ")\n";
    }
    if (!valuation.worst_performer.empty()) {
        std::cout << "Worst Performer: " << valuation.worst_performer 
                  << " (" << format_pct(valuation.worst_performer_pct) << ")\n";
    }
    
    // Position details
    if (!valuation.positions.empty()) {
        print_section("Position Details");
        std::cout << format_position_detail(valuation);
    }
}

/**
 * @brief Demo 6: Position reconciliation
 */
void demo_reconciliation(TradingSystem& system) {
    print_header("Demo 6: Position Reconciliation");
    
    auto result = system.reconcile();
    
    std::cout << format_reconciliation_report(result);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "  Metis Genie Platform v" << genie::VERSION_STRING << "\n";
    std::cout << "  Prototype Trading Demo\n";
    std::cout << "\n";
    
    // Check for API credentials
    const char* api_key = std::getenv("APCA_API_KEY_ID");
    const char* api_secret = std::getenv("APCA_API_SECRET_KEY");
    
    if (!api_key || !api_secret) {
        std::cerr << "Error: Alpaca API credentials not set.\n\n";
        std::cerr << "Please set the following environment variables:\n";
        std::cerr << "  export APCA_API_KEY_ID=\"your-api-key\"\n";
        std::cerr << "  export APCA_API_SECRET_KEY=\"your-secret-key\"\n\n";
        std::cerr << "Get free paper trading credentials at:\n";
        std::cerr << "  https://alpaca.markets\n\n";
        return 1;
    }
    
    // Create trading system from environment
    auto system = create_trading_system_from_env();
    
    if (!system) {
        std::cerr << "Failed to create trading system.\n";
        return 1;
    }
    
    // Set up event callbacks
    system->on_trade_event([](const TradeEvent& event) {
        std::cout << "[EVENT] " << event.message << "\n";
    });
    
    system->on_status_change([](const SystemStatus& status) {
        std::cout << "[STATUS] Broker: " << (status.broker_connected ? "OK" : "DOWN")
                  << ", Positions: " << status.positions_count
                  << ", Orders: " << status.open_orders_count << "\n";
    });
    
    // Initialize the system
    std::cout << "Initializing trading system...\n";
    
    if (!system->initialize()) {
        std::cerr << "Failed to initialize: " << system->last_error() << "\n";
        return 1;
    }
    
    std::cout << "System initialized successfully!\n";
    
    // Run demos
    try {
        demo_system_init(*system);
        demo_market_data(*system);
        demo_positions(*system);
        demo_orders(*system);
        demo_valuation(*system);
        demo_reconciliation(*system);
    }
    catch (const std::exception& e) {
        std::cerr << "Demo error: " << e.what() << "\n";
    }
    
    // Shutdown
    print_header("Shutdown");
    std::cout << "Shutting down trading system...\n";
    system->shutdown();
    std::cout << "Done.\n";
    
    return 0;
}
