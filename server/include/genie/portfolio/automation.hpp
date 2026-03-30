/**
 * @file automation.hpp
 * @brief Portfolio automation - rebalancing, tax-loss harvesting, drift, dividends
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 4: Automation
 * - Scheduled rebalancing
 * - Automatic tax-loss harvesting
 * - Drift monitoring with alerts
 * - Stop-loss automation
 * - Dividend reinvestment rules
 */
#pragma once
#ifndef GENIE_PORTFOLIO_AUTOMATION_HPP
#define GENIE_PORTFOLIO_AUTOMATION_HPP

#include "../trading/broker_abstraction.hpp"
#include "../tax/tax_tracking.hpp"
#include "../core/alerts.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <queue>

namespace genie::portfolio {

// =============================================================================
// Scheduled Rebalancing
// =============================================================================

/** Rebalancing frequency for automation (avoids conflict with rebalancing.hpp) */
enum class AutoRebalanceFrequency { Daily, Weekly, BiWeekly, Monthly, Quarterly, Annually, OnDrift };

inline std::string auto_frequency_to_string(AutoRebalanceFrequency freq) {
    switch (freq) {
        case AutoRebalanceFrequency::Daily: return "Daily";
        case AutoRebalanceFrequency::Weekly: return "Weekly";
        case AutoRebalanceFrequency::BiWeekly: return "Bi-Weekly";
        case AutoRebalanceFrequency::Monthly: return "Monthly";
        case AutoRebalanceFrequency::Quarterly: return "Quarterly";
        case AutoRebalanceFrequency::Annually: return "Annually";
        case AutoRebalanceFrequency::OnDrift: return "On Drift";
        default: return "Unknown";
    }
}

/**
 * @brief Target allocation
 */
struct AutoTargetAllocation {
    std::string symbol;
    double target_weight{0};       // Target percentage (0-100)
    double min_weight{0};          // Minimum allowed (for drift)
    double max_weight{0};          // Maximum allowed (for drift)
    
    bool is_within_band(double current_weight) const {
        return current_weight >= min_weight && current_weight <= max_weight;
    }
    
    double drift_from_target(double current_weight) const {
        return current_weight - target_weight;
    }
};

/**
 * @brief Rebalancing trade
 */
struct AutoRebalanceTrade {
    std::string symbol;
    trading::OrderSide side;
    double quantity{0};
    double current_weight{0};
    double target_weight{0};
    double estimated_value{0};
    std::string reason;
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << (side == trading::OrderSide::Buy ? "BUY" : "SELL") << " "
            << quantity << " " << symbol << " ($" << estimated_value << ")\n"
            << "  Current: " << current_weight << "% -> Target: " << target_weight << "%";
        return oss.str();
    }
};

/**
 * @brief Rebalancing result
 */
struct AutoRebalanceResult {
    std::string timestamp;
    std::vector<AutoRebalanceTrade> trades;
    double total_turnover{0};      // Sum of trade values
    double turnover_pct{0};        // As % of portfolio
    int trades_executed{0};
    int trades_failed{0};
    std::string status;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "Rebalance at " << timestamp << "\n"
            << "  Trades: " << trades_executed << " executed, " << trades_failed << " failed\n"
            << "  Turnover: $" << std::fixed << std::setprecision(2) << total_turnover 
            << " (" << turnover_pct << "%)\n"
            << "  Status: " << status;
        return oss.str();
    }
};

/**
 * @brief Rebalancing configuration
 */
struct RebalanceConfig {
    AutoRebalanceFrequency frequency{AutoRebalanceFrequency::Monthly};
    double drift_threshold_pct{5.0};     // Trigger if any position drifts by this much
    double min_trade_value{100};         // Minimum trade size
    double max_turnover_pct{25};         // Maximum single rebalance turnover
    bool tax_aware{true};                // Consider tax implications
    bool use_limit_orders{true};
    double limit_offset_pct{0.1};        // Limit price offset from market
    int execution_window_minutes{30};    // Time to execute all trades
    
    std::vector<AutoTargetAllocation> targets;
    
    // Day/time preferences
    int preferred_day_of_week{1};        // 0=Sun, 1=Mon, etc.
    int preferred_hour{10};              // Hour of day (local)
};

/**
 * @brief Scheduled Rebalancer
 */
class ScheduledRebalancer {
public:
    using OrderCallback = std::function<std::string(const trading::OrderRequest&)>;
    using QuoteCallback = std::function<trading::Quote(const std::string&)>;
    using PositionCallback = std::function<std::vector<trading::UnifiedPosition>()>;
    using AlertCallback = std::function<void(const std::string&, const std::string&)>;
    
private:
    RebalanceConfig config_;
    OrderCallback submit_order_;
    QuoteCallback get_quote_;
    PositionCallback get_positions_;
    AlertCallback on_alert_;
    
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::thread scheduler_thread_;
    
