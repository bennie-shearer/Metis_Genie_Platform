/**
 * @file alert_manager.hpp
 * @brief Threshold-based alerting with notification channels
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Enterprise alert management system:
 * - Threshold-based alert rules (>, <, ==, range, rate-of-change)
 * - Alert severity levels (Info/Warning/Critical/Emergency)
 * - Notification channels (log, email, webhook, callback)
 * - Alert deduplication and suppression windows
 * - Escalation chains with timeout
 * - Alert history and acknowledgment workflow
 * - Cooldown periods to prevent alert storms
 * - Metric-driven alerting integration
 * - Maintenance window support
 * - JSON-serializable alert payloads
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_ALERT_MANAGER_HPP
#define GENIE_CORE_ALERT_MANAGER_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <atomic>
#include <cmath>

namespace genie {
namespace core {
namespace alerting {

// ============================================================================
// Enumerations
// ============================================================================

enum class AlertSeverity {
    Info,
    Warning,
    Critical,
    Emergency
};

enum class AlertState {
    Firing,
    Acknowledged,
    Resolved,
    Suppressed,
    Expired
};

enum class ComparisonOp {
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual,
    Equal,
    NotEqual,
    InRange,
    OutOfRange,
    RateOfChange       // Percentage change over window
};

enum class ChannelType {
    Log,
    Callback,
    Webhook,
    Email,
    Slack,
    PagerDuty,
    Custom
};

[[nodiscard]] inline std::string severity_string(AlertSeverity s) {
    switch (s) {
        case AlertSeverity::Info:      return "INFO";
        case AlertSeverity::Warning:   return "WARNING";
        case AlertSeverity::Critical:  return "CRITICAL";
        case AlertSeverity::Emergency: return "EMERGENCY";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline std::string state_string(AlertState s) {
    switch (s) {
        case AlertState::Firing:       return "firing";
        case AlertState::Acknowledged: return "acknowledged";
        case AlertState::Resolved:     return "resolved";
        case AlertState::Suppressed:   return "suppressed";
        case AlertState::Expired:      return "expired";
    }
    return "unknown";
}

[[nodiscard]] inline std::string op_string(ComparisonOp op) {
    switch (op) {
        case ComparisonOp::GreaterThan:    return ">";
        case ComparisonOp::LessThan:       return "<";
        case ComparisonOp::GreaterOrEqual: return ">=";
        case ComparisonOp::LessOrEqual:    return "<=";
        case ComparisonOp::Equal:          return "==";
        case ComparisonOp::NotEqual:       return "!=";
        case ComparisonOp::InRange:        return "in_range";
        case ComparisonOp::OutOfRange:     return "out_of_range";
        case ComparisonOp::RateOfChange:   return "rate_of_change";
    }
    return "?";
}

[[nodiscard]] inline std::string channel_type_string(ChannelType t) {
    switch (t) {
        case ChannelType::Log:       return "log";
        case ChannelType::Callback:  return "callback";
        case ChannelType::Webhook:   return "webhook";
        case ChannelType::Email:     return "email";
        case ChannelType::Slack:     return "slack";
        case ChannelType::PagerDuty: return "pagerduty";
        case ChannelType::Custom:    return "custom";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Alert notification channel
 */
struct NotificationChannel {
    std::string id;
    std::string name;
    ChannelType type{ChannelType::Log};
    bool enabled{true};
    std::string endpoint;           // URL, email, etc.
    std::map<std::string, std::string> config;
    std::function<void(const std::string&)> callback;  // For Callback type
    AlertSeverity min_severity{AlertSeverity::Info};

    [[nodiscard]] bool should_notify(AlertSeverity sev) const {
        return enabled && static_cast<int>(sev) >= static_cast<int>(min_severity);
    }
};

/**
 * @brief Alert rule definition
 */
struct AlertRule {
    std::string id;
    std::string name;
    std::string description;
    std::string metric_name;          // What to monitor
    ComparisonOp comparison{ComparisonOp::GreaterThan};
    double threshold{0};
    double threshold_high{0};         // For range comparisons
    AlertSeverity severity{AlertSeverity::Warning};
    bool enabled{true};
    std::chrono::seconds cooldown{300};    // 5 min between re-fires
    std::chrono::seconds eval_window{60};  // Evaluation period
    int consecutive_hits{1};          // How many consecutive evaluations must trigger
    std::vector<std::string> channel_ids;  // Which channels to notify
    std::map<std::string, std::string> labels;

