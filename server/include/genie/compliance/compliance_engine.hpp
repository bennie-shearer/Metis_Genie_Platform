/**
 * @file compliance_engine.hpp
 * @brief Compliance Rule Engine for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_COMPLIANCE_ENGINE_HPP
#define GENIE_COMPLIANCE_ENGINE_HPP
#include "../portfolio/portfolio.hpp"
#include "../core/events.hpp"

namespace genie::compliance {
using namespace genie::portfolio;

struct ComplianceRule {
    std::string id{UuidGenerator::generate()}, name, description;
    ConstraintType type{ConstraintType::Custom}; bool is_hard{true}, is_active{true};
    double min_value{0}, max_value{0}; bool has_min{false}, has_max{false};
    std::function<bool(const Portfolio&, double*)> evaluator;
    ComplianceRule& set_max(double v) { max_value = v; has_max = true; return *this; }
    ComplianceRule& set_min(double v) { min_value = v; has_min = true; return *this; }
    ComplianceRule& soft() { is_hard = false; return *this; }
};

struct ComplianceCheckResult {
    std::string rule_id, rule_name; ComplianceStatus status{ComplianceStatus::Compliant};
    double current_value{0}, limit_value{0}; std::string message; bool is_hard{true};
    [[nodiscard]] bool is_compliant() const { return status == ComplianceStatus::Compliant; }
    [[nodiscard]] bool is_breach() const { return status == ComplianceStatus::Breach; }
};

struct ComplianceReport {
    PortfolioId portfolio_id; TimePoint report_time{std::chrono::system_clock::now()};
    std::vector<ComplianceCheckResult> results;
    size_t total{0}, compliant{0}, warnings{0}, breaches{0};
    ComplianceStatus overall{ComplianceStatus::Compliant};
    [[nodiscard]] double compliance_rate() const { return total > 0 ? (compliant * 100.0) / total : 100.0; }
};

class RuleLibrary {
public:
    static ComplianceRule single_position_limit(double max_pct) {
        ComplianceRule r; r.name = "Single Position Limit"; r.type = ConstraintType::Concentration; r.set_max(max_pct / 100.0);
        r.evaluator = [max_pct](const Portfolio& p, double* v) {
            for (const auto& [id, w] : p.get_weights()) { *v = w * 100; if (w > max_pct / 100.0) return false; } return true;
        };
        return r;
    }
    static ComplianceRule sector_limit(const std::string& sector, double max_pct) {
        ComplianceRule r; r.name = sector + " Sector Limit"; r.type = ConstraintType::Sector; r.set_max(max_pct / 100.0);
        r.evaluator = [sector, max_pct](const Portfolio& p, double* v) {
            auto sw = p.get_sector_weights(); auto it = sw.find(sector);
            *v = it != sw.end() ? it->second * 100 : 0; return *v <= max_pct;
        };
        return r;
    }
    static ComplianceRule max_leverage(double max_lev) {
        ComplianceRule r; r.name = "Maximum Leverage"; r.type = ConstraintType::Regulatory; r.set_max(max_lev);
        r.evaluator = [max_lev](const Portfolio& p, double* v) { *v = p.leverage(); return *v <= max_lev; };
        return r;
    }
    static ComplianceRule minimum_cash(double min_pct) {
        ComplianceRule r; r.name = "Minimum Cash"; r.type = ConstraintType::Liquidity; r.set_min(min_pct / 100.0);
        r.evaluator = [min_pct](const Portfolio& p, double* v) {
            double nav = p.nav().amount; *v = nav > 0 ? (p.cash_balance().amount / nav) * 100 : 0; return *v >= min_pct;
        };
        return r;
    }
    static ComplianceRule asset_class_range(AssetClass ac, double min_pct, double max_pct) {
        ComplianceRule r; r.name = asset_class_to_string(ac) + " Allocation"; r.type = ConstraintType::AssetClass;
        r.set_min(min_pct / 100.0).set_max(max_pct / 100.0);
        r.evaluator = [ac, min_pct, max_pct](const Portfolio& p, double* v) {
            auto aw = p.get_asset_class_weights(); auto it = aw.find(ac);
            *v = it != aw.end() ? it->second * 100 : 0; return *v >= min_pct && *v <= max_pct;
        };
        return r;
    }
};

class ComplianceEngine {
    std::vector<ComplianceRule> rules_; EventBus* event_bus_{nullptr}; mutable std::mutex mutex_;
public:
    void set_event_bus(EventBus* eb) { event_bus_ = eb; }
    void add_rule(ComplianceRule rule) { std::lock_guard lk(mutex_); rules_.push_back(std::move(rule)); }
    void remove_rule(const std::string& id) {
        std::lock_guard lk(mutex_);
        rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [&id](const auto& r) { return r.id == id; }), rules_.end());
    }
    [[nodiscard]] ComplianceReport check_portfolio(const Portfolio& portfolio) {
        ComplianceReport report; report.portfolio_id = portfolio.id();
        std::lock_guard lk(mutex_);
        for (const auto& rule : rules_) {
            if (!rule.is_active) continue;
            ComplianceCheckResult result; result.rule_id = rule.id; result.rule_name = rule.name; result.is_hard = rule.is_hard;
            double value = 0; bool passed = true;
            if (rule.evaluator) passed = rule.evaluator(portfolio, &value);
            result.current_value = value;
            result.limit_value = rule.has_max ? rule.max_value * 100 : (rule.has_min ? rule.min_value * 100 : 0);
            if (!passed) {
                result.status = rule.is_hard ? ComplianceStatus::Breach : ComplianceStatus::Warning;
                result.message = rule.name + ": " + std::to_string(value) + " vs limit " + std::to_string(result.limit_value);
                if (rule.is_hard) report.breaches++; else report.warnings++;
            } else { result.status = ComplianceStatus::Compliant; report.compliant++; }
            report.results.push_back(result); report.total++;
        }
        report.overall = report.breaches > 0 ? ComplianceStatus::Breach : (report.warnings > 0 ? ComplianceStatus::Warning : ComplianceStatus::Compliant);
        return report;
    }
    [[nodiscard]] size_t rule_count() const { std::lock_guard lk(mutex_); return rules_.size(); }
};
} // namespace genie::compliance
#endif