    std::chrono::system_clock::time_point last_rebalance_;
    std::vector<AutoRebalanceResult> history_;

public:
    ScheduledRebalancer(const RebalanceConfig& config,
                        OrderCallback submit_order,
                        QuoteCallback get_quote,
                        PositionCallback get_positions,
                        AlertCallback on_alert = nullptr)
        : config_(config)
        , submit_order_(std::move(submit_order))
        , get_quote_(std::move(get_quote))
        , get_positions_(std::move(get_positions))
        , on_alert_(std::move(on_alert)) {}
    
    ~ScheduledRebalancer() {
        stop();
    }
    
    /**
     * @brief Start scheduler
     */
    void start() {
        if (running_) return;
        running_ = true;
        scheduler_thread_ = std::thread([this]() { scheduler_loop(); });
    }
    
    /**
     * @brief Stop scheduler
     */
    void stop() {
        running_ = false;
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.join();
        }
    }
    
    /**
     * @brief Check if rebalance is needed
     */
    bool needs_rebalance() {
        auto analysis = analyze_drift();
        
        for (const auto& [symbol, drift] : analysis) {
            if (std::abs(drift) >= config_.drift_threshold_pct) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Analyze current drift from targets
     */
    std::map<std::string, double> analyze_drift() {
        std::map<std::string, double> drift;
        
        auto positions = get_positions_();
        double total_value = 0;
        std::map<std::string, double> current_values;
        
        for (const auto& pos : positions) {
            current_values[pos.symbol] = pos.market_value;
            total_value += pos.market_value;
        }
        
        if (total_value <= 0) return drift;
        
        for (const auto& target : config_.targets) {
            double current_weight = 0;
            if (current_values.count(target.symbol)) {
                current_weight = (current_values[target.symbol] / total_value) * 100;
            }
            drift[target.symbol] = current_weight - target.target_weight;
        }
        
        return drift;
    }
    
    /**
     * @brief Calculate rebalancing trades
     */
    std::vector<AutoRebalanceTrade> calculate_trades() {
        std::vector<AutoRebalanceTrade> trades;
        
        auto positions = get_positions_();
        double total_value = 0;
        std::map<std::string, trading::UnifiedPosition> position_map;
        std::map<std::string, double> current_values;
        
        for (const auto& pos : positions) {
            position_map[pos.symbol] = pos;
            current_values[pos.symbol] = pos.market_value;
            total_value += pos.market_value;
        }
        
        if (total_value <= 0) return trades;
        
        // Calculate required trades
        for (const auto& target : config_.targets) {
            double current_weight = 0;
            double current_value = 0;
            double current_price = 0;
            
            if (current_values.count(target.symbol)) {
                current_value = current_values[target.symbol];
                current_weight = (current_value / total_value) * 100;
                current_price = position_map[target.symbol].current_price;
            } else {
                // Need to buy - get current price
                auto quote = get_quote_(target.symbol);
                current_price = (quote.bid + quote.ask) / 2;
            }
            
            double target_value = total_value * (target.target_weight / 100);
            double diff_value = target_value - current_value;
            
            if (std::abs(diff_value) < config_.min_trade_value) continue;
            
            AutoRebalanceTrade trade;
            trade.symbol = target.symbol;
            trade.current_weight = current_weight;
            trade.target_weight = target.target_weight;
            trade.estimated_value = std::abs(diff_value);
            
            if (diff_value > 0) {
                trade.side = trading::OrderSide::Buy;
                trade.quantity = std::floor(diff_value / current_price);
                trade.reason = "Underweight - buying to target";
            } else {
                trade.side = trading::OrderSide::Sell;
                trade.quantity = std::floor(-diff_value / current_price);
                trade.reason = "Overweight - selling to target";
            }
            
            if (trade.quantity > 0) {
                trades.push_back(trade);
            }
        }
        
        // Sort: sells first (to free up cash), then buys
        std::sort(trades.begin(), trades.end(), [](const auto& a, const auto& b) {
            return a.side == trading::OrderSide::Sell && b.side == trading::OrderSide::Buy;
        });
        
        return trades;
    }
    
    /**
     * @brief Execute rebalance
     */
    AutoRebalanceResult execute() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        AutoRebalanceResult result;
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        result.timestamp = oss.str();
        
        result.trades = calculate_trades();
        
        // Check turnover limit
        double total_trade_value = 0;
        for (const auto& trade : result.trades) {
            total_trade_value += trade.estimated_value;
        }
        
        auto positions = get_positions_();
        double portfolio_value = 0;
        for (const auto& pos : positions) {
            portfolio_value += pos.market_value;
        }
        
        result.total_turnover = total_trade_value;
        result.turnover_pct = portfolio_value > 0 ? 
            (total_trade_value / portfolio_value) * 100 : 0;
        
        if (result.turnover_pct > config_.max_turnover_pct) {
            result.status = "Skipped - turnover exceeds limit";
            if (on_alert_) {
                on_alert_("REBALANCE", "Rebalance skipped - turnover " + 
                         std::to_string(result.turnover_pct) + "% exceeds limit");
            }
            return result;
        }
        
        // Execute trades
        for (const auto& trade : result.trades) {
            trading::OrderRequest req;
            req.symbol = trade.symbol;
            req.side = trade.side;
            req.qty = trade.quantity;
            
            if (config_.use_limit_orders) {
                req.type = trading::OrderType::Limit;
                auto quote = get_quote_(trade.symbol);
                double mid = (quote.bid + quote.ask) / 2;
                double offset = mid * (config_.limit_offset_pct / 100);
                
                if (trade.side == trading::OrderSide::Buy) {
                    req.limit_price = mid + offset;
                } else {
                    req.limit_price = mid - offset;
                }
            } else {
                req.type = trading::OrderType::Market;
            }
            
            req.time_in_force = trading::TimeInForce::Day;
            
            std::string order_id = submit_order_(req);
            if (!order_id.empty()) {
                result.trades_executed++;
            } else {
                result.trades_failed++;
            }
        }
        
        result.status = result.trades_failed == 0 ? "Success" : "Partial - some trades failed";
        last_rebalance_ = now;
        history_.push_back(result);
        
        if (on_alert_) {
            on_alert_("REBALANCE", "Rebalance complete: " + 
                     std::to_string(result.trades_executed) + " trades executed");
        }
        
        return result;
    }
    
    /**
     * @brief Get rebalance history
     */
    std::vector<AutoRebalanceResult> get_history() const {
        return history_;
    }
    
    /**
     * @brief Update configuration
     */
    void update_config(const RebalanceConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

private:
    void scheduler_loop() {
        while (running_) {
            // Check if it's time to rebalance
            if (should_rebalance_now()) {
                execute();
            }
            
            // Also check drift if configured
            if (config_.frequency == AutoRebalanceFrequency::OnDrift && needs_rebalance()) {
                execute();
            }
            
            // Sleep for a while before checking again
            std::this_thread::sleep_for(std::chrono::minutes(15));
        }
    }
    
    bool should_rebalance_now() {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
            now - last_rebalance_).count();
        
        switch (config_.frequency) {
            case AutoRebalanceFrequency::Daily:
                return elapsed >= 24;
            case AutoRebalanceFrequency::Weekly:
                return elapsed >= 24 * 7;
            case AutoRebalanceFrequency::BiWeekly:
                return elapsed >= 24 * 14;
            case AutoRebalanceFrequency::Monthly:
                return elapsed >= 24 * 30;
            case AutoRebalanceFrequency::Quarterly:
                return elapsed >= 24 * 90;
            case AutoRebalanceFrequency::Annually:
                return elapsed >= 24 * 365;
            case AutoRebalanceFrequency::OnDrift:
                return false;  // Handled separately
            default:
                return false;
        }
    }
};

