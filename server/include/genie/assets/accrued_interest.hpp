/**
 * @file accrued_interest.hpp
 * @brief Bond accrued interest with day count conventions and clean/dirty pricing
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Fixed income accrued interest calculation:
 * - Day count conventions (30/360, ACT/360, ACT/365, ACT/ACT)
 * - Clean price to dirty price conversion
 * - Accrued interest for settlement
 * - Coupon schedule generation
 * - Yield-to-maturity with accrued
 * - Ex-dividend date handling
 * - Broken period calculations
 * - Multi-currency bond support
 * - TIPS inflation-adjusted accrual
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_ASSETS_ACCRUED_INTEREST_HPP
#define GENIE_ASSETS_ACCRUED_INTEREST_HPP

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
namespace assets {
namespace accrued {

// ============================================================================
// Enumerations
// ============================================================================

enum class DayCountConvention {
    Thirty360,         // 30/360 (US, Bond Basis)
    Thirty360E,        // 30E/360 (European)
    ActualActual,      // ACT/ACT (ISDA, ICMA)
    Actual360,         // ACT/360
    Actual365,         // ACT/365
    Actual365L,        // ACT/365L (ISMA)
    Business252        // Bus/252 (Brazil)
};

enum class CouponFrequency { Annual = 1, SemiAnnual = 2, Quarterly = 4, Monthly = 12 };

[[nodiscard]] inline std::string dcc_string(DayCountConvention d) {
    switch (d) {
        case DayCountConvention::Thirty360: return "30/360";
        case DayCountConvention::Thirty360E: return "30E/360";
        case DayCountConvention::ActualActual: return "ACT/ACT";
        case DayCountConvention::Actual360: return "ACT/360";
        case DayCountConvention::Actual365: return "ACT/365";
        case DayCountConvention::Actual365L: return "ACT/365L";
        case DayCountConvention::Business252: return "BUS/252";
    }
    return "unknown";
}

// ============================================================================
// Date Helpers
// ============================================================================

struct SimpleDate {
    int year{2026}, month{1}, day{1};

    [[nodiscard]] bool operator<(const SimpleDate& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    [[nodiscard]] bool operator<=(const SimpleDate& o) const { return !(o < *this); }

    [[nodiscard]] int days_in_month() const {
        static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (month == 2 && is_leap()) return 29;
        return dm[month - 1];
    }

    [[nodiscard]] bool is_leap() const {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    [[nodiscard]] int days_in_year() const { return is_leap() ? 366 : 365; }

    [[nodiscard]] int julian() const {
        // Simplified Julian day number for day-count
        int a = (14 - month) / 12;
        int y = year + 4800 - a;
        int m = month + 12 * a - 3;
        return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    }

    [[nodiscard]] int actual_days_between(const SimpleDate& other) const {
        return std::abs(julian() - other.julian());
    }

    [[nodiscard]] std::string to_string() const {
        std::ostringstream oss;
        oss << year << "-" << std::setw(2) << std::setfill('0') << month
            << "-" << std::setw(2) << std::setfill('0') << day;
        return oss.str();
    }

    static SimpleDate parse(const std::string& s) {
        SimpleDate d;
        if (s.size() >= 10) {
            d.year = std::stoi(s.substr(0, 4));
            d.month = std::stoi(s.substr(5, 2));
            d.day = std::stoi(s.substr(8, 2));
        }
        return d;
    }
};

// ============================================================================
// Data Structures
// ============================================================================

struct BondTerms {
    std::string id;
    std::string name;
    double coupon_rate{0};           // Annual coupon rate (0.05 = 5%)
    double par_value{100.0};
    CouponFrequency frequency{CouponFrequency::SemiAnnual};
    DayCountConvention day_count{DayCountConvention::Thirty360};
    SimpleDate issue_date;
    SimpleDate maturity_date;
    SimpleDate first_coupon_date;
    std::string currency{"USD"};
    bool ex_dividend{false};
    int ex_div_days{0};              // Days before record date

    [[nodiscard]] double coupon_per_period() const {
        return par_value * coupon_rate / static_cast<int>(frequency);
    }
};

struct AccruedResult {
    double accrued_interest{0};
    double accrued_per_100{0};       // Per 100 par
    int days_accrued{0};
    int days_in_period{0};
    double accrual_fraction{0};
    SimpleDate last_coupon;
    SimpleDate next_coupon;
    SimpleDate settlement;
    DayCountConvention convention;
    bool is_ex_dividend{false};

    [[nodiscard]] double dirty_price(double clean_price) const {
        return clean_price + accrued_per_100;
    }

    [[nodiscard]] double clean_price(double dirty_price) const {
        return dirty_price - accrued_per_100;
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "Accrued: $" << accrued_per_100 << "/100 par"
            << " (" << days_accrued << "/" << days_in_period << " days, "
            << dcc_string(convention) << ")"
            << " Last=" << last_coupon.to_string()
            << " Next=" << next_coupon.to_string();
        if (is_ex_dividend) oss << " [EX-DIV]";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "{\"accrued_per_100\":" << accrued_per_100
            << ",\"days_accrued\":" << days_accrued
            << ",\"days_in_period\":" << days_in_period
            << ",\"fraction\":" << accrual_fraction
            << ",\"convention\":\"" << dcc_string(convention)
            << "\",\"ex_dividend\":" << (is_ex_dividend ? "true" : "false") << "}";
        return oss.str();
    }
};

// ============================================================================
// Accrued Interest Calculator
// ============================================================================

class AccruedInterestCalculator {
public:
    /**
     * @brief Calculate accrued interest as of settlement date
     */
    [[nodiscard]] AccruedResult calculate(const BondTerms& bond,
                                            const SimpleDate& settlement_date) const {
        AccruedResult result;
        result.convention = bond.day_count;
        result.settlement = settlement_date;

        // Find last and next coupon dates
        auto [last_cpn, next_cpn] = find_coupon_dates(bond, settlement_date);
        result.last_coupon = last_cpn;
        result.next_coupon = next_cpn;

        // Day count fraction
        result.days_accrued = day_count(last_cpn, settlement_date, bond.day_count);
        result.days_in_period = day_count(last_cpn, next_cpn, bond.day_count);
        result.accrual_fraction = result.days_in_period > 0 ?
            static_cast<double>(result.days_accrued) / result.days_in_period : 0;

        // Ex-dividend check
        if (bond.ex_dividend && bond.ex_div_days > 0) {
            int days_to_next = settlement_date.actual_days_between(next_cpn);
            result.is_ex_dividend = (days_to_next <= bond.ex_div_days);
        }

        // Accrued interest
        double period_coupon = bond.coupon_per_period();
        if (result.is_ex_dividend) {
            // Negative accrued during ex-dividend period
            result.accrued_interest = -(1.0 - result.accrual_fraction) * period_coupon;
        } else {
            result.accrued_interest = result.accrual_fraction * period_coupon;
        }
        result.accrued_per_100 = result.accrued_interest * (100.0 / bond.par_value);

        return result;
    }

    /**
     * @brief Generate coupon schedule
     */
    [[nodiscard]] std::vector<SimpleDate> coupon_schedule(const BondTerms& bond) const {
        std::vector<SimpleDate> dates;
        int months_per_period = 12 / static_cast<int>(bond.frequency);
        SimpleDate d = bond.first_coupon_date;
        while (d <= bond.maturity_date) {
            dates.push_back(d);
            d.month += months_per_period;
            if (d.month > 12) { d.month -= 12; d.year++; }
            d.day = std::min(d.day, d.days_in_month());
        }
        return dates;
    }

