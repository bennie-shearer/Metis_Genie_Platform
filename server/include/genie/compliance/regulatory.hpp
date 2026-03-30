/**
 * @file regulatory.hpp
 * @brief Regulatory compliance framework for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * UCITS, MiFID II, SEC Rule 18f-4 constraint checks.
 * Configurable rule sets with pre/post-trade validation.
 */
#pragma once
#ifndef GENIE_COMPLIANCE_REGULATORY_HPP
#define GENIE_COMPLIANCE_REGULATORY_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace genie::compliance {

/** Regulatory regime */
enum class Regime { UCITS, MiFID_II, SEC_18f4, Custom };

inline std::string regime_name(Regime r) {
    switch (r) {
        case Regime::UCITS: return "UCITS"; case Regime::MiFID_II: return "MiFID II";
        case Regime::SEC_18f4: return "SEC 18f-4"; case Regime::Custom: return "Custom";
        default: return "Unknown";
    }
}

/** Severity of a rule violation */
enum class Severity { Info, Warning, Breach, HardBlock };

/** Compliance check result */
struct CheckResult {
    bool passed{true};
    std::string rule_id;
    std::string rule_name;
    Severity severity{Severity::Info};
    std::string message;
    double current_value{0};
    double limit_value{0};
    std::string portfolio_id;
    std::string security_id;
};

/** Portfolio position snapshot for compliance checking */
struct CompliancePosition {
    std::string security_id;
    std::string issuer;
    std::string asset_class;       // Equity, FixedIncome, Derivative, Cash
    std::string country;
    std::string sector;
    double weight{0};              // % of NAV
    double market_value{0};
    double notional_exposure{0};   // for derivatives
    bool is_transferable{true};
    int credit_rating{0};          // 1=AAA, 7=CCC
};

struct CompliancePortfolio {
    std::string id;
    std::string name;
    double nav{0};
    double cash{0};
    std::vector<CompliancePosition> positions;

    [[nodiscard]] double total_weight(const std::string& asset_class) const {
        double w = 0;
        for (const auto& p : positions)
            if (p.asset_class == asset_class) w += p.weight;
        return w;
    }

    [[nodiscard]] double issuer_weight(const std::string& issuer) const {
        double w = 0;
        for (const auto& p : positions)
            if (p.issuer == issuer) w += p.weight;
        return w;
    }

    [[nodiscard]] double country_weight(const std::string& country) const {
        double w = 0;
        for (const auto& p : positions)
            if (p.country == country) w += p.weight;
        return w;
    }

    [[nodiscard]] double derivatives_notional() const {
        double n = 0;
        for (const auto& p : positions)
            if (p.asset_class == "Derivative") n += std::abs(p.notional_exposure);
        return n;
    }
};

/** A regulatory rule */
struct Rule {
    std::string id;
    std::string name;
    Regime regime;
    Severity severity{Severity::Breach};
    std::function<CheckResult(const CompliancePortfolio&)> check;
};

/** Regulatory compliance engine */
class RegulatoryEngine {
    std::vector<Rule> rules_;

public:
    RegulatoryEngine() { load_default_rules(); }

    /** Add a custom rule */
    void add_rule(const Rule& rule) { rules_.push_back(rule); }

    /** Run all rules against a portfolio */
    [[nodiscard]] std::vector<CheckResult> check_all(const CompliancePortfolio& portfolio) const {
        std::vector<CheckResult> results;
        for (const auto& rule : rules_) {
            auto result = rule.check(portfolio);
            result.rule_id = rule.id;
            result.rule_name = rule.name;
            result.severity = result.passed ? Severity::Info : rule.severity;
            result.portfolio_id = portfolio.id;
            results.push_back(result);
        }
        return results;
    }

    /** Run rules for a specific regime */
    [[nodiscard]] std::vector<CheckResult> check_regime(
            const CompliancePortfolio& portfolio, Regime regime) const {
        std::vector<CheckResult> results;
        for (const auto& rule : rules_) {
            if (rule.regime != regime) continue;
            auto result = rule.check(portfolio);
            result.rule_id = rule.id;
            result.rule_name = rule.name;
            result.severity = result.passed ? Severity::Info : rule.severity;
            result.portfolio_id = portfolio.id;
            results.push_back(result);
        }
        return results;
    }

    /** Pre-trade compliance check: would adding this position violate any rules? */
    [[nodiscard]] std::vector<CheckResult> pre_trade_check(
            CompliancePortfolio portfolio,
            const CompliancePosition& proposed_position) const {
        // Add proposed position to a copy
        portfolio.positions.push_back(proposed_position);
        // Recalculate NAV
        portfolio.nav += proposed_position.market_value;
        // Re-weight
        for (auto& p : portfolio.positions)
            p.weight = (portfolio.nav > 0) ? p.market_value / portfolio.nav * 100.0 : 0;
        return check_all(portfolio);
    }

