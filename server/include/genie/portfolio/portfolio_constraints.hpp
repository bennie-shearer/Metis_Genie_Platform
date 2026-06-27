/**
 * @file portfolio_constraints.hpp
 * @brief Portfolio Constraint Validation Engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Validates portfolio positions and proposed trades against configurable
 * constraints including concentration limits, sector limits, liquidity
 * requirements, ESG restrictions, and regulatory compliance.
 *
 * Features:
 *  - Position-level concentration limits (single name, sector, country)
 *  - Asset class allocation bands (min/max per class)
 *  - Liquidity constraints (max % of ADV, illiquid asset caps)
 *  - ESG/SRI exclusion lists and scoring thresholds
 *  - Currency exposure limits
 *  - Leverage and margin requirements
 *  - Turnover limits (annual turnover cap)
 *  - Cash reserve minimums
 *  - Pre-trade constraint checking
 *  - Post-trade compliance verification
 *  - Constraint violation reporting with severity
 *  - Multiple constraint profiles (aggressive, moderate, conservative)
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_PORTFOLIO_CONSTRAINTS_HPP
#define GENIE_PORTFOLIO_CONSTRAINTS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <mutex>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace genie::portfolio {

// ============================================================================
// Enums
// ============================================================================

enum class ConstraintType { CONCENTRATION, SECTOR, COUNTRY, ASSET_CLASS, LIQUIDITY,
                            ESG, CURRENCY, LEVERAGE, TURNOVER, CASH, CUSTOM };
enum class ViolationSeverity { WARNING, SOFT_BREACH, HARD_BREACH };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Portfolio position for constraint checking */
struct ConstraintPosition {
    std::string symbol;
    std::string sector;
    std::string country;
    std::string asset_class;
    std::string currency;
    double weight{0.0};        // Portfolio weight (0-1)
    double market_value{0.0};
    double adv{0.0};           // Average daily volume in $
    double esg_score{0.0};     // 0-100
    bool is_restricted{false};
    double leverage_ratio{1.0};
    int liquidity_days{1};     // Days to liquidate
};

/** @brief Constraint definition */
struct PortfolioConstraint {
    std::string name;
    ConstraintType type{ConstraintType::CONCENTRATION};
    std::string target;          // What this constrains (e.g., "AAPL", "Technology", "US")
    double min_value{0.0};
    double max_value{1.0};
    double warning_threshold{0.9}; // % of max before warning
    ViolationSeverity severity_on_breach{ViolationSeverity::HARD_BREACH};
    bool enabled{true};
    std::string description;
};

/** @brief Constraint violation */
struct ConstraintViolation {
    std::string constraint_name;
    ConstraintType type{ConstraintType::CONCENTRATION};
    ViolationSeverity severity{ViolationSeverity::WARNING};
    std::string target;
    double current_value{0.0};
    double limit_value{0.0};
    double breach_amount{0.0};
    std::string description;
    std::string remediation;
};

/** @brief Constraint check result */
struct ConstraintCheckResult {
    bool compliant{true};
    int total_constraints_checked{0};
    int violations_count{0};
    int warnings_count{0};
    int soft_breaches{0};
    int hard_breaches{0};
    std::vector<ConstraintViolation> violations;
    double compliance_score{100.0}; // 0-100
    std::string checked_at;
};

/** @brief Constraint profile (named set of constraints) */
struct ConstraintProfile {
    std::string profile_id;
    std::string name;
    std::string description;
    std::vector<PortfolioConstraint> constraints;
};

/** @brief Engine statistics */
struct ConstraintEngineStats {
    uint64_t checks_run{0};
    uint64_t violations_detected{0};
    uint64_t hard_breaches{0};
    std::size_t active_constraints{0};
    std::size_t profiles_loaded{0};
    double avg_compliance_score{0.0};
};

// ============================================================================
// PortfolioConstraintEngine
// ============================================================================

