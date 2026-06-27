/**
 * @file benchmark_builder.hpp
 * @brief Custom Benchmark Construction Engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Constructs custom composite benchmarks from weighted blends of indices,
 * sectors, or individual securities with configurable rebalancing schedules.
 *
 * Features:
 *   - Weighted index blending (e.g., 60% S&P 500 + 40% Bloomberg Agg)
 *   - Custom universe definition from security lists
 *   - Rebalancing schedules: daily, weekly, monthly, quarterly, annual
 *   - Drift-based rebalancing with configurable tolerance bands
 *   - Benchmark return calculation from constituent returns
 *   - Time-weighted and money-weighted return methods
 *   - Sector/region/asset-class weighting schemes
 *   - Equal-weight, market-cap-weight, and custom-weight modes
 *   - Historical benchmark reconstruction from constituents
 *   - Benchmark vs. portfolio tracking error calculation
 *   - Information ratio, active return, active risk
 *   - Contribution attribution by constituent
 *   - Turnover tracking across rebalance events
 *   - Serialization to/from JSON for persistence
 *   - Thread-safe benchmark registry
 *   - Zero external dependencies
 *
 * Architecture:
 *   BenchmarkDefinition  -- static specification (weights, schedule)
 *   BenchmarkInstance     -- live instance with historical returns
 *   BenchmarkBuilder      -- factory for creating and managing benchmarks
 *   BenchmarkRegistry     -- thread-safe catalog of named benchmarks
 *
 * Usage:
 *   analytics::BenchmarkBuilder builder;
 *
 *   // 60/40 stock/bond blend
 *   auto bench = builder.create("60/40 Balanced")
 *       .add_component("SPX", 0.60, "S&P 500")
 *       .add_component("AGG", 0.40, "Bloomberg US Aggregate")
 *       .rebalance_frequency(analytics::RebalanceFreq::QUARTERLY)
 *       .base_currency("USD")
 *       .build();
 *
 *   // Equal-weight tech basket
 *   auto tech = builder.create("Tech Equal Weight")
 *       .add_components({"AAPL","MSFT","GOOGL","AMZN","META"}, analytics::WeightScheme::EQUAL)
 *       .rebalance_frequency(analytics::RebalanceFreq::MONTHLY)
 *       .build();
 *
 *   // Calculate returns
 *   bench.update_returns(date, constituent_returns);
 *   double tr = bench.total_return();
 *   double te = bench.tracking_error(portfolio_returns);
 *
 * Build:
 *   g++ -std=c++20 -O2 -I include -pthread
 */
#pragma once
#ifndef GENIE_ANALYTICS_BENCHMARK_BUILDER_HPP
#define GENIE_ANALYTICS_BENCHMARK_BUILDER_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace genie {
namespace analytics {

// ============================================================================
// Enumerations
// ============================================================================

/**
 * @brief Rebalancing frequency
 */
enum class RebalanceFreq {
    NONE,           // Buy-and-hold (no rebalancing)
    DAILY,
    WEEKLY,
    MONTHLY,
    QUARTERLY,
    SEMI_ANNUAL,
    ANNUAL,
    ON_DRIFT        // Rebalance only when drift exceeds tolerance
};

inline std::string rebalance_freq_string(RebalanceFreq f) {
    switch (f) {
        case RebalanceFreq::NONE:        return "none";
        case RebalanceFreq::DAILY:       return "daily";
        case RebalanceFreq::WEEKLY:      return "weekly";
        case RebalanceFreq::MONTHLY:     return "monthly";
        case RebalanceFreq::QUARTERLY:   return "quarterly";
        case RebalanceFreq::SEMI_ANNUAL: return "semi_annual";
        case RebalanceFreq::ANNUAL:      return "annual";
        case RebalanceFreq::ON_DRIFT:    return "on_drift";
        default: return "unknown";
    }
}

/**
 * @brief Weighting scheme for components
 */
enum class WeightScheme {
    CUSTOM,         // User-specified weights
    EQUAL,          // Equal weight across all components
    MARKET_CAP,     // Proportional to market capitalization
    INVERSE_VOL,    // Inverse volatility (risk parity lite)
    GDP_WEIGHTED    // GDP-weighted (for country/region indices)
};

inline std::string weight_scheme_string(WeightScheme w) {
    switch (w) {
        case WeightScheme::CUSTOM:      return "custom";
        case WeightScheme::EQUAL:       return "equal";
        case WeightScheme::MARKET_CAP:  return "market_cap";
        case WeightScheme::INVERSE_VOL: return "inverse_vol";
        case WeightScheme::GDP_WEIGHTED: return "gdp_weighted";
        default: return "unknown";
    }
}

/**
 * @brief Return calculation method
 */
enum class ReturnMethod {
    TIME_WEIGHTED,      // Geometric chain-linking (standard)
    MONEY_WEIGHTED,     // IRR-based (Dietz)
    SIMPLE              // Arithmetic average
};

// ============================================================================
// Benchmark Component
// ============================================================================

/**
 * @brief Single component (index, sector, or security) in a benchmark
 */
struct BenchmarkComponent {
    std::string symbol;             // Ticker or index code
    std::string name;               // Display name
    double target_weight    = 0.0;  // Target allocation (0.0 - 1.0)
    double current_weight   = 0.0;  // Current (drifted) weight
    double market_cap       = 0.0;  // For market-cap weighting
    double volatility       = 0.0;  // For inverse-vol weighting
    std::string asset_class;        // "equity", "fixed_income", "commodity", etc.
    std::string region;             // "US", "Europe", "Asia", etc.
    std::string sector;             // "Technology", "Healthcare", etc.

