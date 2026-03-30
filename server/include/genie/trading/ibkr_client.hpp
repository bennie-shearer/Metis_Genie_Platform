/**
 * @file ibkr_client.hpp
 * @brief Interactive Brokers Client Portal API integration
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * IBKR Client Portal API integration providing:
 * - OAuth/session authentication
 * - Account information and balances
 * - Position management
 * - Order submission and management
 * - Real-time market data
 * - Contract search and details
 * 
 * Note: Requires IB Gateway or TWS running locally
 * API Documentation: https://www.interactivebrokers.com/api/doc.html
 */
#pragma once
#ifndef GENIE_TRADING_IBKR_CLIENT_HPP
#define GENIE_TRADING_IBKR_CLIENT_HPP

#include "broker_abstraction.hpp"
#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <mutex>
#include <thread>
#include <chrono>

namespace genie::trading {

/**
 * @brief IBKR configuration
 */
struct IBKRConfig {
    std::string gateway_host{"localhost"};
    int gateway_port{5000};           // Client Portal Gateway port
    bool ssl{true};
    bool paper{false};                // Paper trading account
    int timeout_ms{15000};
    int keepalive_interval_ms{60000}; // Session keepalive
    
    std::string base_url() const {
        std::string protocol = ssl ? "https" : "http";
        return protocol + "://" + gateway_host + ":" + std::to_string(gateway_port) + "/v1/api";
    }
};

/**
 * @brief IBKR contract (instrument)
 */
struct IBKRContract {
    int64_t con_id{0};                // Contract ID
    std::string symbol;
    std::string sec_type;             // STK, OPT, FUT, CASH, etc.
    std::string exchange;
    std::string currency;
    std::string local_symbol;
    std::string trading_class;
    
    // For options
    std::string right;                // C=Call, P=Put
    double strike{0};
    std::string expiry;               // YYYYMMDD
    double multiplier{1};
    
    // Metadata
    std::string description;
    bool is_us{true};
    
    bool is_stock() const { return sec_type == "STK"; }
    bool is_option() const { return sec_type == "OPT"; }
    bool is_future() const { return sec_type == "FUT"; }
    bool is_forex() const { return sec_type == "CASH"; }
};

/**
 * @brief IBKR account info
 */
struct IBKRAccountInfo {
    std::string account_id;
    std::string account_type;         // Individual, IRA, etc.
    std::string account_title;
    std::string base_currency;
    
    // Balances
    double net_liquidation{0};
    double total_cash{0};
    double buying_power{0};
    double excess_liquidity{0};
    double maintenance_margin{0};
    double initial_margin{0};
    double available_funds{0};
    double cushion{0};                // Excess liquidity as %
    
    // Day trading
    double day_trades_remaining{0};
    double sma{0};                    // Special Memorandum Account
    
    // Status
    bool trading_enabled{true};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "IBKR Account: " << account_id << "\n";
        oss << "  Net Liquidation: $" << net_liquidation << "\n";
        oss << "  Buying Power: $" << buying_power << "\n";
        oss << "  Cash: $" << total_cash << "\n";
        oss << "  Maintenance Margin: $" << maintenance_margin << "\n";
        return oss.str();
    }
};

/**
 * @brief IBKR position
 */
struct IBKRPosition {
    std::string account_id;
    int64_t con_id{0};
    std::string symbol;
    std::string sec_type;
    double position{0};               // Shares/contracts
    double market_price{0};
    double market_value{0};
    double avg_cost{0};
    double unrealized_pnl{0};
    double realized_pnl{0};
    std::string currency;
    
    UnifiedPosition to_unified() const {
        UnifiedPosition pos;
        pos.symbol = symbol;
        pos.broker = BrokerId::InteractiveBrokers;
        pos.qty = position;
        pos.avg_entry_price = avg_cost;
        pos.current_price = market_price;
        pos.market_value = market_value;
        pos.cost_basis = position * avg_cost;
        pos.unrealized_pl = unrealized_pnl;
        pos.asset_class = sec_type;
        return pos;
    }
};

/**
 * @brief IBKR order
 */
struct IBKROrder {
    std::string order_id;
    std::string account_id;
    int64_t con_id{0};
    std::string symbol;
    std::string sec_type;
    
    std::string side;                 // BUY, SELL
    std::string order_type;           // MKT, LMT, STP, STP_LMT
    std::string tif;                  // DAY, GTC, IOC, etc.
    
