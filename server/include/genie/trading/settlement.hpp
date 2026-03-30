/**
 * @file settlement.hpp
 * @brief Settlement and clearing engine for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * T+1/T+2 settlement, business day calendar, cash projection,
 * pending settlement tracking, and failed trade handling.
 */
#pragma once
#ifndef GENIE_TRADING_SETTLEMENT_HPP
#define GENIE_TRADING_SETTLEMENT_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace genie::trading {

/** Settlement status */
enum class SettleStatus { Pending, Matched, Settled, Failed, Cancelled };

inline std::string settle_status_name(SettleStatus s) {
    switch (s) {
        case SettleStatus::Pending:   return "Pending";
        case SettleStatus::Matched:   return "Matched";
        case SettleStatus::Settled:   return "Settled";
        case SettleStatus::Failed:    return "Failed";
        case SettleStatus::Cancelled: return "Cancelled";
        default: return "Unknown";
    }
}

/** Settlement cycle for different asset classes */
enum class SettleCycle { T0, T1, T2, T3 };

inline int settle_days(SettleCycle c) {
    switch (c) { case SettleCycle::T0: return 0; case SettleCycle::T1: return 1;
                 case SettleCycle::T2: return 2; case SettleCycle::T3: return 3; default: return 2; }
}

/** Business day calendar (US market default) */
class BusinessCalendar {
    std::set<std::string> holidays_;

public:
    BusinessCalendar() {
        // 2026 US market holidays
        holidays_ = {"2026-01-01", "2026-01-19", "2026-02-16", "2026-04-03",
                      "2026-05-25", "2026-06-19", "2026-07-03", "2026-09-07",
                      "2026-11-26", "2026-12-25"};
    }

    void add_holiday(const std::string& date) { holidays_.insert(date); }

    [[nodiscard]] bool is_business_day(const std::string& date) const {
        if (holidays_.count(date)) return false;
        // Parse YYYY-MM-DD to check weekday
        int y, m, d;
        if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
        // Zeller's formula for day of week
        if (m < 3) { m += 12; y--; }
        int dow = (d + 13*(m+1)/5 + y + y/4 - y/100 + y/400) % 7;
        // 0=Sat, 1=Sun
        return dow != 0 && dow != 1;
    }

    /** Add business days to a date string */
    [[nodiscard]] std::string add_business_days(const std::string& date, int days) const {
        int y, m, d;
        if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return date;

        std::tm tm{};
        tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
        auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));

        int added = 0;
        while (added < days) {
            tp += std::chrono::hours(24);
            auto t = std::chrono::system_clock::to_time_t(tp);
            std::tm* lt = std::localtime(&t);
            char buf[16];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
            if (is_business_day(buf)) added++;
        }

        auto t = std::chrono::system_clock::to_time_t(tp);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
        return buf;
    }
};

/** A settlement instruction */
struct SettleInstruction {
    std::string id;
    std::string trade_id;
    std::string portfolio_id;
    std::string security_id;
    std::string side;               // "Buy" or "Sell"
    double quantity{0};
    double amount{0};               // cash amount
    std::string trade_date;
    std::string settle_date;
    SettleStatus status{SettleStatus::Pending};
    std::string counterparty;
    std::string fail_reason;
    int retry_count{0};
};

/** Cash projection entry */
struct CashProjection {
    std::string date;
    double opening_balance{0};
    double settlements_in{0};       // sells settling
    double settlements_out{0};      // buys settling
    double net_change{0};
    double closing_balance{0};
    int pending_count{0};
};

/** Settlement engine */
class SettlementEngine {
    std::map<std::string, SettleInstruction> instructions_;
    BusinessCalendar calendar_;
    std::map<std::string, SettleCycle> asset_cycles_; // asset class -> cycle
    int instruction_counter_{0};

public:
    SettlementEngine() {
        // Default cycles (post-SEC T+1 rule for US equities)
        asset_cycles_["Equity"] = SettleCycle::T1;
        asset_cycles_["ETF"] = SettleCycle::T1;
        asset_cycles_["FixedIncome"] = SettleCycle::T1;
        asset_cycles_["MutualFund"] = SettleCycle::T1;
        asset_cycles_["Option"] = SettleCycle::T1;
        asset_cycles_["Future"] = SettleCycle::T0;
        asset_cycles_["FX"] = SettleCycle::T2;
        asset_cycles_["Commodity"] = SettleCycle::T2;
    }

    BusinessCalendar& calendar() { return calendar_; }

    void set_cycle(const std::string& asset_class, SettleCycle cycle) {
        asset_cycles_[asset_class] = cycle;
    }