    // Performance tracking
    double cumulative_return = 0.0;
    double contribution     = 0.0;  // Contribution to benchmark return
    int64_t added_date      = 0;    // When component was added
    bool active             = true;

    double drift() const {
        return (target_weight > 0) ? std::abs(current_weight - target_weight) : 0.0;
    }

    double drift_pct() const {
        return (target_weight > 0) ? (drift() / target_weight) * 100.0 : 0.0;
    }
};

// ============================================================================
// Rebalance Event
// ============================================================================

/**
 * @brief Record of a rebalancing event
 */
struct RebalanceEvent {
    int64_t date                = 0;
    std::string reason;             // "scheduled", "drift_exceeded", "manual"
    double turnover             = 0.0;  // Sum of absolute weight changes / 2
    std::map<std::string, double> weight_changes;  // symbol -> delta
    std::map<std::string, double> pre_weights;
    std::map<std::string, double> post_weights;
};

// ============================================================================
// Benchmark Return Entry
// ============================================================================

/**
 * @brief Daily (or periodic) return entry
 */
struct BenchmarkReturn {
    int64_t date                = 0;
    double benchmark_return     = 0.0;  // Weighted sum of component returns
    double cumulative_return    = 0.0;  // Chain-linked from inception
    double index_level          = 100.0; // Rebased to 100
    std::map<std::string, double> component_returns;    // Per-component
    std::map<std::string, double> component_contributions; // Weighted
    std::map<std::string, double> weights_snapshot;     // Weights on this date
};

// ============================================================================
// Benchmark Definition (immutable specification)
// ============================================================================

/**
 * @brief Static benchmark specification
 */
struct BenchmarkDefinition {
    std::string id;
    std::string name;
    std::string description;
    std::string base_currency       = "USD";
    WeightScheme weight_scheme      = WeightScheme::CUSTOM;
    RebalanceFreq rebalance_freq    = RebalanceFreq::QUARTERLY;
    ReturnMethod return_method      = ReturnMethod::TIME_WEIGHTED;
    double drift_tolerance          = 0.05;     // 5% drift triggers rebalance
    double inception_level          = 100.0;    // Starting index value
    int64_t inception_date          = 0;
    std::vector<BenchmarkComponent> components;
    std::map<std::string, std::string> metadata;

    // Validation
    bool is_valid() const {
        if (name.empty()) return false;
        if (components.empty()) return false;
        double total = total_weight();
        return std::abs(total - 1.0) < 0.001; // Weights must sum to ~1.0
    }

    double total_weight() const {
        double sum = 0.0;
        for (const auto& c : components) {
            if (c.active) sum += c.target_weight;
        }
        return sum;
    }

    int active_count() const {
        int n = 0;
        for (const auto& c : components) {
            if (c.active) ++n;
        }
        return n;
    }