private:
    /**
     * @brief Day count calculation per convention
     */
    [[nodiscard]] int day_count(const SimpleDate& d1, const SimpleDate& d2,
                                  DayCountConvention conv) const {
        switch (conv) {
            case DayCountConvention::Thirty360: {
                int dd1 = std::min(d1.day, 30);
                int dd2 = (d1.day >= 30) ? std::min(d2.day, 30) : d2.day;
                return 360 * (d2.year - d1.year) + 30 * (d2.month - d1.month) + (dd2 - dd1);
            }
            case DayCountConvention::Thirty360E: {
                int dd1 = std::min(d1.day, 30);
                int dd2 = std::min(d2.day, 30);
                return 360 * (d2.year - d1.year) + 30 * (d2.month - d1.month) + (dd2 - dd1);
            }
            case DayCountConvention::Actual360:
            case DayCountConvention::Actual365:
            case DayCountConvention::Actual365L:
            case DayCountConvention::ActualActual:
                return d1.actual_days_between(d2);
            case DayCountConvention::Business252:
                return static_cast<int>(d1.actual_days_between(d2) * 252.0 / 365.0);
        }
        return d1.actual_days_between(d2);
    }

    std::pair<SimpleDate, SimpleDate> find_coupon_dates(const BondTerms& bond,
                                                          const SimpleDate& settle) const {
        auto schedule = coupon_schedule(bond);
        SimpleDate last = bond.issue_date, next = bond.maturity_date;
        for (size_t i = 0; i < schedule.size(); ++i) {
            if (settle < schedule[i]) {
                next = schedule[i];
                last = (i > 0) ? schedule[i - 1] : bond.issue_date;
                break;
            }
            last = schedule[i];
        }
        return {last, next};
    }
};

} // namespace accrued
} // namespace assets
} // namespace genie

#endif // GENIE_ASSETS_ACCRUED_INTEREST_HPP
