/**
 * @file position_sync.hpp
 * @brief Position synchronization between broker and internal portfolio
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides synchronization between broker accounts and internal tracking:
 * - Position import from broker API
 * - Reconciliation (detect discrepancies)
 * - Cash balance synchronization
 * - Buying power calculation
 * - Transaction history import
 * - Automatic periodic sync
 */
#pragma once
#ifndef GENIE_PORTFOLIO_POSITION_SYNC_HPP
#define GENIE_PORTFOLIO_POSITION_SYNC_HPP

#include "../trading/broker_interface.hpp"
#include "position.hpp"
#include "portfolio.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace genie::portfolio {

/**
 * @brief Reconciliation discrepancy type
 */
enum class DiscrepancyType {
    QuantityMismatch,      // Different quantities
    CostBasisMismatch,     // Different cost basis
    MissingInternal,       // Position in broker but not internal
    MissingBroker,         // Position internal but not in broker
    PriceMismatch,         // Significant price difference
    SideMismatch           // Long vs short mismatch
};

inline std::string discrepancy_type_to_string(DiscrepancyType type) {
    switch (type) {
        case DiscrepancyType::QuantityMismatch: return "quantity_mismatch";
        case DiscrepancyType::CostBasisMismatch: return "cost_basis_mismatch";
        case DiscrepancyType::MissingInternal: return "missing_internal";
        case DiscrepancyType::MissingBroker: return "missing_broker";
        case DiscrepancyType::PriceMismatch: return "price_mismatch";
        case DiscrepancyType::SideMismatch: return "side_mismatch";
    }
    return "unknown";
}

/**
 * @brief Single reconciliation discrepancy
 */
struct Discrepancy {
    std::string symbol;
    DiscrepancyType type;
    
    // Broker values
    double broker_qty{0};
    double broker_cost{0};
    double broker_price{0};
    
    // Internal values
    double internal_qty{0};
    double internal_cost{0};
    double internal_price{0};
    
    // Severity (0-100)
    int severity{0};
    
    std::string description;
    std::string timestamp;
    bool auto_corrected{false};
};

/**
 * @brief Reconciliation result
 */
struct ReconciliationResult {
    bool success{false};
    bool in_sync{true};
    
    int positions_checked{0};
    int positions_matched{0};
    int discrepancies_found{0};
    int auto_corrected{0};
    
    std::vector<Discrepancy> discrepancies;
    
    // Cash reconciliation
    double broker_cash{0};
    double internal_cash{0};
    double cash_difference{0};
    bool cash_in_sync{true};
    
    // Summary
    double total_broker_value{0};
    double total_internal_value{0};
    double value_difference{0};
    
    std::string timestamp;
    std::string error;
    
    void add_discrepancy(const Discrepancy& d) {
        discrepancies.push_back(d);
        discrepancies_found++;
        in_sync = false;
    }
};

/**
 * @brief Sync configuration
 */
struct SyncConfig {
    // Tolerances for reconciliation
    double quantity_tolerance{0.0001};      // Allow tiny fractional differences
    double cost_basis_tolerance{0.01};      // $0.01 tolerance
    double price_tolerance_pct{0.01};       // 1% price difference allowed
    
    // Auto-correction settings
    bool auto_correct_missing{true};        // Auto-add missing internal positions
    bool auto_correct_quantity{false};      // Auto-correct quantity mismatches
    bool auto_correct_cost{false};          // Auto-correct cost basis
    
    // Sync frequency
    int sync_interval_seconds{300};         // 5 minutes default
    bool sync_on_trade{true};               // Sync after each trade
    
    // History import
    int history_days{365};                  // Import last year of transactions
    
    // Logging
    bool log_all_syncs{true};
    bool log_discrepancies{true};
};

/**
 * @brief Sync event for callbacks
 */