    [[nodiscard]] size_t rule_count() const { return rules_.size(); }
    [[nodiscard]] size_t rule_count(Regime r) const {
        return std::count_if(rules_.begin(), rules_.end(),
            [r](const Rule& rule) { return rule.regime == r; });
    }

    /** Get violations only (failed checks) */
    [[nodiscard]] static std::vector<CheckResult> violations(const std::vector<CheckResult>& results) {
        std::vector<CheckResult> v;
        for (const auto& r : results) if (!r.passed) v.push_back(r);
        return v;
    }

private:
    void load_default_rules() {
        // UCITS 5/10/40 rule
        rules_.push_back({"UCITS-001", "UCITS 5/10/40: Single issuer max 10%", Regime::UCITS, Severity::Breach,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 10.0;
                for (const auto& pos : p.positions) {
                    double iw = p.issuer_weight(pos.issuer);
                    if (iw > 10.0) {
                        r.passed = false; r.current_value = iw;
                        r.security_id = pos.issuer;
                        r.message = "Issuer " + pos.issuer + " weight " + std::to_string(iw) + "% exceeds 10%";
                        return r;
                    }
                }
                r.passed = true; return r;
            }});

        rules_.push_back({"UCITS-002", "UCITS: >5% issuers must not exceed 40% combined", Regime::UCITS, Severity::Breach,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 40.0;
                std::map<std::string, double> issuer_weights;
                for (const auto& pos : p.positions) issuer_weights[pos.issuer] += pos.weight;
                double large_sum = 0;
                for (const auto& [issuer, w] : issuer_weights)
                    if (w > 5.0) large_sum += w;
                r.current_value = large_sum;
                r.passed = (large_sum <= 40.0);
                if (!r.passed) r.message = "Combined weight of >5% issuers: " + std::to_string(large_sum) + "%";
                return r;
            }});

        rules_.push_back({"UCITS-003", "UCITS: Max 10% in non-transferable securities", Regime::UCITS, Severity::Breach,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 10.0;
                double nt = 0;
                for (const auto& pos : p.positions) if (!pos.is_transferable) nt += pos.weight;
                r.current_value = nt; r.passed = (nt <= 10.0);
                if (!r.passed) r.message = "Non-transferable: " + std::to_string(nt) + "%";
                return r;
            }});

        // MiFID II
        rules_.push_back({"MIFID-001", "MiFID II: Single security concentration limit 25%", Regime::MiFID_II, Severity::Warning,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 25.0;
                for (const auto& pos : p.positions) {
                    if (pos.weight > 25.0) {
                        r.passed = false; r.current_value = pos.weight; r.security_id = pos.security_id;
                        r.message = pos.security_id + " is " + std::to_string(pos.weight) + "% of NAV";
                        return r;
                    }
                }
                r.passed = true; return r;
            }});

        rules_.push_back({"MIFID-002", "MiFID II: Country concentration limit 40%", Regime::MiFID_II, Severity::Warning,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 40.0;
                std::map<std::string, double> cw;
                for (const auto& pos : p.positions) cw[pos.country] += pos.weight;
                for (const auto& [country, w] : cw) {
                    if (w > 40.0) {
                        r.passed = false; r.current_value = w; r.security_id = country;
                        r.message = country + " concentration: " + std::to_string(w) + "%";
                        return r;
                    }
                }
                r.passed = true; return r;
            }});

        // SEC Rule 18f-4 (derivatives risk management)
        rules_.push_back({"SEC-001", "SEC 18f-4: Derivatives notional limit 150% of NAV", Regime::SEC_18f4, Severity::HardBlock,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 150.0;
                double notional_pct = (p.nav > 0) ? p.derivatives_notional() / p.nav * 100.0 : 0;
                r.current_value = notional_pct; r.passed = (notional_pct <= 150.0);
                if (!r.passed) r.message = "Derivatives notional " + std::to_string(notional_pct) + "% of NAV exceeds 150%";
                return r;
            }});

        rules_.push_back({"SEC-002", "SEC 18f-4: Cash/cash-eq minimum 5%", Regime::SEC_18f4, Severity::Warning,
            [](const CompliancePortfolio& p) -> CheckResult {
                CheckResult r; r.limit_value = 5.0;
                double cash_pct = (p.nav > 0) ? p.cash / p.nav * 100.0 : 0;
                r.current_value = cash_pct; r.passed = (cash_pct >= 5.0);
                if (!r.passed) r.message = "Cash " + std::to_string(cash_pct) + "% below 5% minimum";
                return r;
            }});
    }
};

} // namespace genie::compliance

#endif // GENIE_COMPLIANCE_REGULATORY_HPP