    /** Create settlement instruction for a trade */
    std::string create(const std::string& trade_id, const std::string& portfolio_id,
                       const std::string& security_id, const std::string& side,
                       double quantity, double amount, const std::string& trade_date,
                       const std::string& asset_class = "Equity",
                       const std::string& counterparty = "DTC") {

        SettleInstruction si;
        si.id = "SI-" + std::to_string(++instruction_counter_);
        si.trade_id = trade_id;
        si.portfolio_id = portfolio_id;
        si.security_id = security_id;
        si.side = side;
        si.quantity = quantity;
        si.amount = amount;
        si.trade_date = trade_date;
        si.counterparty = counterparty;
        si.status = SettleStatus::Pending;

        // Calculate settle date
        auto it = asset_cycles_.find(asset_class);
        int days = (it != asset_cycles_.end()) ? settle_days(it->second) : 1;
        si.settle_date = calendar_.add_business_days(trade_date, days);

        instructions_[si.id] = si;
        return si.id;
    }

    /** Mark instruction as settled */
    bool settle(const std::string& id) {
        auto it = instructions_.find(id);
        if (it == instructions_.end()) return false;
        it->second.status = SettleStatus::Settled;
        return true;
    }

    /** Mark as failed with reason */
    bool fail(const std::string& id, const std::string& reason) {
        auto it = instructions_.find(id);
        if (it == instructions_.end()) return false;
        it->second.status = SettleStatus::Failed;
        it->second.fail_reason = reason;
        it->second.retry_count++;
        return true;
    }

    /** Auto-settle all instructions due on or before a date */
    std::vector<std::string> auto_settle(const std::string& as_of) {
        std::vector<std::string> settled;
        for (auto& [id, si] : instructions_) {
            if (si.status == SettleStatus::Pending && si.settle_date <= as_of) {
                si.status = SettleStatus::Settled;
                settled.push_back(id);
            }
        }
        return settled;
    }

    /** Get pending instructions for a portfolio */
    [[nodiscard]] std::vector<SettleInstruction> pending(const std::string& portfolio_id = "") const {
        std::vector<SettleInstruction> result;
        for (const auto& [id, si] : instructions_) {
            if (si.status == SettleStatus::Pending) {
                if (portfolio_id.empty() || si.portfolio_id == portfolio_id)
                    result.push_back(si);
            }
        }
        std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.settle_date < b.settle_date; });
        return result;
    }

    /** Get failed instructions */
    [[nodiscard]] std::vector<SettleInstruction> failed() const {
        std::vector<SettleInstruction> result;
        for (const auto& [id, si] : instructions_)
            if (si.status == SettleStatus::Failed) result.push_back(si);
        return result;
    }

    /** Project cash balances for a portfolio over date range */
    [[nodiscard]] std::vector<CashProjection> project_cash(
            const std::string& portfolio_id, double opening_balance,
            const std::string& from_date, const std::string& to_date) const {

        // Group by settle date
        std::map<std::string, std::pair<double, double>> daily; // date -> (in, out)
        for (const auto& [id, si] : instructions_) {
            if (si.portfolio_id != portfolio_id) continue;
            if (si.status != SettleStatus::Pending && si.status != SettleStatus::Matched) continue;
            if (si.settle_date < from_date || si.settle_date > to_date) continue;

            if (si.side == "Sell")
                daily[si.settle_date].first += si.amount;
            else
                daily[si.settle_date].second += si.amount;
        }

        std::vector<CashProjection> projections;
        double balance = opening_balance;

        // Iterate through each business day
        std::string current = from_date;
        while (current <= to_date) {
            if (calendar_.is_business_day(current)) {
                CashProjection cp;
                cp.date = current;
                cp.opening_balance = balance;
                auto it = daily.find(current);
                if (it != daily.end()) {
                    cp.settlements_in = it->second.first;
                    cp.settlements_out = it->second.second;
                }
                cp.net_change = cp.settlements_in - cp.settlements_out;
                balance += cp.net_change;
                cp.closing_balance = balance;
                projections.push_back(cp);
            }
            current = calendar_.add_business_days(current, 1);
        }
        return projections;
    }

    // Statistics
    [[nodiscard]] size_t total_count() const { return instructions_.size(); }
    [[nodiscard]] size_t pending_count() const {
        size_t c = 0;
        for (const auto& [k, v] : instructions_) if (v.status == SettleStatus::Pending) c++;
        return c;
    }
    [[nodiscard]] size_t settled_count() const {
        size_t c = 0;
        for (const auto& [k, v] : instructions_) if (v.status == SettleStatus::Settled) c++;
        return c;
    }
    [[nodiscard]] size_t failed_count() const {
        size_t c = 0;
        for (const auto& [k, v] : instructions_) if (v.status == SettleStatus::Failed) c++;
        return c;
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_SETTLEMENT_HPP
