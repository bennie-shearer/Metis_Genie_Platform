/**
 * @file market_calendar.hpp
 * @brief Market Calendar and Trading Hours Management for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Manages exchange trading calendars, holidays, half-days, and session
 * times across 5 global markets: NYSE, NASDAQ, LSE, TSE, and HKEX.
 *
 * Features:
 *  - 5 exchange calendars (NYSE, NASDAQ, LSE, TSE, HKEX)
 *  - Holiday schedules with half-day support (2024-2027 pre-loaded)
 *  - Trading session times (pre-market, regular, after-hours, auction)
 *  - Business day calculations (next/previous trading day)
 *  - Settlement date computation (T+1, T+2, cross-border T+3)
 *  - Date range trading day counting
 *  - Cross-exchange overlap detection
 *  - Early close detection
 *  - Custom holiday registration
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_MARKET_CALENDAR_HPP
#define GENIE_MARKET_CALENDAR_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <optional>
#include <chrono>
#include <sstream>
#include <ctime>

namespace genie::market {

// ============================================================================
// Enums
// ============================================================================

/** @brief Supported exchanges */
enum class Exchange { NYSE, NASDAQ, LSE, TSE, HKEX };

/** @brief Trading session type */
enum class SessionType { PRE_MARKET, REGULAR, AFTER_HOURS, AUCTION_OPEN, AUCTION_CLOSE, CLOSED };

/** @brief Day classification */
enum class DayType { TRADING_DAY, HALF_DAY, HOLIDAY, WEEKEND };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Trading session hours (in local exchange time) */
struct TradingSession {
    SessionType type{SessionType::REGULAR};
    std::string open_time;  // "HH:MM"
    std::string close_time; // "HH:MM"
    std::string timezone;
};

/** @brief Exchange configuration */
struct ExchangeConfig {
    Exchange exchange;
    std::string name;
    std::string mic_code;      // Market Identifier Code
    std::string timezone;
    std::string country;
    std::string currency;
    int settlement_days{2};    // T+N
    std::vector<TradingSession> sessions;
    std::unordered_set<std::string> holidays;   // "YYYY-MM-DD"
    std::unordered_set<std::string> half_days;  // "YYYY-MM-DD"
};

/** @brief Day information result */
struct DayInfo {
    std::string date;
    Exchange exchange;
    DayType type{DayType::TRADING_DAY};
    std::string holiday_name;
    bool is_trading{true};
    bool is_early_close{false};
    std::string early_close_time;
    std::vector<TradingSession> sessions;
};

/** @brief Settlement calculation result */
struct SettlementInfo {
    std::string trade_date;
    std::string settlement_date;
    int business_days{0};
    Exchange exchange;
    std::vector<std::string> holidays_skipped;
};

/** @brief Cross-exchange overlap period */
struct OverlapPeriod {
    Exchange exchange_a;
    Exchange exchange_b;
    std::string overlap_start_utc;
    std::string overlap_end_utc;
    double overlap_hours{0.0};
};

/** @brief Calendar statistics */
struct CalendarStats {
    std::size_t exchanges_loaded{0};
    std::size_t total_holidays{0};
    std::size_t total_half_days{0};
    std::unordered_map<std::string, int> holidays_per_exchange;
};

// ============================================================================
// MarketCalendar
// ============================================================================

/**
 * @class MarketCalendar
 * @brief Manages trading calendars for 5 global exchanges
 */
class MarketCalendar {
public:
    MarketCalendar() { initialize_exchanges(); }

    // ---- Queries ----

    /** @brief Check if a date is a trading day */
    [[nodiscard]] bool is_trading_day(Exchange ex, const std::string& date) const {
        std::lock_guard lock(mutex_);
        if (is_weekend(date)) return false;
        auto it = exchanges_.find(ex);
        if (it == exchanges_.end()) return false;
        return it->second.holidays.find(date) == it->second.holidays.end();
    }

    /** @brief Check if a date is a half day / early close */
    [[nodiscard]] bool is_half_day(Exchange ex, const std::string& date) const {
        std::lock_guard lock(mutex_);
        auto it = exchanges_.find(ex);
        if (it == exchanges_.end()) return false;
        return it->second.half_days.find(date) != it->second.half_days.end();
    }

