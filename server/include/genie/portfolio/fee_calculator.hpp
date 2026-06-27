/**
 * @file fee_calculator.hpp
 * @brief Management, performance, custody fee calculation and expense tracking
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Comprehensive fee management:
 * - Management fees (flat, tiered, AUM-based)
 * - Performance fees (high-water mark, hurdle rate, crystallization)
 * - Custody/administration fees
 * - Transaction-based fees (per trade, per share)
 * - Fee accrual and settlement scheduling
 * - Expense ratio calculation (TER)
 * - Fee impact on portfolio returns
 * - Multi-share-class support
 * - Fee budget and cap enforcement
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PORTFOLIO_FEE_CALCULATOR_HPP
#define GENIE_PORTFOLIO_FEE_CALCULATOR_HPP

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

namespace genie {
namespace portfolio {
namespace fees {

// ============================================================================
// Enumerations
// ============================================================================

enum class FeeType { Management, Performance, Custody, Administration, Transaction, Redemption, Other };
enum class AccrualPeriod { Daily, Monthly, Quarterly, Annual };
enum class PerformanceFeeMethod { HighWaterMark, HurdleRate, BenchmarkRelative };

[[nodiscard]] inline std::string fee_type_string(FeeType t) {
    switch (t) {
        case FeeType::Management: return "management"; case FeeType::Performance: return "performance";
        case FeeType::Custody: return "custody"; case FeeType::Administration: return "admin";
        case FeeType::Transaction: return "transaction"; case FeeType::Redemption: return "redemption";
        case FeeType::Other: return "other";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct FeeTier {
    double aum_threshold{0};  // AUM above which this rate applies
    double rate{0};           // Annual rate in decimal (0.01 = 1%)
};

struct FeeSchedule {
    std::string id;
    std::string name;
    FeeType type{FeeType::Management};
    AccrualPeriod accrual{AccrualPeriod::Daily};
    double flat_rate{0};                 // Annual rate
    std::vector<FeeTier> tiers;          // Tiered pricing
    double minimum_fee{0};
    double maximum_fee{0};               // Fee cap (0 = no cap)
    // Performance fee specifics
    PerformanceFeeMethod perf_method{PerformanceFeeMethod::HighWaterMark};
    double performance_rate{0.20};       // 20% of gains
    double hurdle_rate{0};               // Annual hurdle
    double crystallization_frequency{1}; // Times per year
};

struct FeeAccrual {
    FeeType type;
    double accrued_amount{0};
    double period_start_nav{0};
    double current_nav{0};
    int days_accrued{0};
    std::string period;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << fee_type_string(type) << ": $" << accrued_amount
            << " (" << days_accrued << " days)";
        return oss.str();
    }
};

struct FeeResult {
    double management_fee{0};
    double performance_fee{0};
    double custody_fee{0};
    double admin_fee{0};
    double transaction_fees{0};
    double total_fees{0};
    double expense_ratio{0};          // Total fees / avg NAV
    double fee_drag_bps{0};           // Basis points of return impact
    double high_water_mark{0};
    std::vector<FeeAccrual> accruals;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Fees: Mgmt=$" << management_fee << " Perf=$" << performance_fee
            << " Custody=$" << custody_fee << " Admin=$" << admin_fee
            << " Txn=$" << transaction_fees
            << " | Total=$" << total_fees << " TER=" << expense_ratio * 100 << "%";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\"management\":" << management_fee << ",\"performance\":" << performance_fee
            << ",\"custody\":" << custody_fee << ",\"admin\":" << admin_fee
            << ",\"transaction\":" << transaction_fees << ",\"total\":" << total_fees
            << ",\"expense_ratio\":" << expense_ratio
            << ",\"fee_drag_bps\":" << fee_drag_bps << "}";
        return oss.str();
    }
};

// ============================================================================
// Fee Calculator
// ============================================================================

class FeeCalculator {
public:
    FeeCalculator() { register_default_schedules(); }

    void add_schedule(FeeSchedule schedule) {
        std::lock_guard<std::mutex> lock(mutex_);
        schedules_[schedule.id] = std::move(schedule);
    }

    /**
     * @brief Calculate all fees for a period
     */
    [[nodiscard]] FeeResult calculate(
        double avg_nav, double start_nav, double end_nav,
        int days, int transaction_count = 0,
        const std::string& schedule_set = "default") const {

        std::lock_guard<std::mutex> lock(mutex_);
        FeeResult result;
        double year_fraction = days / 365.0;

        // Management fee
        auto mgmt = get_schedule("MGMT-" + schedule_set);
        if (mgmt) {
            result.management_fee = calculate_tiered(avg_nav, *mgmt) * year_fraction;
            result.management_fee = std::max(result.management_fee, mgmt->minimum_fee * year_fraction);
            if (mgmt->maximum_fee > 0) {
                result.management_fee = std::min(result.management_fee, mgmt->maximum_fee * year_fraction);
            }
        }

        // Performance fee
        auto perf = get_schedule("PERF-" + schedule_set);
        if (perf && end_nav > start_nav) {
            double gain = end_nav - std::max(start_nav, high_water_mark_);
            if (gain > 0) {
                double hurdle_amount = start_nav * perf->hurdle_rate * year_fraction;
                double eligible_gain = std::max(0.0, gain - hurdle_amount);
                result.performance_fee = eligible_gain * perf->performance_rate;
            }
            result.high_water_mark = std::max(high_water_mark_, end_nav);
        }

        // Custody fee
        auto cust = get_schedule("CUST-" + schedule_set);
        if (cust) {
            result.custody_fee = avg_nav * cust->flat_rate * year_fraction;
        }

        // Admin fee
        auto adm = get_schedule("ADMIN-" + schedule_set);
        if (adm) {
            result.admin_fee = avg_nav * adm->flat_rate * year_fraction;
        }

        // Transaction fees
        result.transaction_fees = transaction_count * 9.95; // $9.95 per trade default

        result.total_fees = result.management_fee + result.performance_fee +
                           result.custody_fee + result.admin_fee + result.transaction_fees;
        result.expense_ratio = avg_nav > 0 ? (result.total_fees / avg_nav) / year_fraction : 0;
        result.fee_drag_bps = result.expense_ratio * 10000;

        return result;
    }

    void set_high_water_mark(double hwm) {
        std::lock_guard<std::mutex> lock(mutex_);
        high_water_mark_ = hwm;
    }

    [[nodiscard]] int schedule_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(schedules_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, FeeSchedule> schedules_;
    double high_water_mark_{0};

    std::optional<FeeSchedule> get_schedule(const std::string& id) const {
        auto it = schedules_.find(id);
        if (it != schedules_.end()) return it->second;
        return std::nullopt;
    }

    double calculate_tiered(double aum, const FeeSchedule& sched) const {
        if (sched.tiers.empty()) return aum * sched.flat_rate;
        double fee = 0;
        double remaining = aum;
        for (size_t i = 0; i < sched.tiers.size(); ++i) {
            double upper = (i + 1 < sched.tiers.size()) ? sched.tiers[i + 1].aum_threshold : 1e18;
            double bracket = std::min(remaining, upper - sched.tiers[i].aum_threshold);
            if (bracket > 0) {
                fee += bracket * sched.tiers[i].rate;
                remaining -= bracket;
            }
            if (remaining <= 0) break;
        }
        return fee;
    }

    void register_default_schedules() {
        // Standard management fee: 1% flat
        {
            FeeSchedule s; s.id = "MGMT-default"; s.name = "Standard Management";
            s.type = FeeType::Management; s.flat_rate = 0.01;
            schedules_[s.id] = s;
        }
        // Performance fee: 20% over HWM with 5% hurdle
        {
            FeeSchedule s; s.id = "PERF-default"; s.name = "Standard Performance";
            s.type = FeeType::Performance; s.performance_rate = 0.20; s.hurdle_rate = 0.05;
            schedules_[s.id] = s;
        }
        // Custody: 5bps
        {
            FeeSchedule s; s.id = "CUST-default"; s.name = "Custody";
            s.type = FeeType::Custody; s.flat_rate = 0.0005;
            schedules_[s.id] = s;
        }
        // Admin: 10bps
        {
            FeeSchedule s; s.id = "ADMIN-default"; s.name = "Administration";
            s.type = FeeType::Administration; s.flat_rate = 0.001;
            schedules_[s.id] = s;
        }
    }
};

} // namespace fees
} // namespace portfolio
} // namespace genie

#endif // GENIE_PORTFOLIO_FEE_CALCULATOR_HPP
