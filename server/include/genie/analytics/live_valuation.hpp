/**
 * @file live_valuation.hpp
 * @brief Real-time portfolio valuation and P&L calculation
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides real-time portfolio analytics:
 * - Live portfolio valuation from market prices
 * - Real-time P&L per position and total
 * - Price alert triggers with notifications
 * - Intraday performance tracking
 * - Mark-to-market calculations
 */
#pragma once
#ifndef GENIE_ANALYTICS_LIVE_VALUATION_HPP
#define GENIE_ANALYTICS_LIVE_VALUATION_HPP

#include "../market/price_cache.hpp"
#include "../portfolio/portfolio.hpp"
#include "../core/logging.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace genie::analytics {

/**
 * @brief Position valuation snapshot
 */
struct PositionValuation {
    std::string symbol;
    int64_t quantity{0};
    double cost_basis{0};           // Total cost
    double cost_per_share{0};       // Average cost per share
    double current_price{0};        // Latest market price
    double market_value{0};         // quantity * current_price
    double unrealized_pnl{0};       // market_value - cost_basis
    double unrealized_pnl_pct{0};   // (market_value - cost_basis) / cost_basis
    double day_pnl{0};              // Intraday P&L
    double day_pnl_pct{0};          // Intraday P&L %
    double previous_close{0};       // Yesterday's close
    double weight{0};               // Position weight in portfolio
    bool price_is_stale{false};
    
    /** @brief Where the current price came from */
    enum class PriceSource { Live, Cached, CostBasis, Unknown };
    PriceSource price_source{PriceSource::Unknown};
    
    /** @brief Seconds since price was last updated (0 if live) */
    int64_t staleness_seconds{0};
    
    std::chrono::system_clock::time_point price_timestamp;
    std::chrono::system_clock::time_point calculated_at;
    
    /** @brief True if price was obtained from market data (not fallback to cost) */
    bool has_market_price() const { return price_source != PriceSource::CostBasis; }
    
    /** @brief Human-readable price source */
    std::string price_source_str() const {
        switch (price_source) {
            case PriceSource::Live:      return "live";
            case PriceSource::Cached:    return "cached";
            case PriceSource::CostBasis: return "cost_basis_fallback";
            default:                     return "unknown";
        }
    }
};

/**
 * @brief Portfolio valuation snapshot
 */
struct PortfolioValuation {
    std::string portfolio_id;
    double total_market_value{0};
    double total_cost_basis{0};
    double cash_balance{0};
    double total_value{0};          // market_value + cash
    double total_unrealized_pnl{0};
    double total_unrealized_pnl_pct{0};
    double day_pnl{0};
    double day_pnl_pct{0};
    double previous_close_value{0};
    int positions_count{0};
    int stale_positions_count{0};
    std::vector<PositionValuation> positions;
    std::chrono::system_clock::time_point calculated_at;
    
    bool is_valid() const {
        return total_value > 0 && !positions.empty();
    }
};

/**
 * @brief Alert condition types
 */
enum class AlertCondition {
    PriceAbove,         // Price crosses above threshold
    PriceBelow,         // Price crosses below threshold
    PriceChange,        // Absolute price change
    PriceChangePct,     // Percentage price change
    VolumeAbove,        // Volume exceeds threshold
    PnlAbove,           // P&L exceeds threshold
    PnlBelow,           // P&L falls below threshold
    PnlPctAbove,        // P&L % exceeds threshold
    PnlPctBelow,        // P&L % falls below threshold
    PortfolioValue,     // Total portfolio value threshold
    PositionWeight      // Position weight threshold
};

/**
 * @brief Price alert definition
 */
struct PriceAlert {
    std::string id;
    std::string symbol;
    AlertCondition condition;
    double threshold{0};
    bool triggered{false};
    bool one_time{true};            // Delete after trigger
    bool enabled{true};
    std::string message;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point triggered_at;
    double triggered_value{0};
};

/**
 * @brief Alert trigger event
 */
struct AlertEvent {
    std::string alert_id;
    std::string symbol;
    AlertCondition condition;
    double threshold;
    double actual_value;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Callback types
 */
using ValuationCallback = std::function<void(const PortfolioValuation&)>;
using AlertCallback = std::function<void(const AlertEvent&)>;
using PriceUpdateCallback = std::function<void(const std::string&, double)>;

/**
 * @brief Live portfolio valuation engine
 */
class LiveValuationEngine {
public:
    explicit LiveValuationEngine(market::PriceCache& price_cache)
        : price_cache_(price_cache) {}
    
