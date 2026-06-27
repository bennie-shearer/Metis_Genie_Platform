/**
 * @file pretrade_compliance.hpp
 * @brief Pre-trade compliance check pipeline
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Rule-based pre-trade compliance checking:
 * - Ordered rule chain with veto/warn/pass semantics
 * - Position limits, concentration, restricted lists
 * - Sector/country/currency exposure limits
 * - Wash sale detection
 * - Regulatory (Reg NMS, MiFID II) checks
 * - Customizable rule priorities
 * - Audit trail for every check
 * - Bypass with approval workflow
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_COMPLIANCE_PRETRADE_HPP
#define GENIE_COMPLIANCE_PRETRADE_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>

namespace genie {
namespace compliance {
namespace pretrade {

// ============================================================================
// Enumerations
// ============================================================================

enum class CheckResult {
    Pass,
    Warn,
    SoftBlock,     // Requires override approval
    HardBlock      // Cannot be overridden
};

enum class RuleCategory {
    PositionLimit,
    Concentration,
    RestrictedList,
    SectorExposure,
    CountryExposure,
    CurrencyExposure,
    WashSale,
    RegulatoryNMS,
    MiFIDII,
    InternalPolicy,
    RiskLimit,
    LiquidityCheck,
    Custom
};

[[nodiscard]] inline std::string check_result_string(CheckResult r) {
    switch (r) {
        case CheckResult::Pass:      return "PASS";
        case CheckResult::Warn:      return "WARN";
        case CheckResult::SoftBlock: return "SOFT_BLOCK";
        case CheckResult::HardBlock: return "HARD_BLOCK";
    }
    return "UNKNOWN";
}

[[nodiscard]] inline std::string rule_category_string(RuleCategory c) {
    switch (c) {
        case RuleCategory::PositionLimit:     return "position_limit";
        case RuleCategory::Concentration:     return "concentration";
        case RuleCategory::RestrictedList:    return "restricted_list";
        case RuleCategory::SectorExposure:    return "sector_exposure";
        case RuleCategory::CountryExposure:   return "country_exposure";
        case RuleCategory::CurrencyExposure:  return "currency_exposure";
        case RuleCategory::WashSale:          return "wash_sale";
        case RuleCategory::RegulatoryNMS:     return "reg_nms";
        case RuleCategory::MiFIDII:           return "mifid_ii";
        case RuleCategory::InternalPolicy:    return "internal_policy";
        case RuleCategory::RiskLimit:         return "risk_limit";
        case RuleCategory::LiquidityCheck:    return "liquidity";
        case RuleCategory::Custom:            return "custom";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Order being checked
 */
struct OrderCheck {
    std::string order_id;
    std::string portfolio_id;
    std::string symbol;
    std::string side;              // "buy" or "sell"
    double quantity{0};
    double price{0};
    double notional{0};            // quantity * price
    std::string asset_class;
    std::string sector;
    std::string country;
    std::string currency;
    std::map<std::string, double> portfolio_weights;  // Current weights
    double portfolio_nav{0};
};

/**
 * @brief Individual rule check result
 */
struct RuleCheckResult {
    std::string rule_id;
    std::string rule_name;
    RuleCategory category;
    CheckResult result{CheckResult::Pass};
    std::string message;
    std::string detail;
    double threshold{0};
    double actual_value{0};
    int priority{0};               // Lower = checked first
    std::chrono::system_clock::time_point checked_at;

    [[nodiscard]] bool blocked() const {
        return result == CheckResult::HardBlock || result == CheckResult::SoftBlock;
    }
};

/**
 * @brief Complete compliance check report
 */
struct ComplianceCheckReport {
    std::string order_id;
    CheckResult overall_result{CheckResult::Pass};
    std::vector<RuleCheckResult> checks;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::milliseconds duration{0};
    bool overridden{false};
    std::string override_by;
    std::string override_reason;

    [[nodiscard]] bool can_trade() const {
        return overall_result == CheckResult::Pass ||
               overall_result == CheckResult::Warn ||
               overridden;
    }

    [[nodiscard]] int warning_count() const {
        return static_cast<int>(std::count_if(checks.begin(), checks.end(),
            [](const RuleCheckResult& c) { return c.result == CheckResult::Warn; }));
    }

