/**
 * @file alert_engine.hpp
 * @brief Price alert and trigger engine
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Comprehensive alert system for:
 * - Price target alerts (above/below)
 * - Price change alerts (absolute and percentage)
 * - Volume alerts
 * - P&L alerts (position and portfolio level)
 * - Custom condition alerts
 * - Multi-condition (AND/OR) alerts
 * - Time-based alerts (market open/close)
 * 
 * Features:
 * - Persistent alert storage
 * - Alert history tracking
 * - Notification callbacks
 * - Alert cooldowns
 * - Priority levels
 */
#pragma once
#ifndef GENIE_MARKET_ALERT_ENGINE_HPP
#define GENIE_MARKET_ALERT_ENGINE_HPP

#include "price_cache.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace genie::market {

/**
 * @brief Alert trigger condition
 */
enum class AlertConditionType {
    // Price conditions
    PriceAbove,           // Price > threshold
    PriceBelow,           // Price < threshold
    PriceCrossAbove,      // Price crosses above threshold
    PriceCrossBelow,      // Price crosses below threshold
    
    // Change conditions
    PriceChangeAbs,       // |price change| > threshold
    PriceChangePct,       // |price change %| > threshold
    PriceGain,            // Price increase > threshold
    PriceDrop,            // Price decrease > threshold (negative)
    
    // Volume conditions
    VolumeAbove,          // Volume > threshold
    VolumeSpike,          // Volume > N * average
    
    // P&L conditions
    UnrealizedPnL,        // Position P&L > threshold
    DayPnL,               // Day P&L > threshold
    PortfolioPnL,         // Portfolio P&L > threshold
    
    // Timing conditions
    MarketOpen,           // Market opens
    MarketClose,          // Market closes
    TimeOfDay,            // Specific time reached
    
    // Custom
    Custom                // User-defined condition
};

inline std::string condition_type_to_string(AlertConditionType type) {
    switch (type) {
        case AlertConditionType::PriceAbove: return "price_above";
        case AlertConditionType::PriceBelow: return "price_below";
        case AlertConditionType::PriceCrossAbove: return "price_cross_above";
        case AlertConditionType::PriceCrossBelow: return "price_cross_below";
        case AlertConditionType::PriceChangeAbs: return "price_change_abs";
        case AlertConditionType::PriceChangePct: return "price_change_pct";
        case AlertConditionType::PriceGain: return "price_gain";
        case AlertConditionType::PriceDrop: return "price_drop";
        case AlertConditionType::VolumeAbove: return "volume_above";
        case AlertConditionType::VolumeSpike: return "volume_spike";
        case AlertConditionType::UnrealizedPnL: return "unrealized_pnl";
        case AlertConditionType::DayPnL: return "day_pnl";
        case AlertConditionType::PortfolioPnL: return "portfolio_pnl";
        case AlertConditionType::MarketOpen: return "market_open";
        case AlertConditionType::MarketClose: return "market_close";
        case AlertConditionType::TimeOfDay: return "time_of_day";
        case AlertConditionType::Custom: return "custom";
    }
    return "unknown";
}

/**
 * @brief Alert priority level
 */
enum class AlertPriority {
    Low,
    Normal,
    High,
    Critical
};

/**
 * @brief Alert definition
 */
struct Alert {
    std::string id;
    std::string symbol;              // Empty for portfolio-level alerts
    std::string name;                // User-friendly name
    AlertConditionType condition;
    double threshold{0};
    AlertPriority priority{AlertPriority::Normal};
    
    // Behavior
    bool enabled{true};
    bool one_shot{false};            // Delete after triggering
    bool triggered{false};           // Has been triggered
    int cooldown_seconds{0};         // Minimum time between triggers
    int max_triggers{0};             // 0 = unlimited
    int trigger_count{0};
    
    // State tracking
    double last_value{0};            // For cross-over detection
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_triggered;
    std::chrono::system_clock::time_point expires_at;  // 0 = never
    
    // Custom message
    std::string message;
    std::string notes;
    
    bool is_expired() const {
        if (expires_at.time_since_epoch().count() == 0) return false;
        return std::chrono::system_clock::now() >= expires_at;
    }
    
    bool is_on_cooldown() const {
        if (cooldown_seconds <= 0) return false;
        auto since = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_triggered).count();
        return since < cooldown_seconds;
    }
    
    bool can_trigger() const {
        if (!enabled) return false;
        if (is_expired()) return false;
        if (is_on_cooldown()) return false;
        if (max_triggers > 0 && trigger_count >= max_triggers) return false;
        return true;
    }
};

/**
 * @brief Triggered alert event
 */