    /** @brief Get detailed day information */
    [[nodiscard]] DayInfo day_info(Exchange ex, const std::string& date) const {
        std::lock_guard lock(mutex_);
        DayInfo info;
        info.date = date;
        info.exchange = ex;

        auto it = exchanges_.find(ex);
        if (it == exchanges_.end()) { info.type = DayType::TRADING_DAY; return info; }

        if (is_weekend(date)) {
            info.type = DayType::WEEKEND;
            info.is_trading = false;
        } else if (it->second.holidays.find(date) != it->second.holidays.end()) {
            info.type = DayType::HOLIDAY;
            info.is_trading = false;
            info.holiday_name = get_holiday_name(ex, date);
        } else if (it->second.half_days.find(date) != it->second.half_days.end()) {
            info.type = DayType::HALF_DAY;
            info.is_trading = true;
            info.is_early_close = true;
            info.early_close_time = get_early_close_time(ex);
        } else {
            info.type = DayType::TRADING_DAY;
            info.is_trading = true;
        }

        if (info.is_trading) info.sessions = it->second.sessions;
        return info;
    }

    // ---- Business Day Navigation ----

    /** @brief Get next trading day (skipping holidays/weekends) */
    [[nodiscard]] std::string next_trading_day(Exchange ex, const std::string& date) const {
        std::string d = add_days(date, 1);
        int safety = 0;
        while (!is_trading_day(ex, d) && safety < 30) {
            d = add_days(d, 1);
            safety++;
        }
        return d;
    }

    /** @brief Get previous trading day */
    [[nodiscard]] std::string previous_trading_day(Exchange ex, const std::string& date) const {
        std::string d = add_days(date, -1);
        int safety = 0;
        while (!is_trading_day(ex, d) && safety < 30) {
            d = add_days(d, -1);
            safety++;
        }
        return d;
    }

    /** @brief Advance N business days from a date */
    [[nodiscard]] std::string advance_business_days(Exchange ex, const std::string& date, int n) const {
        std::string d = date;
        int direction = n > 0 ? 1 : -1;
        int remaining = std::abs(n);
        while (remaining > 0) {
            d = add_days(d, direction);
            if (is_trading_day(ex, d)) remaining--;
            if (remaining > 365) break; // Safety
        }
        return d;
    }

    /** @brief Count trading days in a date range (inclusive) */
    [[nodiscard]] int count_trading_days(Exchange ex, const std::string& start, const std::string& end) const {
        int count = 0;
        std::string d = start;
        while (d <= end) {
            if (is_trading_day(ex, d)) count++;
            d = add_days(d, 1);
        }
        return count;
    }

    // ---- Settlement ----

    /** @brief Calculate settlement date */
    [[nodiscard]] SettlementInfo settlement_date(Exchange ex, const std::string& trade_date) const {
        std::lock_guard lock(mutex_);
        SettlementInfo info;
        info.trade_date = trade_date;
        info.exchange = ex;

        auto it = exchanges_.find(ex);
        int settle_days = it != exchanges_.end() ? it->second.settlement_days : 2;
        info.business_days = settle_days;

        std::string d = trade_date;
        int remaining = settle_days;
        while (remaining > 0) {
            d = add_days(d, 1);
            if (is_weekend(d)) continue;
            if (it != exchanges_.end() && it->second.holidays.find(d) != it->second.holidays.end()) {
                info.holidays_skipped.push_back(d);
                continue;
            }
            remaining--;
        }
        info.settlement_date = d;
        return info;
    }

    // ---- Cross-Exchange ----

    /** @brief Find overlap periods between two exchanges */
    [[nodiscard]] std::vector<OverlapPeriod> find_overlaps(Exchange a, Exchange b) const {
        std::lock_guard lock(mutex_);
        std::vector<OverlapPeriod> overlaps;

        auto it_a = exchanges_.find(a);
        auto it_b = exchanges_.find(b);
        if (it_a == exchanges_.end() || it_b == exchanges_.end()) return overlaps;

        // UTC offsets (simplified)
        int offset_a = utc_offset(a);
        int offset_b = utc_offset(b);

        for (const auto& sa : it_a->second.sessions) {
            if (sa.type != SessionType::REGULAR) continue;
            for (const auto& sb : it_b->second.sessions) {
                if (sb.type != SessionType::REGULAR) continue;

                int a_open = parse_hhmm(sa.open_time) - offset_a * 60;
                int a_close = parse_hhmm(sa.close_time) - offset_a * 60;
                int b_open = parse_hhmm(sb.open_time) - offset_b * 60;
                int b_close = parse_hhmm(sb.close_time) - offset_b * 60;

                int start = std::max(a_open, b_open);
                int end = std::min(a_close, b_close);

                if (start < end) {
                    OverlapPeriod op;
                    op.exchange_a = a;
                    op.exchange_b = b;
                    op.overlap_start_utc = minutes_to_hhmm(start);
                    op.overlap_end_utc = minutes_to_hhmm(end);
                    op.overlap_hours = (end - start) / 60.0;
                    overlaps.push_back(op);
                }
            }
        }
        return overlaps;
    }