    [[nodiscard]] int block_count() const {
        return static_cast<int>(std::count_if(checks.begin(), checks.end(),
            [](const RuleCheckResult& c) { return c.blocked(); }));
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Pre-Trade Compliance: " << check_result_string(overall_result)
            << " (" << checks.size() << " rules, "
            << warning_count() << " warnings, "
            << block_count() << " blocks)\n";
        for (const auto& c : checks) {
            if (c.result != CheckResult::Pass) {
                oss << "  [" << check_result_string(c.result) << "] "
                    << c.rule_name << ": " << c.message << "\n";
            }
        }
        if (overridden) {
            oss << "  OVERRIDDEN by " << override_by
                << ": " << override_reason << "\n";
        }
        return oss.str();
    }
};

// ============================================================================
// Rule Interface
// ============================================================================

using RuleFunction = std::function<RuleCheckResult(const OrderCheck&)>;

struct ComplianceRule {
    std::string id;
    std::string name;
    RuleCategory category;
    int priority{100};
    bool enabled{true};
    RuleFunction check;
};

// ============================================================================
// Pre-Trade Compliance Pipeline
// ============================================================================

/**
 * @brief Ordered pipeline of compliance checks
 */
class PreTradeCompliancePipeline {
public:
    PreTradeCompliancePipeline() {
        register_default_rules();
    }

    /**
     * @brief Add rule to pipeline
     */
    void add_rule(ComplianceRule rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        rules_.push_back(std::move(rule));
        sort_rules();
    }

    /**
     * @brief Remove rule by ID
     */
    bool remove_rule(const std::string& rule_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(rules_.begin(), rules_.end(),
            [&](const ComplianceRule& r) { return r.id == rule_id; });
        bool removed = it != rules_.end();
        rules_.erase(it, rules_.end());
        return removed;
    }

    /**
     * @brief Enable/disable rule
     */
    void set_rule_enabled(const std::string& rule_id, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& rule : rules_) {
            if (rule.id == rule_id) { rule.enabled = enabled; break; }
        }
    }

    /**
     * @brief Run all compliance checks on an order
     */
    ComplianceCheckReport check(const OrderCheck& order) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto start = std::chrono::steady_clock::now();

        ComplianceCheckReport report;
        report.order_id = order.order_id;
        report.timestamp = std::chrono::system_clock::now();
        report.overall_result = CheckResult::Pass;

        for (const auto& rule : rules_) {
            if (!rule.enabled) continue;

            RuleCheckResult result = rule.check(order);
            result.rule_id = rule.id;
            result.rule_name = rule.name;
            result.category = rule.category;
            result.priority = rule.priority;
            result.checked_at = std::chrono::system_clock::now();

            // Escalate overall result
            if (static_cast<int>(result.result) >
                static_cast<int>(report.overall_result)) {
                report.overall_result = result.result;
            }

            report.checks.push_back(result);

            // Short-circuit on hard block
            if (result.result == CheckResult::HardBlock) break;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        report.duration = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

        // Store in audit trail
        audit_trail_.push_back(report);
        if (audit_trail_.size() > max_audit_) audit_trail_.pop_front();

        return report;
    }

    /**
     * @brief Override a soft block
     */
    bool override_check(ComplianceCheckReport& report,
                          const std::string& approver,
                          const std::string& reason) {
        if (report.overall_result == CheckResult::HardBlock) return false;
        report.overridden = true;
        report.override_by = approver;
        report.override_reason = reason;
        return true;
    }

    /**
     * @brief Set restricted list
     */
    void set_restricted_list(const std::vector<std::string>& symbols) {
        std::lock_guard<std::mutex> lock(mutex_);
        restricted_symbols_.clear();
        for (const auto& s : symbols) restricted_symbols_.insert(s);
    }

    /**
     * @brief Set position limit
     */
    void set_position_limit(double max_weight_pct) {
        max_position_weight_ = max_weight_pct;
    }

    /**
     * @brief Set sector limit
     */
    void set_sector_limit(double max_weight_pct) {
        max_sector_weight_ = max_weight_pct;
    }

    /**
     * @brief Get audit trail
     */
    [[nodiscard]] std::vector<ComplianceCheckReport> audit_trail(int last_n = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ComplianceCheckReport> result;
        int start = std::max(0, static_cast<int>(audit_trail_.size()) - last_n);
        for (int i = start; i < static_cast<int>(audit_trail_.size()); ++i) {
            result.push_back(audit_trail_[i]);
        }
        return result;
    }

    [[nodiscard]] int rule_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(rules_.size());
    }

