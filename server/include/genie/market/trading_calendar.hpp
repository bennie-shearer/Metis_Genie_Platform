/**
 * @file trading_calendar.hpp
 * @brief NYSE/NASDAQ trading calendar with US market holidays
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides:
 *   - US market holiday detection (2020-2030, extensible)
 *   - Weekend detection
 *   - Trading day validation
 *   - Half-day (early close) detection
 *   - Trading day counting between dates
 *   - Next/previous trading day lookup
 *   - Expected trading days per year
 *
 * Holidays tracked:
 *   - New Year's Day (Jan 1, or observed)
 *   - Martin Luther King Jr. Day (3rd Monday in Jan)
 *   - Presidents' Day (3rd Monday in Feb)
 *   - Good Friday (varies)
 *   - Memorial Day (last Monday in May)
 *   - Juneteenth (Jun 19, or observed) - since 2022
 *   - Independence Day (Jul 4, or observed)
 *   - Labor Day (1st Monday in Sep)
 *   - Thanksgiving (4th Thursday in Nov)
 *   - Christmas Day (Dec 25, or observed)
 *
 * Early close days (1:00 PM ET):
 *   - Day before Independence Day (if weekday)
 *   - Day after Thanksgiving (Black Friday)
 *   - Christmas Eve (if weekday)
 */
#pragma once
#ifndef GENIE_MARKET_TRADING_CALENDAR_HPP
#define GENIE_MARKET_TRADING_CALENDAR_HPP

#include <string>
#include <set>
#include <vector>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief Date utility (YYYY-MM-DD string based)
 */
struct CalendarDate {
    int year{0};
    int month{0};  // 1-12
    int day{0};    // 1-31

    CalendarDate() = default;
    CalendarDate(int y, int m, int d) : year(y), month(m), day(d) {}

    static CalendarDate from_string(const std::string& s) {
        // "YYYY-MM-DD"
        CalendarDate d;
        if (s.size() >= 10) {
            try {
                d.year = std::stoi(s.substr(0, 4));
                d.month = std::stoi(s.substr(5, 2));
                d.day = std::stoi(s.substr(8, 2));
            } catch (...) {}
        }
        return d;
    }

    std::string to_string() const {
        std::ostringstream ss;
        ss << std::setfill('0') << std::setw(4) << year << "-"
           << std::setw(2) << month << "-" << std::setw(2) << day;
        return ss.str();
    }

    bool operator<(const CalendarDate& o) const {
        if (year != o.year) return year < o.year;
        if (month != o.month) return month < o.month;
        return day < o.day;
    }

    bool operator==(const CalendarDate& o) const {
        return year == o.year && month == o.month && day == o.day;
    }

    bool operator<=(const CalendarDate& o) const { return *this < o || *this == o; }
    bool operator>(const CalendarDate& o) const { return !(*this <= o); }
    bool operator>=(const CalendarDate& o) const { return !(*this < o); }
    bool operator!=(const CalendarDate& o) const { return !(*this == o); }

    bool valid() const { return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }

    /**
     * @brief Get day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
     */
    int day_of_week() const {
        // Tomohiko Sakamoto's algorithm
        static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = year;
        if (month < 3) y--;
        return (y + y/4 - y/100 + y/400 + t[month - 1] + day) % 7;
    }

    bool is_weekend() const {
        int dow = day_of_week();
        return dow == 0 || dow == 6; // Sunday or Saturday
    }

    /**
     * @brief Advance by one calendar day
     */
    CalendarDate next_day() const {
        int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        // Leap year
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
            days_in_month[1] = 29;
        }
        CalendarDate d = *this;
        d.day++;
        if (d.day > days_in_month[d.month - 1]) {
            d.day = 1;
            d.month++;
            if (d.month > 12) {
                d.month = 1;
                d.year++;
            }
        }
        return d;
    }

    /**
     * @brief Go back one calendar day
     */
    CalendarDate prev_day() const {
        int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        CalendarDate d = *this;
        // Check prev year for leap
        int check_year = (d.month == 1) ? d.year - 1 : d.year;
        if ((check_year % 4 == 0 && check_year % 100 != 0) || check_year % 400 == 0) {
            days_in_month[1] = 29;
        }
        d.day--;
        if (d.day < 1) {
            d.month--;
            if (d.month < 1) {
                d.month = 12;
                d.year--;
            }
            // Recheck leap year for the target year
            if ((d.year % 4 == 0 && d.year % 100 != 0) || d.year % 400 == 0) {
                days_in_month[1] = 29;
            } else {
                days_in_month[1] = 28;
            }
            d.day = days_in_month[d.month - 1];
        }
        return d;
    }
};