    /**
     * @brief Serialize to JSON
     */
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{\n"
            << "  \"id\": \"" << id << "\",\n"
            << "  \"name\": \"" << name << "\",\n"
            << "  \"description\": \"" << description << "\",\n"
            << "  \"currency\": \"" << base_currency << "\",\n"
            << "  \"weight_scheme\": \"" << weight_scheme_string(weight_scheme) << "\",\n"
            << "  \"rebalance\": \"" << rebalance_freq_string(rebalance_freq) << "\",\n"
            << "  \"drift_tolerance\": " << drift_tolerance << ",\n"
            << "  \"inception_level\": " << inception_level << ",\n"
            << "  \"components\": [\n";

        for (size_t i = 0; i < components.size(); ++i) {
            const auto& c = components[i];
            oss << "    {\"symbol\": \"" << c.symbol
                << "\", \"name\": \"" << c.name
                << "\", \"weight\": " << std::fixed << std::setprecision(4) << c.target_weight;
            if (!c.asset_class.empty()) oss << ", \"asset_class\": \"" << c.asset_class << "\"";
            if (!c.region.empty()) oss << ", \"region\": \"" << c.region << "\"";
            if (!c.sector.empty()) oss << ", \"sector\": \"" << c.sector << "\"";
            oss << "}";
            if (i + 1 < components.size()) oss << ",";
            oss << "\n";
        }

        oss << "  ]\n}";
        return oss.str();
    }
};

// ============================================================================
// Benchmark Instance (live with returns history)
// ============================================================================

/**
 * @brief Live benchmark instance with performance tracking
 */
class BenchmarkInstance {
public:
    BenchmarkInstance() = default;

    explicit BenchmarkInstance(const BenchmarkDefinition& def)
        : definition_(def) {
        // Initialize current weights from target
        for (auto& c : definition_.components) {
            c.current_weight = c.target_weight;
        }
    }

    const BenchmarkDefinition& definition() const { return definition_; }
    const std::string& id() const { return definition_.id; }
    const std::string& name() const { return definition_.name; }

    // ---- Return Updates ----

    /**
     * @brief Update benchmark with component returns for a date
     * @param date Date (epoch ms or YYYYMMDD)
     * @param returns Map of symbol -> return for the period
     */
    void update_returns(int64_t date,
                        const std::map<std::string, double>& returns) {
        BenchmarkReturn entry;
        entry.date = date;

        double weighted_return = 0.0;

        for (auto& comp : definition_.components) {
            if (!comp.active) continue;

            double comp_return = 0.0;
            auto it = returns.find(comp.symbol);
            if (it != returns.end()) {
                comp_return = it->second;
            }

            // Contribution = weight * return
            double contribution = comp.current_weight * comp_return;
            weighted_return += contribution;

            entry.component_returns[comp.symbol] = comp_return;
            entry.component_contributions[comp.symbol] = contribution;
            entry.weights_snapshot[comp.symbol] = comp.current_weight;

            // Update cumulative for component
            comp.cumulative_return = (1.0 + comp.cumulative_return) * (1.0 + comp_return) - 1.0;
            comp.contribution += contribution;

            // Drift weights based on return (before rebalance check)
            comp.current_weight *= (1.0 + comp_return);
        }

        // Normalize drifted weights
        normalize_weights();

        entry.benchmark_return = weighted_return;

        // Chain-link cumulative
        if (returns_history_.empty()) {
            entry.cumulative_return = weighted_return;
            entry.index_level = definition_.inception_level * (1.0 + weighted_return);
        } else {
            const auto& prev = returns_history_.back();
            entry.cumulative_return = (1.0 + prev.cumulative_return) * (1.0 + weighted_return) - 1.0;
            entry.index_level = prev.index_level * (1.0 + weighted_return);
        }

        returns_history_.push_back(entry);

        // Check if rebalance needed
        if (should_rebalance(date)) {
            rebalance(date, "scheduled");
        } else if (max_drift() > definition_.drift_tolerance) {
            if (definition_.rebalance_freq == RebalanceFreq::ON_DRIFT) {
                rebalance(date, "drift_exceeded");
            }
        }
    }

    /**
     * @brief Force a rebalance back to target weights
     */
    void rebalance(int64_t date, const std::string& reason = "manual") {
        RebalanceEvent event;
        event.date = date;
        event.reason = reason;

        double turnover = 0.0;
        for (auto& comp : definition_.components) {
            if (!comp.active) continue;

            event.pre_weights[comp.symbol] = comp.current_weight;
            event.post_weights[comp.symbol] = comp.target_weight;

            double delta = std::abs(comp.current_weight - comp.target_weight);
            event.weight_changes[comp.symbol] = comp.target_weight - comp.current_weight;
            turnover += delta;

            comp.current_weight = comp.target_weight;
        }

        event.turnover = turnover / 2.0; // One-way turnover
        rebalance_history_.push_back(event);
    }