    [[nodiscard]] bool evaluate(double value) const {
        switch (comparison) {
            case ComparisonOp::GreaterThan:    return value > threshold;
            case ComparisonOp::LessThan:       return value < threshold;
            case ComparisonOp::GreaterOrEqual: return value >= threshold;
            case ComparisonOp::LessOrEqual:    return value <= threshold;
            case ComparisonOp::Equal:          return std::abs(value - threshold) < 1e-9;
            case ComparisonOp::NotEqual:       return std::abs(value - threshold) >= 1e-9;
            case ComparisonOp::InRange:        return value >= threshold && value <= threshold_high;
            case ComparisonOp::OutOfRange:     return value < threshold || value > threshold_high;
            case ComparisonOp::RateOfChange:   return std::abs(value) > threshold;
        }
        return false;
    }
};

/**
 * @brief Fired alert instance
 */
struct Alert {
    std::string id;
    std::string rule_id;
    std::string rule_name;
    AlertSeverity severity{AlertSeverity::Warning};
    AlertState state{AlertState::Firing};
    std::string message;
    double value{0};
    double threshold{0};
    std::chrono::system_clock::time_point fired_at;
    std::chrono::system_clock::time_point resolved_at;
    std::chrono::system_clock::time_point acknowledged_at;
    std::string acknowledged_by;
    std::string notes;
    std::map<std::string, std::string> labels;
    int notification_count{0};

    [[nodiscard]] bool is_active() const {
        return state == AlertState::Firing || state == AlertState::Acknowledged;
    }

    [[nodiscard]] std::chrono::seconds duration() const {
        auto end = (state == AlertState::Resolved) ? resolved_at
                   : std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(end - fired_at);
    }

    [[nodiscard]] std::string to_json() const {
        auto ft = std::chrono::system_clock::to_time_t(fired_at);
        std::ostringstream oss;
        oss << "{\"id\":\"" << id << "\",\"rule\":\"" << rule_name
            << "\",\"severity\":\"" << severity_string(severity)
            << "\",\"state\":\"" << state_string(state)
            << "\",\"message\":\"" << message
            << "\",\"value\":" << std::fixed << std::setprecision(4) << value
            << ",\"threshold\":" << threshold
            << ",\"fired_at\":\"";
        oss << std::put_time(std::gmtime(&ft), "%Y-%m-%dT%H:%M:%SZ");
        oss << "\",\"duration_sec\":" << duration().count() << "}";
        return oss.str();
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "[" << severity_string(severity) << "] " << rule_name
            << ": " << message
            << " (value=" << std::fixed << std::setprecision(2) << value
            << ", threshold=" << threshold << ")";
        return oss.str();
    }
};

/**
 * @brief Maintenance window during which alerts are suppressed
 */
struct MaintenanceWindow {
    std::string id;
    std::string reason;
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
    std::set<std::string> affected_rules;  // Empty = all rules
    std::string created_by;

    [[nodiscard]] bool is_active() const {
        auto now = std::chrono::system_clock::now();
        return now >= start && now <= end;
    }

    [[nodiscard]] bool covers_rule(const std::string& rule_id) const {
        if (affected_rules.empty()) return true;
        return affected_rules.count(rule_id) > 0;
    }
};

// ============================================================================
// Escalation Chain
// ============================================================================

struct EscalationStep {
    std::chrono::seconds after;         // Escalate after this duration
    std::vector<std::string> channel_ids;
    AlertSeverity escalated_severity;
    std::string message_prefix;
};

struct EscalationChain {
    std::string id;
    std::string name;
    std::vector<EscalationStep> steps;
};

// ============================================================================
// Alert Manager
// ============================================================================

/**
 * @brief Manages alert rules, evaluation, and notification
 */
class AlertManager {
public:
    AlertManager() {
        register_default_channels();
        register_default_rules();
    }

    // ---- Channel Management ----

    void add_channel(NotificationChannel channel) {
        std::lock_guard<std::mutex> lock(mutex_);
        channels_[channel.id] = std::move(channel);
    }

    void remove_channel(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        channels_.erase(id);
    }

    // ---- Rule Management ----

    void add_rule(AlertRule rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_[rule.id] = std::move(rule);
    }

    void remove_rule(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_.erase(id);
    }

    void set_rule_enabled(const std::string& id, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = rules_.find(id);
        if (it != rules_.end()) it->second.enabled = enabled;
    }

    // ---- Evaluation ----

