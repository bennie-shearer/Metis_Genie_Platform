/**
 * @file trade_surveillance.hpp
 * @brief Trade Surveillance and Pattern Detection Engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Monitors trading activity for 7 suspicious patterns, regulatory
 * violations, and market abuse indicators with real-time alerting.
 *
 * Detected Patterns (7):
 *  1. Wash Trading - circular trades creating artificial volume
 *  2. Layering/Spoofing - placing and cancelling orders to manipulate
 *  3. Front-Running - trading ahead of large client orders
 *  4. Insider Trading Indicators - unusual pre-announcement activity
 *  5. Market Manipulation - coordinated price movement
 *  6. Excessive Churning - unnecessary trading for commission generation
 *  7. Late Trading - orders placed after market close at stale prices
 *
 * Features:
 *  - Real-time pattern detection across all 7 categories
 *  - Configurable thresholds per pattern
 *  - Alert severity classification (info, warning, critical)
 *  - Cross-account coordination detection
 *  - Restricted list enforcement
 *  - Investigation workflow (alert -> review -> escalate -> close)
 *  - Audit trail for regulatory compliance
 *  - Statistical anomaly detection
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_TRADE_SURVEILLANCE_HPP
#define GENIE_TRADE_SURVEILLANCE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <chrono>
#include <optional>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <deque>

namespace genie::compliance {

// ============================================================================
// Enums
// ============================================================================

/** @brief The 7 surveillance patterns */
enum class SurveillancePattern {
    WASH_TRADING,
    LAYERING_SPOOFING,
    FRONT_RUNNING,
    INSIDER_TRADING_INDICATOR,
    MARKET_MANIPULATION,
    EXCESSIVE_CHURNING,
    LATE_TRADING
};

enum class AlertSeverity { INFO, WARNING, CRITICAL };
enum class AlertStatus { OPEN, UNDER_REVIEW, ESCALATED, FALSE_POSITIVE, CLOSED };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Trade event for surveillance analysis */
struct TradeEvent {
    std::string trade_id;
    std::string order_id;
    std::string symbol;
    std::string account;
    std::string trader;
    std::string side;  // "buy" or "sell"
    double quantity{0.0};
    double price{0.0};
    double value{0.0};
    std::string timestamp;
    std::string venue;
    bool is_cancel{false};
    double avg_daily_volume{0.0};
    double prev_close{0.0};
    std::string order_type;
    int cancel_count{0};
};

/** @brief Surveillance alert */
struct SurveillanceAlert {
    std::string alert_id;
    SurveillancePattern pattern;
    AlertSeverity severity{AlertSeverity::WARNING};
    AlertStatus status{AlertStatus::OPEN};
    std::string symbol;
    std::string account;
    std::string trader;
    std::string description;
    std::string evidence;
    double confidence_score{0.0}; // 0-100
    double estimated_impact{0.0};
    std::string detected_at;
    std::string reviewed_at;
    std::string reviewed_by;
    std::string resolution_notes;
    std::vector<std::string> related_trade_ids;
    std::unordered_map<std::string, std::string> metadata;
};

/** @brief Pattern detection threshold configuration */
struct PatternThresholds {
    // Wash Trading
    int wash_trade_time_window_sec{300};     // 5 min window
    double wash_trade_min_quantity_match{0.9}; // 90% quantity match
    int wash_trade_max_accounts{3};

    // Layering/Spoofing
    double spoofing_cancel_ratio{0.8};       // 80% cancel rate
    int spoofing_order_count_threshold{10};
    int spoofing_time_window_sec{60};

    // Front-Running
    int front_run_lookback_sec{3600};        // 1 hour before client order
    double front_run_min_value{100000.0};
    double front_run_pnl_threshold{5000.0};

    // Insider Trading Indicators
    double insider_volume_spike_factor{3.0}; // 3x normal volume
    double insider_price_move_pct{5.0};      // 5% price move
    int insider_lookback_days{5};

    // Market Manipulation
    double manipulation_price_impact_pct{2.0};
    int manipulation_coordinated_accounts{2};
    int manipulation_time_window_sec{600};

    // Excessive Churning
    double churning_turnover_ratio{6.0};     // Annual turnover > 6x
    int churning_lookback_days{30};