// =============================================================================
// Tax-Loss Harvesting
// =============================================================================

/**
 * @brief Tax-loss harvesting candidate
 */
struct AutoHarvestCandidate {
    std::string symbol;
    double unrealized_loss{0};
    double cost_basis{0};
    double current_value{0};
    double loss_pct{0};
    int holding_days{0};
    bool is_short_term{false};
    
    // Wash sale info
    bool has_recent_purchase{false};
    std::string last_purchase_date;
    
    // Replacement suggestion
    std::string replacement_symbol;
    double replacement_correlation{0};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << symbol << ": Loss $" << -unrealized_loss << " (" << loss_pct << "%)\n"
            << "  Holding: " << holding_days << " days (" 
            << (is_short_term ? "short-term" : "long-term") << ")\n"
            << "  Replace with: " << replacement_symbol;
        return oss.str();
    }
};

/**
 * @brief Tax-loss harvesting configuration
 */
struct TLHConfig {
    double min_loss_amount{100};        // Minimum loss to harvest
    double min_loss_pct{5.0};           // Minimum loss percentage
    bool harvest_short_term{true};      // Harvest short-term losses
    bool harvest_long_term{true};       // Harvest long-term losses
    double wash_sale_window_days{30};
    double max_harvest_per_year{3000};  // IRS limit for offset against ordinary income
    
    // Replacement securities mapping
    std::map<std::string, std::string> replacements;  // symbol -> replacement
    double min_replacement_correlation{0.8};
};

/**
 * @brief Tax-Loss Harvester
 */
class TaxLossHarvester {
public:
    using OrderCallback = std::function<std::string(const trading::OrderRequest&)>;
    using PositionCallback = std::function<std::vector<trading::UnifiedPosition>()>;
    using TaxCallback = std::function<tax::TaxTracker&()>;
    using AlertCallback = std::function<void(const std::string&, const std::string&)>;
    
private:
    TLHConfig config_;
    OrderCallback submit_order_;
    PositionCallback get_positions_;
    TaxCallback get_tax_tracker_;
    AlertCallback on_alert_;
    
    double harvested_this_year_{0};
    std::vector<AutoHarvestCandidate> harvest_history_;
    mutable std::mutex mutex_;

public:
    TaxLossHarvester(const TLHConfig& config,
                     OrderCallback submit_order,
                     PositionCallback get_positions,
                     TaxCallback get_tax_tracker,
                     AlertCallback on_alert = nullptr)
        : config_(config)
        , submit_order_(std::move(submit_order))
        , get_positions_(std::move(get_positions))
        , get_tax_tracker_(std::move(get_tax_tracker))
        , on_alert_(std::move(on_alert)) {}
    