    // ---- Performance Metrics ----

    /**
     * @brief Total cumulative return since inception
     */
    double total_return() const {
        if (returns_history_.empty()) return 0.0;
        return returns_history_.back().cumulative_return;
    }

    /**
     * @brief Current index level
     */
    double index_level() const {
        if (returns_history_.empty()) return definition_.inception_level;
        return returns_history_.back().index_level;
    }

    /**
     * @brief Annualized return
     */
    double annualized_return(int periods_per_year = 252) const {
        if (returns_history_.size() < 2) return 0.0;
        double total = total_return();
        double n = static_cast<double>(returns_history_.size());
        double years = n / periods_per_year;
        if (years <= 0) return 0.0;
        return std::pow(1.0 + total, 1.0 / years) - 1.0;
    }

    /**
     * @brief Annualized volatility
     */
    double volatility(int periods_per_year = 252) const {
        auto rets = period_returns();
        if (rets.size() < 2) return 0.0;

        double mean = 0.0;
        for (double r : rets) mean += r;
        mean /= rets.size();

        double var = 0.0;
        for (double r : rets) var += (r - mean) * (r - mean);
        var /= (rets.size() - 1);

        return std::sqrt(var * periods_per_year);
    }

    /**
     * @brief Sharpe ratio (excess return / volatility)
     */
    double sharpe_ratio(double risk_free_rate = 0.05, int periods_per_year = 252) const {
        double vol = volatility(periods_per_year);
        if (vol < 1e-10) return 0.0;
        return (annualized_return(periods_per_year) - risk_free_rate) / vol;
    }

    /**
     * @brief Maximum drawdown
     */
    double max_drawdown() const {
        if (returns_history_.empty()) return 0.0;

        double peak = definition_.inception_level;
        double max_dd = 0.0;

        for (const auto& entry : returns_history_) {
            if (entry.index_level > peak) peak = entry.index_level;
            double dd = (peak - entry.index_level) / peak;
            if (dd > max_dd) max_dd = dd;
        }

        return max_dd;
    }

    // ---- Relative Performance (vs. Portfolio) ----

    /**
     * @brief Tracking error vs. portfolio returns
     */
    double tracking_error(const std::vector<double>& portfolio_returns,
                          int periods_per_year = 252) const {
        auto bench_rets = period_returns();
        if (bench_rets.size() != portfolio_returns.size() || bench_rets.size() < 2) return 0.0;

        // Active returns
        std::vector<double> active;
        for (size_t i = 0; i < bench_rets.size(); ++i) {
            active.push_back(portfolio_returns[i] - bench_rets[i]);
        }

        // Standard deviation of active returns
        double mean = 0.0;
        for (double a : active) mean += a;
        mean /= active.size();

        double var = 0.0;
        for (double a : active) var += (a - mean) * (a - mean);
        var /= (active.size() - 1);

        return std::sqrt(var * periods_per_year);
    }

    /**
     * @brief Information ratio (active return / tracking error)
     */
    double information_ratio(const std::vector<double>& portfolio_returns,
                              int periods_per_year = 252) const {
        double te = tracking_error(portfolio_returns, periods_per_year);
        if (te < 1e-10) return 0.0;

        auto bench_rets = period_returns();
        if (bench_rets.size() != portfolio_returns.size()) return 0.0;

        // Annualized active return
        double active_sum = 0.0;
        for (size_t i = 0; i < bench_rets.size(); ++i) {
            active_sum += portfolio_returns[i] - bench_rets[i];
        }
        double active_ann = (active_sum / bench_rets.size()) * periods_per_year;

        return active_ann / te;
    }