/**
 * @brief US Stock Market Trading Calendar
 */
class TradingCalendar {
public:
    TradingCalendar() {
        build_holiday_table();
    }

    /**
     * @brief Is the given date a trading day?
     */
    bool is_trading_day(const CalendarDate& date) const {
        if (date.is_weekend()) return false;
        return holidays_.find(date.to_string()) == holidays_.end();
    }

    bool is_trading_day(const std::string& date_str) const {
        return is_trading_day(CalendarDate::from_string(date_str));
    }

    /**
     * @brief Is the given date a market holiday?
     */
    bool is_holiday(const CalendarDate& date) const {
        return holidays_.find(date.to_string()) != holidays_.end();
    }

    bool is_holiday(const std::string& date_str) const {
        return is_holiday(CalendarDate::from_string(date_str));
    }

    /**
     * @brief Is the given date an early close day? (1:00 PM ET)
     */
    bool is_early_close(const CalendarDate& date) const {
        return early_close_.find(date.to_string()) != early_close_.end();
    }

    /**
     * @brief Get the next trading day on or after the given date
     */
    CalendarDate next_trading_day(CalendarDate date) const {
        date = date.next_day();
        while (!is_trading_day(date)) {
            date = date.next_day();
        }
        return date;
    }

    /**
     * @brief Get the previous trading day on or before the given date
     */
    CalendarDate prev_trading_day(CalendarDate date) const {
        date = date.prev_day();
        while (!is_trading_day(date)) {
            date = date.prev_day();
        }
        return date;
    }

    /**
     * @brief Count trading days between two dates (inclusive of both)
     */
    int trading_days_between(const CalendarDate& start, const CalendarDate& end) const {
        if (end < start) return 0;
        int count = 0;
        CalendarDate d = start;
        while (d <= end) {
            if (is_trading_day(d)) count++;
            d = d.next_day();
        }
        return count;
    }

    int trading_days_between(const std::string& start, const std::string& end) const {
        return trading_days_between(CalendarDate::from_string(start),
                                     CalendarDate::from_string(end));
    }

    /**
     * @brief Expected trading days in a given year
     */
    int expected_trading_days(int year) const {
        CalendarDate start(year, 1, 1);
        CalendarDate end(year, 12, 31);
        return trading_days_between(start, end);
    }