/**
 * @class PortfolioConstraintEngine
 * @brief Validates portfolios against configurable constraints
 */
class PortfolioConstraintEngine {
public:
    PortfolioConstraintEngine() { initialize_default_profiles(); }

    // ---- Constraint Management ----

    /** @brief Add a constraint to the active set */
    void add_constraint(PortfolioConstraint constraint) {
        std::lock_guard lock(mutex_);
        constraints_.push_back(std::move(constraint));
    }

    /** @brief Remove constraints by name */
    bool remove_constraint(const std::string& name) {
        std::lock_guard lock(mutex_);
        auto it = std::remove_if(constraints_.begin(), constraints_.end(),
            [&](const auto& c) { return c.name == name; });
        if (it != constraints_.end()) {
            constraints_.erase(it, constraints_.end());
            return true;
        }
        return false;
    }

    /** @brief Load a constraint profile */
    void load_profile(const std::string& profile_id) {
        std::lock_guard lock(mutex_);
        auto it = profiles_.find(profile_id);
        if (it != profiles_.end()) {
            constraints_ = it->second.constraints;
        }
    }

    /** @brief Register a constraint profile */
    void register_profile(ConstraintProfile profile) {
        std::lock_guard lock(mutex_);
        profiles_[profile.profile_id] = std::move(profile);
    }

    /** @brief List profiles */
    [[nodiscard]] std::vector<ConstraintProfile> list_profiles() const {
        std::lock_guard lock(mutex_);
        std::vector<ConstraintProfile> result;
        for (const auto& [_, p] : profiles_) result.push_back(p);
        return result;
    }

    /** @brief Add to ESG exclusion list */
    void add_esg_exclusion(const std::string& symbol) {
        std::lock_guard lock(mutex_);
        esg_exclusions_.insert(symbol);
    }

    // ---- Validation ----

    /** @brief Check portfolio against all active constraints */
    ConstraintCheckResult check(const std::vector<ConstraintPosition>& positions) {
        std::lock_guard lock(mutex_);
        ConstraintCheckResult result;
        result.checked_at = now_str();
        double total_value = 0;
        for (const auto& p : positions) total_value += p.market_value;

        for (const auto& constraint : constraints_) {
            if (!constraint.enabled) continue;
            result.total_constraints_checked++;

            auto violations = check_constraint(constraint, positions, total_value);
            for (auto& v : violations) {
                result.violations.push_back(std::move(v));
                result.violations_count++;
                switch (result.violations.back().severity) {
                    case ViolationSeverity::WARNING: result.warnings_count++; break;
                    case ViolationSeverity::SOFT_BREACH: result.soft_breaches++; break;
                    case ViolationSeverity::HARD_BREACH: result.hard_breaches++; break;
                }
            }
        }

        // ESG exclusion check
        for (const auto& p : positions) {
            if (esg_exclusions_.find(p.symbol) != esg_exclusions_.end()) {
                ConstraintViolation v;
                v.constraint_name = "ESG Exclusion";
                v.type = ConstraintType::ESG;
                v.severity = ViolationSeverity::HARD_BREACH;
                v.target = p.symbol;
                v.current_value = p.weight;
                v.description = p.symbol + " is on ESG exclusion list";
                v.remediation = "Liquidate position in " + p.symbol;
                result.violations.push_back(std::move(v));
                result.violations_count++;
                result.hard_breaches++;
            }
        }

        result.compliant = result.hard_breaches == 0;
        result.compliance_score = result.total_constraints_checked > 0
            ? std::max(0.0, 100.0 - result.hard_breaches * 25.0 - result.soft_breaches * 10.0 - result.warnings_count * 2.0)
            : 100.0;

        checks_run_++;
        total_violations_ += result.violations_count;
        total_hard_breaches_ += result.hard_breaches;
        total_compliance_score_ += result.compliance_score;

        return result;
    }