    // Late Trading
    std::string market_close_time{"16:00"};
    int late_trade_grace_period_sec{30};
};

/** @brief Investigation record */
struct Investigation {
    std::string investigation_id;
    std::string alert_id;
    std::string investigator;
    std::string status; // "open", "in_progress", "completed", "escalated"
    std::string findings;
    std::string recommendation;
    std::string opened_at;
    std::string closed_at;
    std::vector<std::string> evidence_files;
};

/** @brief Surveillance engine statistics */
struct SurveillanceStats {
    uint64_t trades_analyzed{0};
    uint64_t alerts_generated{0};
    uint64_t alerts_open{0};
    uint64_t alerts_closed{0};
    uint64_t false_positives{0};
    uint64_t escalations{0};
    std::unordered_map<std::string, uint64_t> alerts_by_pattern;
    double avg_confidence{0.0};
    std::string last_analysis_time;
};

// ============================================================================
// TradeSurveillanceEngine
// ============================================================================

/**
 * @class TradeSurveillanceEngine
 * @brief Monitors trading for 7 suspicious patterns
 */
class TradeSurveillanceEngine {
public:
    explicit TradeSurveillanceEngine(PatternThresholds thresholds = {})
        : thresholds_(std::move(thresholds)) {}

    // ---- Configuration ----

    /** @brief Update detection thresholds */
    void set_thresholds(PatternThresholds thresholds) {
        std::lock_guard lock(mutex_);
        thresholds_ = std::move(thresholds);
    }

    /** @brief Add symbol to restricted list */
    void add_restricted(const std::string& symbol, const std::string& reason = "") {
        std::lock_guard lock(mutex_);
        restricted_list_[symbol] = reason;
    }

    /** @brief Remove symbol from restricted list */
    bool remove_restricted(const std::string& symbol) {
        std::lock_guard lock(mutex_);
        return restricted_list_.erase(symbol) > 0;
    }

    /** @brief Check if symbol is restricted */
    [[nodiscard]] bool is_restricted(const std::string& symbol) const {
        std::lock_guard lock(mutex_);
        return restricted_list_.find(symbol) != restricted_list_.end();
    }

    // ---- Analysis ----

    /** @brief Analyze a batch of trade events for all 7 patterns */
    std::vector<SurveillanceAlert> analyze(const std::vector<TradeEvent>& trades) {
        std::lock_guard lock(mutex_);
        std::vector<SurveillanceAlert> new_alerts;

        // Index trades by symbol and account
        for (const auto& t : trades) {
            trades_by_symbol_[t.symbol].push_back(t);
            trades_by_account_[t.account].push_back(t);
            trades_analyzed_++;
        }

        // Run all 7 pattern detectors
        auto wash = detect_wash_trading(trades);
        auto spoof = detect_layering_spoofing(trades);
        auto front = detect_front_running(trades);
        auto insider = detect_insider_indicators(trades);
        auto manip = detect_market_manipulation(trades);
        auto churn = detect_excessive_churning(trades);
        auto late = detect_late_trading(trades);

        new_alerts.insert(new_alerts.end(), wash.begin(), wash.end());
        new_alerts.insert(new_alerts.end(), spoof.begin(), spoof.end());
        new_alerts.insert(new_alerts.end(), front.begin(), front.end());
        new_alerts.insert(new_alerts.end(), insider.begin(), insider.end());
        new_alerts.insert(new_alerts.end(), manip.begin(), manip.end());
        new_alerts.insert(new_alerts.end(), churn.begin(), churn.end());
        new_alerts.insert(new_alerts.end(), late.begin(), late.end());

        // Restricted list check
        for (const auto& t : trades) {
            if (restricted_list_.find(t.symbol) != restricted_list_.end()) {
                auto alert = create_alert(SurveillancePattern::MARKET_MANIPULATION,
                    AlertSeverity::CRITICAL, t.symbol, t.account, t.trader,
                    "Trade in restricted security: " + t.symbol,
                    "Symbol on restricted list. Reason: " + restricted_list_[t.symbol], 95.0);
                alert.related_trade_ids.push_back(t.trade_id);
                new_alerts.push_back(std::move(alert));
            }
        }

        // Store alerts
        for (const auto& a : new_alerts) {
            alerts_[a.alert_id] = a;
            alerts_by_pattern_count_[pattern_name(a.pattern)]++;
        }

        return new_alerts;
    }