    /**
     * @brief Get all holidays for a year
     */
    std::vector<std::string> holidays_for_year(int year) const {
        std::vector<std::string> result;
        std::string prefix = std::to_string(year) + "-";
        for (const auto& h : holidays_) {
            if (h.substr(0, 5) == prefix) {
                result.push_back(h);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    /**
     * @brief Get holiday name for a date
     */
    std::string holiday_name(const std::string& date_str) const {
        auto it = holiday_names_.find(date_str);
        return (it != holiday_names_.end()) ? it->second : "";
    }

private:
    std::set<std::string> holidays_;
    std::set<std::string> early_close_;
    std::map<std::string, std::string> holiday_names_;

    void add_holiday(int y, int m, int d, const std::string& name) {
        CalendarDate date(y, m, d);
        // If holiday falls on Saturday, observe Friday
        // If holiday falls on Sunday, observe Monday
        if (date.day_of_week() == 6) {
            date = date.prev_day(); // Friday
        } else if (date.day_of_week() == 0) {
            date = date.next_day(); // Monday
        }
        std::string ds = date.to_string();
        holidays_.insert(ds);
        holiday_names_[ds] = name;
    }

    void add_holiday_exact(int y, int m, int d, const std::string& name) {
        CalendarDate date(y, m, d);
        if (!date.is_weekend()) {
            std::string ds = date.to_string();
            holidays_.insert(ds);
            holiday_names_[ds] = name;
        }
    }

    /**
     * @brief Find the nth occurrence of a weekday in a month
     * @param dow Day of week (0=Sun, 1=Mon, ..., 6=Sat)
     * @param n Which occurrence (1-5)
     */
    CalendarDate nth_weekday(int year, int month, int dow, int n) const {
        CalendarDate d(year, month, 1);
        int count = 0;
        while (d.month == month) {
            if (d.day_of_week() == dow) {
                count++;
                if (count == n) return d;
            }
            d = d.next_day();
        }
        return CalendarDate(); // shouldn't happen
    }

    /**
     * @brief Find the last occurrence of a weekday in a month
     */
    CalendarDate last_weekday(int year, int month, int dow) const {
        int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) days_in_month[1] = 29;
        CalendarDate d(year, month, days_in_month[month - 1]);
        while (d.day_of_week() != dow) {
            d = d.prev_day();
        }
        return d;
    }

    /**
     * @brief Easter date (Computus algorithm - Anonymous Gregorian)
     */
    CalendarDate easter(int year) const {
        int a = year % 19;
        int b = year / 100;
        int c = year % 100;
        int d = b / 4;
        int e = b % 4;
        int f = (b + 8) / 25;
        int g = (b - f + 1) / 3;
        int h = (19*a + b - d - g + 15) % 30;
        int i = c / 4;
        int k = c % 4;
        int l = (32 + 2*e + 2*i - h - k) % 7;
        int m = (a + 11*h + 22*l) / 451;
        int month = (h + l - 7*m + 114) / 31;
        int day = ((h + l - 7*m + 114) % 31) + 1;
        return CalendarDate(year, month, day);
    }

    /**
     * @brief Good Friday = Easter Sunday - 2 days
     */
    CalendarDate good_friday(int year) const {
        CalendarDate e = easter(year);
        return e.prev_day().prev_day();
    }

    void build_holiday_table() {
        // Build holidays for 2020-2030
        for (int y = 2020; y <= 2030; ++y) {
            // New Year's Day
            add_holiday(y, 1, 1, "New Year's Day");

            // Martin Luther King Jr. Day (3rd Monday in January)
            auto mlk = nth_weekday(y, 1, 1, 3);
            add_holiday_exact(y, mlk.month, mlk.day, "MLK Jr. Day");

            // Presidents' Day (3rd Monday in February)
            auto pres = nth_weekday(y, 2, 1, 3);
            add_holiday_exact(y, pres.month, pres.day, "Presidents' Day");

            // Good Friday
            auto gf = good_friday(y);
            add_holiday_exact(gf.year, gf.month, gf.day, "Good Friday");

            // Memorial Day (last Monday in May)
            auto mem = last_weekday(y, 5, 1);
            add_holiday_exact(y, mem.month, mem.day, "Memorial Day");

            // Juneteenth (since 2022)
            if (y >= 2022) {
                add_holiday(y, 6, 19, "Juneteenth");
            }

            // Independence Day
            add_holiday(y, 7, 4, "Independence Day");

            // Labor Day (1st Monday in September)
            auto labor = nth_weekday(y, 9, 1, 1);
            add_holiday_exact(y, labor.month, labor.day, "Labor Day");

            // Thanksgiving (4th Thursday in November)
            auto thanks = nth_weekday(y, 11, 4, 4);
            add_holiday_exact(y, thanks.month, thanks.day, "Thanksgiving");

            // Christmas Day
            add_holiday(y, 12, 25, "Christmas Day");

            // --- Early close days ---

            // Day before Independence Day (Jul 3 or nearest weekday)
            CalendarDate jul3(y, 7, 3);
            if (!jul3.is_weekend() && !is_holiday(jul3)) {
                early_close_.insert(jul3.to_string());
            }

            // Black Friday (day after Thanksgiving)
            CalendarDate bf = thanks.next_day();
            if (!bf.is_weekend()) {
                early_close_.insert(bf.to_string());
            }

            // Christmas Eve
            CalendarDate xmas_eve(y, 12, 24);
            if (!xmas_eve.is_weekend() && !is_holiday(xmas_eve)) {
                early_close_.insert(xmas_eve.to_string());
            }
        }

        // Special closures
        // (Add any ad-hoc closures like weather, national mourning, etc.)
    }
};

/**
 * @brief Global trading calendar instance
 */
inline const TradingCalendar& trading_calendar() {
    static TradingCalendar instance;
    return instance;
}

} // namespace genie::market

#endif // GENIE_MARKET_TRADING_CALENDAR_HPP