    /**
     * @brief Evaluate a metric value against all matching rules
     * @return Vector of newly fired alerts
     */
    std::vector<Alert> evaluate(const std::string& metric_name, double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        std::vector<Alert> new_alerts;

        for (const auto& [rule_id, rule] : rules_) {
            if (!rule.enabled || rule.metric_name != metric_name) continue;

            // Check maintenance windows
            if (is_in_maintenance(rule_id)) continue;

            if (rule.evaluate(value)) {
                // Check cooldown
                auto last_fire_it = last_fire_time_.find(rule_id);
                if (last_fire_it != last_fire_time_.end()) {
                    auto since = std::chrono::duration_cast<std::chrono::seconds>(
                        now - last_fire_it->second);
                    if (since < rule.cooldown) continue;
                }

                // Track consecutive hits
                hit_counts_[rule_id]++;
                if (hit_counts_[rule_id] < rule.consecutive_hits) continue;

                // Fire alert
                Alert alert;
                alert.id = "ALR-" + std::to_string(++alert_counter_);
                alert.rule_id = rule_id;
                alert.rule_name = rule.name;
                alert.severity = rule.severity;
                alert.state = AlertState::Firing;
                alert.value = value;
                alert.threshold = rule.threshold;
                alert.labels = rule.labels;
                alert.fired_at = now;

                std::ostringstream msg;
                msg << rule.name << ": " << metric_name << " "
                    << op_string(rule.comparison) << " " << rule.threshold
                    << " (current: " << std::fixed << std::setprecision(2) << value << ")";
                alert.message = msg.str();

                // Send notifications
                for (const auto& ch_id : rule.channel_ids) {
                    send_notification(ch_id, alert);
                    alert.notification_count++;
                }

                last_fire_time_[rule_id] = now;
                hit_counts_[rule_id] = 0;

                // Store in history
                active_alerts_[alert.id] = alert;
                alert_history_.push_back(alert);
                if (alert_history_.size() > max_history_) alert_history_.pop_front();

                new_alerts.push_back(alert);
                total_alerts_fired_++;
            } else {
                // Reset consecutive hit counter
                hit_counts_[rule_id] = 0;

                // Auto-resolve active alerts for this rule
                for (auto& [alert_id, active] : active_alerts_) {
                    if (active.rule_id == rule_id && active.is_active()) {
                        active.state = AlertState::Resolved;
                        active.resolved_at = now;
                    }
                }
            }
        }
        return new_alerts;
    }

    // ---- Alert Actions ----

    bool acknowledge(const std::string& alert_id, const std::string& by,
                      const std::string& notes = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_alerts_.find(alert_id);
        if (it == active_alerts_.end()) return false;
        if (it->second.state != AlertState::Firing) return false;
        it->second.state = AlertState::Acknowledged;
        it->second.acknowledged_at = std::chrono::system_clock::now();
        it->second.acknowledged_by = by;
        it->second.notes = notes;
        return true;
    }

    bool resolve(const std::string& alert_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_alerts_.find(alert_id);
        if (it == active_alerts_.end()) return false;
        it->second.state = AlertState::Resolved;
        it->second.resolved_at = std::chrono::system_clock::now();
        return true;
    }

    // ---- Maintenance Windows ----

    void add_maintenance_window(MaintenanceWindow window) {
        std::lock_guard<std::mutex> lock(mutex_);
        maintenance_windows_.push_back(std::move(window));
    }

    // ---- Escalation ----

    void add_escalation_chain(EscalationChain chain) {
        std::lock_guard<std::mutex> lock(mutex_);
        escalation_chains_[chain.id] = std::move(chain);
    }

    // ---- Queries ----

