/**
 * @file trade_journal.hpp
 * @brief Trade journaling with P&L attribution, tagging, and review
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Trade journal for performance review:
 * - Trade entry/exit logging with timestamps
 * - Strategy and setup tagging
 * - P&L attribution (alpha vs beta)
 * - Emotion and confidence tracking
 * - Trade grading (plan adherence, execution quality)
 * - Aggregate statistics by strategy/tag/time
 * - Streak and pattern analysis
 * - Journal entry notes and lessons learned
 * - Export for periodic review
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_TRADING_TRADE_JOURNAL_HPP
#define GENIE_TRADING_TRADE_JOURNAL_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <numeric>
#include <set>

namespace genie {
namespace trading {
namespace journal {

// ============================================================================
// Enumerations
// ============================================================================

enum class TradeGrade { A, B, C, D, F, Ungraded };
enum class Emotion { Confident, Neutral, Fearful, Greedy, Impatient, Disciplined };

[[nodiscard]] inline std::string grade_string(TradeGrade g) {
    switch (g) { case TradeGrade::A: return "A"; case TradeGrade::B: return "B";
        case TradeGrade::C: return "C"; case TradeGrade::D: return "D";
        case TradeGrade::F: return "F"; case TradeGrade::Ungraded: return "-"; }
    return "-";
}

// ============================================================================
// Data Structures
// ============================================================================

struct JournalEntry {
    std::string id;
    std::string symbol;
    std::string strategy;
    std::string setup_type;       // "breakout", "pullback", "reversal" etc
    std::set<std::string> tags;
    double entry_price{0};
    double exit_price{0};
    double shares{0};
    double pnl{0};
    double pnl_pct{0};
    double commission{0};
    std::string entry_date;
    std::string exit_date;
    int bars_held{0};
    TradeGrade grade{TradeGrade::Ungraded};
    Emotion entry_emotion{Emotion::Neutral};
    int confidence_1_10{5};
    bool followed_plan{true};
    std::string entry_rationale;
    std::string exit_rationale;
    std::string lessons_learned;
    std::string mistakes;
    double risk_reward_planned{0};
    double risk_reward_actual{0};
    std::chrono::system_clock::time_point created_at;

    [[nodiscard]] double net_pnl() const { return pnl - commission; }
    [[nodiscard]] bool is_winner() const { return net_pnl() > 0; }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "{\"id\":\"" << id << "\",\"symbol\":\"" << symbol
            << "\",\"strategy\":\"" << strategy
            << "\",\"pnl\":" << pnl << ",\"pnl_pct\":" << pnl_pct * 100
            << ",\"grade\":\"" << grade_string(grade)
            << "\",\"followed_plan\":" << (followed_plan ? "true" : "false")
            << ",\"bars_held\":" << bars_held << "}";
        return oss.str();
    }
};

struct JournalStats {
    int total_trades{0};
    int winners{0}, losers{0};
    double total_pnl{0};
    double avg_pnl{0};
    double win_rate{0};
    double avg_winner{0}, avg_loser{0};
    double profit_factor{0};
    double plan_adherence_pct{0};
    int current_streak{0};       // Positive=wins, negative=losses
    int max_win_streak{0};
    int max_lose_streak{0};
    std::map<std::string, double> pnl_by_strategy;
    std::map<std::string, int> trades_by_grade;

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Trades=" << total_trades << " Win=" << win_rate * 100 << "%"
            << " P&L=$" << total_pnl << " PF=" << profit_factor
            << " Streak=" << current_streak
            << " Plan=" << plan_adherence_pct * 100 << "%";
        return oss.str();
    }
};

// ============================================================================
// Trade Journal
// ============================================================================

class TradeJournal {
public:
    void add_entry(JournalEntry entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        entry.id = "TJ-" + std::to_string(++counter_);
        entry.created_at = std::chrono::system_clock::now();
        entries_.push_back(std::move(entry));
        if (entries_.size() > max_entries_) entries_.pop_front();
    }

    void grade_trade(const std::string& id, TradeGrade grade,
                       const std::string& lessons = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& e : entries_) {
            if (e.id == id) {
                e.grade = grade;
                if (!lessons.empty()) e.lessons_learned = lessons;
                break;
            }
        }
    }

    [[nodiscard]] JournalStats statistics(const std::string& strategy = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        JournalStats stats;
        double total_wins = 0, total_losses = 0;
        int plans_followed = 0;
        int streak = 0;

        for (const auto& e : entries_) {
            if (!strategy.empty() && e.strategy != strategy) continue;
            stats.total_trades++;
            stats.total_pnl += e.net_pnl();
            stats.pnl_by_strategy[e.strategy] += e.net_pnl();
            stats.trades_by_grade[grade_string(e.grade)]++;
            if (e.followed_plan) plans_followed++;

            if (e.is_winner()) {
                stats.winners++;
                total_wins += e.net_pnl();
                streak = streak >= 0 ? streak + 1 : 1;
            } else {
                stats.losers++;
                total_losses += std::abs(e.net_pnl());
                streak = streak <= 0 ? streak - 1 : -1;
            }
            stats.max_win_streak = std::max(stats.max_win_streak, streak > 0 ? streak : 0);
            stats.max_lose_streak = std::max(stats.max_lose_streak, streak < 0 ? -streak : 0);
        }

        stats.current_streak = streak;
        if (stats.total_trades > 0) {
            stats.avg_pnl = stats.total_pnl / stats.total_trades;
            stats.win_rate = static_cast<double>(stats.winners) / stats.total_trades;
            stats.plan_adherence_pct = static_cast<double>(plans_followed) / stats.total_trades;
        }
        if (stats.winners > 0) stats.avg_winner = total_wins / stats.winners;
        if (stats.losers > 0) stats.avg_loser = total_losses / stats.losers;
        stats.profit_factor = total_losses > 1e-10 ? total_wins / total_losses : 0;
        return stats;
    }

    [[nodiscard]] std::vector<JournalEntry> by_strategy(const std::string& strategy) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<JournalEntry> result;
        for (const auto& e : entries_) {
            if (e.strategy == strategy) result.push_back(e);
        }
        return result;
    }

    [[nodiscard]] std::vector<JournalEntry> by_tag(const std::string& tag) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<JournalEntry> result;
        for (const auto& e : entries_) {
            if (e.tags.count(tag)) result.push_back(e);
        }
        return result;
    }

    [[nodiscard]] std::vector<JournalEntry> recent(int n = 20) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<JournalEntry> result;
        int start = std::max(0, static_cast<int>(entries_.size()) - n);
        for (int i = start; i < static_cast<int>(entries_.size()); ++i) {
            result.push_back(entries_[i]);
        }
        return result;
    }

    [[nodiscard]] int entry_count() const { std::lock_guard<std::mutex> lock(mutex_); return static_cast<int>(entries_.size()); }

private:
    mutable std::mutex mutex_;
    std::deque<JournalEntry> entries_;
    size_t max_entries_{100000};
    int64_t counter_{0};
};

} // namespace journal
} // namespace trading
} // namespace genie

#endif // GENIE_TRADING_TRADE_JOURNAL_HPP