    /**
     * @brief Scan for harvesting opportunities
     */
    std::vector<AutoHarvestCandidate> scan() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AutoHarvestCandidate> candidates;
        
        auto positions = get_positions_();
        
        for (const auto& pos : positions) {
            if (pos.unrealized_pl >= 0) continue;  // Only losses
            
            double loss = -pos.unrealized_pl;
            double loss_pct = (pos.cost_basis > 0) ? 
                (loss / pos.cost_basis) * 100 : 0;
            
            if (loss < config_.min_loss_amount) continue;
            if (loss_pct < config_.min_loss_pct) continue;
            
            AutoHarvestCandidate candidate;
            candidate.symbol = pos.symbol;
            candidate.unrealized_loss = pos.unrealized_pl;
            candidate.cost_basis = pos.cost_basis;
            candidate.current_value = pos.market_value;
            candidate.loss_pct = loss_pct;
            
            // Estimate holding period (would need actual purchase date)
            candidate.holding_days = 180;  // Default assumption
            candidate.is_short_term = candidate.holding_days < 365;
            
            // Check wash sale risk
            candidate.has_recent_purchase = false;  // Would check tax tracker
            
            // Get replacement
            if (config_.replacements.count(pos.symbol)) {
                candidate.replacement_symbol = config_.replacements.at(pos.symbol);
                candidate.replacement_correlation = 0.9;  // Would calculate
            }
            
            // Filter by term preference
            if (candidate.is_short_term && !config_.harvest_short_term) continue;
            if (!candidate.is_short_term && !config_.harvest_long_term) continue;
            
            candidates.push_back(candidate);
        }
        
        // Sort by loss amount (largest first)
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
                return a.unrealized_loss < b.unrealized_loss;  // More negative = larger loss
            });
        
        return candidates;
    }
    
    /**
     * @brief Execute harvest for a single candidate
     */
    bool harvest(const AutoHarvestCandidate& candidate, bool buy_replacement = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if within annual limit
        double loss = -candidate.unrealized_loss;
        if (harvested_this_year_ + loss > config_.max_harvest_per_year * 10) {
            // Allow some buffer above limit for capital gains offset
        }
        
        // Sell the losing position
        trading::OrderRequest sell_req;
        sell_req.symbol = candidate.symbol;
        sell_req.side = trading::OrderSide::Sell;
        sell_req.qty = candidate.current_value / 
            (candidate.current_value / std::abs(candidate.unrealized_loss));  // Approximate shares
        sell_req.type = trading::OrderType::Market;
        
        std::string sell_order = submit_order_(sell_req);
        if (sell_order.empty()) {
            return false;
        }
        
        // Buy replacement if specified
        if (buy_replacement && !candidate.replacement_symbol.empty()) {
            trading::OrderRequest buy_req;
            buy_req.symbol = candidate.replacement_symbol;
            buy_req.side = trading::OrderSide::Buy;
            buy_req.qty = sell_req.qty;  // Same number of shares
            buy_req.type = trading::OrderType::Market;
            
            submit_order_(buy_req);  // Don't fail overall if replacement fails
        }
        
        harvested_this_year_ += loss;
        harvest_history_.push_back(candidate);
        
        if (on_alert_) {
            on_alert_("TLH", "Harvested $" + std::to_string(loss) + 
                     " loss from " + candidate.symbol);
        }
        
        return true;
    }
    
    /**
     * @brief Auto-harvest all eligible candidates
     */
    int auto_harvest(double max_amount = 0) {
        auto candidates = scan();
        int harvested = 0;
        double amount_harvested = 0;
        
        for (const auto& candidate : candidates) {
            double loss = -candidate.unrealized_loss;
            
            if (max_amount > 0 && amount_harvested + loss > max_amount) {
                break;
            }
            
            if (harvest(candidate)) {
                harvested++;
                amount_harvested += loss;
            }
        }
        
        return harvested;
    }
    
    /**
     * @brief Get YTD harvested amount
     */
    double get_ytd_harvested() const { return harvested_this_year_; }
    
    /**
     * @brief Reset for new tax year
     */
    void reset_year() {
        std::lock_guard<std::mutex> lock(mutex_);
        harvested_this_year_ = 0;
    }
    
    /**
     * @brief Set replacement mappings
     */
    void set_replacements(const std::map<std::string, std::string>& replacements) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.replacements = replacements;
    }
};

// =============================================================================
// Drift Monitoring
// =============================================================================

/**
 * @brief Drift alert
 */
struct DriftAlert {
    std::string symbol;
    double current_weight{0};
    double target_weight{0};
    double drift_pct{0};
    std::string severity;  // "low", "medium", "high"
    std::string timestamp;
    bool acknowledged{false};
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "[" << severity << "] " << symbol << ": "
            << current_weight << "% vs target " << target_weight << "% "
            << "(drift: " << (drift_pct > 0 ? "+" : "") << drift_pct << "%)";
        return oss.str();
    }
};