    /**
     * @brief Active return (annualized portfolio return - benchmark return)
     */
    double active_return(const std::vector<double>& portfolio_returns,
                         int periods_per_year = 252) const {
        auto bench_rets = period_returns();
        if (bench_rets.size() != portfolio_returns.size() || bench_rets.empty()) return 0.0;

        double port_cum = 1.0, bench_cum = 1.0;
        for (size_t i = 0; i < bench_rets.size(); ++i) {
            port_cum *= (1.0 + portfolio_returns[i]);
            bench_cum *= (1.0 + bench_rets[i]);
        }

        double years = static_cast<double>(bench_rets.size()) / periods_per_year;
        if (years <= 0) return 0.0;

        double port_ann = std::pow(port_cum, 1.0 / years) - 1.0;
        double bench_ann = std::pow(bench_cum, 1.0 / years) - 1.0;

        return port_ann - bench_ann;
    }

    // ---- Component Analysis ----

    /**
     * @brief Get contribution attribution by component
     */
    std::map<std::string, double> contribution_attribution() const {
        std::map<std::string, double> contrib;
        for (const auto& c : definition_.components) {
            if (c.active) {
                contrib[c.symbol] = c.contribution;
            }
        }
        return contrib;
    }

    /**
     * @brief Get current weights (after drift)
     */
    std::map<std::string, double> current_weights() const {
        std::map<std::string, double> weights;
        for (const auto& c : definition_.components) {
            if (c.active) {
                weights[c.symbol] = c.current_weight;
            }
        }
        return weights;
    }

    /**
     * @brief Maximum drift of any component
     */
    double max_drift() const {
        double max_d = 0.0;
        for (const auto& c : definition_.components) {
            if (c.active && c.drift() > max_d) max_d = c.drift();
        }
        return max_d;
    }

    /**
     * @brief Total turnover across all rebalance events
     */
    double total_turnover() const {
        double t = 0.0;
        for (const auto& e : rebalance_history_) t += e.turnover;
        return t;
    }

    // ---- Data Access ----

    std::vector<double> period_returns() const {
        std::vector<double> rets;
        for (const auto& e : returns_history_) {
            rets.push_back(e.benchmark_return);
        }
        return rets;
    }

    const std::vector<BenchmarkReturn>& returns_history() const { return returns_history_; }
    const std::vector<RebalanceEvent>& rebalance_history() const { return rebalance_history_; }
    int num_periods() const { return static_cast<int>(returns_history_.size()); }
    int num_rebalances() const { return static_cast<int>(rebalance_history_.size()); }

    /**
     * @brief Get return for a specific date
     */
    std::optional<BenchmarkReturn> return_on(int64_t date) const {
        for (const auto& e : returns_history_) {
            if (e.date == date) return e;
        }
        return std::nullopt;
    }

    /**
     * @brief Get returns for a date range
     */
    std::vector<BenchmarkReturn> returns_between(int64_t start, int64_t end) const {
        std::vector<BenchmarkReturn> result;
        for (const auto& e : returns_history_) {
            if (e.date >= start && e.date <= end) {
                result.push_back(e);
            }
        }
        return result;
    }

    /**
     * @brief JSON status report
     */
    std::string status_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "{\n"
            << "  \"id\": \"" << definition_.id << "\",\n"
            << "  \"name\": \"" << definition_.name << "\",\n"
            << "  \"index_level\": " << index_level() << ",\n"
            << "  \"total_return\": " << total_return() << ",\n"
            << "  \"annualized_return\": " << annualized_return() << ",\n"
            << "  \"volatility\": " << volatility() << ",\n"
            << "  \"sharpe_ratio\": " << sharpe_ratio() << ",\n"
            << "  \"max_drawdown\": " << max_drawdown() << ",\n"
            << "  \"max_drift\": " << max_drift() << ",\n"
            << "  \"periods\": " << num_periods() << ",\n"
            << "  \"rebalances\": " << num_rebalances() << ",\n"
            << "  \"total_turnover\": " << total_turnover() << ",\n"
            << "  \"components\": " << definition_.active_count() << "\n"
            << "}";
        return oss.str();
    }

private:
    void normalize_weights() {
        double sum = 0.0;
        for (const auto& c : definition_.components) {
            if (c.active) sum += c.current_weight;
        }
        if (sum > 0 && std::abs(sum - 1.0) > 1e-10) {
            for (auto& c : definition_.components) {
                if (c.active) c.current_weight /= sum;
            }
        }
    }