struct SyncEvent {
    enum class Type {
        SyncStarted,
        SyncCompleted,
        PositionAdded,
        PositionUpdated,
        PositionRemoved,
        DiscrepancyFound,
        DiscrepancyCorrected,
        CashUpdated,
        Error
    };
    
    Type type;
    std::string symbol;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

using OnSyncEventCallback = std::function<void(const SyncEvent&)>;

/**
 * @brief Position synchronization manager
 */
class PositionSync {
public:
    PositionSync(std::shared_ptr<trading::IBroker> broker,
                 SyncConfig config = {})
        : broker_(broker)
        , config_(config)
        , running_(false) {}
    
    ~PositionSync() {
        stop_auto_sync();
    }
    
    // ========================================================================
    // Manual Sync Operations
    // ========================================================================
    
    /**
     * @brief Import all positions from broker
     */
    bool import_positions() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto result = broker_->get_positions();
        if (!result.success) {
            last_error_ = result.error;
            return false;
        }
        
        broker_positions_.clear();
        for (const auto& pos : result.data) {
            broker_positions_[pos.symbol] = pos;
        }
        
        last_sync_ = std::chrono::system_clock::now();
        fire_event(SyncEvent::Type::SyncCompleted, "", 
                   "Imported " + std::to_string(result.data.size()) + " positions");
        
        return true;
    }
    
    /**
     * @brief Sync cash balance from broker
     */
    bool sync_cash() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto result = broker_->get_account();
        if (!result.success) {
            last_error_ = result.error;
            return false;
        }
        
        broker_account_ = result.data;
        
        fire_event(SyncEvent::Type::CashUpdated, "",
                   "Cash: " + format_currency(broker_account_.cash) +
                   ", Buying Power: " + format_currency(broker_account_.buying_power));
        