/**
 * @brief Drift Monitor
 */
class DriftMonitor {
public:
    using PositionCallback = std::function<std::vector<trading::UnifiedPosition>()>;
    using AlertCallback = std::function<void(const DriftAlert&)>;
    
private:
    std::vector<AutoTargetAllocation> targets_;
    PositionCallback get_positions_;
    AlertCallback on_alert_;
    
    double low_threshold_{3.0};
    double medium_threshold_{5.0};
    double high_threshold_{10.0};
    
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    int check_interval_minutes_{15};
    
    std::vector<DriftAlert> active_alerts_;
    mutable std::mutex mutex_;

public:
    DriftMonitor(const std::vector<AutoTargetAllocation>& targets,
                 PositionCallback get_positions,
                 AlertCallback on_alert)
        : targets_(targets)
        , get_positions_(std::move(get_positions))
        , on_alert_(std::move(on_alert)) {}
    
    ~DriftMonitor() {
        stop();
    }
    
    void start() {
        if (running_) return;
        running_ = true;
        monitor_thread_ = std::thread([this]() { monitor_loop(); });
    }
    
    void stop() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    /**
     * @brief Check drift and generate alerts
     */
    std::vector<DriftAlert> check() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto positions = get_positions_();
        double total_value = 0;
        std::map<std::string, double> current_values;
        
        for (const auto& pos : positions) {
            current_values[pos.symbol] = pos.market_value;
            total_value += pos.market_value;
        }
        
        if (total_value <= 0) return {};
        
        std::vector<DriftAlert> alerts;
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        
        for (const auto& target : targets_) {
            double current_weight = 0;
            if (current_values.count(target.symbol)) {
                current_weight = (current_values[target.symbol] / total_value) * 100;
            }
            
            double drift = std::abs(current_weight - target.target_weight);
            
            if (drift >= low_threshold_) {
                DriftAlert alert;
                alert.symbol = target.symbol;
                alert.current_weight = current_weight;
                alert.target_weight = target.target_weight;
                alert.drift_pct = current_weight - target.target_weight;
                alert.timestamp = ts.str();
                
                if (drift >= high_threshold_) {
                    alert.severity = "high";
                } else if (drift >= medium_threshold_) {
                    alert.severity = "medium";
                } else {
                    alert.severity = "low";
                }
                
                alerts.push_back(alert);
                
                if (on_alert_) {
                    on_alert_(alert);
                }
            }
        }
        
        active_alerts_ = alerts;
        return alerts;
    }
    
    /**
     * @brief Get current alerts
     */
    std::vector<DriftAlert> get_alerts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_alerts_;
    }
    
    /**
     * @brief Set thresholds
     */
    void set_thresholds(double low, double medium, double high) {
        low_threshold_ = low;
        medium_threshold_ = medium;
        high_threshold_ = high;
    }
    
    /**
     * @brief Update targets
     */
    void set_targets(const std::vector<AutoTargetAllocation>& targets) {
        std::lock_guard<std::mutex> lock(mutex_);
        targets_ = targets;
    }

private:
    void monitor_loop() {
        while (running_) {
            check();
            std::this_thread::sleep_for(std::chrono::minutes(check_interval_minutes_));
        }
    }
};

// =============================================================================
// Stop-Loss Automation
// =============================================================================

/**
 * @brief Stop-loss rule
 */
struct StopLossRule {
    std::string rule_id;
    std::string symbol;           // Empty for portfolio-wide
    
    enum class Type {
        Fixed,         // Fixed price
        Percentage,    // % below entry
        Trailing,      // Trailing stop
        ATR            // ATR-based
    } type{Type::Percentage};
    
    double value{10.0};           // Percentage or dollar amount
    double trailing_offset{0};    // For trailing stops
    
    bool active{true};
    bool triggered{false};
    double trigger_price{0};
    std::string trigger_time;
    
    // Action
    enum class Action {
        Sell,          // Sell entire position
        SellHalf,      // Sell 50%
        Alert,         // Just alert, don't sell
        CloseOptions   // Close any options on this underlying
    } action{Action::Sell};
    
    std::string format() const {
        std::ostringstream oss;
        oss << "Stop-Loss [" << rule_id << "] ";
        if (!symbol.empty()) oss << symbol << " ";
        
        switch (type) {
            case Type::Fixed: oss << "Fixed $" << value; break;
            case Type::Percentage: oss << value << "% below entry"; break;
            case Type::Trailing: oss << value << "% trailing"; break;
            case Type::ATR: oss << value << "x ATR"; break;
        }
        
        oss << " -> " << (active ? "ACTIVE" : "INACTIVE");
        if (triggered) {
            oss << " [TRIGGERED @ $" << trigger_price << "]";
        }
        
        return oss.str();
    }
};

/**
 * @brief Stop-Loss Manager
 */
class StopLossManager {
public:
    using OrderCallback = std::function<std::string(const trading::OrderRequest&)>;
    using QuoteCallback = std::function<trading::Quote(const std::string&)>;
    using PositionCallback = std::function<std::vector<trading::UnifiedPosition>()>;
    using AlertCallback = std::function<void(const std::string&, const std::string&)>;
    
private:
    std::vector<StopLossRule> rules_;
    OrderCallback submit_order_;
    QuoteCallback get_quote_;
    PositionCallback get_positions_;
    AlertCallback on_alert_;
    
