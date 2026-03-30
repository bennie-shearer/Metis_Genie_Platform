/**
 * @file alerts.hpp
 * @brief Alert and notification system for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_ALERTS_HPP
#define GENIE_ALERTS_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <queue>
#include <iostream>

namespace genie {
namespace alerts {

using TimePoint = std::chrono::system_clock::time_point;

enum class AlertSeverity { Info, Warning, Critical, Emergency };
enum class AlertType { PriceThreshold, RiskLimit, Compliance, PortfolioDrift, SystemHealth, Custom };
enum class AlertStatus { Active, Acknowledged, Resolved, Expired };

struct Alert {
    std::string id;
    AlertType type;
    AlertSeverity severity;
    AlertStatus status{AlertStatus::Active};
    std::string title;
    std::string message;
    std::string source;           // Security ID, portfolio ID, or system component
    TimePoint created_at;
    TimePoint acknowledged_at;
    TimePoint resolved_at;
    std::map<std::string, std::string> metadata;
};

struct AlertRule {
    std::string id;
    std::string name;
    AlertType type;
    AlertSeverity severity;
    bool enabled{true};
    std::function<bool()> condition;
    std::string message_template;
    int cooldown_minutes{5};       // Minimum time between alerts
    TimePoint last_triggered;
};

// Price alert conditions
struct PriceAlert {
    std::string security_id;
    double threshold_price;
    bool trigger_above;  // true = alert when price > threshold
    bool triggered{false};
};

// Risk limit conditions
struct RiskLimitAlert {
    std::string metric_name;
    double threshold;
    bool trigger_above;
    double current_value{0};
};

class AlertManager {
    std::vector<Alert> alerts_;
    std::map<std::string, AlertRule> rules_;
    std::vector<PriceAlert> price_alerts_;
    std::vector<RiskLimitAlert> risk_alerts_;
    std::vector<std::function<void(const Alert&)>> handlers_;
    mutable std::mutex mutex_;
    int next_id_{1};
    size_t max_alerts_{1000};
    
    std::string generate_id() {
        return "ALERT-" + std::to_string(next_id_++);
    }
    
public:
    // Register alert handler (callback)
    void register_handler(std::function<void(const Alert&)> handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back(handler);
    }
    
    // Create and raise an alert
    std::string raise_alert(AlertType type, AlertSeverity severity,
                            const std::string& title, const std::string& message,
                            const std::string& source = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Alert alert;
        alert.id = generate_id();
        alert.type = type;
        alert.severity = severity;
        alert.title = title;
        alert.message = message;
        alert.source = source;
        alert.created_at = std::chrono::system_clock::now();
        
        alerts_.push_back(alert);
        
        // Trim old alerts if needed
        while (alerts_.size() > max_alerts_) {
            alerts_.erase(alerts_.begin());
        }
        
        // Notify handlers
        for (const auto& handler : handlers_) {
            handler(alert);
        }
        
        return alert.id;
    }
    
    // Price alert management
    void add_price_alert(const std::string& security_id, double threshold, bool trigger_above) {
        PriceAlert pa;
        pa.security_id = security_id;
        pa.threshold_price = threshold;
        pa.trigger_above = trigger_above;
        price_alerts_.push_back(pa);
    }
    
    // Check prices and trigger alerts
    void check_prices(const std::map<std::string, double>& current_prices) {
        for (auto& pa : price_alerts_) {
            if (pa.triggered) continue;
            if (!current_prices.count(pa.security_id)) continue;
            
            double price = current_prices.at(pa.security_id);
            bool should_trigger = pa.trigger_above ? (price > pa.threshold_price) 
                                                   : (price < pa.threshold_price);
            
            if (should_trigger) {
                pa.triggered = true;
                std::string direction = pa.trigger_above ? "above" : "below";
                raise_alert(AlertType::PriceThreshold, AlertSeverity::Warning,
                           "Price Alert: " + pa.security_id,
                           pa.security_id + " price (" + std::to_string(price) + 
                           ") crossed " + direction + " threshold (" + 
                           std::to_string(pa.threshold_price) + ")",
                           pa.security_id);
            }
        }
    }
    
    // Risk limit alerts
    void add_risk_limit_alert(const std::string& metric, double threshold, bool trigger_above) {
        RiskLimitAlert rla;
        rla.metric_name = metric;
        rla.threshold = threshold;
        rla.trigger_above = trigger_above;
        risk_alerts_.push_back(rla);
    }
    
    void check_risk_limits(const std::map<std::string, double>& metrics) {
        for (auto& rla : risk_alerts_) {
            if (!metrics.count(rla.metric_name)) continue;
            
            double value = metrics.at(rla.metric_name);
            rla.current_value = value;
            bool breached = rla.trigger_above ? (value > rla.threshold) : (value < rla.threshold);
            
            if (breached) {
                raise_alert(AlertType::RiskLimit, AlertSeverity::Critical,
                           "Risk Limit Breach: " + rla.metric_name,
                           rla.metric_name + " (" + std::to_string(value) + 
                           ") breached limit (" + std::to_string(rla.threshold) + ")",
                           rla.metric_name);
            }
        }
    }
    
    // Portfolio drift alert
    void check_portfolio_drift(const std::string& portfolio_id, double drift_pct, double threshold = 0.05) {
        if (drift_pct > threshold) {
            raise_alert(AlertType::PortfolioDrift, AlertSeverity::Warning,
                       "Portfolio Drift: " + portfolio_id,
                       "Portfolio " + portfolio_id + " drift (" + 
                       std::to_string(drift_pct * 100) + "%) exceeds threshold (" +
                       std::to_string(threshold * 100) + "%)",
                       portfolio_id);
        }
    }
    
    // Acknowledge alert
    bool acknowledge(const std::string& alert_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& alert : alerts_) {
            if (alert.id == alert_id && alert.status == AlertStatus::Active) {
                alert.status = AlertStatus::Acknowledged;
                alert.acknowledged_at = std::chrono::system_clock::now();
                return true;
            }
        }
        return false;
    }
    
    // Resolve alert
    bool resolve(const std::string& alert_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& alert : alerts_) {
            if (alert.id == alert_id && alert.status != AlertStatus::Resolved) {
                alert.status = AlertStatus::Resolved;
                alert.resolved_at = std::chrono::system_clock::now();
                return true;
            }
        }
        return false;
    }
    
    // Get active alerts
    std::vector<Alert> get_active_alerts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        for (const auto& a : alerts_) {
            if (a.status == AlertStatus::Active) result.push_back(a);
        }
        return result;
    }
    
    // Get alerts by severity
    std::vector<Alert> get_alerts_by_severity(AlertSeverity severity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        for (const auto& a : alerts_) {
            if (a.severity == severity) result.push_back(a);
        }
        return result;
    }
    
    // Get all alerts
    std::vector<Alert> get_all_alerts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return alerts_;
    }
    
    // Clear resolved alerts
    void clear_resolved() {
        std::lock_guard<std::mutex> lock(mutex_);
        alerts_.erase(
            std::remove_if(alerts_.begin(), alerts_.end(),
                          [](const Alert& a) { return a.status == AlertStatus::Resolved; }),
            alerts_.end());
    }
    
    // Reset price alerts
    void reset_price_alerts() {
        for (auto& pa : price_alerts_) {
            pa.triggered = false;
        }
    }
    
    // Summary counts
    struct AlertCounts {
        int total{0};
        int active{0};
        int acknowledged{0};
        int resolved{0};
        int critical{0};
        int warning{0};
    };
    
    AlertCounts get_counts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        AlertCounts counts;
        counts.total = alerts_.size();
        for (const auto& a : alerts_) {
            switch (a.status) {
                case AlertStatus::Active: ++counts.active; break;
                case AlertStatus::Acknowledged: ++counts.acknowledged; break;
                case AlertStatus::Resolved: ++counts.resolved; break;
                default: break;
            }
            if (a.severity == AlertSeverity::Critical) ++counts.critical;
            if (a.severity == AlertSeverity::Warning) ++counts.warning;
        }
        return counts;
    }
    
    // Generate report
    std::string report() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        ss << "=== ALERT SUMMARY ===\n";
        
        auto counts = get_counts();
        ss << "Total: " << counts.total << " | Active: " << counts.active
           << " | Critical: " << counts.critical << " | Warning: " << counts.warning << "\n\n";
        
        ss << "Active Alerts:\n";
        for (const auto& a : alerts_) {
            if (a.status == AlertStatus::Active) {
                auto t = std::chrono::system_clock::to_time_t(a.created_at);
                ss << "  [" << a.id << "] " << a.title << "\n";
                ss << "    " << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
                ss << " | " << a.message << "\n";
            }
        }
        
        return ss.str();
    }
};

// Console alert handler
inline void console_alert_handler(const Alert& alert) {
    std::string severity_str;
    switch (alert.severity) {
        case AlertSeverity::Info: severity_str = "INFO"; break;
        case AlertSeverity::Warning: severity_str = "WARN"; break;
        case AlertSeverity::Critical: severity_str = "CRIT"; break;
        case AlertSeverity::Emergency: severity_str = "EMRG"; break;
    }
    
    auto t = std::chrono::system_clock::to_time_t(alert.created_at);
    std::cout << "[" << std::put_time(std::localtime(&t), "%H:%M:%S") << "] "
              << "[" << severity_str << "] " << alert.title << ": " << alert.message << "\n";
}

} // namespace alerts
} // namespace genie
#endif // GENIE_ALERTS_HPP