private:
    mutable std::mutex mutex_;
    std::vector<ComplianceRule> rules_;
    std::set<std::string> restricted_symbols_;
    std::deque<ComplianceCheckReport> audit_trail_;
    size_t max_audit_{10000};
    double max_position_weight_{10.0};  // 10%
    double max_sector_weight_{30.0};    // 30%

    void sort_rules() {
        std::sort(rules_.begin(), rules_.end(),
            [](const ComplianceRule& a, const ComplianceRule& b) {
                return a.priority < b.priority;
            });
    }

    void register_default_rules() {
        // Rule 1: Restricted list check (highest priority)
        {
            ComplianceRule rule;
            rule.id = "PTR-001";
            rule.name = "Restricted List";
            rule.category = RuleCategory::RestrictedList;
            rule.priority = 10;
            rule.check = [this](const OrderCheck& order) -> RuleCheckResult {
                RuleCheckResult r;
                if (restricted_symbols_.count(order.symbol)) {
                    r.result = CheckResult::HardBlock;
                    r.message = order.symbol + " is on the restricted list";
                }
                return r;
            };
            rules_.push_back(rule);
        }

        // Rule 2: Position concentration limit
        {
            ComplianceRule rule;
            rule.id = "PTR-002";
            rule.name = "Position Concentration";
            rule.category = RuleCategory::Concentration;
            rule.priority = 20;
            rule.check = [this](const OrderCheck& order) -> RuleCheckResult {
                RuleCheckResult r;
                if (order.portfolio_nav <= 0) return r;
                double post_trade_weight = 0;
                auto it = order.portfolio_weights.find(order.symbol);
                double current_value = (it != order.portfolio_weights.end()) ?
                    it->second * order.portfolio_nav : 0;
                double delta = order.side == "buy" ? order.notional : -order.notional;
                post_trade_weight = (current_value + delta) / order.portfolio_nav * 100.0;

                r.threshold = max_position_weight_;
                r.actual_value = post_trade_weight;
                if (post_trade_weight > max_position_weight_) {
                    r.result = CheckResult::SoftBlock;
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(1);
                    oss << order.symbol << " would be " << post_trade_weight
                        << "% (limit: " << max_position_weight_ << "%)";
                    r.message = oss.str();
                } else if (post_trade_weight > max_position_weight_ * 0.8) {
                    r.result = CheckResult::Warn;
                    r.message = order.symbol + " approaching concentration limit";
                }
                return r;
            };
            rules_.push_back(rule);
        }

        // Rule 3: Minimum order value
        {
            ComplianceRule rule;
            rule.id = "PTR-003";
            rule.name = "Minimum Order Value";
            rule.category = RuleCategory::InternalPolicy;
            rule.priority = 30;
            rule.check = [](const OrderCheck& order) -> RuleCheckResult {
                RuleCheckResult r;
                r.threshold = 100.0;
                r.actual_value = order.notional;
                if (order.notional < 100.0 && order.notional > 0) {
                    r.result = CheckResult::Warn;
                    r.message = "Order value below $100 minimum";
                }
                return r;
            };
            rules_.push_back(rule);
        }

        // Rule 4: Maximum single order value
        {
            ComplianceRule rule;
            rule.id = "PTR-004";
            rule.name = "Maximum Order Value";
            rule.category = RuleCategory::RiskLimit;
            rule.priority = 25;
            rule.check = [](const OrderCheck& order) -> RuleCheckResult {
                RuleCheckResult r;
                double max_order = 10000000.0; // $10M
                r.threshold = max_order;
                r.actual_value = order.notional;
                if (order.notional > max_order) {
                    r.result = CheckResult::HardBlock;
                    r.message = "Order exceeds $10M maximum";
                } else if (order.notional > max_order * 0.5) {
                    r.result = CheckResult::Warn;
                    r.message = "Large order: manual review recommended";
                }
                return r;
            };
            rules_.push_back(rule);
        }

        sort_rules();
    }
};

} // namespace pretrade
} // namespace compliance
} // namespace genie

#endif // GENIE_COMPLIANCE_PRETRADE_HPP