    // === Position Valuation ===
    
    /**
     * @brief Calculate valuation for single position
     */
    PositionValuation value_position(const std::string& symbol,
                                     int64_t quantity,
                                     double cost_basis,
                                     double previous_close = 0) {
        PositionValuation val;
        val.symbol = symbol;
        val.quantity = quantity;
        val.cost_basis = cost_basis;
        val.cost_per_share = quantity != 0 ? cost_basis / quantity : 0;
        val.previous_close = previous_close;
        val.calculated_at = std::chrono::system_clock::now();
        
        // Get current price from cache
        auto cached = price_cache_.get(symbol);
        if (cached && cached->is_valid()) {
            val.current_price = cached->price;
            val.price_timestamp = cached->cached_at;
            val.price_is_stale = price_cache_.is_stale(symbol);
            
            // Calculate staleness in seconds
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                val.calculated_at - cached->cached_at);
            val.staleness_seconds = age.count();
            
            // Classify price source
            val.price_source = val.price_is_stale ? 
                PositionValuation::PriceSource::Cached : 
                PositionValuation::PriceSource::Live;
            
            // Use previous close from cache if not provided
            if (previous_close <= 0 && cached->change != 0) {
                val.previous_close = cached->price - cached->change;
            }
            
            // Warn if stale price for actively-traded security
            if (val.price_is_stale && val.staleness_seconds > 3600) {
                ::genie::logger().log(::genie::LogLevel::WARN, "LiveValuation", "Stale price for " + symbol 
                          + " (" + std::to_string(val.staleness_seconds / 60) 
                          + " min old) -- consider refreshing market data.");
            }
        } else {
            // No market price available - fall back to cost basis as placeholder.
            // This prevents zero-value positions but may mask stale data for 
            // normally-traded securities. The price_source field and warning log 
            // allow callers and operators to detect this condition.
            val.current_price = val.cost_per_share;
            val.price_is_stale = true;
            val.price_source = PositionValuation::PriceSource::CostBasis;
            val.staleness_seconds = -1;  // Unknown age
            
            ::genie::logger().log(::genie::LogLevel::WARN, "LiveValuation", "No market price for " + symbol 
                      + " -- using cost basis ($" + std::to_string(val.cost_per_share) 
                      + ") as fallback; P&L will show zero "
                      + "until live prices are available.");
        }
        
        // Calculate market value
        val.market_value = val.quantity * val.current_price;
        
        // Calculate unrealized P&L
        val.unrealized_pnl = val.market_value - val.cost_basis;
        val.unrealized_pnl_pct = val.cost_basis != 0 ? 
            (val.unrealized_pnl / val.cost_basis) * 100.0 : 0;
        
        // Calculate day P&L
        if (val.previous_close > 0) {
            double prev_value = val.quantity * val.previous_close;
            val.day_pnl = val.market_value - prev_value;
            val.day_pnl_pct = prev_value != 0 ?
                (val.day_pnl / prev_value) * 100.0 : 0;
        }
        
        return val;
    }
    
    /**
     * @brief Calculate full portfolio valuation
     */
    PortfolioValuation value_portfolio(const portfolio::Portfolio& port) {
        PortfolioValuation val;
        val.portfolio_id = port.id();
        val.cash_balance = port.cash_balance().amount;
        val.calculated_at = std::chrono::system_clock::now();
        
        double total_value = 0;
        double total_cost = 0;
        double total_day_pnl = 0;
        double prev_total_value = 0;
        int stale_count = 0;
        
        // Value each position
        for (const auto& [sym, position] : port.positions()) {
            double prev_close = get_previous_close(sym);
            
            auto pos_val = value_position(
                sym, static_cast<int64_t>(position.quantity()), 
                position.cost_basis().amount, prev_close);
            
            total_value += pos_val.market_value;
            total_cost += pos_val.cost_basis;
            total_day_pnl += pos_val.day_pnl;
            prev_total_value += position.quantity() * pos_val.previous_close;
            
            if (pos_val.price_is_stale) {
                stale_count++;
            }
            
            val.positions.push_back(pos_val);
        }
        
        // Calculate weights
        double portfolio_total = total_value + val.cash_balance;
        for (auto& pos : val.positions) {
            pos.weight = portfolio_total != 0 ?
                (pos.market_value / portfolio_total) * 100.0 : 0;
        }
        
        // Set portfolio totals
        val.total_market_value = total_value;
        val.total_cost_basis = total_cost;
        val.total_value = portfolio_total;
        val.total_unrealized_pnl = total_value - total_cost;
        val.total_unrealized_pnl_pct = total_cost != 0 ?
            (val.total_unrealized_pnl / total_cost) * 100.0 : 0;
        
        val.day_pnl = total_day_pnl;
        val.previous_close_value = prev_total_value + val.cash_balance;
        val.day_pnl_pct = val.previous_close_value != 0 ?
            (val.day_pnl / val.previous_close_value) * 100.0 : 0;
        
        val.positions_count = static_cast<int>(val.positions.size());
        val.stale_positions_count = stale_count;
        
        return val;
    }
    