    bool should_rebalance(int64_t /*date*/) const {
        if (definition_.rebalance_freq == RebalanceFreq::NONE) return false;
        if (definition_.rebalance_freq == RebalanceFreq::ON_DRIFT) return false;
        if (definition_.rebalance_freq == RebalanceFreq::DAILY) return true;

        // Simple period-based check (count periods since last rebalance)
        int periods_since = 0;
        if (!rebalance_history_.empty()) {
            // Count returns since last rebalance
            for (auto it = returns_history_.rbegin(); it != returns_history_.rend(); ++it) {
                if (it->date <= rebalance_history_.back().date) break;
                periods_since++;
            }
        } else {
            periods_since = static_cast<int>(returns_history_.size());
        }

        switch (definition_.rebalance_freq) {
            case RebalanceFreq::WEEKLY:      return periods_since >= 5;
            case RebalanceFreq::MONTHLY:     return periods_since >= 21;
            case RebalanceFreq::QUARTERLY:   return periods_since >= 63;
            case RebalanceFreq::SEMI_ANNUAL: return periods_since >= 126;
            case RebalanceFreq::ANNUAL:      return periods_since >= 252;
            default: return false;
        }
    }

    BenchmarkDefinition definition_;
    std::vector<BenchmarkReturn> returns_history_;
    std::vector<RebalanceEvent> rebalance_history_;
};

// ============================================================================
// Benchmark Builder (Fluent API)
// ============================================================================

/**
 * @brief Fluent builder for constructing BenchmarkDefinition
 */
class BenchmarkDefBuilder {
public:
    explicit BenchmarkDefBuilder(const std::string& name) {
        def_.name = name;
        def_.id = generate_id(name);
    }

    BenchmarkDefBuilder& id(const std::string& id) {
        def_.id = id;
        return *this;
    }

    BenchmarkDefBuilder& description(const std::string& desc) {
        def_.description = desc;
        return *this;
    }

    BenchmarkDefBuilder& base_currency(const std::string& ccy) {
        def_.base_currency = ccy;
        return *this;
    }

    BenchmarkDefBuilder& weight_scheme(WeightScheme ws) {
        def_.weight_scheme = ws;
        return *this;
    }

    BenchmarkDefBuilder& rebalance_frequency(RebalanceFreq freq) {
        def_.rebalance_freq = freq;
        return *this;
    }

    BenchmarkDefBuilder& return_method(ReturnMethod method) {
        def_.return_method = method;
        return *this;
    }

    BenchmarkDefBuilder& drift_tolerance(double tol) {
        def_.drift_tolerance = tol;
        return *this;
    }

    BenchmarkDefBuilder& inception_level(double level) {
        def_.inception_level = level;
        return *this;
    }

    BenchmarkDefBuilder& inception_date(int64_t date) {
        def_.inception_date = date;
        return *this;
    }

    /**
     * @brief Add a single component with explicit weight
     */
    BenchmarkDefBuilder& add_component(const std::string& symbol,
                                        double weight,
                                        const std::string& name = "") {
        BenchmarkComponent c;
        c.symbol = symbol;
        c.name = name.empty() ? symbol : name;
        c.target_weight = weight;
        c.current_weight = weight;
        def_.components.push_back(c);
        return *this;
    }

    /**
     * @brief Add component with full details
     */
    BenchmarkDefBuilder& add_component(const BenchmarkComponent& comp) {
        def_.components.push_back(comp);
        return *this;
    }

    /**
     * @brief Add multiple components with automatic weighting
     */
    BenchmarkDefBuilder& add_components(const std::vector<std::string>& symbols,
                                         WeightScheme scheme = WeightScheme::EQUAL) {
        if (symbols.empty()) return *this;

        def_.weight_scheme = scheme;

        if (scheme == WeightScheme::EQUAL) {
            double w = 1.0 / symbols.size();
            for (const auto& sym : symbols) {
                BenchmarkComponent c;
                c.symbol = sym;
                c.name = sym;
                c.target_weight = w;
                c.current_weight = w;
                def_.components.push_back(c);
            }
        } else {
            // For market-cap or other schemes, add with zero weight
            // (to be computed later from market data)
            for (const auto& sym : symbols) {
                BenchmarkComponent c;
                c.symbol = sym;
                c.name = sym;
                c.target_weight = 0.0;
                c.current_weight = 0.0;
                def_.components.push_back(c);
            }
        }

        return *this;
    }

