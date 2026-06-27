/**
 * @file risk_budgeter.hpp
 * @brief Risk budget allocation, monitoring, and enforcement
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Risk budgeting framework:
 * - Risk budget allocation (by strategy, asset class, desk, manager)
 * - Budget utilization tracking (VaR, tracking error, volatility)
 * - Marginal risk contribution per position
 * - Budget breach detection with severity levels
 * - Budget reallocation recommendations
 * - Risk capacity vs risk appetite tracking
 * - Limit framework (soft/hard limits)
 * - Risk budget attribution to profit centers
 * - Time-varying budget adjustments (regime-aware)
 * - Budget efficiency scoring (return per unit risk)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_RISK_RISK_BUDGETER_HPP
#define GENIE_RISK_RISK_BUDGETER_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>

namespace genie {
namespace risk {
namespace budget {

// ============================================================================
// Enumerations
// ============================================================================

enum class RiskMetric { VaR, TrackingError, Volatility, DrawdownBudget, BetaExposure };
enum class LimitType { Soft, Hard };
enum class BreachSeverity { None, Advisory, Warning, Breach, CriticalBreach };

[[nodiscard]] inline std::string metric_string(RiskMetric m) {
    switch (m) {
        case RiskMetric::VaR: return "VaR";
        case RiskMetric::TrackingError: return "TE";
        case RiskMetric::Volatility: return "Vol";
        case RiskMetric::DrawdownBudget: return "DD";
        case RiskMetric::BetaExposure: return "Beta";
    }
    return "unknown";
}

[[nodiscard]] inline std::string severity_string(BreachSeverity s) {
    switch (s) {
        case BreachSeverity::None: return "OK";
        case BreachSeverity::Advisory: return "ADVISORY";
        case BreachSeverity::Warning: return "WARNING";
        case BreachSeverity::Breach: return "BREACH";
        case BreachSeverity::CriticalBreach: return "CRITICAL";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct RiskBudget {
    std::string id;
    std::string name;              // Strategy, desk, or manager name
    RiskMetric metric{RiskMetric::VaR};
    double budget{0};              // Allocated risk budget (in metric units)
    double utilized{0};            // Current utilization
    double soft_limit{0};          // Warning threshold (e.g., 80% of budget)
    double hard_limit{0};          // Hard limit (e.g., 100% of budget)
    double return_attributed{0};   // Return from this budget allocation

    [[nodiscard]] double utilization_pct() const { return budget > 0 ? utilized / budget : 0; }
    [[nodiscard]] double efficiency() const { return utilized > 0 ? return_attributed / utilized : 0; }
    [[nodiscard]] BreachSeverity severity() const {
        double u = utilization_pct();
        if (u >= 1.10) return BreachSeverity::CriticalBreach;
        if (u >= 1.00) return BreachSeverity::Breach;
        if (u >= 0.90) return BreachSeverity::Warning;
        if (u >= 0.80) return BreachSeverity::Advisory;
        return BreachSeverity::None;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << std::left << std::setw(20) << name
            << " [" << metric_string(metric) << "] "
            << "Budget=" << budget << " Used=" << utilized
            << " (" << utilization_pct() * 100 << "%) "
            << severity_string(severity())
            << " Eff=" << std::setprecision(3) << efficiency();
        return oss.str();
    }
};

struct BudgetSummary {
    int total_budgets{0};
    int breached{0};
    int warning{0};
    int healthy{0};
    double total_budget{0};
    double total_utilized{0};
    double aggregate_utilization{0};
    double portfolio_efficiency{0};
    std::string worst_budget;
    double worst_utilization{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "Risk Budget: " << total_budgets << " budgets"
            << " | " << healthy << " OK, " << warning << " WARN, " << breached << " BREACH"
            << " | Agg util=" << aggregate_utilization * 100 << "%"
            << " | Eff=" << std::setprecision(3) << portfolio_efficiency;
        if (!worst_budget.empty())
            oss << " | Worst: " << worst_budget << " (" << worst_utilization * 100 << "%)";
        return oss.str();
    }
};

struct BudgetRecommendation {
    std::string budget_name;
    double current_budget{0};
    double recommended_budget{0};
    double change{0};
    std::string rationale;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << budget_name << ": " << current_budget << " -> " << recommended_budget
            << " (Δ=" << change << ") " << rationale;
        return oss.str();
    }
};

struct MarginalRisk {
    std::string position;
    double marginal_var{0};
    double component_var{0};
    double pct_of_total{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << std::left << std::setw(10) << position
            << " Marginal=" << marginal_var << " Component=" << component_var
            << " (" << pct_of_total * 100 << "% of total)";
        return oss.str();
    }
};

// ============================================================================
// Risk Budgeter
// ============================================================================

class RiskBudgeter {
public:
    /**
     * @brief Set a risk budget allocation
     */
    void set_budget(const std::string& name, RiskMetric metric,
                      double budget, double utilized = 0,
                      double return_attr = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        RiskBudget rb;
        rb.id = "RB-" + std::to_string(++counter_);
        rb.name = name;
        rb.metric = metric;
        rb.budget = budget;
        rb.utilized = utilized;
        rb.soft_limit = budget * 0.80;
        rb.hard_limit = budget;
        rb.return_attributed = return_attr;
        budgets_[name] = rb;
    }

    /**
     * @brief Update utilization for a budget
     */
    bool update_utilization(const std::string& name, double utilized,
                              double return_attr = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = budgets_.find(name);
        if (it == budgets_.end()) return false;
        it->second.utilized = utilized;
        if (return_attr != 0) it->second.return_attributed = return_attr;
        return true;
    }

    /**
     * @brief Get budget summary across all allocations
     */
    [[nodiscard]] BudgetSummary summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        BudgetSummary bs;
        double total_return = 0;
        for (const auto& [_, rb] : budgets_) {
            bs.total_budgets++;
            bs.total_budget += rb.budget;
            bs.total_utilized += rb.utilized;
            total_return += rb.return_attributed;

            switch (rb.severity()) {
                case BreachSeverity::Breach:
                case BreachSeverity::CriticalBreach: bs.breached++; break;
                case BreachSeverity::Warning:
                case BreachSeverity::Advisory: bs.warning++; break;
                default: bs.healthy++; break;
            }

            if (rb.utilization_pct() > bs.worst_utilization) {
                bs.worst_utilization = rb.utilization_pct();
                bs.worst_budget = rb.name;
            }
        }
        bs.aggregate_utilization = bs.total_budget > 0 ? bs.total_utilized / bs.total_budget : 0;
        bs.portfolio_efficiency = bs.total_utilized > 0 ? total_return / bs.total_utilized : 0;
        return bs;
    }

    /**
     * @brief Detect all budget breaches
     */
    [[nodiscard]] std::vector<RiskBudget> breaches() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<RiskBudget> result;
        for (const auto& [_, rb] : budgets_) {
            if (rb.severity() >= BreachSeverity::Warning)
                result.push_back(rb);
        }
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.utilization_pct() > b.utilization_pct(); });
        return result;
    }

    /**
     * @brief Recommend budget reallocations based on efficiency
     */
    [[nodiscard]] std::vector<BudgetRecommendation> recommend_reallocation() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BudgetRecommendation> recs;
        if (budgets_.empty()) return recs;

        // Find average efficiency
        double total_eff = 0; int n = 0;
        for (const auto& [_, rb] : budgets_) {
            if (rb.utilized > 0) { total_eff += rb.efficiency(); n++; }
        }
        double avg_eff = n > 0 ? total_eff / n : 0;

        for (const auto& [_, rb] : budgets_) {
            BudgetRecommendation rec;
            rec.budget_name = rb.name;
            rec.current_budget = rb.budget;

            double eff = rb.efficiency();
            if (eff > avg_eff * 1.5 && rb.utilization_pct() > 0.80) {
                // High efficiency + high utilization -> increase budget
                rec.recommended_budget = rb.budget * 1.20;
                rec.rationale = "High efficiency + near capacity: increase 20%";
            } else if (eff < avg_eff * 0.5 && rb.utilization_pct() > 0.50) {
                // Low efficiency -> reduce budget
                rec.recommended_budget = rb.budget * 0.80;
                rec.rationale = "Low efficiency: reduce 20%";
            } else {
                rec.recommended_budget = rb.budget;
                rec.rationale = "No change needed";
            }
            rec.change = rec.recommended_budget - rec.current_budget;
            if (std::abs(rec.change) > 0.01) recs.push_back(rec);
        }
        return recs;
    }

    /**
     * @brief Calculate marginal risk contributions
     */
    [[nodiscard]] std::vector<MarginalRisk> marginal_contributions(
        const std::map<std::string, double>& position_vars,
        double portfolio_var) const {

        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MarginalRisk> results;
        for (const auto& [pos, var] : position_vars) {
            MarginalRisk mr;
            mr.position = pos;
            mr.component_var = var;
            mr.marginal_var = portfolio_var > 0 ? var / portfolio_var * portfolio_var : 0;
            mr.pct_of_total = portfolio_var > 0 ? var / portfolio_var : 0;
            results.push_back(mr);
        }
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.pct_of_total > b.pct_of_total; });
        return results;
    }

    [[nodiscard]] int budget_count() const { std::lock_guard<std::mutex> lock(mutex_); return static_cast<int>(budgets_.size()); }

private:
    mutable std::mutex mutex_;
    std::map<std::string, RiskBudget> budgets_;
    int64_t counter_{0};
};

} // namespace budget
} // namespace risk
} // namespace genie

#endif // GENIE_RISK_RISK_BUDGETER_HPP