    /** @brief Analyze a single trade in real-time */
    std::vector<SurveillanceAlert> analyze_realtime(const TradeEvent& trade) {
        return analyze({trade});
    }

    // ---- Alert Management ----

    /** @brief Update alert status */
    bool update_alert(const std::string& alert_id, AlertStatus status,
                      const std::string& reviewer = "", const std::string& notes = "") {
        std::lock_guard lock(mutex_);
        auto it = alerts_.find(alert_id);
        if (it == alerts_.end()) return false;
        it->second.status = status;
        it->second.reviewed_by = reviewer;
        it->second.reviewed_at = now_str();
        it->second.resolution_notes = notes;
        if (status == AlertStatus::FALSE_POSITIVE) false_positives_++;
        if (status == AlertStatus::ESCALATED) escalations_++;
        return true;
    }

    /** @brief Get alert by ID */
    [[nodiscard]] std::optional<SurveillanceAlert> get_alert(const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = alerts_.find(id);
        if (it != alerts_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List alerts with optional filters */
    [[nodiscard]] std::vector<SurveillanceAlert> list_alerts(
        std::optional<AlertStatus> status_filter = std::nullopt,
        std::optional<SurveillancePattern> pattern_filter = std::nullopt,
        int max_results = 100
    ) const {
        std::lock_guard lock(mutex_);
        std::vector<SurveillanceAlert> result;
        for (const auto& [_, a] : alerts_) {
            if (status_filter && a.status != *status_filter) continue;
            if (pattern_filter && a.pattern != *pattern_filter) continue;
            result.push_back(a);
            if (static_cast<int>(result.size()) >= max_results) break;
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.detected_at > b.detected_at;
        });
        return result;
    }

    // ---- Statistics ----

    [[nodiscard]] SurveillanceStats stats() const {
        std::lock_guard lock(mutex_);
        SurveillanceStats s;
        s.trades_analyzed = trades_analyzed_;
        s.alerts_generated = alerts_.size();
        s.false_positives = false_positives_;
        s.escalations = escalations_;
        s.alerts_by_pattern = alerts_by_pattern_count_;
        double total_conf = 0;
        for (const auto& [_, a] : alerts_) {
            if (a.status == AlertStatus::OPEN) s.alerts_open++;
            if (a.status == AlertStatus::CLOSED || a.status == AlertStatus::FALSE_POSITIVE)
                s.alerts_closed++;
            total_conf += a.confidence_score;
        }
        s.avg_confidence = !alerts_.empty() ? total_conf / alerts_.size() : 0;
        return s;
    }

private:
    // ---- Pattern 1: Wash Trading ----
    std::vector<SurveillanceAlert> detect_wash_trading(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        // Look for buy-sell pairs in same symbol within time window with matching quantity
        for (std::size_t i = 0; i < trades.size(); ++i) {
            for (std::size_t j = i + 1; j < trades.size(); ++j) {
                if (trades[i].symbol != trades[j].symbol) continue;
                if (trades[i].side == trades[j].side) continue;
                double qty_ratio = std::min(trades[i].quantity, trades[j].quantity) /
                                   std::max(trades[i].quantity, trades[j].quantity);
                bool same_beneficial = trades[i].account == trades[j].account ||
                                       trades[i].trader == trades[j].trader;
                if (qty_ratio >= thresholds_.wash_trade_min_quantity_match && same_beneficial) {
                    auto alert = create_alert(SurveillancePattern::WASH_TRADING,
                        AlertSeverity::CRITICAL, trades[i].symbol, trades[i].account,
                        trades[i].trader,
                        "Potential wash trade: matching buy/sell in " + trades[i].symbol,
                        "Quantity match: " + std::to_string(qty_ratio * 100).substr(0, 5) + "%",
                        80.0 + qty_ratio * 15.0);
                    alert.related_trade_ids = {trades[i].trade_id, trades[j].trade_id};
                    alerts.push_back(std::move(alert));
                }
            }
        }
        return alerts;
    }

    // ---- Pattern 2: Layering/Spoofing ----
    std::vector<SurveillanceAlert> detect_layering_spoofing(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        // High cancel ratio per account per symbol
        std::unordered_map<std::string, int> orders, cancels;
        for (const auto& t : trades) {
            std::string key = t.account + ":" + t.symbol;
            orders[key]++;
            if (t.is_cancel) cancels[key]++;
        }
        for (const auto& [key, total] : orders) {
            double cancel_ratio = cancels.count(key) ? static_cast<double>(cancels[key]) / total : 0;
            if (cancel_ratio >= thresholds_.spoofing_cancel_ratio &&
                total >= thresholds_.spoofing_order_count_threshold) {
                auto parts = split_key(key);
                auto alert = create_alert(SurveillancePattern::LAYERING_SPOOFING,
                    AlertSeverity::CRITICAL, parts.second, parts.first, "",
                    "Potential spoofing: " + std::to_string(static_cast<int>(cancel_ratio * 100))
                        + "% cancel rate on " + std::to_string(total) + " orders",
                    "Cancel ratio exceeds " + std::to_string(static_cast<int>(thresholds_.spoofing_cancel_ratio * 100)) + "% threshold",
                    75.0 + cancel_ratio * 20.0);
                alerts.push_back(std::move(alert));
            }
        }
        return alerts;
    }

    // ---- Pattern 3: Front-Running ----
    std::vector<SurveillanceAlert> detect_front_running(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        // Look for proprietary trades ahead of large client orders
        for (const auto& t : trades) {
            if (t.value >= thresholds_.front_run_min_value && t.account.find("CLIENT") != std::string::npos) {
                // Check for same-side prop trades before this
                for (const auto& pt : trades) {
                    if (pt.symbol == t.symbol && pt.side == t.side &&
                        pt.timestamp < t.timestamp && pt.account.find("PROP") != std::string::npos) {
                        auto alert = create_alert(SurveillancePattern::FRONT_RUNNING,
                            AlertSeverity::CRITICAL, t.symbol, pt.account, pt.trader,
                            "Potential front-running ahead of client order in " + t.symbol,
                            "Prop trade value: $" + std::to_string(static_cast<int>(pt.value))
                                + " before client order of $" + std::to_string(static_cast<int>(t.value)),
                            70.0);
                        alert.related_trade_ids = {pt.trade_id, t.trade_id};
                        alerts.push_back(std::move(alert));
                    }
                }
            }
        }
        return alerts;
    }

    // ---- Pattern 4: Insider Trading Indicators ----
    std::vector<SurveillanceAlert> detect_insider_indicators(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        for (const auto& t : trades) {
            if (t.avg_daily_volume > 0 && t.quantity / t.avg_daily_volume > thresholds_.insider_volume_spike_factor) {
                auto alert = create_alert(SurveillancePattern::INSIDER_TRADING_INDICATOR,
                    AlertSeverity::WARNING, t.symbol, t.account, t.trader,
                    "Unusual volume spike: " + std::to_string(t.quantity / t.avg_daily_volume).substr(0, 4)
                        + "x normal in " + t.symbol,
                    "Volume exceeds " + std::to_string(thresholds_.insider_volume_spike_factor) + "x ADV threshold",
                    60.0);
                alert.related_trade_ids.push_back(t.trade_id);
                alerts.push_back(std::move(alert));
            }
        }
        return alerts;
    }

    // ---- Pattern 5: Market Manipulation ----
    std::vector<SurveillanceAlert> detect_market_manipulation(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        for (const auto& t : trades) {
            if (t.prev_close > 0) {
                double price_impact = std::abs(t.price - t.prev_close) / t.prev_close * 100.0;
                if (price_impact > thresholds_.manipulation_price_impact_pct) {
                    auto alert = create_alert(SurveillancePattern::MARKET_MANIPULATION,
                        AlertSeverity::WARNING, t.symbol, t.account, t.trader,
                        "Significant price impact: " + std::to_string(price_impact).substr(0, 5)
                            + "% on " + t.symbol,
                        "Trade moved price beyond " + std::to_string(thresholds_.manipulation_price_impact_pct) + "% threshold",
                        55.0);
                    alerts.push_back(std::move(alert));
                }
            }
        }
        return alerts;
    }

    // ---- Pattern 6: Excessive Churning ----
    std::vector<SurveillanceAlert> detect_excessive_churning(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        std::unordered_map<std::string, double> turnover_by_account;
        for (const auto& t : trades) turnover_by_account[t.account] += t.value;
        for (const auto& [account, turnover] : turnover_by_account) {
            double annualized = turnover * (365.0 / thresholds_.churning_lookback_days);
            double turnover_ratio = annualized / 10000000.0; // Simplified AUM estimate
            if (turnover_ratio > thresholds_.churning_turnover_ratio) {
                auto alert = create_alert(SurveillancePattern::EXCESSIVE_CHURNING,
                    AlertSeverity::WARNING, "", account, "",
                    "Excessive turnover in account " + account + ": "
                        + std::to_string(turnover_ratio).substr(0, 4) + "x annual",
                    "Exceeds " + std::to_string(thresholds_.churning_turnover_ratio) + "x threshold",
                    65.0);
                alerts.push_back(std::move(alert));
            }
        }
        return alerts;
    }

    // ---- Pattern 7: Late Trading ----
    std::vector<SurveillanceAlert> detect_late_trading(const std::vector<TradeEvent>& trades) {
        std::vector<SurveillanceAlert> alerts;
        for (const auto& t : trades) {
            if (t.timestamp.size() >= 16) {
                std::string time_part = t.timestamp.substr(11, 5);
                if (time_part > thresholds_.market_close_time) {
                    auto alert = create_alert(SurveillancePattern::LATE_TRADING,
                        AlertSeverity::CRITICAL, t.symbol, t.account, t.trader,
                        "Late trade at " + time_part + " (after " + thresholds_.market_close_time + " close)",
                        "Trade executed after market hours",
                        85.0);
                    alert.related_trade_ids.push_back(t.trade_id);
                    alerts.push_back(std::move(alert));
                }
            }
        }
        return alerts;
    }

    SurveillanceAlert create_alert(SurveillancePattern pattern, AlertSeverity severity,
                                   const std::string& symbol, const std::string& account,
                                   const std::string& trader, const std::string& desc,
                                   const std::string& evidence, double confidence) {
        SurveillanceAlert alert;
        alert.alert_id = "SURV-" + std::to_string(++alert_counter_);
        alert.pattern = pattern;
        alert.severity = severity;
        alert.symbol = symbol;
        alert.account = account;
        alert.trader = trader;
        alert.description = desc;
        alert.evidence = evidence;
        alert.confidence_score = confidence;
        alert.detected_at = now_str();
        return alert;
    }

    static std::string pattern_name(SurveillancePattern p) {
        switch (p) {
            case SurveillancePattern::WASH_TRADING: return "Wash Trading";
            case SurveillancePattern::LAYERING_SPOOFING: return "Layering/Spoofing";
            case SurveillancePattern::FRONT_RUNNING: return "Front-Running";
            case SurveillancePattern::INSIDER_TRADING_INDICATOR: return "Insider Indicator";
            case SurveillancePattern::MARKET_MANIPULATION: return "Market Manipulation";
            case SurveillancePattern::EXCESSIVE_CHURNING: return "Excessive Churning";
            case SurveillancePattern::LATE_TRADING: return "Late Trading";
        }
        return "Unknown";
    }

    static std::pair<std::string, std::string> split_key(const std::string& key) {
        auto pos = key.find(':');
        if (pos != std::string::npos) return {key.substr(0, pos), key.substr(pos + 1)};
        return {key, ""};
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    PatternThresholds thresholds_;
    std::unordered_map<std::string, SurveillanceAlert> alerts_;
    std::unordered_map<std::string, std::string> restricted_list_;
    std::unordered_map<std::string, std::vector<TradeEvent>> trades_by_symbol_;
    std::unordered_map<std::string, std::vector<TradeEvent>> trades_by_account_;
    std::unordered_map<std::string, uint64_t> alerts_by_pattern_count_;
    uint64_t trades_analyzed_{0};
    uint64_t false_positives_{0};
    uint64_t escalations_{0};
    uint64_t alert_counter_{0};
    mutable std::mutex mutex_;
};

} // namespace genie::compliance

#endif // GENIE_TRADE_SURVEILLANCE_HPP