    double total_qty{0};
    double filled_qty{0};
    double remaining_qty{0};
    double limit_price{0};
    double stop_price{0};
    double avg_fill_price{0};
    
    std::string status;               // PreSubmitted, Submitted, Filled, Cancelled, etc.
    std::string last_execution_time;
    std::string order_ref;            // Client reference
    
    // Advanced
    bool outside_rth{false};          // Outside regular trading hours
    std::string algo;                 // Algorithm type
    
    UnifiedOrder to_unified() const {
        UnifiedOrder order;
        order.id = order_id;
        order.broker_order_id = order_id;
        order.account_id = account_id;
        order.broker = BrokerId::InteractiveBrokers;
        order.symbol = symbol;
        
        order.side = (side == "BUY") ? OrderSide::Buy : OrderSide::Sell;
        
        if (order_type == "MKT") order.type = OrderType::Market;
        else if (order_type == "LMT") order.type = OrderType::Limit;
        else if (order_type == "STP") order.type = OrderType::Stop;
        else if (order_type == "STP_LMT") order.type = OrderType::StopLimit;
        
        if (tif == "DAY") order.time_in_force = TimeInForce::Day;
        else if (tif == "GTC") order.time_in_force = TimeInForce::GTC;
        else if (tif == "IOC") order.time_in_force = TimeInForce::IOC;
        
        order.qty = total_qty;
        order.filled_qty = filled_qty;
        order.limit_price = limit_price;
        order.stop_price = stop_price;
        order.filled_avg_price = avg_fill_price;
        order.extended_hours = outside_rth;
        
        if (status == "PreSubmitted") order.status = OrderStatus::PendingNew;
        else if (status == "Submitted") order.status = OrderStatus::New;
        else if (status == "Filled") order.status = OrderStatus::Filled;
        else if (status == "Cancelled") order.status = OrderStatus::Canceled;
        else if (status == "Inactive") order.status = OrderStatus::Rejected;
        else if (status == "PendingCancel") order.status = OrderStatus::PendingCancel;
        else order.status = OrderStatus::Accepted;
        
        return order;
    }
};

/**
 * @brief Interactive Brokers Client Portal API client
 */
class IBKRBroker : public IUnifiedBroker {
public:
    explicit IBKRBroker(const IBKRConfig& config)
        : config_(config)
        , http_(config.base_url())
        , connected_(false)
        , authenticated_(false) {
        
        http_.set_timeout(config.timeout_ms);
        // IBKR uses self-signed certs in local gateway
        // TLS certificate verification is configured at the platform level
        // IBKR Client Portal Gateway uses self-signed certs in local dev
    }
    
    ~IBKRBroker() {
        stop_keepalive();
    }
    
    BrokerId broker_id() const override { return BrokerId::InteractiveBrokers; }
    std::string broker_name() const override { return "Interactive Brokers"; }
    bool is_connected() const override { return connected_ && authenticated_; }
    
    // ========================================================================
    // Connection
    // ========================================================================
    
    bool connect() override {
        // Check authentication status
        auto status = get_auth_status();
        if (!status.has_value()) {
            last_error_ = "Cannot reach IB Gateway";
            return false;
        }
        
        authenticated_ = status->authenticated;
        connected_ = status->connected;
        
        if (authenticated_ && connected_) {
            // Start keepalive thread
            start_keepalive();
            
            // Get account list
            auto accounts = get_accounts();
            if (accounts.success && !accounts.data.empty()) {
                selected_account_ = accounts.data[0].broker_account_id;
            }
            
            return true;
        }
        
        // If not authenticated, need to complete SSO login
        last_error_ = "Please complete SSO login at " + config_.base_url() + "/sso/Login";
        return false;
    }
    
    void disconnect() override {
        stop_keepalive();
        
        // Logout
        http_.post("/logout", "");
        
        connected_ = false;
        authenticated_ = false;
    }
    
    /**
     * @brief Reauthenticate session
     */
    bool reauthenticate() {
        auto response = http_.post("/iserver/reauthenticate", "");
        return response.success && response.status_code == 200;
    }
    
    // ========================================================================
    // Account
    // ========================================================================
    