    // === P&L Calculations ===
    
    /**
     * @brief Get unrealized P&L for symbol
     */
    std::optional<double> get_unrealized_pnl(const std::string& symbol,
                                              int64_t quantity,
                                              double cost_basis) {
        auto price = price_cache_.get_price(symbol);
        if (!price) return std::nullopt;
        
        double market_value = quantity * (*price);
        return market_value - cost_basis;
    }
    
    /**
     * @brief Get day P&L for symbol
     */
    std::optional<double> get_day_pnl(const std::string& symbol,
                                       int64_t quantity) {
        auto cached = price_cache_.get(symbol);
        if (!cached || cached->change == 0) return std::nullopt;
        
        return quantity * cached->change;
    }
    
    /**
     * @brief Calculate P&L from trades
     */
    double calculate_realized_pnl([[maybe_unused]] const std::string& symbol,
                                  int64_t sell_quantity,
                                  double sell_price,
                                  double cost_per_share) {
        return sell_quantity * (sell_price - cost_per_share);
    }
    
    // === Price Alerts ===
    
    /**
     * @brief Create price alert
     */
    std::string create_alert(const std::string& symbol,
                             AlertCondition condition,
                             double threshold,
                             const std::string& message = "") {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        PriceAlert alert;
        alert.id = generate_alert_id();
        alert.symbol = symbol;
        alert.condition = condition;
        alert.threshold = threshold;
        alert.message = message.empty() ? 
            format_alert_message(symbol, condition, threshold) : message;
        alert.created_at = std::chrono::system_clock::now();
        
        alerts_[alert.id] = alert;
        symbol_alerts_[symbol].insert(alert.id);
        
        return alert.id;
    }
    
    /**
     * @brief Delete alert
     */
    bool delete_alert(const std::string& alert_id) {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        auto it = alerts_.find(alert_id);
        if (it == alerts_.end()) return false;
        
        symbol_alerts_[it->second.symbol].erase(alert_id);
        alerts_.erase(it);
        return true;
    }
    
    /**
     * @brief Add alert (alias for create_alert)
     */
    std::string add_alert(const std::string& symbol,
                          AlertCondition condition,
                          double threshold,
                          const std::string& message = "") {
        return create_alert(symbol, condition, threshold, message);
    }
    
    /**
     * @brief Add alert from PriceAlert struct
     */
    std::string add_alert(const PriceAlert& alert) {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        PriceAlert new_alert = alert;
        if (new_alert.id.empty()) {
            new_alert.id = generate_alert_id();
        }
        new_alert.created_at = std::chrono::system_clock::now();
        
        alerts_[new_alert.id] = new_alert;
        symbol_alerts_[new_alert.symbol].insert(new_alert.id);
        
        return new_alert.id;
    }
    
    /**
     * @brief Get all alerts for symbol
     */
    std::vector<PriceAlert> get_alerts(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        std::vector<PriceAlert> result;
        auto it = symbol_alerts_.find(symbol);
        if (it != symbol_alerts_.end()) {
            for (const auto& id : it->second) {
                auto alert_it = alerts_.find(id);
                if (alert_it != alerts_.end()) {
                    result.push_back(alert_it->second);
                }
            }
        }
        return result;
    }
    
    /**
     * @brief Get all active alerts
     */
    std::vector<PriceAlert> get_all_alerts() const {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        std::vector<PriceAlert> result;
        for (const auto& [_, alert] : alerts_) {
            if (alert.enabled && !alert.triggered) {
                result.push_back(alert);
            }
        }
        return result;
    }
    