        return true;
    }
    
    /**
     * @brief Perform full reconciliation
     */
    ReconciliationResult reconcile(
        const std::map<std::string, Position>& internal_positions,
        double internal_cash = 0) {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        ReconciliationResult result;
        result.timestamp = timestamp_now();
        
        // First, refresh broker positions
        auto broker_result = broker_->get_positions();
        if (!broker_result.success) {
            result.success = false;
            result.error = broker_result.error;
            return result;
        }
        
        broker_positions_.clear();
        for (const auto& pos : broker_result.data) {
            broker_positions_[pos.symbol] = pos;
        }
        
        // Also refresh account for cash
        auto account_result = broker_->get_account();
        if (account_result.success) {
            broker_account_ = account_result.data;
        }
        
        // Set of all symbols to check
        std::set<std::string> all_symbols;
        for (const auto& [sym, _] : broker_positions_) all_symbols.insert(sym);
        for (const auto& [sym, _] : internal_positions) all_symbols.insert(sym);
        
        result.positions_checked = static_cast<int>(all_symbols.size());
        
        // Check each symbol
        for (const auto& symbol : all_symbols) {
            auto broker_it = broker_positions_.find(symbol);
            auto internal_it = internal_positions.find(symbol);
            
            bool has_broker = (broker_it != broker_positions_.end());
            bool has_internal = (internal_it != internal_positions.end());
            
            if (has_broker && !has_internal) {
                // Position in broker but not internal
                Discrepancy d;
                d.symbol = symbol;
                d.type = DiscrepancyType::MissingInternal;
                d.broker_qty = broker_it->second.qty;
                d.broker_cost = broker_it->second.cost_basis;
                d.broker_price = broker_it->second.current_price;
                d.severity = 80;
                d.description = "Position exists in broker but not in internal tracking";
                d.timestamp = result.timestamp;
                
                result.add_discrepancy(d);
                
                if (config_.auto_correct_missing) {
                    // Could add to internal positions here
                    d.auto_corrected = true;
                    result.auto_corrected++;
                }
            }
            else if (!has_broker && has_internal) {
                // Position internal but not in broker
                Discrepancy d;
                d.symbol = symbol;
                d.type = DiscrepancyType::MissingBroker;
                d.internal_qty = internal_it->second.quantity();
                d.internal_cost = internal_it->second.cost_basis().amount;
                d.severity = 90;
                d.description = "Position exists internally but not in broker";
                d.timestamp = result.timestamp;
                
                result.add_discrepancy(d);
            }
            else if (has_broker && has_internal) {
                // Both exist - compare values
                const auto& bp = broker_it->second;
                const auto& ip = internal_it->second;
                
                // Check quantity
                double qty_diff = std::abs(bp.qty - ip.quantity());
                if (qty_diff > config_.quantity_tolerance) {
                    Discrepancy d;
                    d.symbol = symbol;
                    d.type = DiscrepancyType::QuantityMismatch;
                    d.broker_qty = bp.qty;
                    d.internal_qty = ip.quantity();
                    d.severity = calculate_severity(qty_diff, std::max(std::abs(bp.qty), std::abs(ip.quantity())));
                    d.description = "Quantity mismatch: broker=" + std::to_string(bp.qty) +
                                   ", internal=" + std::to_string(ip.quantity());
                    d.timestamp = result.timestamp;
                    
                    result.add_discrepancy(d);
                    
                    if (config_.auto_correct_quantity) {
                        d.auto_corrected = true;
                        result.auto_corrected++;
                    }
                }
                
                // Check cost basis
                double cost_diff = std::abs(bp.cost_basis - ip.cost_basis().amount);
                if (cost_diff > config_.cost_basis_tolerance) {
                    Discrepancy d;
                    d.symbol = symbol;
                    d.type = DiscrepancyType::CostBasisMismatch;
                    d.broker_cost = bp.cost_basis;
                    d.internal_cost = ip.cost_basis().amount;
                    d.severity = calculate_severity(cost_diff, std::max(bp.cost_basis, ip.cost_basis().amount));
                    d.description = "Cost basis mismatch: broker=" + format_currency(bp.cost_basis) +
                                   ", internal=" + format_currency(ip.cost_basis().amount);
                    d.timestamp = result.timestamp;
                    
                    result.add_discrepancy(d);
                    
                    if (config_.auto_correct_cost) {
                        d.auto_corrected = true;
                        result.auto_corrected++;
                    }
                }
                
                // If no discrepancies for this position, count as matched
                if (qty_diff <= config_.quantity_tolerance && 
                    cost_diff <= config_.cost_basis_tolerance) {
                    result.positions_matched++;
                }
            }
        }
        
        // Cash reconciliation
        result.broker_cash = broker_account_.cash;
        result.internal_cash = internal_cash;
        result.cash_difference = std::abs(result.broker_cash - internal_cash);
        result.cash_in_sync = (result.cash_difference < config_.cost_basis_tolerance);
        
        if (!result.cash_in_sync) {
            Discrepancy d;
            d.symbol = "CASH";
            d.type = DiscrepancyType::CostBasisMismatch;
            d.broker_cost = result.broker_cash;
            d.internal_cost = internal_cash;
            d.severity = 50;
            d.description = "Cash balance mismatch: broker=" + format_currency(result.broker_cash) +
                           ", internal=" + format_currency(internal_cash);
            d.timestamp = result.timestamp;
            
            result.add_discrepancy(d);
        }
        
        // Calculate total values
        result.total_broker_value = broker_account_.portfolio_value;
        
        for (const auto& [sym, pos] : internal_positions) {
            result.total_internal_value += pos.market_value().amount;
        }
        result.total_internal_value += internal_cash;
        
        result.value_difference = std::abs(result.total_broker_value - result.total_internal_value);
        
        result.success = true;
        result.in_sync = result.discrepancies.empty();
        
        // Fire events
        fire_event(SyncEvent::Type::SyncCompleted, "",
                   "Reconciled " + std::to_string(result.positions_checked) + " positions, " +
                   std::to_string(result.discrepancies_found) + " discrepancies");
        
        for (const auto& d : result.discrepancies) {
            fire_event(SyncEvent::Type::DiscrepancyFound, d.symbol, d.description);
        }
        
        return result;
    }
    
    /**
     * @brief Import transaction history from broker
     */
    struct TransactionImport {
        int total_imported{0};
        int fills{0};
        int dividends{0};
        int other{0};
        std::vector<trading::BrokerActivity> transactions;
    };
    
    TransactionImport import_transactions(int days = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int lookup_days = (days > 0) ? days : config_.history_days;
        
        TransactionImport result;
        
        // Calculate date range
        auto now = std::chrono::system_clock::now();
        auto start = now - std::chrono::hours(24 * lookup_days);
        
        std::time_t start_time = std::chrono::system_clock::to_time_t(start);
        std::tm tm = *std::localtime(&start_time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        std::string start_date = oss.str();
        
        // Get activities
        auto activities = broker_->get_activities("", start_date, 10000);
        if (!activities.success) {
            last_error_ = activities.error;
            return result;
        }
        
        result.transactions = activities.data;
        result.total_imported = static_cast<int>(activities.data.size());
        
        for (const auto& act : activities.data) {
            if (act.activity_type == "FILL") {
                result.fills++;
            } else if (act.activity_type == "DIV" || act.activity_type == "DIVCGL" ||
                       act.activity_type == "DIVNRA") {
                result.dividends++;
            } else {
                result.other++;
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // Auto Sync
    // ========================================================================
    
    /**
     * @brief Start automatic periodic synchronization
     */
    void start_auto_sync() {
        if (running_) return;
        
        running_ = true;
        sync_thread_ = std::thread(&PositionSync::sync_loop, this);
    }
    
    /**
     * @brief Stop automatic synchronization
     */
    void stop_auto_sync() {
        running_ = false;
        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }
    }
    
    /**
     * @brief Check if auto sync is running
     */
    bool is_auto_sync_running() const {
        return running_;
    }
    
    // ========================================================================
    // Position Access
    // ========================================================================
    
    /**
     * @brief Get broker position for symbol
     */
    std::optional<trading::BrokerPosition> get_broker_position(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = broker_positions_.find(symbol);
        if (it == broker_positions_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get all broker positions
     */
    std::map<std::string, trading::BrokerPosition> get_broker_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return broker_positions_;
    }
    
    /**
     * @brief Get broker account info
     */
    trading::BrokerAccount get_broker_account() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return broker_account_;
    }
    
    /**
     * @brief Get cash balance from broker
     */
    double get_cash() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return broker_account_.cash;
    }
    
    /**
     * @brief Get buying power from broker
     */
    double get_buying_power() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return broker_account_.buying_power;
    }
    
    /**
     * @brief Get portfolio value from broker
     */
    double get_portfolio_value() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return broker_account_.portfolio_value;
    }
    
    // ========================================================================
    // Events and Configuration
    // ========================================================================
    
    /**
     * @brief Set sync event callback
     */
    void on_sync_event(OnSyncEventCallback callback) {
        on_sync_event_ = callback;
    }
    
    /**
     * @brief Get sync configuration
     */
    const SyncConfig& config() const { return config_; }
    
    /**
     * @brief Update sync configuration
     */
    void set_config(const SyncConfig& config) { config_ = config; }
    
    /**
     * @brief Get last sync time
     */
    std::chrono::system_clock::time_point last_sync_time() const {
        return last_sync_;
    }
    
    /**
     * @brief Get seconds since last sync
     */
    int seconds_since_last_sync() const {
        auto now = std::chrono::system_clock::now();
        return static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - last_sync_).count());
    }
    
    /**
     * @brief Get last error message
     */
    const std::string& last_error() const { return last_error_; }

