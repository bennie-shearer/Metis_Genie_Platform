/**
 * @file date_utils.hpp
 * @brief Date utilities for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CORE_DATE_UTILS_HPP
#define GENIE_CORE_DATE_UTILS_HPP
#include "types.hpp"
#include <ctime>

namespace genie::date_utils {
[[nodiscard]] inline TimePoint from_ymd(int year, unsigned month, unsigned day) {
    std::tm tm{}; tm.tm_year = year - 1900; tm.tm_mon = static_cast<int>(month) - 1; tm.tm_mday = static_cast<int>(day); tm.tm_hour = 12;
    #ifdef GENIE_PLATFORM_WINDOWS
        return std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
    #else
        return std::chrono::system_clock::from_time_t(timegm(&tm));
    #endif
}
[[nodiscard]] inline std::tuple<int, unsigned, unsigned> to_ymd(TimePoint tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp); std::tm tm;
    #ifdef GENIE_PLATFORM_WINDOWS
        gmtime_s(&tm, &t);
    #else
        gmtime_r(&t, &tm);
    #endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}
[[nodiscard]] inline std::string format_date(TimePoint tp, const std::string& fmt = "%Y-%m-%d") {
    std::time_t t = std::chrono::system_clock::to_time_t(tp); std::tm tm;
    #ifdef GENIE_PLATFORM_WINDOWS
        gmtime_s(&tm, &t);
    #else
        gmtime_r(&t, &tm);
    #endif
    std::ostringstream s; s << std::put_time(&tm, fmt.c_str()); return s.str();
}
[[nodiscard]] inline TimePoint today() { return std::chrono::system_clock::now(); }
[[nodiscard]] inline int day_of_week(TimePoint tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp); std::tm tm;
    #ifdef GENIE_PLATFORM_WINDOWS
        gmtime_s(&tm, &t);
    #else
        gmtime_r(&t, &tm);
    #endif
    return tm.tm_wday;
}
[[nodiscard]] inline bool is_weekend(TimePoint tp) { int d = day_of_week(tp); return d == 0 || d == 6; }
[[nodiscard]] inline int days_between(TimePoint s, TimePoint e) { return std::chrono::duration_cast<std::chrono::hours>(e - s).count() / 24; }
[[nodiscard]] inline double years_between(TimePoint s, TimePoint e) { return days_between(s, e) / 365.0; }

/**
 * @brief Parse date string in YYYY-MM-DD format
 */
[[nodiscard]] inline TimePoint parse_date(const std::string& date_str) {
    if (date_str.empty()) return TimePoint{};
    std::tm tm{};
    // Try YYYY-MM-DD format
    if (date_str.length() >= 10) {
        tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(date_str.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(date_str.substr(8, 2));
        tm.tm_hour = 12;
    }
    #ifdef GENIE_PLATFORM_WINDOWS
        return std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
    #else
        return std::chrono::system_clock::from_time_t(timegm(&tm));
    #endif
}

class BusinessCalendar {
    std::set<TimePoint> holidays_; std::string name_{"Default"};
public:
    BusinessCalendar() = default;
    explicit BusinessCalendar(std::string n) : name_(std::move(n)) {}
    void add_holiday(TimePoint d) { holidays_.insert(d); }
    void add_holiday(int y, unsigned m, unsigned d) { holidays_.insert(from_ymd(y, m, d)); }
    [[nodiscard]] bool is_holiday(TimePoint d) const { return holidays_.count(d) > 0; }
    [[nodiscard]] bool is_business_day(TimePoint d) const { return !is_weekend(d) && !is_holiday(d); }
    [[nodiscard]] TimePoint next_business_day(TimePoint d) const { d += std::chrono::hours(24); while (!is_business_day(d)) d += std::chrono::hours(24); return d; }
    [[nodiscard]] int business_days_between(TimePoint s, TimePoint e) const { int c = 0; while (s < e) { s += std::chrono::hours(24); if (is_business_day(s)) ++c; } return c; }
};
} // namespace genie::date_utils
#endif