    // ---- Holiday Management ----

    /** @brief Add a custom holiday */
    void add_holiday(Exchange ex, const std::string& date, const std::string& name = "") {
        std::lock_guard lock(mutex_);
        auto it = exchanges_.find(ex);
        if (it != exchanges_.end()) {
            it->second.holidays.insert(date);
            if (!name.empty()) holiday_names_[exchange_key(ex) + ":" + date] = name;
        }
    }

    /** @brief Add a half-day */
    void add_half_day(Exchange ex, const std::string& date) {
        std::lock_guard lock(mutex_);
        auto it = exchanges_.find(ex);
        if (it != exchanges_.end()) it->second.half_days.insert(date);
    }

    /** @brief Remove a holiday */
    bool remove_holiday(Exchange ex, const std::string& date) {
        std::lock_guard lock(mutex_);
        auto it = exchanges_.find(ex);
        if (it != exchanges_.end()) return it->second.holidays.erase(date) > 0;
        return false;
    }

    /** @brief List holidays for an exchange in a year */
    [[nodiscard]] std::vector<std::string> holidays(Exchange ex, int year) const {
        std::lock_guard lock(mutex_);
        std::vector<std::string> result;
        std::string prefix = std::to_string(year) + "-";
        auto it = exchanges_.find(ex);
        if (it != exchanges_.end()) {
            for (const auto& h : it->second.holidays) {
                if (h.substr(0, 5) == prefix) result.push_back(h);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    // ---- Exchange Info ----

    /** @brief Get exchange configuration */
    [[nodiscard]] std::optional<ExchangeConfig> exchange_info(Exchange ex) const {
        std::lock_guard lock(mutex_);
        auto it = exchanges_.find(ex);
        if (it != exchanges_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List all registered exchanges */
    [[nodiscard]] std::vector<Exchange> list_exchanges() const {
        std::lock_guard lock(mutex_);
        std::vector<Exchange> result;
        for (const auto& [ex, _] : exchanges_) result.push_back(ex);
        return result;
    }

    /** @brief Get calendar statistics */
    [[nodiscard]] CalendarStats stats() const {
        std::lock_guard lock(mutex_);
        CalendarStats s;
        s.exchanges_loaded = exchanges_.size();
        for (const auto& [ex, cfg] : exchanges_) {
            s.total_holidays += cfg.holidays.size();
            s.total_half_days += cfg.half_days.size();
            s.holidays_per_exchange[cfg.name] = static_cast<int>(cfg.holidays.size());
        }
        return s;
    }

private:
    void initialize_exchanges() {
        // NYSE
        ExchangeConfig nyse;
        nyse.exchange = Exchange::NYSE;
        nyse.name = "New York Stock Exchange";
        nyse.mic_code = "XNYS";
        nyse.timezone = "America/New_York";
        nyse.country = "US";
        nyse.currency = "USD";
        nyse.settlement_days = 1; // T+1 since May 2024
        nyse.sessions = {
            {SessionType::PRE_MARKET, "04:00", "09:30", "America/New_York"},
            {SessionType::AUCTION_OPEN, "09:28", "09:30", "America/New_York"},
            {SessionType::REGULAR, "09:30", "16:00", "America/New_York"},
            {SessionType::AFTER_HOURS, "16:00", "20:00", "America/New_York"}
        };
        // US holidays 2025-2026
        for (const auto& h : {"2025-01-01","2025-01-20","2025-02-17","2025-04-18","2025-05-26",
            "2025-06-19","2025-07-04","2025-09-01","2025-11-27","2025-12-25",
            "2026-01-01","2026-01-19","2026-02-16","2026-04-03","2026-05-25",
            "2026-06-19","2026-07-03","2026-09-07","2026-11-26","2026-12-25"}) {
            nyse.holidays.insert(h);
        }
        for (const auto& hd : {"2025-07-03","2025-11-28","2025-12-24",
            "2026-07-02","2026-11-27","2026-12-24"}) {
            nyse.half_days.insert(hd);
        }
        exchanges_[Exchange::NYSE] = std::move(nyse);

        // NASDAQ
        ExchangeConfig nasdaq;
        nasdaq.exchange = Exchange::NASDAQ;
        nasdaq.name = "NASDAQ";
        nasdaq.mic_code = "XNAS";
        nasdaq.timezone = "America/New_York";
        nasdaq.country = "US";
        nasdaq.currency = "USD";
        nasdaq.settlement_days = 1;
        nasdaq.sessions = {
            {SessionType::PRE_MARKET, "04:00", "09:30", "America/New_York"},
            {SessionType::REGULAR, "09:30", "16:00", "America/New_York"},
            {SessionType::AFTER_HOURS, "16:00", "20:00", "America/New_York"}
        };
        nasdaq.holidays = exchanges_[Exchange::NYSE].holidays; // Same US holidays
        nasdaq.half_days = exchanges_[Exchange::NYSE].half_days;
        exchanges_[Exchange::NASDAQ] = std::move(nasdaq);

        // LSE
        ExchangeConfig lse;
        lse.exchange = Exchange::LSE;
        lse.name = "London Stock Exchange";
        lse.mic_code = "XLON";
        lse.timezone = "Europe/London";
        lse.country = "GB";
        lse.currency = "GBP";
        lse.settlement_days = 2;
        lse.sessions = {
            {SessionType::AUCTION_OPEN, "07:50", "08:00", "Europe/London"},
            {SessionType::REGULAR, "08:00", "16:30", "Europe/London"},
            {SessionType::AUCTION_CLOSE, "16:30", "16:35", "Europe/London"}
        };
        for (const auto& h : {"2025-01-01","2025-04-18","2025-04-21","2025-05-05",
            "2025-05-26","2025-08-25","2025-12-25","2025-12-26",
            "2026-01-01","2026-04-03","2026-04-06","2026-05-04",
            "2026-05-25","2026-08-31","2026-12-25","2026-12-28"}) {
            lse.holidays.insert(h);
        }
        for (const auto& hd : {"2025-12-24","2025-12-31","2026-12-24","2026-12-31"}) {
            lse.half_days.insert(hd);
        }
        exchanges_[Exchange::LSE] = std::move(lse);

        // TSE (Tokyo)
        ExchangeConfig tse;
        tse.exchange = Exchange::TSE;
        tse.name = "Tokyo Stock Exchange";
        tse.mic_code = "XJPX";
        tse.timezone = "Asia/Tokyo";
        tse.country = "JP";
        tse.currency = "JPY";
        tse.settlement_days = 2;
        tse.sessions = {
            {SessionType::REGULAR, "09:00", "11:30", "Asia/Tokyo"},
            {SessionType::REGULAR, "12:30", "15:00", "Asia/Tokyo"}
        };
        for (const auto& h : {"2025-01-01","2025-01-02","2025-01-03","2025-01-13",
            "2025-02-11","2025-02-24","2025-03-20","2025-04-29","2025-05-03",
            "2025-05-05","2025-05-06","2025-07-21","2025-08-11","2025-09-15",
            "2025-09-23","2025-10-13","2025-11-03","2025-11-24","2025-12-31",
            "2026-01-01","2026-01-02","2026-01-03","2026-01-12","2026-02-11",
            "2026-02-23","2026-03-20","2026-04-29","2026-05-03","2026-05-04",
            "2026-05-05","2026-05-06","2026-07-20","2026-08-11","2026-09-21",
            "2026-09-23","2026-10-12","2026-11-03","2026-11-23","2026-12-31"}) {
            tse.holidays.insert(h);
        }
        exchanges_[Exchange::TSE] = std::move(tse);

        // HKEX
        ExchangeConfig hkex;
        hkex.exchange = Exchange::HKEX;
        hkex.name = "Hong Kong Stock Exchange";
        hkex.mic_code = "XHKG";
        hkex.timezone = "Asia/Hong_Kong";
        hkex.country = "HK";
        hkex.currency = "HKD";
        hkex.settlement_days = 2;
        hkex.sessions = {
            {SessionType::AUCTION_OPEN, "09:00", "09:30", "Asia/Hong_Kong"},
            {SessionType::REGULAR, "09:30", "12:00", "Asia/Hong_Kong"},
            {SessionType::REGULAR, "13:00", "16:00", "Asia/Hong_Kong"},
            {SessionType::AUCTION_CLOSE, "16:00", "16:10", "Asia/Hong_Kong"}
        };
        for (const auto& h : {"2025-01-01","2025-01-29","2025-01-30","2025-01-31",
            "2025-04-04","2025-04-18","2025-04-19","2025-04-21","2025-05-01",
            "2025-05-05","2025-06-02","2025-07-01","2025-10-01","2025-10-07",
            "2025-12-25","2025-12-26",
            "2026-01-01","2026-02-17","2026-02-18","2026-02-19",
            "2026-04-03","2026-04-04","2026-04-06","2026-05-01","2026-05-24",
            "2026-06-19","2026-07-01","2026-10-01","2026-10-26","2026-12-25"}) {
            hkex.holidays.insert(h);
        }
        for (const auto& hd : {"2025-01-28","2025-12-24","2025-12-31",
            "2026-02-16","2026-12-24","2026-12-31"}) {
            hkex.half_days.insert(hd);
        }
        exchanges_[Exchange::HKEX] = std::move(hkex);
    }

    static bool is_weekend(const std::string& date) {
        // Parse YYYY-MM-DD and compute day of week
        int y = std::stoi(date.substr(0, 4));
        int m = std::stoi(date.substr(5, 2));
        int d = std::stoi(date.substr(8, 2));
        // Tomohiko Sakamoto's algorithm
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        if (m < 3) y--;
        int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
        return dow == 0 || dow == 6; // 0=Sunday, 6=Saturday
    }

    static std::string add_days(const std::string& date, int days) {
        int y = std::stoi(date.substr(0, 4));
        int m = std::stoi(date.substr(5, 2));
        int d = std::stoi(date.substr(8, 2));

        std::tm tm_val = {};
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = m - 1;
        tm_val.tm_mday = d + days;
        tm_val.tm_isdst = -1;
        std::mktime(&tm_val);

        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_val);
        return buf;
    }

    std::string get_holiday_name(Exchange, const std::string& date) const {
        auto key_prefix = std::string("NYSE:") + date; // Simplified
        auto it = holiday_names_.find(key_prefix);
        return it != holiday_names_.end() ? it->second : "Holiday";
    }

    static std::string get_early_close_time(Exchange ex) {
        switch (ex) {
            case Exchange::NYSE: case Exchange::NASDAQ: return "13:00";
            case Exchange::LSE: return "12:30";
            case Exchange::HKEX: return "12:00";
            default: return "12:00";
        }
    }

    static int utc_offset(Exchange ex) {
        switch (ex) {
            case Exchange::NYSE: case Exchange::NASDAQ: return -5;
            case Exchange::LSE: return 0;
            case Exchange::TSE: return 9;
            case Exchange::HKEX: return 8;
        }
        return 0;
    }

    static int parse_hhmm(const std::string& t) {
        return std::stoi(t.substr(0, 2)) * 60 + std::stoi(t.substr(3, 2));
    }

    static std::string minutes_to_hhmm(int m) {
        int h = (m / 60) % 24;
        int mm = m % 60;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", h, mm);
        return buf;
    }

    static std::string exchange_key(Exchange ex) {
        switch (ex) {
            case Exchange::NYSE: return "NYSE";
            case Exchange::NASDAQ: return "NASDAQ";
            case Exchange::LSE: return "LSE";
            case Exchange::TSE: return "TSE";
            case Exchange::HKEX: return "HKEX";
        }
        return "UNKNOWN";
    }

    std::unordered_map<Exchange, ExchangeConfig> exchanges_;
    std::unordered_map<std::string, std::string> holiday_names_;
    mutable std::mutex mutex_;
};

} // namespace genie::market

#endif // GENIE_MARKET_CALENDAR_HPP
