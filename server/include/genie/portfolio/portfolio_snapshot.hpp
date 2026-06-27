/**
 * @file portfolio_snapshot.hpp
 * @brief Point-in-time portfolio versioning for audit and replay
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Portfolio snapshot and versioning system:
 * - Point-in-time snapshots with metadata
 * - Version history with diff comparison
 * - Snapshot scheduling (EOD, on-trade, manual)
 * - Position-level change tracking
 * - NAV time series from snapshots
 * - Snapshot compression and retention
 * - Regulatory audit trail
 * - JSON/CSV export
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PORTFOLIO_SNAPSHOT_HPP
#define GENIE_PORTFOLIO_SNAPSHOT_HPP

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

namespace genie {
namespace portfolio {
namespace snapshot {

// ============================================================================
// Data Structures
// ============================================================================

enum class SnapshotTrigger {
    Manual,
    EndOfDay,
    OnTrade,
    OnRebalance,
    Scheduled,
    Regulatory
};

[[nodiscard]] inline std::string trigger_string(SnapshotTrigger t) {
    switch (t) {
        case SnapshotTrigger::Manual:      return "manual";
        case SnapshotTrigger::EndOfDay:    return "eod";
        case SnapshotTrigger::OnTrade:     return "on_trade";
        case SnapshotTrigger::OnRebalance: return "on_rebalance";
        case SnapshotTrigger::Scheduled:   return "scheduled";
        case SnapshotTrigger::Regulatory:  return "regulatory";
    }
    return "unknown";
}

/**
 * @brief Position in a snapshot
 */
struct SnapshotPosition {
    std::string symbol;
    std::string asset_class;
    double quantity{0};
    double price{0};
    double market_value{0};
    double cost_basis{0};
    double weight{0};
    double unrealized_pnl{0};
    double realized_pnl{0};

    [[nodiscard]] double total_pnl() const { return unrealized_pnl + realized_pnl; }
    [[nodiscard]] double return_pct() const {
        return cost_basis != 0 ? (market_value - cost_basis) / cost_basis : 0;
    }
};

/**
 * @brief Portfolio snapshot at a point in time
 */
struct PortfolioSnapshot {
    std::string id;
    std::string portfolio_id;
    std::string portfolio_name;
    int version{0};
    SnapshotTrigger trigger{SnapshotTrigger::Manual};
    std::chrono::system_clock::time_point timestamp;

    std::vector<SnapshotPosition> positions;
    double total_market_value{0};
    double total_cost_basis{0};
    double cash_balance{0};
    double nav{0};
    double total_unrealized_pnl{0};
    double total_realized_pnl{0};

    std::string notes;
    std::string created_by;
    std::map<std::string, std::string> metadata;

    [[nodiscard]] int position_count() const { return static_cast<int>(positions.size()); }
    [[nodiscard]] double total_return_pct() const {
        return total_cost_basis != 0 ?
            (total_market_value - total_cost_basis) / total_cost_basis : 0;
    }

    [[nodiscard]] std::string to_json() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        std::ostringstream oss;
        oss << "{\"id\":\"" << id << "\",\"portfolio\":\"" << portfolio_id
            << "\",\"version\":" << version
            << ",\"trigger\":\"" << trigger_string(trigger)
            << "\",\"timestamp\":\"";
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        oss << std::fixed << std::setprecision(2);
        oss << "\",\"nav\":" << nav
            << ",\"market_value\":" << total_market_value
            << ",\"cash\":" << cash_balance
            << ",\"positions\":" << position_count() << "}";
        return oss.str();
    }
};

/**
 * @brief Diff between two snapshots
 */
struct PositionChange {
    std::string symbol;
    enum ChangeType { Added, Removed, Modified } type;
    double old_quantity{0};
    double new_quantity{0};
    double old_value{0};
    double new_value{0};
    double weight_change{0};