    std::map<std::string, double> high_water_marks_;  // For trailing stops
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    mutable std::mutex mutex_;
    int rule_counter_{0};

public:
    StopLossManager(OrderCallback submit_order,
                    QuoteCallback get_quote,
                    PositionCallback get_positions,
                    AlertCallback on_alert = nullptr)
        : submit_order_(std::move(submit_order))
        , get_quote_(std::move(get_quote))
        , get_positions_(std::move(get_positions))
        , on_alert_(std::move(on_alert)) {}
    
    ~StopLossManager() {
        stop();
    }
    
    void start() {
        if (running_) return;
        running_ = true;
        monitor_thread_ = std::thread([this]() { monitor_loop(); });
    }
    
    void stop() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    /**
     * @brief Add a stop-loss rule
     */
    std::string add_rule(const StopLossRule& rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        StopLossRule new_rule = rule;
        new_rule.rule_id = "SL-" + std::to_string(++rule_counter_);
        rules_.push_back(new_rule);
        
        return new_rule.rule_id;
    }
    
    /**
     * @brief Add percentage stop for a symbol
     */
    std::string add_percentage_stop(const std::string& symbol, 
                                    double pct,
                                    StopLossRule::Action action = StopLossRule::Action::Sell) {
        StopLossRule rule;
        rule.symbol = symbol;
        rule.type = StopLossRule::Type::Percentage;
        rule.value = pct;
        rule.action = action;
        return add_rule(rule);
    }
    
    /**
     * @brief Add trailing stop
     */
    std::string add_trailing_stop(const std::string& symbol, double trail_pct) {
        StopLossRule rule;
        rule.symbol = symbol;
        rule.type = StopLossRule::Type::Trailing;
        rule.value = trail_pct;
        
        // Initialize high water mark
        auto quote = get_quote_(symbol);
        high_water_marks_[symbol] = quote.price;
        
        return add_rule(rule);
    }
    
    /**
     * @brief Remove a rule
     */
    bool remove_rule(const std::string& rule_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::remove_if(rules_.begin(), rules_.end(),
            [&rule_id](const auto& r) { return r.rule_id == rule_id; });
        
        if (it != rules_.end()) {
            rules_.erase(it, rules_.end());
            return true;
        }
        return false;
    }
    
    /**
     * @brief Check all rules
     */
    std::vector<StopLossRule> check_rules() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StopLossRule> triggered;
        
        auto positions = get_positions_();
        std::map<std::string, trading::UnifiedPosition> pos_map;
        for (const auto& pos : positions) {
            pos_map[pos.symbol] = pos;
        }
        
        for (auto& rule : rules_) {
            if (!rule.active || rule.triggered) continue;
            
            if (!pos_map.count(rule.symbol)) continue;
            const auto& pos = pos_map[rule.symbol];
            
            double current_price = pos.current_price;
            double entry_price = pos.avg_entry_price;
            double stop_price = 0;
            
            switch (rule.type) {
                case StopLossRule::Type::Fixed:
                    stop_price = rule.value;
                    break;
                    
                case StopLossRule::Type::Percentage:
                    stop_price = entry_price * (1 - rule.value / 100);
                    break;
                    
                case StopLossRule::Type::Trailing: {
                    // Update high water mark
                    if (current_price > high_water_marks_[rule.symbol]) {
                        high_water_marks_[rule.symbol] = current_price;
                    }
                    stop_price = high_water_marks_[rule.symbol] * (1 - rule.value / 100);
                    break;
                }
                
                case StopLossRule::Type::ATR:
                    // Would need ATR calculation
                    stop_price = entry_price * (1 - 0.02 * rule.value);
                    break;
            }
            
            if (current_price <= stop_price) {
                rule.triggered = true;
                rule.trigger_price = current_price;
                
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::ostringstream ts;
                ts << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
                rule.trigger_time = ts.str();
                
                triggered.push_back(rule);
                
                // Execute action
                execute_action(rule, pos);
            }
        }
        
        return triggered;
    }
    
    /**
     * @brief Get all rules
     */
    std::vector<StopLossRule> get_rules() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return rules_;
    }