    BrokerResponse<UnifiedAccount> get_account() override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedAccount>::fail("No account selected");
        }
        
        auto response = http_.get("/portfolio/" + selected_account_ + "/summary");
        if (!response.success) {
            return BrokerResponse<UnifiedAccount>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        UnifiedAccount account;
        account.id = selected_account_;
        account.broker_account_id = selected_account_;
        account.broker = BrokerId::InteractiveBrokers;
        account.name = "Interactive Brokers";
        
        // Parse summary fields
        if (json.contains("netliquidation")) {
            account.portfolio_value = parse_summary_value(json["netliquidation"]);
        }
        if (json.contains("totalcashvalue")) {
            account.cash = parse_summary_value(json["totalcashvalue"]);
        }
        if (json.contains("buyingpower")) {
            account.buying_power = parse_summary_value(json["buyingpower"]);
        }
        if (json.contains("maintmarginreq")) {
            account.maintenance_margin = parse_summary_value(json["maintmarginreq"]);
        }
        if (json.contains("equity")) {
            account.equity = parse_summary_value(json["equity"]);
        }
        
        account.currency = "USD";
        account.type = config_.paper ? AccountType::Paper : AccountType::Margin;
        
        return BrokerResponse<UnifiedAccount>::ok(account);
    }
    
    BrokerResponse<std::vector<UnifiedAccount>> get_accounts() override {
        auto response = http_.get("/portfolio/accounts");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array()) {
            return BrokerResponse<std::vector<UnifiedAccount>>::fail("Invalid response");
        }
        
        std::vector<UnifiedAccount> accounts;
        for (const auto& item : json.array()) {
            UnifiedAccount acc;
            acc.id = item.get_string("id", "");
            acc.broker_account_id = acc.id;
            acc.broker = BrokerId::InteractiveBrokers;
            acc.name = item.get_string("accountTitle", "IB Account");
            acc.currency = item.get_string("currency", "USD");
            
            std::string type = item.get_string("type", "");
            if (type == "INDIVIDUAL") acc.type = AccountType::Margin;
            else if (type == "IRA") acc.type = AccountType::IRA;
            
            accounts.push_back(acc);
        }
        
        return BrokerResponse<std::vector<UnifiedAccount>>::ok(accounts);
    }
    
    void select_account(const std::string& account_id) {
        selected_account_ = account_id;
    }
    
    // ========================================================================
    // Positions
    // ========================================================================
    
    BrokerResponse<std::vector<UnifiedPosition>> get_positions() override {
        if (selected_account_.empty()) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail("No account selected");
        }
        
        auto response = http_.get("/portfolio/" + selected_account_ + "/positions/0");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array()) {
            return BrokerResponse<std::vector<UnifiedPosition>>::fail("Invalid response");
        }
        
        std::vector<UnifiedPosition> positions;
        for (const auto& item : json.array()) {
            IBKRPosition pos;
            pos.account_id = selected_account_;
            pos.con_id = item.get_int("conid", 0);
            pos.symbol = item.get_string("contractDesc", "");
            pos.sec_type = item.get_string("assetClass", "STK");
            pos.position = item.get_double("position", 0);
            pos.market_price = item.get_double("mktPrice", 0);
            pos.market_value = item.get_double("mktValue", 0);
            pos.avg_cost = item.get_double("avgCost", 0);
            pos.unrealized_pnl = item.get_double("unrealizedPnl", 0);
            pos.realized_pnl = item.get_double("realizedPnl", 0);
            pos.currency = item.get_string("currency", "USD");
            
            // Extract just the symbol from contract description
            auto space_pos = pos.symbol.find(' ');
            if (space_pos != std::string::npos) {
                pos.symbol = pos.symbol.substr(0, space_pos);
            }
            
            positions.push_back(pos.to_unified());
        }
        
        return BrokerResponse<std::vector<UnifiedPosition>>::ok(positions);
    }
    
    BrokerResponse<UnifiedPosition> get_position(const std::string& symbol) override {
        auto result = get_positions();
        if (!result.success) {
            return BrokerResponse<UnifiedPosition>::fail(result.error);
        }
        
        for (const auto& pos : result.data) {
            if (pos.symbol == symbol) {
                return BrokerResponse<UnifiedPosition>::ok(pos);
            }
        }
        
        return BrokerResponse<UnifiedPosition>::fail("Position not found");
    }
    
    // ========================================================================
    // Orders
    // ========================================================================
    
    BrokerResponse<UnifiedOrder> submit_order(const OrderRequest& request) override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("No account selected");
        }
        
        // First, search for contract
        auto contract = search_contract(request.symbol);
        if (!contract.has_value()) {
            return BrokerResponse<UnifiedOrder>::fail("Symbol not found: " + request.symbol);
        }
        
        // Build order request
        core::JsonObject order;
        order["conid"] = std::to_string(contract->con_id);
        order["secType"] = contract->con_id;  // Actually needs conid string
        order["orderType"] = ibkr_order_type(request.type);
        order["side"] = (request.side == OrderSide::Buy) ? "BUY" : "SELL";
        order["quantity"] = static_cast<int>(request.qty);
        order["tif"] = ibkr_tif(request.time_in_force);
        
        if (request.limit_price && *request.limit_price > 0) {
            order["price"] = *request.limit_price;
        }
        if (request.stop_price && *request.stop_price > 0) {
            order["auxPrice"] = *request.stop_price;
        }
        if (request.extended_hours) {
            order["outsideRTH"] = true;
        }
        
        core::JsonObject body;
        body["orders"] = "[" + core::JsonParser::stringify(order) + "]";
        
        auto response = http_.post(
            "/iserver/account/" + selected_account_ + "/orders",
            core::JsonParser::stringify(body));
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        // Check for order confirmation required
        if (json.is_array() && !json.array().empty()) {
            const auto& first = json.array()[0];
            
            // If order needs confirmation
            if (first.contains("id") && first.contains("message")) {
                // Reply to confirm
                std::string reply_id = first.get_string("id", "");
                auto confirm_response = http_.post(
                    "/iserver/reply/" + reply_id,
                    R"({"confirmed":true})");
                
                if (confirm_response.success) {
                    json = core::JsonParser::parse(confirm_response.body);
                }
            }
            
            // Parse order response
            if (json.is_array() && !json.array().empty()) {
                const auto& order_resp = json.array()[0];
                
                UnifiedOrder unified;
                unified.id = order_resp.get_string("order_id", "");
                unified.broker_order_id = unified.id;
                unified.broker = BrokerId::InteractiveBrokers;
                unified.symbol = request.symbol;
                unified.side = request.side;
                unified.type = request.type;
                unified.qty = request.qty;
                unified.status = OrderStatus::New;
                
                return BrokerResponse<UnifiedOrder>::ok(unified);
            }
        }
        
        return BrokerResponse<UnifiedOrder>::fail("Order submission failed");
    }
    
    BrokerResponse<UnifiedOrder> get_order(const std::string& order_id) override {
        auto response = http_.get("/iserver/account/order/status/" + order_id);
        if (!response.success) {
            return BrokerResponse<UnifiedOrder>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        return BrokerResponse<UnifiedOrder>::ok(parse_order(json));
    }
    
    BrokerResponse<std::vector<UnifiedOrder>> get_orders(const std::string& status) override {
        auto response = http_.get("/iserver/account/orders");
        if (!response.success) {
            return BrokerResponse<std::vector<UnifiedOrder>>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        std::vector<UnifiedOrder> orders;
        
        if (json.contains("orders") && json["orders"].is_array()) {
            for (const auto& item : json["orders"].array()) {
                auto order = parse_order(item);
                
                if (status.empty() || order_status_to_string(order.status) == status) {
                    orders.push_back(order);
                }
            }
        }
        
        return BrokerResponse<std::vector<UnifiedOrder>>::ok(orders);
    }
    
    BrokerResponse<bool> cancel_order(const std::string& order_id) override {
        if (selected_account_.empty()) {
            return BrokerResponse<bool>::fail("No account selected");
        }
        
        auto response = http_.del(
            "/iserver/account/" + selected_account_ + "/order/" + order_id);
        
        if (!response.success && response.status_code != 200) {
            return BrokerResponse<bool>::fail(response.error);
        }
        
        return BrokerResponse<bool>::ok(true);
    }
    
    BrokerResponse<UnifiedOrder> replace_order(const std::string& order_id,
                                                const OrderRequest& request) override {
        if (selected_account_.empty()) {
            return BrokerResponse<UnifiedOrder>::fail("No account selected");
        }
        
        core::JsonObject body;
        body["quantity"] = static_cast<int>(request.qty);
        
        if (request.limit_price && *request.limit_price > 0) {
            body["price"] = *request.limit_price;
        }
        if (request.stop_price && *request.stop_price > 0) {
            body["auxPrice"] = *request.stop_price;
        }
        
        auto response = http_.post(
            "/iserver/account/" + selected_account_ + "/order/" + order_id,
            core::JsonParser::stringify(body));
        
        if (!response.success || response.status_code >= 400) {
            return BrokerResponse<UnifiedOrder>::fail(
                response.success ? response.body : response.error);
        }
        
        return get_order(order_id);
    }
    
    // ========================================================================
    // Contract Search
    // ========================================================================
    
    /**
     * @brief Search for contract by symbol
     */
    std::optional<IBKRContract> search_contract(const std::string& symbol,
                                                 const std::string& sec_type = "STK") {
        core::JsonObject body;
        body["symbol"] = symbol;
        body["name"] = true;
        body["secType"] = sec_type;
        
        auto response = http_.post("/iserver/secdef/search",
                                   core::JsonParser::stringify(body));
        
        if (!response.success) return std::nullopt;
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array() || json.array().empty()) return std::nullopt;
        
        // Find US stock match
        for (const auto& item : json.array()) {
            if (item.get_string("description", "").find("NASDAQ") != std::string::npos ||
                item.get_string("description", "").find("NYSE") != std::string::npos) {
                
                IBKRContract contract;
                contract.con_id = item.get_int("conid", 0);
                contract.symbol = symbol;
                contract.sec_type = sec_type;
                contract.description = item.get_string("description", "");
                
                // Get full contract details
                if (contract.con_id > 0) {
                    auto details = get_contract_details(contract.con_id);
                    if (details.has_value()) {
                        return details;
                    }
                }
                
                return contract;
            }
        }
        
        // Return first match if no US match
        const auto& first = json.array()[0];
        IBKRContract contract;
        contract.con_id = first.get_int("conid", 0);
        contract.symbol = symbol;
        contract.sec_type = sec_type;
        contract.description = first.get_string("description", "");
        
        return contract;
    }
    
    /**
     * @brief Get full contract details
     */
    std::optional<IBKRContract> get_contract_details(int64_t con_id) {
        auto response = http_.get("/iserver/contract/" + std::to_string(con_id) + "/info");
        if (!response.success) return std::nullopt;
        
        auto json = core::JsonParser::parse(response.body);
        
        IBKRContract contract;
        contract.con_id = con_id;
        contract.symbol = json.get_string("symbol", "");
        contract.sec_type = json.get_string("secType", "STK");
        contract.exchange = json.get_string("exchange", "");
        contract.currency = json.get_string("currency", "USD");
        contract.local_symbol = json.get_string("localSymbol", "");
        contract.trading_class = json.get_string("tradingClass", "");
        contract.description = json.get_string("companyName", "");
        
        if (contract.sec_type == "OPT") {
            contract.right = json.get_string("right", "");
            contract.strike = json.get_double("strike", 0);
            contract.expiry = json.get_string("maturity", "");
            contract.multiplier = json.get_double("multiplier", 100);
        }
        
        return contract;
    }
    
    // ========================================================================
    // Market Data
    // ========================================================================
    
    bool supports_market_data() const override { return true; }
    
    BrokerResponse<Quote> get_quote(const std::string& symbol) override {
        auto contract = search_contract(symbol);
        if (!contract.has_value()) {
            return BrokerResponse<Quote>::fail("Symbol not found");
        }
        
        // Request market data snapshot
        std::string fields = "31,84,85,86,87,88";  // Last, bid, ask, bid size, ask size, volume
        auto response = http_.get("/iserver/marketdata/snapshot?conids=" + 
                                  std::to_string(contract->con_id) + "&fields=" + fields);
        
        if (!response.success) {
            return BrokerResponse<Quote>::fail(response.error);
        }
        
        auto json = core::JsonParser::parse(response.body);
        if (!json.is_array() || json.array().empty()) {
            return BrokerResponse<Quote>::fail("No market data");
        }
        
        const auto& md = json.array()[0];
        
        Quote quote;
        quote.symbol = symbol;
        quote.price = parse_md_field(md, "31");      // Last price
        quote.bid = parse_md_field(md, "84");        // Bid
        quote.ask = parse_md_field(md, "85");        // Ask
        quote.bid_size = static_cast<int>(parse_md_field(md, "86"));
        quote.ask_size = static_cast<int>(parse_md_field(md, "87"));
        quote.volume = static_cast<int64_t>(parse_md_field(md, "87031"));
        
        return BrokerResponse<Quote>::ok(quote);
    }
    
    std::string last_error() const override { return last_error_; }

private:
    IBKRConfig config_;
    core::HttpClient http_;
    bool connected_;
    bool authenticated_;
    std::string selected_account_;
    std::string last_error_;
    
    std::thread keepalive_thread_;
    std::atomic<bool> keepalive_running_{false};
    
    struct AuthStatus {
        bool authenticated{false};
        bool connected{false};
        bool competing{false};
        std::string message;
    };
    
    std::optional<AuthStatus> get_auth_status() {
        auto response = http_.get("/iserver/auth/status");
        if (!response.success) return std::nullopt;
        
        auto json = core::JsonParser::parse(response.body);
        
        AuthStatus status;
        status.authenticated = json.get_bool("authenticated", false);
        status.connected = json.get_bool("connected", false);
        status.competing = json.get_bool("competing", false);
        status.message = json.get_string("message", "");
        
        return status;
    }
    
    void start_keepalive() {
        keepalive_running_ = true;
        keepalive_thread_ = std::thread([this]() {
            while (keepalive_running_) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.keepalive_interval_ms));
                
                if (keepalive_running_) {
                    // Tickle endpoint to keep session alive
                    http_.post("/tickle", "");
                }
            }
        });
    }
    
    void stop_keepalive() {
        keepalive_running_ = false;
        if (keepalive_thread_.joinable()) {
            keepalive_thread_.join();
        }
    }
    
    double parse_summary_value(const core::JsonValue& field) {
        if (field.is_object()) {
            return field.get_double("amount", 0);
        }
        return field.get_double(0);
    }
    
    double parse_md_field(const core::JsonValue& md, const std::string& field) {
        if (md.contains(field)) {
            auto val = md[field];
            if (val.is_string()) {
                std::string s = val.to_string();
                // Remove formatting characters
                s.erase(std::remove(s.begin(), s.end(), ','), s.end());
                s.erase(std::remove(s.begin(), s.end(), 'C'), s.end());
                try {
                    return std::stod(s);
                } catch (...) {
                    return 0;
                }
            }
            return val.get_double(0);
        }
        return 0;
    }
    
    std::string ibkr_order_type(OrderType type) {
        switch (type) {
            case OrderType::Market: return "MKT";
            case OrderType::Limit: return "LMT";
            case OrderType::Stop: return "STP";
            case OrderType::StopLimit: return "STP_LMT";
            default: return "MKT";
        }
    }
    
    std::string ibkr_tif(TimeInForce tif) {
        switch (tif) {
            case TimeInForce::Day: return "DAY";
            case TimeInForce::GTC: return "GTC";
            case TimeInForce::IOC: return "IOC";
            case TimeInForce::FOK: return "FOK";
            default: return "DAY";
        }
    }
    
    UnifiedOrder parse_order(const core::JsonValue& json) {
        IBKROrder order;
        order.order_id = json.get_string("orderId", "");
        order.account_id = json.get_string("acct", selected_account_);
        order.con_id = json.get_int("conid", 0);
        order.symbol = json.get_string("ticker", "");
        order.sec_type = json.get_string("secType", "STK");
        order.side = json.get_string("side", "BUY");
        order.order_type = json.get_string("orderType", "MKT");
        order.tif = json.get_string("timeInForce", "DAY");
        order.total_qty = json.get_double("totalSize", 0);
        order.filled_qty = json.get_double("filledQuantity", 0);
        order.remaining_qty = json.get_double("remainingQuantity", 0);
        order.limit_price = json.get_double("price", 0);
        order.stop_price = json.get_double("auxPrice", 0);
        order.avg_fill_price = json.get_double("avgPrice", 0);
        order.status = json.get_string("status", "");
        order.outside_rth = json.get_bool("outsideRTH", false);
        
        return order.to_unified();
    }
};

/**
 * @brief Create IBKR client from environment
 */
inline std::shared_ptr<IBKRBroker> create_ibkr_broker_from_env() {
    IBKRConfig config;
    
    if (const char* host = std::getenv("IBKR_GATEWAY_HOST")) {
        config.gateway_host = host;
    }
    if (const char* port = std::getenv("IBKR_GATEWAY_PORT")) {
        config.gateway_port = std::stoi(port);
    }
    if (const char* paper = std::getenv("IBKR_PAPER")) {
        config.paper = std::string(paper) == "true";
    }
    
    return std::make_shared<IBKRBroker>(config);
}

} // namespace genie::trading

#endif // GENIE_TRADING_IBKR_CLIENT_HPP