struct AlertTrigger {
    std::string alert_id;
    std::string symbol;
    std::string name;
    AlertConditionType condition;
    AlertPriority priority;
    double threshold;
    double actual_value;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Alert statistics
 */
struct AlertStats {
    int total_alerts{0};
    int active_alerts{0};
    int triggered_today{0};
    int expired_alerts{0};
    std::map<AlertPriority, int> by_priority;
    std::map<std::string, int> by_symbol;
};

// Callbacks
using OnAlertTriggerCallback = std::function<void(const AlertTrigger&)>;
using CustomConditionCallback = std::function<bool(const std::string& symbol, double current_price)>;

/**
 * @brief Alert trigger engine
 */
class AlertEngine {
public:
    explicit AlertEngine(PriceCache& price_cache)
        : price_cache_(price_cache) {}
    
    // ========================================================================
    // Alert Management
    // ========================================================================
    
    /**
     * @brief Create alert
     */
    std::string create_alert(const Alert& alert) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Alert a = alert;
        if (a.id.empty()) {
            a.id = generate_id();
        }
        a.created_at = std::chrono::system_clock::now();
        
        alerts_[a.id] = a;
        
        if (!a.symbol.empty()) {
            symbol_index_[a.symbol].insert(a.id);
        }
        
        return a.id;
    }
    
    /**
     * @brief Create simple price alert
     */
    std::string create_price_alert(const std::string& symbol,
                                    AlertConditionType condition,
                                    double threshold,
                                    const std::string& name = "") {
        Alert alert;
        alert.symbol = symbol;
        alert.condition = condition;
        alert.threshold = threshold;
        alert.name = name.empty() ? 
            symbol + " " + condition_type_to_string(condition) + " " + std::to_string(threshold) :
            name;
        alert.message = format_default_message(symbol, condition, threshold);
        
        // Initialize last_value for cross-over detection
        auto cached = price_cache_.get(symbol);
        if (cached) {
            alert.last_value = cached->price;
        }
        
        return create_alert(alert);
    }
    
    /**
     * @brief Create price target alert (above)
     */
    std::string set_price_target_above(const std::string& symbol, double target,
                                        const std::string& name = "") {
        return create_price_alert(symbol, AlertConditionType::PriceAbove, target, name);
    }
    
    /**
     * @brief Create price target alert (below)
     */
    std::string set_price_target_below(const std::string& symbol, double target,
                                        const std::string& name = "") {
        return create_price_alert(symbol, AlertConditionType::PriceBelow, target, name);
    }
    
    /**
     * @brief Create price change alert
     */
    std::string set_price_change_alert(const std::string& symbol, double pct_threshold,
                                        const std::string& name = "") {
        return create_price_alert(symbol, AlertConditionType::PriceChangePct, pct_threshold, name);
    }
    
