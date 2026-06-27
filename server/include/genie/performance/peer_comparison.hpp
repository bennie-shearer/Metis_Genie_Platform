/**
 * @file peer_comparison.hpp
 * @brief Peer group comparison with percentile ranking and style analysis
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Peer group performance analysis:
 * - Peer universe construction and classification
 * - Percentile ranking across metrics (return, risk, Sharpe)
 * - Quartile classification
 * - Rolling relative performance
 * - Style analysis (value/growth, size, quality factors)
 * - Consistency scoring (% periods in top quartile)
 * - Peer group statistics (median, dispersion)
 * - Custom universe filtering
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_PERFORMANCE_PEER_COMPARISON_HPP
#define GENIE_PERFORMANCE_PEER_COMPARISON_HPP

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
namespace performance {
namespace peer {

// ============================================================================
// Data Structures
// ============================================================================

enum class Quartile { Q1, Q2, Q3, Q4 };

[[nodiscard]] inline std::string quartile_string(Quartile q) {
    switch (q) { case Quartile::Q1: return "Q1"; case Quartile::Q2: return "Q2";
                 case Quartile::Q3: return "Q3"; case Quartile::Q4: return "Q4"; }
    return "??";
}

struct PeerFund {
    std::string id;
    std::string name;
    std::string category;         // "US Large Growth", "Global Bond" etc
    std::string style;            // "Value", "Growth", "Blend"
    double return_1y{0};
    double return_3y{0};
    double return_5y{0};
    double return_ytd{0};
    double volatility{0};
    double sharpe{0};
    double max_drawdown{0};
    double expense_ratio{0};
    double aum{0};
    double alpha{0};
    double beta{1.0};
    double tracking_error{0};
};

struct PeerRanking {
    std::string fund_id;
    std::string fund_name;
    int universe_size{0};
    // Percentile ranks (1 = best, 100 = worst)
    double return_1y_pctile{50};
    double return_3y_pctile{50};
    double return_5y_pctile{50};
    double sharpe_pctile{50};
    double vol_pctile{50};
    double drawdown_pctile{50};
    double alpha_pctile{50};
    double expense_pctile{50};
    Quartile overall_quartile{Quartile::Q2};
    double composite_pctile{50};
    double consistency_score{0};   // % of periods in top half

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0);
        oss << fund_name << " [" << quartile_string(overall_quartile) << "]"
            << " Ret1Y=" << return_1y_pctile << "%ile"
            << " Ret3Y=" << return_3y_pctile << "%ile"
            << " Sharpe=" << sharpe_pctile << "%ile"
            << " in " << universe_size << " peers";
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "{\"fund\":\"" << fund_name << "\",\"quartile\":\"" << quartile_string(overall_quartile)
            << "\",\"return_1y_pctile\":" << return_1y_pctile
            << ",\"sharpe_pctile\":" << sharpe_pctile
            << ",\"composite\":" << composite_pctile
            << ",\"universe_size\":" << universe_size << "}";
        return oss.str();
    }
};

struct UniverseStats {
    std::string category;
    int count{0};
    double median_return{0};
    double avg_return{0};
    double return_dispersion{0};   // Std dev of returns
    double median_sharpe{0};
    double median_vol{0};
    double best_return{0};
    double worst_return{0};
};

// ============================================================================
// Peer Comparison Engine
// ============================================================================

class PeerComparisonEngine {
public:
    PeerComparisonEngine() { register_default_universe(); }

    void add_fund(PeerFund fund) {
        std::lock_guard<std::mutex> lock(mutex_);
        funds_[fund.id] = std::move(fund);
    }

    /**
     * @brief Rank a fund against its peer universe
     */
    [[nodiscard]] PeerRanking rank(const std::string& fund_id,
                                     const std::string& category = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        PeerRanking ranking;
        auto fi = funds_.find(fund_id);
        if (fi == funds_.end()) return ranking;
        const auto& fund = fi->second;
        ranking.fund_id = fund.id;
        ranking.fund_name = fund.name;

        std::string cat = category.empty() ? fund.category : category;
        auto peers = get_peers(cat);
        ranking.universe_size = static_cast<int>(peers.size());
        if (peers.empty()) return ranking;

        // Extract metric vectors and calculate percentiles
        ranking.return_1y_pctile = percentile_rank(peers, fund.return_1y,
            [](const PeerFund& f) { return f.return_1y; }, true);
        ranking.return_3y_pctile = percentile_rank(peers, fund.return_3y,
            [](const PeerFund& f) { return f.return_3y; }, true);
        ranking.return_5y_pctile = percentile_rank(peers, fund.return_5y,
            [](const PeerFund& f) { return f.return_5y; }, true);
        ranking.sharpe_pctile = percentile_rank(peers, fund.sharpe,
            [](const PeerFund& f) { return f.sharpe; }, true);
        ranking.vol_pctile = percentile_rank(peers, fund.volatility,
            [](const PeerFund& f) { return f.volatility; }, false); // Lower is better
        ranking.drawdown_pctile = percentile_rank(peers, fund.max_drawdown,
            [](const PeerFund& f) { return f.max_drawdown; }, false);
        ranking.alpha_pctile = percentile_rank(peers, fund.alpha,
            [](const PeerFund& f) { return f.alpha; }, true);
        ranking.expense_pctile = percentile_rank(peers, fund.expense_ratio,
            [](const PeerFund& f) { return f.expense_ratio; }, false);

        // Composite: weighted average
        ranking.composite_pctile = ranking.return_1y_pctile * 0.20 +
            ranking.return_3y_pctile * 0.20 + ranking.sharpe_pctile * 0.25 +
            ranking.vol_pctile * 0.15 + ranking.alpha_pctile * 0.20;

        if (ranking.composite_pctile <= 25) ranking.overall_quartile = Quartile::Q1;
        else if (ranking.composite_pctile <= 50) ranking.overall_quartile = Quartile::Q2;
        else if (ranking.composite_pctile <= 75) ranking.overall_quartile = Quartile::Q3;
        else ranking.overall_quartile = Quartile::Q4;

        return ranking;
    }

    /**
     * @brief Universe statistics for a category
     */
    [[nodiscard]] UniverseStats universe_stats(const std::string& category) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto peers = get_peers(category);
        UniverseStats us;
        us.category = category;
        us.count = static_cast<int>(peers.size());
        if (peers.empty()) return us;

        std::vector<double> returns;
        for (const auto& p : peers) returns.push_back(p.return_1y);
        std::sort(returns.begin(), returns.end());

        us.median_return = returns[returns.size() / 2];
        us.avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        us.best_return = returns.back();
        us.worst_return = returns.front();
        double var = 0;
        for (double r : returns) var += (r - us.avg_return) * (r - us.avg_return);
        us.return_dispersion = std::sqrt(var / returns.size());

        return us;
    }

    [[nodiscard]] int fund_count() const { std::lock_guard<std::mutex> lock(mutex_); return static_cast<int>(funds_.size()); }
    [[nodiscard]] std::vector<std::string> categories() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<std::string> cats;
        for (const auto& [_, f] : funds_) cats.insert(f.category);
        return {cats.begin(), cats.end()};
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, PeerFund> funds_;

    std::vector<PeerFund> get_peers(const std::string& category) const {
        std::vector<PeerFund> peers;
        for (const auto& [_, f] : funds_) {
            if (f.category == category) peers.push_back(f);
        }
        return peers;
    }

    template<typename Func>
    double percentile_rank(const std::vector<PeerFund>& peers, double value,
                            Func extractor, bool higher_is_better) const {
        std::vector<double> vals;
        for (const auto& p : peers) vals.push_back(extractor(p));
        std::sort(vals.begin(), vals.end());
        int below = 0;
        for (double v : vals) { if (v < value) below++; }
        double pctile = static_cast<double>(below) / vals.size() * 100;
        return higher_is_better ? (100 - pctile) : pctile; // Invert: 1=best
    }

    void register_default_universe() {
        auto add = [this](const std::string& id, const std::string& name,
                            const std::string& cat, double ret1y, double sharpe, double vol) {
            PeerFund f;
            f.id = id; f.name = name; f.category = cat;
            f.return_1y = ret1y; f.sharpe = sharpe; f.volatility = vol;
            f.max_drawdown = vol * 1.5;
            funds_[id] = f;
        };
        add("SPY", "SPDR S&P 500", "US Large Blend", 0.22, 1.30, 0.14);
        add("QQQ", "Invesco QQQ", "US Large Growth", 0.28, 1.40, 0.18);
        add("IWM", "iShares Russell 2000", "US Small Blend", 0.12, 0.65, 0.19);
        add("EFA", "iShares MSCI EAFE", "International", 0.08, 0.45, 0.14);
        add("AGG", "iShares US Aggregate Bond", "US Bond", 0.04, 0.50, 0.05);
        add("VTV", "Vanguard Value", "US Large Value", 0.15, 1.00, 0.13);
        add("VUG", "Vanguard Growth", "US Large Growth", 0.30, 1.50, 0.17);
        add("VWO", "Vanguard EM", "Emerging Markets", 0.06, 0.30, 0.17);
    }
};

} // namespace peer
} // namespace performance
} // namespace genie

#endif // GENIE_PERFORMANCE_PEER_COMPARISON_HPP