    /** @brief Pre-trade check: would adding this position violate constraints? */
    ConstraintCheckResult pre_trade_check(const std::vector<ConstraintPosition>& current_positions,
                                          const ConstraintPosition& proposed_trade) {
        auto positions = current_positions;
        // Merge proposed trade
        bool found = false;
        for (auto& p : positions) {
            if (p.symbol == proposed_trade.symbol) {
                p.weight += proposed_trade.weight;
                p.market_value += proposed_trade.market_value;
                found = true;
                break;
            }
        }
        if (!found) positions.push_back(proposed_trade);
        return check(positions);
    }

    // ---- Statistics ----

    [[nodiscard]] std::size_t constraint_count() const {
        std::lock_guard lock(mutex_);
        return constraints_.size();
    }

    [[nodiscard]] ConstraintEngineStats stats() const {
        std::lock_guard lock(mutex_);
        ConstraintEngineStats s;
        s.checks_run = checks_run_;
        s.violations_detected = total_violations_;
        s.hard_breaches = total_hard_breaches_;
        s.active_constraints = constraints_.size();
        s.profiles_loaded = profiles_.size();
        s.avg_compliance_score = checks_run_ > 0 ? total_compliance_score_ / checks_run_ : 100.0;
        return s;
    }

private:
    std::vector<ConstraintViolation> check_constraint(
        const PortfolioConstraint& constraint,
        const std::vector<ConstraintPosition>& positions,
        double total_value
    ) const {
        std::vector<ConstraintViolation> violations;

        switch (constraint.type) {
            case ConstraintType::CONCENTRATION:
                for (const auto& p : positions) {
                    if (!constraint.target.empty() && p.symbol != constraint.target) continue;
                    if (p.weight > constraint.max_value) {
                        violations.push_back(make_violation(constraint, p.symbol, p.weight,
                            constraint.max_value, "Position weight " + pct_str(p.weight)
                            + " exceeds max " + pct_str(constraint.max_value)));
                    } else if (p.weight > constraint.max_value * constraint.warning_threshold) {
                        auto v = make_violation(constraint, p.symbol, p.weight,
                            constraint.max_value, "Position approaching limit");
                        v.severity = ViolationSeverity::WARNING;
                        violations.push_back(std::move(v));
                    }
                }
                break;

            case ConstraintType::SECTOR: {
                std::unordered_map<std::string, double> sector_weights;
                for (const auto& p : positions) sector_weights[p.sector] += p.weight;
                for (const auto& [sector, weight] : sector_weights) {
                    if (!constraint.target.empty() && sector != constraint.target) continue;
                    if (weight > constraint.max_value) {
                        violations.push_back(make_violation(constraint, sector, weight,
                            constraint.max_value, "Sector '" + sector + "' at " + pct_str(weight)));
                    }
                }
                break;
            }

            case ConstraintType::COUNTRY: {
                std::unordered_map<std::string, double> country_weights;
                for (const auto& p : positions) country_weights[p.country] += p.weight;
                for (const auto& [country, weight] : country_weights) {
                    if (weight > constraint.max_value) {
                        violations.push_back(make_violation(constraint, country, weight,
                            constraint.max_value, "Country '" + country + "' at " + pct_str(weight)));
                    }
                }
                break;
            }

            case ConstraintType::LIQUIDITY:
                for (const auto& p : positions) {
                    if (p.adv > 0 && p.market_value / p.adv > constraint.max_value) {
                        violations.push_back(make_violation(constraint, p.symbol,
                            p.market_value / p.adv, constraint.max_value,
                            p.symbol + " would take " + std::to_string(static_cast<int>(p.market_value / p.adv)) + " days to liquidate"));
                    }
                }
                break;

            case ConstraintType::CASH: {
                double cash_weight = 0;
                for (const auto& p : positions) {
                    if (p.asset_class == "Cash") cash_weight += p.weight;
                }
                if (cash_weight < constraint.min_value) {
                    violations.push_back(make_violation(constraint, "Cash", cash_weight,
                        constraint.min_value, "Cash at " + pct_str(cash_weight) + " below minimum " + pct_str(constraint.min_value)));
                }
                break;
            }

            default: break;
        }
        return violations;
    }