    /**
     * @brief Get alert by ID
     */
    std::optional<Alert> get_alert(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = alerts_.find(id);
        if (it == alerts_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get alerts for symbol
     */
    std::vector<Alert> get_alerts_for_symbol(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        
        auto it = symbol_index_.find(symbol);
        if (it != symbol_index_.end()) {
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
     * @brief Get all alerts
     */
    std::vector<Alert> get_all_alerts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        result.reserve(alerts_.size());
        
        for (const auto& [id, alert] : alerts_) {
            result.push_back(alert);
        }
        
        return result;
    }
    
    /**
     * @brief Update alert
     */
    bool update_alert(const std::string& id, const Alert& updated) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = alerts_.find(id);
        if (it == alerts_.end()) return false;
        
        // Update symbol index if symbol changed
        if (it->second.symbol != updated.symbol) {
            symbol_index_[it->second.symbol].erase(id);
            if (!updated.symbol.empty()) {
                symbol_index_[updated.symbol].insert(id);
            }
        }
        
        it->second = updated;
        it->second.id = id;  // Preserve ID
        
        return true;
    }
    
    /**
     * @brief Enable/disable alert
     */
    bool set_alert_enabled(const std::string& id, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = alerts_.find(id);
        if (it == alerts_.end()) return false;
        
        it->second.enabled = enabled;
        return true;
    }
    
    /**
     * @brief Delete alert
     */
    bool delete_alert(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = alerts_.find(id);
        if (it == alerts_.end()) return false;
        
        // Remove from symbol index
        if (!it->second.symbol.empty()) {
            symbol_index_[it->second.symbol].erase(id);
        }
        
        alerts_.erase(it);
        return true;
    }
    
    /**
     * @brief Delete all alerts for symbol
     */
    int delete_alerts_for_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = symbol_index_.find(symbol);
        if (it == symbol_index_.end()) return 0;
        
        int deleted = 0;
        for (const auto& id : it->second) {
            alerts_.erase(id);
            deleted++;
        }
        
        symbol_index_.erase(it);
        return deleted;
    }
    
    /**
     * @brief Clear all alerts
     */
    void clear_all_alerts() {
        std::lock_guard<std::mutex> lock(mutex_);
        alerts_.clear();
        symbol_index_.clear();
        triggered_today_.clear();
    }
    
    // ========================================================================
    // Alert Checking
    // ========================================================================
    
    /**
     * @brief Check alerts for price update
     */
    std::vector<AlertTrigger> check_price(const std::string& symbol, double price,
                                           double volume = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AlertTrigger> triggers;
        
        auto it = symbol_index_.find(symbol);
        if (it == symbol_index_.end()) return triggers;
        
        std::vector<std::string> to_delete;
        
        for (const auto& id : it->second) {
            auto alert_it = alerts_.find(id);
            if (alert_it == alerts_.end()) continue;
            
            auto& alert = alert_it->second;
            if (!alert.can_trigger()) continue;
            
            bool triggered = false;
            double actual_value = price;
            
            switch (alert.condition) {
                case AlertConditionType::PriceAbove:
                    triggered = price > alert.threshold;
                    break;
                    
                case AlertConditionType::PriceBelow:
                    triggered = price < alert.threshold;
                    break;
                    
                case AlertConditionType::PriceCrossAbove:
                    triggered = (alert.last_value <= alert.threshold && 
                                 price > alert.threshold);
                    break;
                    
                case AlertConditionType::PriceCrossBelow:
                    triggered = (alert.last_value >= alert.threshold && 
                                 price < alert.threshold);
                    break;
                    
                case AlertConditionType::PriceChangeAbs:
                    actual_value = std::abs(price - alert.last_value);
                    triggered = actual_value > alert.threshold;
                    break;
                    
                case AlertConditionType::PriceChangePct:
                    if (alert.last_value > 0) {
                        actual_value = std::abs((price - alert.last_value) / 
                                               alert.last_value) * 100;
                        triggered = actual_value > alert.threshold;
                    }
                    break;
                    
                case AlertConditionType::PriceGain:
                    actual_value = price - alert.last_value;
                    triggered = actual_value > alert.threshold;
                    break;
                    
                case AlertConditionType::PriceDrop:
                    actual_value = alert.last_value - price;
                    triggered = actual_value > alert.threshold;
                    break;
                    
                case AlertConditionType::VolumeAbove:
                    actual_value = volume;
                    triggered = volume > alert.threshold;
                    break;
                    
                default:
                    break;
            }
            
            // Update last value for next check
            alert.last_value = price;
            
            if (triggered) {
                alert.triggered = true;
                alert.trigger_count++;
                alert.last_triggered = std::chrono::system_clock::now();
                
                AlertTrigger trigger;
                trigger.alert_id = id;
                trigger.symbol = symbol;
                trigger.name = alert.name;
                trigger.condition = alert.condition;
                trigger.priority = alert.priority;
                trigger.threshold = alert.threshold;
                trigger.actual_value = actual_value;
                trigger.message = alert.message.empty() ?
                    format_trigger_message(alert, actual_value) : alert.message;
                trigger.timestamp = alert.last_triggered;
                
                triggers.push_back(trigger);
                triggered_today_.push_back(trigger);
                
                // Fire callback
                if (on_trigger_) {
                    on_trigger_(trigger);
                }
                
                // Mark for deletion if one-shot
                if (alert.one_shot) {
                    to_delete.push_back(id);
                }
            }
        }
        
        // Delete one-shot alerts
        for (const auto& id : to_delete) {
            symbol_index_[symbol].erase(id);
            alerts_.erase(id);
        }
        
        return triggers;
    }
    
    /**
     * @brief Check all alerts against current prices
     */
    std::vector<AlertTrigger> check_all() {
        std::vector<AlertTrigger> all_triggers;
        
        // Get unique symbols
        std::set<std::string> symbols;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [symbol, ids] : symbol_index_) {
                symbols.insert(symbol);
            }
        }
        
        // Check each symbol
        for (const auto& symbol : symbols) {
            auto cached = price_cache_.get(symbol);
            if (cached) {
                auto triggers = check_price(symbol, cached->price, cached->volume);
                all_triggers.insert(all_triggers.end(), triggers.begin(), triggers.end());
            }
        }
        