    /**
     * @brief Add metadata key-value pair
     */
    BenchmarkDefBuilder& metadata(const std::string& key, const std::string& value) {
        def_.metadata[key] = value;
        return *this;
    }

    /**
     * @brief Validate and build the benchmark definition
     */
    BenchmarkDefinition build_definition() const {
        auto def = def_;

        // Auto-compute weights for non-custom schemes
        if (def.weight_scheme == WeightScheme::EQUAL && !def.components.empty()) {
            // Check if any weights are zero (need recompute)
            bool need_recompute = false;
            for (const auto& c : def.components) {
                if (c.target_weight <= 0) { need_recompute = true; break; }
            }
            if (need_recompute) {
                double w = 1.0 / def.components.size();
                for (auto& c : def.components) {
                    c.target_weight = w;
                    c.current_weight = w;
                }
            }
        } else if (def.weight_scheme == WeightScheme::INVERSE_VOL) {
            // Compute inverse-vol weights from component volatilities
            double sum_inv_vol = 0.0;
            for (const auto& c : def.components) {
                if (c.volatility > 0) sum_inv_vol += 1.0 / c.volatility;
            }
            if (sum_inv_vol > 0) {
                for (auto& c : def.components) {
                    if (c.volatility > 0) {
                        c.target_weight = (1.0 / c.volatility) / sum_inv_vol;
                    }
                    c.current_weight = c.target_weight;
                }
            }
        } else if (def.weight_scheme == WeightScheme::MARKET_CAP) {
            // Market-cap weighted
            double total_cap = 0.0;
            for (const auto& c : def.components) total_cap += c.market_cap;
            if (total_cap > 0) {
                for (auto& c : def.components) {
                    c.target_weight = c.market_cap / total_cap;
                    c.current_weight = c.target_weight;
                }
            }
        }

        // Validate
        if (!def.is_valid()) {
            // Attempt to normalize
            double total = def.total_weight();
            if (total > 0 && std::abs(total - 1.0) > 0.001) {
                for (auto& c : def.components) {
                    c.target_weight /= total;
                    c.current_weight = c.target_weight;
                }
            }
        }

        return def;
    }

    /**
     * @brief Build a live benchmark instance
     */
    BenchmarkInstance build() const {
        return BenchmarkInstance(build_definition());
    }

private:
    static std::string generate_id(const std::string& name) {
        static std::atomic<int> counter{0};
        std::string id;
        for (char c : name) {
            if (std::isalnum(c)) id += std::tolower(c);
            else if (c == ' ' || c == '-') id += '_';
        }
        id += "_" + std::to_string(counter.fetch_add(1));
        return id;
    }

    BenchmarkDefinition def_;
};

// ============================================================================
// Benchmark Builder (Factory)
// ============================================================================

/**
 * @brief Factory for creating benchmarks
 */
class BenchmarkBuilder {
public:
    /**
     * @brief Start building a new benchmark
     */
    BenchmarkDefBuilder create(const std::string& name) {
        return BenchmarkDefBuilder(name);
    }

    // ---- Pre-built Common Benchmarks ----

    /**
     * @brief 60/40 Stock/Bond benchmark
     */
    BenchmarkInstance balanced_60_40() {
        return create("60/40 Balanced")
            .description("60% US Equity / 40% US Aggregate Bond")
            .add_component("SPX", 0.60, "S&P 500")
            .add_component("AGG", 0.40, "Bloomberg US Aggregate")
            .rebalance_frequency(RebalanceFreq::QUARTERLY)
            .build();
    }

    /**
     * @brief Global equity benchmark (developed markets)
     */
    BenchmarkInstance global_equity() {
        return create("Global Equity")
            .description("Market-cap weighted developed market equity")
            .add_component("SPX",   0.55, "S&P 500")
            .add_component("SXXP",  0.20, "STOXX Europe 600")
            .add_component("NKY",   0.10, "Nikkei 225")
            .add_component("HSI",   0.08, "Hang Seng")
            .add_component("AS51",  0.04, "ASX 200")
            .add_component("SPTSX", 0.03, "S&P/TSX Composite")
            .rebalance_frequency(RebalanceFreq::QUARTERLY)
            .build();
    }