    /**
     * @brief Check alerts against current prices
     */
    std::vector<AlertEvent> check_alerts() {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        std::vector<AlertEvent> triggered;
        std::vector<std::string> to_delete;
        
        for (auto& [id, alert] : alerts_) {
            if (!alert.enabled || alert.triggered) continue;
            
            auto price_entry = price_cache_.get(alert.symbol);
            if (!price_entry) continue;
            
            double value = 0;
            bool is_triggered = false;
            
            switch (alert.condition) {
                case AlertCondition::PriceAbove:
                    value = price_entry->price;
                    is_triggered = value > alert.threshold;
                    break;
                    
                case AlertCondition::PriceBelow:
                    value = price_entry->price;
                    is_triggered = value < alert.threshold;
                    break;
                    
                case AlertCondition::PriceChange:
                    value = std::abs(price_entry->change);
                    is_triggered = value >= alert.threshold;
                    break;
                    
                case AlertCondition::PriceChangePct:
                    value = std::abs(price_entry->change_percent);
                    is_triggered = value >= alert.threshold;
                    break;
                    
                case AlertCondition::VolumeAbove:
                    value = static_cast<double>(price_entry->volume);
                    is_triggered = value > alert.threshold;
                    break;
                    
                default:
                    continue;
            }
            
            if (is_triggered) {
                alert.triggered = true;
                alert.triggered_at = std::chrono::system_clock::now();
                alert.triggered_value = value;
                
                AlertEvent event;
                event.alert_id = id;
                event.symbol = alert.symbol;
                event.condition = alert.condition;
                event.threshold = alert.threshold;
                event.actual_value = value;
                event.message = alert.message;
                event.timestamp = alert.triggered_at;
                
                triggered.push_back(event);
                
                // Fire callback
                if (on_alert_) {
                    on_alert_(event);
                }
                
                // Mark for deletion if one-time
                if (alert.one_time) {
                    to_delete.push_back(id);
                }
            }
        }
        
        // Clean up one-time alerts
        for (const auto& id : to_delete) {
            symbol_alerts_[alerts_[id].symbol].erase(id);
            alerts_.erase(id);
        }
        
        return triggered;
    }
    
    /**
     * @brief Process price update and check alerts
     */
    void on_price_update(const std::string& symbol, double price) {
        // Update cache if callback is set to receive updates
        market::CachedPrice entry;
        entry.symbol = symbol;
        entry.price = price;
        entry.timestamp = std::chrono::system_clock::now();
        price_cache_.set(entry);
        
        // Check alerts for this symbol
        check_symbol_alerts(symbol);
        
        // Fire update callback
        if (on_price_update_) {
            on_price_update_(symbol, price);
        }
    }
    
    // === Callbacks ===
    
    void on_valuation_update(ValuationCallback callback) {
        on_valuation_ = callback;
    }
    
    void on_alert(AlertCallback callback) {
        on_alert_ = callback;
    }
    
    void on_price(PriceUpdateCallback callback) {
        on_price_update_ = callback;
    }
    
    // === Previous Close Management ===
    
    void set_previous_close(const std::string& symbol, double price) {
        std::lock_guard<std::mutex> lock(prev_close_mutex_);
        previous_closes_[symbol] = price;
    }
    
    double get_previous_close(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(prev_close_mutex_);
        auto it = previous_closes_.find(symbol);
        return it != previous_closes_.end() ? it->second : 0;
    }
    
    void set_previous_closes(const std::map<std::string, double>& closes) {
        std::lock_guard<std::mutex> lock(prev_close_mutex_);
        previous_closes_ = closes;
    }

private:
    market::PriceCache& price_cache_;
    
    mutable std::mutex alerts_mutex_;
    std::map<std::string, PriceAlert> alerts_;
    std::map<std::string, std::set<std::string>> symbol_alerts_;
    
    mutable std::mutex prev_close_mutex_;
    std::map<std::string, double> previous_closes_;
    
    ValuationCallback on_valuation_;
    AlertCallback on_alert_;
    PriceUpdateCallback on_price_update_;
    
    int alert_counter_{0};
    