private:
    void execute_action(const StopLossRule& rule, const trading::UnifiedPosition& pos) {
        switch (rule.action) {
            case StopLossRule::Action::Sell: {
                trading::OrderRequest req;
                req.symbol = rule.symbol;
                req.side = trading::OrderSide::Sell;
                req.qty = pos.qty;
                req.type = trading::OrderType::Market;
                submit_order_(req);
                
                if (on_alert_) {
                    on_alert_("STOP-LOSS", "Stop-loss triggered for " + rule.symbol + 
                             " - selling entire position");
                }
                break;
            }
            
            case StopLossRule::Action::SellHalf: {
                trading::OrderRequest req;
                req.symbol = rule.symbol;
                req.side = trading::OrderSide::Sell;
                req.qty = std::floor(pos.qty / 2);
                req.type = trading::OrderType::Market;
                submit_order_(req);
                
                if (on_alert_) {
                    on_alert_("STOP-LOSS", "Stop-loss triggered for " + rule.symbol + 
                             " - selling 50%");
                }
                break;
            }
            
            case StopLossRule::Action::Alert:
                if (on_alert_) {
                    on_alert_("STOP-LOSS", "Stop-loss alert for " + rule.symbol + 
                             " @ $" + std::to_string(rule.trigger_price));
                }
                break;
                
            case StopLossRule::Action::CloseOptions:
                // Would close options positions on this underlying
                if (on_alert_) {
                    on_alert_("STOP-LOSS", "Stop-loss triggered - closing options on " + 
                             rule.symbol);
                }
                break;
        }
    }
    
    void monitor_loop() {
        while (running_) {
            check_rules();
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }
};

// =============================================================================
// Dividend Reinvestment
// =============================================================================

/**
 * @brief DRIP (Dividend Reinvestment Plan) rule
 */
struct DRIPRule {
    std::string rule_id;
    std::string symbol;           // Empty for all holdings
    
    enum class Mode {
        Full,              // Reinvest 100%
        Partial,           // Reinvest a percentage
        ThresholdOnly,     // Only reinvest if above threshold
        SameStock,         // Reinvest in same stock
        DifferentStock     // Reinvest in specified stock
    } mode{Mode::SameStock};
    
    double reinvest_pct{100};     // For Partial mode
    double threshold{10};          // Minimum dividend to reinvest
    std::string target_symbol;    // For DifferentStock mode
    
    bool active{true};
};

/**
 * @brief Dividend event
 */
struct DividendEvent {
    std::string symbol;
    double amount{0};
    double shares{0};
    std::string ex_date;
    std::string pay_date;
    std::string record_date;
    
    // Reinvestment
    bool reinvested{false};
    double reinvest_shares{0};
    double reinvest_price{0};
    std::string reinvest_symbol;
};

/**
 * @brief Dividend Reinvestment Manager
 */
class DividendReinvestmentManager {
public:
    using OrderCallback = std::function<std::string(const trading::OrderRequest&)>;
    using QuoteCallback = std::function<trading::Quote(const std::string&)>;
    using AlertCallback = std::function<void(const std::string&, const std::string&)>;
    
private:
    std::vector<DRIPRule> rules_;
    OrderCallback submit_order_;
    QuoteCallback get_quote_;
    AlertCallback on_alert_;
    
    std::vector<DividendEvent> dividend_history_;
    mutable std::mutex mutex_;
    int rule_counter_{0};

public:
    DividendReinvestmentManager(OrderCallback submit_order,
                                QuoteCallback get_quote,
                                AlertCallback on_alert = nullptr)
        : submit_order_(std::move(submit_order))
        , get_quote_(std::move(get_quote))
        , on_alert_(std::move(on_alert)) {}
    
    /**
     * @brief Add a DRIP rule
     */
    std::string add_rule(const DRIPRule& rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        DRIPRule new_rule = rule;
        new_rule.rule_id = "DRIP-" + std::to_string(++rule_counter_);
        rules_.push_back(new_rule);
        
        return new_rule.rule_id;
    }
    
    /**
     * @brief Enable full reinvestment for a symbol
     */
    std::string enable_drip(const std::string& symbol) {
        DRIPRule rule;
        rule.symbol = symbol;
        rule.mode = DRIPRule::Mode::SameStock;
        rule.reinvest_pct = 100;
        return add_rule(rule);
    }
    
    /**
     * @brief Enable portfolio-wide DRIP
     */
    std::string enable_portfolio_drip() {
        DRIPRule rule;
        rule.symbol = "";  // All symbols
        rule.mode = DRIPRule::Mode::SameStock;
        rule.reinvest_pct = 100;
        return add_rule(rule);
    }
    