    /**
     * @brief US sector rotation benchmark
     */
    BenchmarkInstance us_sector_equal() {
        return create("US Sector Equal Weight")
            .description("Equal-weight across 11 GICS sectors")
            .add_components({
                "XLK", "XLV", "XLF", "XLE", "XLI",
                "XLP", "XLY", "XLU", "XLRE", "XLB", "XLC"
            }, WeightScheme::EQUAL)
            .rebalance_frequency(RebalanceFreq::MONTHLY)
            .build();
    }

    /**
     * @brief Risk parity benchmark
     */
    BenchmarkInstance risk_parity() {
        BenchmarkComponent equity;
        equity.symbol = "SPX"; equity.name = "US Equity";
        equity.volatility = 0.16; equity.asset_class = "equity";

        BenchmarkComponent bonds;
        bonds.symbol = "AGG"; bonds.name = "US Bonds";
        bonds.volatility = 0.04; bonds.asset_class = "fixed_income";

        BenchmarkComponent commodities;
        commodities.symbol = "DJP"; commodities.name = "Commodities";
        commodities.volatility = 0.12; commodities.asset_class = "commodity";

        BenchmarkComponent gold;
        gold.symbol = "GLD"; gold.name = "Gold";
        gold.volatility = 0.15; gold.asset_class = "commodity";

        return create("Risk Parity")
            .description("Inverse-volatility weighted multi-asset")
            .weight_scheme(WeightScheme::INVERSE_VOL)
            .add_component(equity)
            .add_component(bonds)
            .add_component(commodities)
            .add_component(gold)
            .rebalance_frequency(RebalanceFreq::MONTHLY)
            .build();
    }
};

// ============================================================================
// Benchmark Registry (Thread-safe Catalog)
// ============================================================================

/**
 * @brief Thread-safe registry of named benchmarks
 */
class BenchmarkRegistry {
public:
    BenchmarkRegistry() = default;

    /**
     * @brief Register a benchmark instance
     */
    void add(const std::string& id, BenchmarkInstance benchmark) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        benchmarks_[id] = std::make_shared<BenchmarkInstance>(std::move(benchmark));
    }

    /**
     * @brief Retrieve a benchmark by ID
     */
    std::shared_ptr<BenchmarkInstance> get(const std::string& id) const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        auto it = benchmarks_.find(id);
        if (it == benchmarks_.end()) return nullptr;
        return it->second;
    }

    /**
     * @brief Remove a benchmark
     */
    bool remove(const std::string& id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        return benchmarks_.erase(id) > 0;
    }

    /**
     * @brief Check if benchmark exists
     */
    bool exists(const std::string& id) const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return benchmarks_.count(id) > 0;
    }

    /**
     * @brief List all benchmark IDs
     */
    std::vector<std::string> list() const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        std::vector<std::string> ids;
        for (const auto& [id, _] : benchmarks_) {
            ids.push_back(id);
        }
        return ids;
    }

    /**
     * @brief Number of registered benchmarks
     */
    int count() const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return static_cast<int>(benchmarks_.size());
    }

    /**
     * @brief Update returns for all benchmarks
     */
    void update_all(int64_t date,
                    const std::map<std::string, double>& all_returns) {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        for (auto& [id, bench] : benchmarks_) {
            bench->update_returns(date, all_returns);
        }
    }

    /**
     * @brief Get summary for all benchmarks
     */
    std::string summary_json() const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        std::ostringstream oss;
        oss << "{\n  \"benchmarks\": [\n";

        int i = 0;
        for (const auto& [id, bench] : benchmarks_) {
            if (i > 0) oss << ",\n";
            oss << "    " << bench->status_json();
            ++i;
        }

        oss << "\n  ],\n  \"count\": " << benchmarks_.size() << "\n}";
        return oss.str();
    }

    /**
     * @brief Initialize with common pre-built benchmarks
     */
    void load_defaults() {
        BenchmarkBuilder builder;
        auto b1 = builder.balanced_60_40();
        add(b1.id(), std::move(b1));
        auto b2 = builder.global_equity();
        add(b2.id(), std::move(b2));
        auto b3 = builder.us_sector_equal();
        add(b3.id(), std::move(b3));
        auto b4 = builder.risk_parity();
        add(b4.id(), std::move(b4));
    }

private:
    mutable std::shared_mutex mtx_;
    std::map<std::string, std::shared_ptr<BenchmarkInstance>> benchmarks_;
};

} // namespace analytics
} // namespace genie

#endif // GENIE_ANALYTICS_BENCHMARK_BUILDER_HPP