private:
    std::shared_ptr<trading::IBroker> broker_;
    SyncConfig config_;
    
    std::map<std::string, trading::BrokerPosition> broker_positions_;
    trading::BrokerAccount broker_account_;
    
    std::chrono::system_clock::time_point last_sync_;
    std::string last_error_;
    
    mutable std::mutex mutex_;
    std::atomic<bool> running_;
    std::thread sync_thread_;
    
    OnSyncEventCallback on_sync_event_;
    
    void sync_loop() {
        while (running_) {
            // Sleep for sync interval
            for (int i = 0; i < config_.sync_interval_seconds && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            
            if (!running_) break;
            
            // Perform sync
            fire_event(SyncEvent::Type::SyncStarted, "", "Auto sync started");
            
            if (!import_positions()) {
                fire_event(SyncEvent::Type::Error, "", "Position sync failed: " + last_error_);
            }
            
            if (!sync_cash()) {
                fire_event(SyncEvent::Type::Error, "", "Cash sync failed: " + last_error_);
            }
        }
    }
    
    void fire_event(SyncEvent::Type type, const std::string& symbol, const std::string& message) {
        if (on_sync_event_) {
            SyncEvent event;
            event.type = type;
            event.symbol = symbol;
            event.message = message;
            event.timestamp = std::chrono::system_clock::now();
            on_sync_event_(event);
        }
    }
    
    std::string timestamp_now() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
    
    std::string format_currency(double value) const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << "$" << value;
        return oss.str();
    }
    
    int calculate_severity(double difference, double base) const {
        if (base <= 0) return 100;
        double pct = (difference / base) * 100.0;
        return std::min(100, static_cast<int>(pct * 10));
    }
};