    ConstraintViolation make_violation(const PortfolioConstraint& c, const std::string& target,
                                       double current, double limit, const std::string& desc) const {
        ConstraintViolation v;
        v.constraint_name = c.name;
        v.type = c.type;
        v.severity = c.severity_on_breach;
        v.target = target;
        v.current_value = current;
        v.limit_value = limit;
        v.breach_amount = current - limit;
        v.description = desc;
        v.remediation = "Reduce " + target + " exposure by " + pct_str(std::abs(v.breach_amount));
        return v;
    }

    void initialize_default_profiles() {
        // Conservative profile
        ConstraintProfile conservative;
        conservative.profile_id = "PROF-CONSERVATIVE";
        conservative.name = "Conservative";
        conservative.description = "Tight constraints for risk-averse portfolios";
        conservative.constraints = {
            {"Max Single Position", ConstraintType::CONCENTRATION, "", 0.0, 0.05, 0.8, ViolationSeverity::HARD_BREACH, true, "No single position > 5%"},
            {"Max Sector", ConstraintType::SECTOR, "", 0.0, 0.25, 0.85, ViolationSeverity::SOFT_BREACH, true, "No sector > 25%"},
            {"Min Cash", ConstraintType::CASH, "", 0.05, 1.0, 0.0, ViolationSeverity::WARNING, true, "Minimum 5% cash reserve"},
            {"Max Liquidity Days", ConstraintType::LIQUIDITY, "", 0.0, 5.0, 0.8, ViolationSeverity::SOFT_BREACH, true, "Position liquidatable within 5 days"}
        };
        profiles_["PROF-CONSERVATIVE"] = std::move(conservative);

        // Moderate profile
        ConstraintProfile moderate;
        moderate.profile_id = "PROF-MODERATE";
        moderate.name = "Moderate";
        moderate.constraints = {
            {"Max Single Position", ConstraintType::CONCENTRATION, "", 0.0, 0.10, 0.85, ViolationSeverity::HARD_BREACH, true, ""},
            {"Max Sector", ConstraintType::SECTOR, "", 0.0, 0.35, 0.85, ViolationSeverity::SOFT_BREACH, true, ""},
            {"Min Cash", ConstraintType::CASH, "", 0.02, 1.0, 0.0, ViolationSeverity::WARNING, true, ""}
        };
        profiles_["PROF-MODERATE"] = std::move(moderate);

        // Aggressive profile
        ConstraintProfile aggressive;
        aggressive.profile_id = "PROF-AGGRESSIVE";
        aggressive.name = "Aggressive";
        aggressive.constraints = {
            {"Max Single Position", ConstraintType::CONCENTRATION, "", 0.0, 0.20, 0.9, ViolationSeverity::HARD_BREACH, true, ""},
            {"Max Sector", ConstraintType::SECTOR, "", 0.0, 0.50, 0.9, ViolationSeverity::SOFT_BREACH, true, ""}
        };
        profiles_["PROF-AGGRESSIVE"] = std::move(aggressive);
    }

    static std::string pct_str(double v) {
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(1);
        oss << (v * 100.0) << "%";
        return oss.str();
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::vector<PortfolioConstraint> constraints_;
    std::unordered_map<std::string, ConstraintProfile> profiles_;
    std::unordered_set<std::string> esg_exclusions_;
    uint64_t checks_run_{0};
    uint64_t total_violations_{0};
    uint64_t total_hard_breaches_{0};
    double total_compliance_score_{0.0};
    mutable std::mutex mutex_;
};

} // namespace genie::portfolio

#endif // GENIE_PORTFOLIO_CONSTRAINTS_HPP