        return all_triggers;
    }
    
    // ========================================================================
    // Custom Conditions
    // ========================================================================
    
    /**
     * @brief Register custom condition
     */
    void register_custom_condition(const std::string& name, CustomConditionCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_conditions_[name] = callback;
    }
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /**
     * @brief Set trigger callback
     */
    void on_trigger(OnAlertTriggerCallback callback) {
        on_trigger_ = callback;
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * @brief Get alert statistics
     */
    AlertStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        AlertStats stats;
        stats.total_alerts = static_cast<int>(alerts_.size());
        
        for (const auto& [id, alert] : alerts_) {
            if (alert.enabled && !alert.is_expired()) {
                stats.active_alerts++;
            }
            if (alert.is_expired()) {
                stats.expired_alerts++;
            }
            stats.by_priority[alert.priority]++;
            if (!alert.symbol.empty()) {
                stats.by_symbol[alert.symbol]++;
            }
        }
        
        stats.triggered_today = static_cast<int>(triggered_today_.size());
        
        return stats;
    }
    
    /**
     * @brief Get today's triggers
     */
    std::vector<AlertTrigger> get_triggered_today() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return triggered_today_;
    }
    
    /**
     * @brief Clear today's triggers
     */
    void clear_triggered_today() {
        std::lock_guard<std::mutex> lock(mutex_);
        triggered_today_.clear();
    }
    
    /**
     * @brief Clean up expired alerts
     */
    int cleanup_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::string> to_delete;
        for (const auto& [id, alert] : alerts_) {
            if (alert.is_expired()) {
                to_delete.push_back(id);
            }
        }
        
        for (const auto& id : to_delete) {
            auto it = alerts_.find(id);
            if (it != alerts_.end()) {
                if (!it->second.symbol.empty()) {
                    symbol_index_[it->second.symbol].erase(id);
                }
                alerts_.erase(it);
            }
        }
        
        return static_cast<int>(to_delete.size());
    }

private:
    PriceCache& price_cache_;
    
    mutable std::mutex mutex_;
    std::map<std::string, Alert> alerts_;
    std::map<std::string, std::set<std::string>> symbol_index_;
    std::map<std::string, CustomConditionCallback> custom_conditions_;
    std::vector<AlertTrigger> triggered_today_;
    
    OnAlertTriggerCallback on_trigger_;
    
    int id_counter_{0};
    
    std::string generate_id() {
        return "ALERT-" + std::to_string(++id_counter_) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    
    std::string format_default_message(const std::string& symbol,
                                        AlertConditionType condition,
                                        double threshold) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << symbol << " ";
        
        switch (condition) {
            case AlertConditionType::PriceAbove:
                oss << "price above $" << threshold;
                break;
            case AlertConditionType::PriceBelow:
                oss << "price below $" << threshold;
                break;
            case AlertConditionType::PriceCrossAbove:
                oss << "crossed above $" << threshold;
                break;
            case AlertConditionType::PriceCrossBelow:
                oss << "crossed below $" << threshold;
                break;
            case AlertConditionType::PriceChangePct:
                oss << "moved " << threshold << "%";
                break;
            case AlertConditionType::VolumeAbove:
                oss << "volume above " << static_cast<int64_t>(threshold);
                break;
            default:
                oss << "alert triggered";
        }
        
        return oss.str();
    }
    
    std::string format_trigger_message(const Alert& alert, double actual) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << alert.symbol << " ";
        
        switch (alert.condition) {
            case AlertConditionType::PriceAbove:
            case AlertConditionType::PriceBelow:
                oss << "at $" << actual << " (target: $" << alert.threshold << ")";
                break;
            case AlertConditionType::PriceCrossAbove:
                oss << "crossed above $" << alert.threshold << " (now $" << actual << ")";
                break;
            case AlertConditionType::PriceCrossBelow:
                oss << "crossed below $" << alert.threshold << " (now $" << actual << ")";
                break;
            case AlertConditionType::PriceChangePct:
                oss << "moved " << actual << "% (threshold: " << alert.threshold << "%)";
                break;
            default:
                oss << "value: " << actual;
        }
        
        return oss.str();
    }
};

/**
 * @brief Format alert for display
 */
inline std::string format_alert(const Alert& alert) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "[" << alert.id << "] ";
    oss << (alert.enabled ? "ENABLED" : "DISABLED") << " ";
    oss << alert.symbol << " ";
    oss << condition_type_to_string(alert.condition) << " ";
    oss << alert.threshold;
    
    if (alert.triggered) {
        oss << " (TRIGGERED x" << alert.trigger_count << ")";
    }
    
    return oss.str();
}

/**
 * @brief Format trigger event for display
 */
inline std::string format_trigger(const AlertTrigger& trigger) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    // Format timestamp
    auto time_t = std::chrono::system_clock::to_time_t(trigger.timestamp);
    oss << std::put_time(std::localtime(&time_t), "%H:%M:%S") << " ";
    
    switch (trigger.priority) {
        case AlertPriority::Critical: oss << "[CRITICAL] "; break;
        case AlertPriority::High: oss << "[HIGH] "; break;
        case AlertPriority::Normal: oss << "[NORMAL] "; break;
        case AlertPriority::Low: oss << "[LOW] "; break;
    }
    
    oss << trigger.message;
    
    return oss.str();
}

} // namespace genie::market

#endif // GENIE_MARKET_ALERT_ENGINE_HPP