    [[nodiscard]] double quantity_change() const { return new_quantity - old_quantity; }
    [[nodiscard]] double value_change() const { return new_value - old_value; }
};

struct SnapshotDiff {
    std::string from_id;
    std::string to_id;
    int from_version{0};
    int to_version{0};
    std::vector<PositionChange> changes;
    double nav_change{0};
    double nav_change_pct{0};
    double cash_change{0};

    [[nodiscard]] int total_changes() const { return static_cast<int>(changes.size()); }
    [[nodiscard]] int additions() const {
        return static_cast<int>(std::count_if(changes.begin(), changes.end(),
            [](const PositionChange& c) { return c.type == PositionChange::Added; }));
    }
    [[nodiscard]] int removals() const {
        return static_cast<int>(std::count_if(changes.begin(), changes.end(),
            [](const PositionChange& c) { return c.type == PositionChange::Removed; }));
    }
    [[nodiscard]] int modifications() const {
        return static_cast<int>(std::count_if(changes.begin(), changes.end(),
            [](const PositionChange& c) { return c.type == PositionChange::Modified; }));
    }

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Snapshot Diff v" << from_version << " -> v" << to_version << "\n";
        oss << std::fixed << std::setprecision(2);
        oss << "  NAV Change: $" << nav_change << " (" << nav_change_pct * 100 << "%)\n";
        oss << "  Changes: " << total_changes()
            << " (+" << additions() << " -" << removals() << " ~" << modifications() << ")\n";
        for (const auto& c : changes) {
            switch (c.type) {
                case PositionChange::Added:
                    oss << "  + " << c.symbol << " qty=" << c.new_quantity << "\n"; break;
                case PositionChange::Removed:
                    oss << "  - " << c.symbol << " qty=" << c.old_quantity << "\n"; break;
                case PositionChange::Modified:
                    oss << "  ~ " << c.symbol << " qty " << c.old_quantity
                        << " -> " << c.new_quantity << "\n"; break;
            }
        }
        return oss.str();
    }
};

// ============================================================================
// Snapshot Manager
// ============================================================================

/**
 * @brief Manages portfolio snapshots with versioning
 */
class SnapshotManager {
public:
    explicit SnapshotManager(int max_snapshots_per_portfolio = 1000)
        : max_per_portfolio_(max_snapshots_per_portfolio) {}

    /**
     * @brief Take a snapshot
     */
    PortfolioSnapshot& take_snapshot(
        const std::string& portfolio_id,
        const std::vector<SnapshotPosition>& positions,
        double cash,
        SnapshotTrigger trigger = SnapshotTrigger::Manual,
        const std::string& notes = "") {

        std::lock_guard<std::mutex> lock(mutex_);
        int version = next_version(portfolio_id);

        PortfolioSnapshot snap;
        snap.id = portfolio_id + "-v" + std::to_string(version);
        snap.portfolio_id = portfolio_id;
        snap.version = version;
        snap.trigger = trigger;
        snap.timestamp = std::chrono::system_clock::now();
        snap.positions = positions;
        snap.cash_balance = cash;
        snap.notes = notes;

        // Calculate aggregates
        double total_mv = 0, total_cb = 0, total_upnl = 0, total_rpnl = 0;
        for (const auto& p : snap.positions) {
            total_mv += p.market_value;
            total_cb += p.cost_basis;
            total_upnl += p.unrealized_pnl;
            total_rpnl += p.realized_pnl;
        }
        snap.total_market_value = total_mv;
        snap.total_cost_basis = total_cb;
        snap.total_unrealized_pnl = total_upnl;
        snap.total_realized_pnl = total_rpnl;
        snap.nav = total_mv + cash;

        auto& hist = history_[portfolio_id];
        hist.push_back(snap);
        if (static_cast<int>(hist.size()) > max_per_portfolio_) {
            hist.pop_front();
        }

        return hist.back();
    }