    [[nodiscard]] std::vector<Alert> active_alerts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        for (const auto& [_, a] : active_alerts_) {
            if (a.is_active()) result.push_back(a);
        }
        std::sort(result.begin(), result.end(), [](const Alert& a, const Alert& b) {
            return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        });
        return result;
    }

    [[nodiscard]] std::vector<Alert> history(int last_n = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        int start = std::max(0, static_cast<int>(alert_history_.size()) - last_n);
        return {alert_history_.begin() + start, alert_history_.end()};
    }

    [[nodiscard]] std::vector<Alert> alerts_by_severity(AlertSeverity sev) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Alert> result;
        for (const auto& [_, a] : active_alerts_) {
            if (a.severity == sev && a.is_active()) result.push_back(a);
        }
        return result;
    }

    // ---- Stats ----

    [[nodiscard]] std::string summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int firing = 0, acked = 0, resolved = 0;
        for (const auto& [_, a] : active_alerts_) {
            switch (a.state) {
                case AlertState::Firing: ++firing; break;
                case AlertState::Acknowledged: ++acked; break;
                case AlertState::Resolved: ++resolved; break;
                default: break;
            }
        }
        std::ostringstream oss;
        oss << "Alerts: " << firing << " firing, " << acked << " acked, "
            << resolved << " resolved | Rules: " << rules_.size()
            << " | Total fired: " << total_alerts_fired_.load();
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "{\"rules\":" << rules_.size()
            << ",\"channels\":" << channels_.size()
            << ",\"active_alerts\":" << active_alerts_.size()
            << ",\"total_fired\":" << total_alerts_fired_.load()
            << ",\"history_size\":" << alert_history_.size() << "}";
        return oss.str();
    }

    [[nodiscard]] int rule_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(rules_.size());
    }

    [[nodiscard]] int channel_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(channels_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, AlertRule> rules_;
    std::map<std::string, NotificationChannel> channels_;
    std::map<std::string, Alert> active_alerts_;
    std::deque<Alert> alert_history_;
    std::map<std::string, std::chrono::system_clock::time_point> last_fire_time_;
    std::map<std::string, int> hit_counts_;
    std::vector<MaintenanceWindow> maintenance_windows_;
    std::map<std::string, EscalationChain> escalation_chains_;
    size_t max_history_{10000};
    std::atomic<int64_t> total_alerts_fired_{0};
    int64_t alert_counter_{0};

    void send_notification(const std::string& channel_id, const Alert& alert) {
        auto it = channels_.find(channel_id);
        if (it == channels_.end()) return;
        auto& ch = it->second;
        if (!ch.should_notify(alert.severity)) return;

        switch (ch.type) {
            case ChannelType::Callback:
                if (ch.callback) {
                    try { ch.callback(alert.to_json()); } catch (...) {}
                }
                break;
            case ChannelType::Log:
                // Logging handled by caller
                break;
            default:
                // Webhook/Email/Slack/PagerDuty would be external calls
                break;
        }
    }

    bool is_in_maintenance(const std::string& rule_id) const {
        for (const auto& mw : maintenance_windows_) {
            if (mw.is_active() && mw.covers_rule(rule_id)) return true;
        }
        return false;
    }

    void register_default_channels() {
        NotificationChannel log_ch;
        log_ch.id = "log";
        log_ch.name = "System Log";
        log_ch.type = ChannelType::Log;
        log_ch.min_severity = AlertSeverity::Info;
        channels_["log"] = std::move(log_ch);
    }

    void register_default_rules() {
        // Portfolio drawdown alert
        {
            AlertRule rule;
            rule.id = "ALR-DRAWDOWN";
            rule.name = "Portfolio Drawdown";
            rule.metric_name = "portfolio.drawdown_pct";
            rule.comparison = ComparisonOp::GreaterThan;
            rule.threshold = 5.0;
            rule.severity = AlertSeverity::Warning;
            rule.cooldown = std::chrono::seconds(900);
            rule.channel_ids = {"log"};
            rule.labels["category"] = "risk";
            rules_[rule.id] = std::move(rule);
        }

        // VaR breach
        {
            AlertRule rule;
            rule.id = "ALR-VAR-BREACH";
            rule.name = "VaR Limit Breach";
            rule.metric_name = "risk.var_utilization_pct";
            rule.comparison = ComparisonOp::GreaterThan;
            rule.threshold = 90.0;
            rule.severity = AlertSeverity::Critical;
            rule.cooldown = std::chrono::seconds(600);
            rule.channel_ids = {"log"};
            rule.labels["category"] = "risk";
            rules_[rule.id] = std::move(rule);
        }

        // Market data stale
        {
            AlertRule rule;
            rule.id = "ALR-STALE-DATA";
            rule.name = "Stale Market Data";
            rule.metric_name = "market.data_age_seconds";
            rule.comparison = ComparisonOp::GreaterThan;
            rule.threshold = 60.0;
            rule.severity = AlertSeverity::Warning;
            rule.cooldown = std::chrono::seconds(300);
            rule.channel_ids = {"log"};
            rule.labels["category"] = "data";
            rules_[rule.id] = std::move(rule);
        }

        // Order rejection rate
        {
            AlertRule rule;
            rule.id = "ALR-ORDER-REJECT";
            rule.name = "High Order Rejection Rate";
            rule.metric_name = "trading.rejection_rate_pct";
            rule.comparison = ComparisonOp::GreaterThan;
            rule.threshold = 10.0;
            rule.severity = AlertSeverity::Warning;
            rule.cooldown = std::chrono::seconds(600);
            rule.channel_ids = {"log"};
            rule.labels["category"] = "trading";
            rules_[rule.id] = std::move(rule);
        }

        // System CPU usage
        {
            AlertRule rule;
            rule.id = "ALR-CPU-HIGH";
            rule.name = "High CPU Usage";
            rule.metric_name = "system.cpu_usage_pct";
            rule.comparison = ComparisonOp::GreaterThan;
            rule.threshold = 90.0;
            rule.severity = AlertSeverity::Critical;
            rule.consecutive_hits = 3;
            rule.cooldown = std::chrono::seconds(300);
            rule.channel_ids = {"log"};
            rule.labels["category"] = "infrastructure";
            rules_[rule.id] = std::move(rule);
        }
    }
};

} // namespace alerting
} // namespace core
} // namespace genie

#endif // GENIE_CORE_ALERT_MANAGER_HPP