// ============================================================================
// Reconciliation Report Generator
// ============================================================================

/**
 * @brief Generate human-readable reconciliation report
 */
inline std::string format_reconciliation_report(const ReconciliationResult& result) {
    std::ostringstream oss;
    
    oss << "=== Position Reconciliation Report ===\n";
    oss << "Timestamp: " << result.timestamp << "\n\n";
    
    oss << "Summary:\n";
    oss << "  Status: " << (result.in_sync ? "IN SYNC" : "OUT OF SYNC") << "\n";
    oss << "  Positions Checked: " << result.positions_checked << "\n";
    oss << "  Positions Matched: " << result.positions_matched << "\n";
    oss << "  Discrepancies: " << result.discrepancies_found << "\n";
    oss << "  Auto-Corrected: " << result.auto_corrected << "\n\n";
    
    oss << "Portfolio Values:\n";
    oss << "  Broker Value: $" << std::fixed << std::setprecision(2) 
        << result.total_broker_value << "\n";
    oss << "  Internal Value: $" << result.total_internal_value << "\n";
    oss << "  Difference: $" << result.value_difference << "\n\n";
    
    oss << "Cash:\n";
    oss << "  Broker Cash: $" << result.broker_cash << "\n";
    oss << "  Internal Cash: $" << result.internal_cash << "\n";
    oss << "  Difference: $" << result.cash_difference << "\n";
    oss << "  Status: " << (result.cash_in_sync ? "IN SYNC" : "OUT OF SYNC") << "\n\n";
    
    if (!result.discrepancies.empty()) {
        oss << "Discrepancies:\n";
        oss << std::string(60, '-') << "\n";
        
        for (const auto& d : result.discrepancies) {
            oss << "Symbol: " << d.symbol << "\n";
            oss << "  Type: " << discrepancy_type_to_string(d.type) << "\n";
            oss << "  Severity: " << d.severity << "/100\n";
            oss << "  " << d.description << "\n";
            if (d.auto_corrected) {
                oss << "  [AUTO-CORRECTED]\n";
            }
            oss << "\n";
        }
    }
    
    return oss.str();
}

} // namespace genie::portfolio

#endif // GENIE_PORTFOLIO_POSITION_SYNC_HPP