    std::string generate_alert_id() {
        return "ALERT-" + std::to_string(++alert_counter_) + "-" +
               std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    std::string format_alert_message(const std::string& symbol,
                                     AlertCondition condition,
                                     double threshold) {
        std::string cond_str;
        switch (condition) {
            case AlertCondition::PriceAbove: cond_str = "price above"; break;
            case AlertCondition::PriceBelow: cond_str = "price below"; break;
            case AlertCondition::PriceChange: cond_str = "price change of"; break;
            case AlertCondition::PriceChangePct: cond_str = "price change of"; break;
            case AlertCondition::VolumeAbove: cond_str = "volume above"; break;
            case AlertCondition::PnlAbove: cond_str = "P&L above"; break;
            case AlertCondition::PnlBelow: cond_str = "P&L below"; break;
            default: cond_str = "condition"; break;
        }
        
        std::ostringstream oss;
        oss << symbol << " " << cond_str << " " << std::fixed << std::setprecision(2) << threshold;
        
        if (condition == AlertCondition::PriceChangePct ||
            condition == AlertCondition::PnlPctAbove ||
            condition == AlertCondition::PnlPctBelow) {
            oss << "%";
        }
        
        return oss.str();
    }
    
    void check_symbol_alerts(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(alerts_mutex_);
        
        auto it = symbol_alerts_.find(symbol);
        if (it == symbol_alerts_.end()) return;
        
        auto price_entry = price_cache_.get(symbol);
        if (!price_entry) return;
        
        for (const auto& alert_id : it->second) {
            auto alert_it = alerts_.find(alert_id);
            if (alert_it == alerts_.end()) continue;
            
            auto& alert = alert_it->second;
            if (!alert.enabled || alert.triggered) continue;
            
            double value = 0;
            bool is_triggered = false;
            
            switch (alert.condition) {
                case AlertCondition::PriceAbove:
                    value = price_entry->price;
                    is_triggered = value > alert.threshold;
                    break;
                    
                case AlertCondition::PriceBelow:
                    value = price_entry->price;
                    is_triggered = value < alert.threshold;
                    break;
                    
                default:
                    continue;
            }
            
            if (is_triggered) {
                alert.triggered = true;
                alert.triggered_at = std::chrono::system_clock::now();
                alert.triggered_value = value;
                
                AlertEvent event;
                event.alert_id = alert_id;
                event.symbol = symbol;
                event.condition = alert.condition;
                event.threshold = alert.threshold;
                event.actual_value = value;
                event.message = alert.message;
                event.timestamp = alert.triggered_at;
                
                if (on_alert_) {
                    on_alert_(event);
                }
            }
        }
    }
};

/**
 * @brief Intraday P&L tracker
 */
class IntradayPnLTracker {
public:
    struct PnLSnapshot {
        std::chrono::system_clock::time_point timestamp;
        double total_pnl{0};
        double total_value{0};
        std::map<std::string, double> position_pnl;
    };
    
    void record_snapshot(const PortfolioValuation& val) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PnLSnapshot snapshot;
        snapshot.timestamp = val.calculated_at;
        snapshot.total_pnl = val.day_pnl;
        snapshot.total_value = val.total_value;
        
        for (const auto& pos : val.positions) {
            snapshot.position_pnl[pos.symbol] = pos.day_pnl;
        }
        
        snapshots_.push_back(snapshot);
        
        // Keep only last N snapshots
        if (snapshots_.size() > max_snapshots_) {
            snapshots_.erase(snapshots_.begin());
        }
    }
    
    std::vector<PnLSnapshot> get_snapshots() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshots_;
    }
    
    double get_high_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshots_.empty()) return 0;
        
        return std::max_element(snapshots_.begin(), snapshots_.end(),
            [](const PnLSnapshot& a, const PnLSnapshot& b) {
                return a.total_pnl < b.total_pnl;
            })->total_pnl;
    }
    
    double get_low_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (snapshots_.empty()) return 0;
        
        return std::min_element(snapshots_.begin(), snapshots_.end(),
            [](const PnLSnapshot& a, const PnLSnapshot& b) {
                return a.total_pnl < b.total_pnl;
            })->total_pnl;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.clear();
    }
    
    void set_max_snapshots(size_t max) { max_snapshots_ = max; }

private:
    mutable std::mutex mutex_;
    std::vector<PnLSnapshot> snapshots_;
    size_t max_snapshots_{1000};
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_LIVE_VALUATION_HPP