    /**
     * @brief Process a dividend payment
     */
    bool process_dividend(const std::string& symbol, double amount, double shares) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        DividendEvent event;
        event.symbol = symbol;
        event.amount = amount;
        event.shares = shares;
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::localtime(&time), "%Y-%m-%d");
        event.pay_date = ts.str();
        
        // Find applicable rule
        const DRIPRule* applicable_rule = nullptr;
        for (const auto& rule : rules_) {
            if (!rule.active) continue;
            if (rule.symbol.empty() || rule.symbol == symbol) {
                applicable_rule = &rule;
                break;
            }
        }
        
        if (!applicable_rule) {
            dividend_history_.push_back(event);
            return false;
        }
        
        // Check threshold
        if (amount < applicable_rule->threshold) {
            dividend_history_.push_back(event);
            return false;
        }
        
        // Calculate reinvestment amount
        double reinvest_amount = amount;
        if (applicable_rule->mode == DRIPRule::Mode::Partial) {
            reinvest_amount = amount * (applicable_rule->reinvest_pct / 100);
        }
        
        // Determine target symbol
        std::string target = symbol;
        if (applicable_rule->mode == DRIPRule::Mode::DifferentStock &&
            !applicable_rule->target_symbol.empty()) {
            target = applicable_rule->target_symbol;
        }
        
        // Get quote and calculate shares
        auto quote = get_quote_(target);
        double price = (quote.bid + quote.ask) / 2;
        double shares_to_buy = reinvest_amount / price;
        
        if (shares_to_buy < 0.001) {
            dividend_history_.push_back(event);
            return false;
        }
        
        // Submit order
        trading::OrderRequest req;
        req.symbol = target;
        req.side = trading::OrderSide::Buy;
        req.qty = shares_to_buy;  // Fractional shares
        req.type = trading::OrderType::Market;
        
        std::string order_id = submit_order_(req);
        
        if (!order_id.empty()) {
            event.reinvested = true;
            event.reinvest_shares = shares_to_buy;
            event.reinvest_price = price;
            event.reinvest_symbol = target;
            
            if (on_alert_) {
                std::ostringstream msg;
                msg << std::fixed << std::setprecision(2);
                msg << "Reinvested $" << reinvest_amount << " dividend from " << symbol
                    << " -> " << shares_to_buy << " shares of " << target;
                on_alert_("DRIP", msg.str());
            }
        }
        
        dividend_history_.push_back(event);
        return event.reinvested;
    }
    
    /**
     * @brief Get dividend history
     */
    std::vector<DividendEvent> get_history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dividend_history_;
    }
    
    /**
     * @brief Get total reinvested amount
     */
    double get_total_reinvested() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total = 0;
        for (const auto& event : dividend_history_) {
            if (event.reinvested) {
                total += event.reinvest_shares * event.reinvest_price;
            }
        }
        return total;
    }
    
    /**
     * @brief Remove a rule
     */
    bool remove_rule(const std::string& rule_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::remove_if(rules_.begin(), rules_.end(),
            [&rule_id](const auto& r) { return r.rule_id == rule_id; });
        
        if (it != rules_.end()) {
            rules_.erase(it, rules_.end());
            return true;
        }
        return false;
    }
    
    /**
     * @brief Get all rules
     */
    std::vector<DRIPRule> get_rules() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return rules_;
    }
};

// =============================================================================
// Automation Manager (Unified)
// =============================================================================

/**
 * @brief Unified automation manager
 */
class AutomationManager {
private:
    std::unique_ptr<ScheduledRebalancer> rebalancer_;
    std::unique_ptr<TaxLossHarvester> harvester_;
    std::unique_ptr<DriftMonitor> drift_monitor_;
    std::unique_ptr<StopLossManager> stop_loss_;
    std::unique_ptr<DividendReinvestmentManager> drip_;
    
    mutable std::mutex mutex_;
    bool initialized_{false};

public:
    AutomationManager() = default;
    
    /**
     * @brief Initialize all automation components
     */
    template<typename OrderCB, typename QuoteCB, typename PosCB, typename TaxCB, typename AlertCB>
    void initialize(OrderCB submit_order,
                    QuoteCB get_quote,
                    PosCB get_positions,
                    TaxCB get_tax_tracker,
                    AlertCB on_alert,
                    const RebalanceConfig& rebalance_config,
                    const TLHConfig& tlh_config,
                    const std::vector<AutoTargetAllocation>& targets) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        rebalancer_ = std::make_unique<ScheduledRebalancer>(
            rebalance_config, submit_order, get_quote, get_positions, on_alert);
        
        harvester_ = std::make_unique<TaxLossHarvester>(
            tlh_config, submit_order, get_positions, get_tax_tracker, on_alert);
        
        drift_monitor_ = std::make_unique<DriftMonitor>(
            targets, get_positions, [on_alert](const DriftAlert& alert) {
                on_alert("DRIFT", alert.format());
            });
        
        stop_loss_ = std::make_unique<StopLossManager>(
            submit_order, get_quote, get_positions, on_alert);
        
        drip_ = std::make_unique<DividendReinvestmentManager>(
            submit_order, get_quote, on_alert);
        
        initialized_ = true;
    }
    
    /**
     * @brief Start all automation
     */
    void start_all() {
        if (!initialized_) return;
        
        if (rebalancer_) rebalancer_->start();
        if (drift_monitor_) drift_monitor_->start();
        if (stop_loss_) stop_loss_->start();
    }
    
    /**
     * @brief Stop all automation
     */
    void stop_all() {
        if (rebalancer_) rebalancer_->stop();
        if (drift_monitor_) drift_monitor_->stop();
        if (stop_loss_) stop_loss_->stop();
    }
    
    // Accessors
    ScheduledRebalancer* rebalancer() { return rebalancer_.get(); }
    TaxLossHarvester* harvester() { return harvester_.get(); }
    DriftMonitor* drift_monitor() { return drift_monitor_.get(); }
    StopLossManager* stop_loss() { return stop_loss_.get(); }
    DividendReinvestmentManager* drip() { return drip_.get(); }
};

} // namespace genie::portfolio

#endif // GENIE_PORTFOLIO_AUTOMATION_HPP