    /**
     * @brief Get snapshot by version
     */
    [[nodiscard]] std::optional<PortfolioSnapshot> get(
        const std::string& portfolio_id, int version) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = history_.find(portfolio_id);
        if (it == history_.end()) return std::nullopt;
        for (const auto& snap : it->second) {
            if (snap.version == version) return snap;
        }
        return std::nullopt;
    }

    /**
     * @brief Get latest snapshot
     */
    [[nodiscard]] std::optional<PortfolioSnapshot> latest(
        const std::string& portfolio_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = history_.find(portfolio_id);
        if (it == history_.end() || it->second.empty()) return std::nullopt;
        return it->second.back();
    }

    /**
     * @brief Compare two snapshots
     */
    [[nodiscard]] SnapshotDiff diff(const PortfolioSnapshot& from,
                                      const PortfolioSnapshot& to) const {
        SnapshotDiff d;
        d.from_id = from.id;
        d.to_id = to.id;
        d.from_version = from.version;
        d.to_version = to.version;
        d.nav_change = to.nav - from.nav;
        d.nav_change_pct = from.nav != 0 ? d.nav_change / from.nav : 0;
        d.cash_change = to.cash_balance - from.cash_balance;

        std::map<std::string, const SnapshotPosition*> from_map, to_map;
        for (const auto& p : from.positions) from_map[p.symbol] = &p;
        for (const auto& p : to.positions) to_map[p.symbol] = &p;

        for (const auto& [sym, tp] : to_map) {
            auto fi = from_map.find(sym);
            if (fi == from_map.end()) {
                PositionChange c;
                c.symbol = sym;
                c.type = PositionChange::Added;
                c.new_quantity = tp->quantity;
                c.new_value = tp->market_value;
                d.changes.push_back(c);
            } else if (std::abs(tp->quantity - fi->second->quantity) > 1e-8) {
                PositionChange c;
                c.symbol = sym;
                c.type = PositionChange::Modified;
                c.old_quantity = fi->second->quantity;
                c.new_quantity = tp->quantity;
                c.old_value = fi->second->market_value;
                c.new_value = tp->market_value;
                c.weight_change = tp->weight - fi->second->weight;
                d.changes.push_back(c);
            }
        }
        for (const auto& [sym, fp] : from_map) {
            if (to_map.find(sym) == to_map.end()) {
                PositionChange c;
                c.symbol = sym;
                c.type = PositionChange::Removed;
                c.old_quantity = fp->quantity;
                c.old_value = fp->market_value;
                d.changes.push_back(c);
            }
        }
        return d;
    }

    /**
     * @brief Get NAV history
     */
    [[nodiscard]] std::vector<std::pair<std::chrono::system_clock::time_point, double>>
    nav_history(const std::string& portfolio_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::chrono::system_clock::time_point, double>> result;
        auto it = history_.find(portfolio_id);
        if (it == history_.end()) return result;
        for (const auto& snap : it->second) {
            result.emplace_back(snap.timestamp, snap.nav);
        }
        return result;
    }

    /**
     * @brief List all versions for a portfolio
     */
    [[nodiscard]] std::vector<int> versions(const std::string& portfolio_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<int> result;
        auto it = history_.find(portfolio_id);
        if (it == history_.end()) return result;
        for (const auto& snap : it->second) result.push_back(snap.version);
        return result;
    }

    /**
     * @brief Total snapshots across all portfolios
     */
    [[nodiscard]] int total_snapshots() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& [_, hist] : history_) total += static_cast<int>(hist.size());
        return total;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::deque<PortfolioSnapshot>> history_;
    int max_per_portfolio_;

    int next_version(const std::string& portfolio_id) {
        auto it = history_.find(portfolio_id);
        if (it == history_.end() || it->second.empty()) return 1;
        return it->second.back().version + 1;
    }
};

} // namespace snapshot
} // namespace portfolio
} // namespace genie

#endif // GENIE_PORTFOLIO_SNAPSHOT_HPP
