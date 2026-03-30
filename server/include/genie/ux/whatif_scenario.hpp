/**
 * @file whatif_scenario.hpp
 * @brief What-If Scenario Builder Engine for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Interactive scenario analysis engine that enables users to model hypothetical
 * changes to portfolios, market conditions, and economic variables, then
 * instantly see the projected impact on risk, return, and compliance metrics.
 * Supports portfolio rebalancing previews, trade impact analysis, macro shock
 * scenarios, and multi-variable stress testing with real-time calculation.
 *
 * Features:
 *   - Portfolio what-if: add/remove/resize positions and preview impact
 *   - Trade impact analysis: P&L, risk delta, compliance check before execution
 *   - Market scenario shocks: equity, rates, FX, volatility, credit spreads
 *   - Macro scenarios: recession, inflation, rate hike, geopolitical events
 *   - Historical replay: apply past crisis patterns to current portfolio
 *   - Multi-variable sensitivity: grid analysis across 2+ dimensions
 *   - Factor exposure changes from hypothetical trades
 *   - Tax impact preview: capital gains, wash sale detection
 *   - Rebalancing preview: drift correction impact analysis
 *   - Scenario comparison: side-by-side results across multiple scenarios
 *   - Scenario templates: pre-built and custom scenario libraries
 *   - Undo/redo stack for iterative exploration
 *   - Snapshot and restore for scenario bookmarking
 *   - JSON serialization for scenario persistence
 *   - Thread-safe concurrent scenario evaluation
 *   - Real-time calculation with incremental updates
 *   - Zero external dependencies
 *
 * Architecture:
 *   ScenarioEngine owns:
 *     - ScenarioBuilder: fluent API for constructing scenarios
 *     - ImpactCalculator: computes risk/return/compliance deltas
 *     - HistoricalReplayEngine: applies historical crisis patterns
 *     - SensitivityGrid: multi-dimensional sensitivity analysis
 *     - ScenarioLibrary: pre-built and user scenario templates
 *     - UndoStack: history for iterative exploration
 *
 * @note Header-only. No external dependencies.
 */

#ifndef GENIE_UX_WHATIF_SCENARIO_HPP
#define GENIE_UX_WHATIF_SCENARIO_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <functional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <memory>
#include <variant>
#include <cassert>
#include <deque>

namespace genie {
namespace ux {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** Type of scenario modification */
enum class ScenarioAction {
    AddPosition,        ///< Add a new holding
    RemovePosition,     ///< Remove an existing holding
    ResizePosition,     ///< Change position size (shares or weight)
    SetWeight,          ///< Set target weight directly
    MarketShock,        ///< Apply market variable shock
    RateShock,          ///< Interest rate change
    FxShock,            ///< Currency rate change
    VolShock,           ///< Volatility change
    CreditShock,        ///< Credit spread change
    MacroScenario,      ///< Pre-defined macro scenario
    HistoricalReplay,   ///< Replay historical period
    CustomShock         ///< User-defined custom shock
};

/** Market variable to shock */
enum class MarketVariable {
    EquityLevel,
    InterestRate,
    CreditSpread,
    FxRate,
    Volatility,
    Inflation,
    OilPrice,
    GoldPrice,
    RealEstate,
    CommodityIndex,
    CorrelationShift
};

/** Pre-defined macro scenario templates */
enum class MacroTemplate {
    MildRecession,
    SevereRecession,
    Stagflation,
    RateHike100bps,
    RateHike200bps,
    RateCut100bps,
    DollarCrash,
    EmergingMarketCrisis,
    TechBubbleBurst,
    PandemicShock,
    GeopoliticalEscalation,
    DeflationarySpiral,
    Goldilocks,
    SupplyChainDisruption,
    EnergyShock
};

/** Historical crisis periods */
enum class HistoricalCrisis {
    GFC2008,            ///< Global Financial Crisis 2007-2009
    DotComBust,         ///< Dot-com bubble burst 2000-2002
    Covid2020,          ///< COVID-19 crash March 2020
    BlackMonday1987,    ///< October 19, 1987
    AsianCrisis1997,    ///< Asian financial crisis
    EuroDebtCrisis,     ///< European sovereign debt 2010-2012
    TaperTantrum2013,   ///< Fed taper tantrum
    VolmageddonFeb2018, ///< VIX spike February 2018
    CovidRecovery2020,  ///< V-shaped recovery April-Dec 2020
    RateShock2022       ///< 2022 rate hiking cycle
};

/** Impact metric type */
enum class ImpactMetric {
    ReturnImpact,
    VaRChange,
    MaxDrawdownChange,
    SharpeRatioChange,
    TrackingErrorChange,
    BetaChange,
    DurationChange,
    ConcentrationChange,
    ComplianceStatus,
    TaxImpact,
    TransactionCost
};

// ---------------------------------------------------------------------------
// Data Structures
// ---------------------------------------------------------------------------

/** Single portfolio position */
struct WhatIfPosition {
    std::string symbol;
    std::string name;
    std::string sector;
    double shares = 0.0;
    double price = 0.0;
    double market_value = 0.0;
    double weight = 0.0;            ///< Portfolio weight (0-1)
    double cost_basis = 0.0;
    double unrealized_gain = 0.0;
    double beta = 1.0;
    double volatility = 0.0;
};

/** A single scenario modification */
struct ScenarioModification {
    ScenarioAction action = ScenarioAction::AddPosition;
    std::string description;

    // For position changes
    std::string symbol;
    double target_shares = 0.0;
    double target_weight = 0.0;
    double target_value = 0.0;

    // For market shocks
    MarketVariable variable = MarketVariable::EquityLevel;
    double shock_magnitude = 0.0;   ///< e.g., -0.20 for -20% equity
    std::string shock_unit;         ///< "percent", "bps", "absolute"

    // For macro/historical
    MacroTemplate macro = MacroTemplate::MildRecession;
    HistoricalCrisis crisis = HistoricalCrisis::GFC2008;

    // Metadata
    std::chrono::system_clock::time_point created;
};

/** Impact assessment for a single metric */
struct MetricImpact {
    ImpactMetric metric;
    double before = 0.0;
    double after = 0.0;
    double delta = 0.0;
    double delta_pct = 0.0;
    std::string formatted;  ///< Human-readable representation
};

/** Complete impact analysis result */
struct ScenarioImpact {
    std::string scenario_id;
    std::string scenario_name;
    std::vector<ScenarioModification> modifications;

    // Portfolio level
    double portfolio_value_before = 0.0;
    double portfolio_value_after = 0.0;
    double portfolio_return = 0.0;
    double pnl = 0.0;

    // Risk metrics
    std::vector<MetricImpact> metrics;

    // Position-level impacts
    struct PositionImpact {
        std::string symbol;
        double weight_before = 0.0;
        double weight_after = 0.0;
        double value_before = 0.0;
        double value_after = 0.0;
        double pnl = 0.0;
        double contribution_to_return = 0.0;
    };
    std::vector<PositionImpact> position_impacts;

    // Compliance
    bool compliance_passed = true;
    std::vector<std::string> compliance_violations;

    // Tax
    double estimated_tax_impact = 0.0;
    double short_term_gains = 0.0;
    double long_term_gains = 0.0;
    bool has_wash_sales = false;

    // Transaction costs
    double estimated_commission = 0.0;
    double estimated_spread_cost = 0.0;
    double estimated_market_impact = 0.0;

    // Sector allocation changes
    std::map<std::string, std::pair<double, double>> sector_weights;

    // Timestamp
    std::chrono::system_clock::time_point calculated_at;
};

/** Sensitivity grid point */
struct SensitivityPoint {
    double x_value = 0.0;
    double y_value = 0.0;
    double result = 0.0;
    std::string x_label;
    std::string y_label;
};

/** Sensitivity analysis result */
struct SensitivityResult {
    std::string x_variable;
    std::string y_variable;
    std::string result_metric;
    std::vector<double> x_range;
    std::vector<double> y_range;
    std::vector<std::vector<double>> grid;  ///< [x][y] = result
    double min_result = 0.0;
    double max_result = 0.0;
};

/** Scenario snapshot for bookmarking */
struct ScenarioSnapshot {
    std::string id;
    std::string name;
    std::string description;
    std::vector<WhatIfPosition> positions;
    std::vector<ScenarioModification> modifications;
    ScenarioImpact impact;
    std::chrono::system_clock::time_point saved_at;
};

/** Scenario comparison entry */
struct ScenarioComparison {
    std::vector<std::string> scenario_names;
    std::map<ImpactMetric, std::vector<double>> metric_comparison;
    std::map<std::string, std::vector<double>> position_pnl;
    std::vector<double> portfolio_returns;
};

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------

inline std::string action_to_string(ScenarioAction a) {
    switch (a) {
        case ScenarioAction::AddPosition:     return "Add Position";
        case ScenarioAction::RemovePosition:  return "Remove Position";
        case ScenarioAction::ResizePosition:  return "Resize Position";
        case ScenarioAction::SetWeight:       return "Set Weight";
        case ScenarioAction::MarketShock:     return "Market Shock";
        case ScenarioAction::RateShock:       return "Rate Shock";
        case ScenarioAction::FxShock:         return "FX Shock";
        case ScenarioAction::VolShock:        return "Volatility Shock";
        case ScenarioAction::CreditShock:     return "Credit Shock";
        case ScenarioAction::MacroScenario:   return "Macro Scenario";
        case ScenarioAction::HistoricalReplay:return "Historical Replay";
        case ScenarioAction::CustomShock:     return "Custom Shock";
    }
    return "Unknown";
}

inline std::string macro_to_string(MacroTemplate m) {
    switch (m) {
        case MacroTemplate::MildRecession:          return "Mild Recession";
        case MacroTemplate::SevereRecession:        return "Severe Recession";
        case MacroTemplate::Stagflation:            return "Stagflation";
        case MacroTemplate::RateHike100bps:         return "Rate Hike +100bps";
        case MacroTemplate::RateHike200bps:         return "Rate Hike +200bps";
        case MacroTemplate::RateCut100bps:          return "Rate Cut -100bps";
        case MacroTemplate::DollarCrash:            return "Dollar Crash";
        case MacroTemplate::EmergingMarketCrisis:   return "EM Crisis";
        case MacroTemplate::TechBubbleBurst:        return "Tech Bubble Burst";
        case MacroTemplate::PandemicShock:          return "Pandemic Shock";
        case MacroTemplate::GeopoliticalEscalation: return "Geopolitical Escalation";
        case MacroTemplate::DeflationarySpiral:     return "Deflationary Spiral";
        case MacroTemplate::Goldilocks:             return "Goldilocks";
        case MacroTemplate::SupplyChainDisruption:  return "Supply Chain Disruption";
        case MacroTemplate::EnergyShock:            return "Energy Shock";
    }
    return "Unknown";
}

inline std::string crisis_to_string(HistoricalCrisis c) {
    switch (c) {
        case HistoricalCrisis::GFC2008:             return "GFC 2008";
        case HistoricalCrisis::DotComBust:          return "Dot-Com Bust";
        case HistoricalCrisis::Covid2020:           return "COVID-19 Crash";
        case HistoricalCrisis::BlackMonday1987:     return "Black Monday 1987";
        case HistoricalCrisis::AsianCrisis1997:     return "Asian Crisis 1997";
        case HistoricalCrisis::EuroDebtCrisis:      return "Euro Debt Crisis";
        case HistoricalCrisis::TaperTantrum2013:    return "Taper Tantrum 2013";
        case HistoricalCrisis::VolmageddonFeb2018:  return "Volmageddon Feb 2018";
        case HistoricalCrisis::CovidRecovery2020:   return "COVID Recovery";
        case HistoricalCrisis::RateShock2022:       return "Rate Shock 2022";
    }
    return "Unknown";
}

inline std::string metric_to_string(ImpactMetric m) {
    switch (m) {
        case ImpactMetric::ReturnImpact:        return "Return Impact";
        case ImpactMetric::VaRChange:           return "VaR Change";
        case ImpactMetric::MaxDrawdownChange:   return "Max Drawdown Change";
        case ImpactMetric::SharpeRatioChange:   return "Sharpe Ratio Change";
        case ImpactMetric::TrackingErrorChange: return "Tracking Error Change";
        case ImpactMetric::BetaChange:          return "Beta Change";
        case ImpactMetric::DurationChange:      return "Duration Change";
        case ImpactMetric::ConcentrationChange: return "Concentration Change";
        case ImpactMetric::ComplianceStatus:    return "Compliance Status";
        case ImpactMetric::TaxImpact:           return "Tax Impact";
        case ImpactMetric::TransactionCost:     return "Transaction Cost";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// MacroShockModel
// ---------------------------------------------------------------------------

/**
 * Pre-defined macro scenario shock parameters.
 * Maps macro templates to specific market variable shocks.
 */
class MacroShockModel {
public:
    struct ShockSet {
        double equity_shock = 0.0;      ///< % change in equity markets
        double rate_shock_bps = 0.0;    ///< bps change in rates
        double credit_shock_bps = 0.0;  ///< bps change in credit spreads
        double fx_shock = 0.0;          ///< % change in USD (positive = stronger)
        double vol_shock = 0.0;         ///< Absolute VIX change
        double oil_shock = 0.0;         ///< % change in oil
    };

    /** Get shock parameters for a macro template */
    [[nodiscard]] static ShockSet get_shocks(MacroTemplate m) {
        switch (m) {
            case MacroTemplate::MildRecession:
                return {-0.15, -0.50, 1.50, 0.02, 8.0, -0.20};
            case MacroTemplate::SevereRecession:
                return {-0.40, -1.50, 3.50, 0.05, 25.0, -0.40};
            case MacroTemplate::Stagflation:
                return {-0.20, 1.00, 2.00, -0.05, 12.0, 0.30};
            case MacroTemplate::RateHike100bps:
                return {-0.05, 1.00, 0.25, 0.03, 3.0, 0.0};
            case MacroTemplate::RateHike200bps:
                return {-0.12, 2.00, 0.50, 0.05, 5.0, 0.0};
            case MacroTemplate::RateCut100bps:
                return {0.08, -1.00, -0.15, -0.02, -2.0, 0.0};
            case MacroTemplate::DollarCrash:
                return {0.05, -0.25, 0.50, -0.15, 5.0, 0.15};
            case MacroTemplate::EmergingMarketCrisis:
                return {-0.10, 0.25, 2.00, 0.08, 10.0, -0.10};
            case MacroTemplate::TechBubbleBurst:
                return {-0.35, -0.50, 1.00, 0.0, 18.0, -0.15};
            case MacroTemplate::PandemicShock:
                return {-0.30, -1.00, 3.00, 0.03, 40.0, -0.50};
            case MacroTemplate::GeopoliticalEscalation:
                return {-0.10, 0.0, 1.00, 0.03, 8.0, 0.25};
            case MacroTemplate::DeflationarySpiral:
                return {-0.25, -2.00, 2.50, 0.10, 15.0, -0.30};
            case MacroTemplate::Goldilocks:
                return {0.15, 0.0, -0.25, 0.0, -3.0, 0.05};
            case MacroTemplate::SupplyChainDisruption:
                return {-0.08, 0.50, 0.75, 0.0, 5.0, 0.20};
            case MacroTemplate::EnergyShock:
                return {-0.12, 0.25, 0.50, 0.02, 6.0, 0.50};
        }
        return {};
    }
};

// ---------------------------------------------------------------------------
// HistoricalReplayEngine
// ---------------------------------------------------------------------------

/**
 * Replays historical crisis patterns on current portfolio.
 * Maps past sector/factor returns to current holdings.
 */
class HistoricalReplayEngine {
public:
    struct CrisisReturn {
        double sp500 = 0.0;
        double nasdaq = 0.0;
        double bonds_10y = 0.0;
        double high_yield = 0.0;
        double gold = 0.0;
        double oil = 0.0;
        double reit = 0.0;
        double emerging = 0.0;
        int duration_days = 0;
        std::map<std::string, double> sector_returns;  ///< GICS sector returns
    };

    /** Get historical crisis returns */
    [[nodiscard]] static CrisisReturn get_crisis(HistoricalCrisis crisis) {
        switch (crisis) {
            case HistoricalCrisis::GFC2008:
                return {-0.57, -0.56, 0.20, -0.26, 0.25, -0.56, -0.68, -0.53, 510,
                    {{"Technology", -0.53}, {"Financials", -0.83}, {"Healthcare", -0.39},
                     {"Energy", -0.54}, {"Consumer Discretionary", -0.59},
                     {"Utilities", -0.29}, {"Real Estate", -0.68}}};
            case HistoricalCrisis::DotComBust:
                return {-0.49, -0.78, 0.37, -0.05, 0.12, 0.0, 0.20, -0.45, 730,
                    {{"Technology", -0.82}, {"Financials", -0.20}, {"Healthcare", -0.35},
                     {"Energy", 0.10}, {"Utilities", -0.30}}};
            case HistoricalCrisis::Covid2020:
                return {-0.34, -0.30, 0.08, -0.12, 0.03, -0.65, -0.28, -0.32, 33,
                    {{"Technology", -0.26}, {"Financials", -0.43}, {"Healthcare", -0.25},
                     {"Energy", -0.62}, {"Consumer Discretionary", -0.38},
                     {"Airlines", -0.55}, {"Hotels", -0.50}}};
            case HistoricalCrisis::BlackMonday1987:
                return {-0.34, -0.33, 0.05, -0.05, 0.04, -0.10, -0.15, -0.20, 1,
                    {{"Technology", -0.35}, {"Financials", -0.33}}};
            case HistoricalCrisis::RateShock2022:
                return {-0.25, -0.33, -0.17, -0.11, -0.01, 0.05, -0.26, -0.20, 280,
                    {{"Technology", -0.33}, {"Financials", -0.12}, {"Healthcare", -0.05},
                     {"Energy", 0.59}, {"Utilities", 0.01}, {"Real Estate", -0.26}}};
            default:
                return {-0.15, -0.15, 0.05, -0.05, 0.02, -0.10, -0.10, -0.15, 60, {}};
        }
    }

    /** Apply crisis returns to a portfolio */
    [[nodiscard]] static double estimate_portfolio_return(
            const std::vector<WhatIfPosition>& positions,
            HistoricalCrisis crisis) {
        auto cr = get_crisis(crisis);
        double portfolio_return = 0.0;

        for (const auto& pos : positions) {
            double pos_return = cr.sp500;  // Default to broad market

            // Try sector-specific return
            auto sit = cr.sector_returns.find(pos.sector);
            if (sit != cr.sector_returns.end()) {
                pos_return = sit->second;
            }

            // Beta-adjust
            pos_return *= pos.beta;

            portfolio_return += pos.weight * pos_return;
        }

        return portfolio_return;
    }
};

// ---------------------------------------------------------------------------
// UndoStack
// ---------------------------------------------------------------------------

/**
 * Undo/redo stack for scenario exploration.
 */
template<typename T>
class UndoStack {
public:
    explicit UndoStack(size_t max_depth = 50) : max_depth_(max_depth) {}

    /** Push a new state */
    void push(const T& state) {
        // Clear redo stack
        redo_stack_.clear();

        undo_stack_.push_back(state);
        if (undo_stack_.size() > max_depth_) {
            undo_stack_.pop_front();
        }
    }

    /** Undo: pop last state and push to redo */
    [[nodiscard]] std::optional<T> undo() {
        if (undo_stack_.empty()) return std::nullopt;
        auto state = undo_stack_.back();
        undo_stack_.pop_back();
        redo_stack_.push_back(state);
        return state;
    }

    /** Redo: pop from redo and push to undo */
    [[nodiscard]] std::optional<T> redo() {
        if (redo_stack_.empty()) return std::nullopt;
        auto state = redo_stack_.back();
        redo_stack_.pop_back();
        undo_stack_.push_back(state);
        return state;
    }

    [[nodiscard]] bool can_undo() const { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_stack_.empty(); }
    [[nodiscard]] size_t undo_depth() const { return undo_stack_.size(); }

    void clear() {
        undo_stack_.clear();
        redo_stack_.clear();
    }

private:
    size_t max_depth_;
    std::deque<T> undo_stack_;
    std::vector<T> redo_stack_;
};

// ---------------------------------------------------------------------------
// ScenarioEngine -- Main Engine
// ---------------------------------------------------------------------------

/**
 * Central what-if scenario analysis engine.
 * Builds scenarios, calculates impacts, and supports iterative exploration.
 */
class ScenarioEngine {
public:
    ScenarioEngine() : scenario_counter_(0) {}

    // -- Portfolio Setup --

    /** Set the base portfolio for what-if analysis */
    void set_base_portfolio(const std::vector<WhatIfPosition>& positions) {
        std::lock_guard<std::mutex> lock(mutex_);
        base_positions_ = positions;
        working_positions_ = positions;
        recalculate_weights();
        undo_stack_.clear();
    }

    /** Get current working positions */
    [[nodiscard]] std::vector<WhatIfPosition> working_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return working_positions_;
    }

    /** Get base (original) positions */
    [[nodiscard]] std::vector<WhatIfPosition> base_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return base_positions_;
    }

    // -- Scenario Modifications --

    /** Add a position to the working portfolio */
    bool add_position(const WhatIfPosition& pos) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        // Check if position already exists
        for (auto& p : working_positions_) {
            if (p.symbol == pos.symbol) {
                p.shares += pos.shares;
                p.market_value = p.shares * p.price;
                recalculate_weights();

                ScenarioModification mod;
                mod.action = ScenarioAction::ResizePosition;
                mod.symbol = pos.symbol;
                mod.target_shares = p.shares;
                mod.created = std::chrono::system_clock::now();
                modifications_.push_back(mod);
                return true;
            }
        }

        working_positions_.push_back(pos);
        working_positions_.back().market_value =
            working_positions_.back().shares * working_positions_.back().price;
        recalculate_weights();

        ScenarioModification mod;
        mod.action = ScenarioAction::AddPosition;
        mod.symbol = pos.symbol;
        mod.target_shares = pos.shares;
        mod.created = std::chrono::system_clock::now();
        modifications_.push_back(mod);
        return true;
    }

    /** Remove a position from working portfolio */
    bool remove_position(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        auto it = std::remove_if(working_positions_.begin(), working_positions_.end(),
            [&](const WhatIfPosition& p) { return p.symbol == symbol; });

        if (it == working_positions_.end()) return false;
        working_positions_.erase(it, working_positions_.end());
        recalculate_weights();

        ScenarioModification mod;
        mod.action = ScenarioAction::RemovePosition;
        mod.symbol = symbol;
        mod.created = std::chrono::system_clock::now();
        modifications_.push_back(mod);
        return true;
    }

    /** Resize a position */
    bool resize_position(const std::string& symbol, double new_shares) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        for (auto& p : working_positions_) {
            if (p.symbol == symbol) {
                p.shares = new_shares;
                p.market_value = p.shares * p.price;
                recalculate_weights();

                ScenarioModification mod;
                mod.action = ScenarioAction::ResizePosition;
                mod.symbol = symbol;
                mod.target_shares = new_shares;
                mod.created = std::chrono::system_clock::now();
                modifications_.push_back(mod);
                return true;
            }
        }
        return false;
    }

    /** Set target weight for a position */
    bool set_target_weight(const std::string& symbol, double weight) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        double total_value = calculate_value(working_positions_);
        double target_value = total_value * weight;

        for (auto& p : working_positions_) {
            if (p.symbol == symbol && p.price > 0) {
                p.shares = target_value / p.price;
                p.market_value = target_value;
                recalculate_weights();

                ScenarioModification mod;
                mod.action = ScenarioAction::SetWeight;
                mod.symbol = symbol;
                mod.target_weight = weight;
                mod.created = std::chrono::system_clock::now();
                modifications_.push_back(mod);
                return true;
            }
        }
        return false;
    }

    /** Apply a market shock */
    void apply_market_shock(MarketVariable variable, double magnitude) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        for (auto& p : working_positions_) {
            double impact = calculate_position_shock(p, variable, magnitude);
            p.price *= (1.0 + impact);
            p.market_value = p.shares * p.price;
        }
        recalculate_weights();

        ScenarioModification mod;
        mod.action = ScenarioAction::MarketShock;
        mod.variable = variable;
        mod.shock_magnitude = magnitude;
        mod.created = std::chrono::system_clock::now();
        modifications_.push_back(mod);
    }

    /** Apply a macro scenario template */
    void apply_macro_scenario(MacroTemplate macro) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        auto shocks = MacroShockModel::get_shocks(macro);

        for (auto& p : working_positions_) {
            // Apply equity shock with beta adjustment
            double equity_impact = shocks.equity_shock * p.beta;

            // Sector-specific adjustments
            if (p.sector == "Technology" &&
                (macro == MacroTemplate::TechBubbleBurst ||
                 macro == MacroTemplate::RateHike200bps)) {
                equity_impact *= 1.5;
            } else if (p.sector == "Utilities" &&
                       macro == MacroTemplate::RateHike100bps) {
                equity_impact *= 1.3;
            } else if (p.sector == "Energy" &&
                       macro == MacroTemplate::EnergyShock) {
                equity_impact *= 0.5;  // Energy benefits from energy shock
            }

            p.price *= (1.0 + equity_impact);
            p.market_value = p.shares * p.price;
        }
        recalculate_weights();

        ScenarioModification mod;
        mod.action = ScenarioAction::MacroScenario;
        mod.macro = macro;
        mod.description = macro_to_string(macro);
        mod.created = std::chrono::system_clock::now();
        modifications_.push_back(mod);
    }

    /** Apply historical crisis replay */
    void apply_historical_replay(HistoricalCrisis crisis) {
        std::lock_guard<std::mutex> lock(mutex_);
        save_undo_state();

        auto cr = HistoricalReplayEngine::get_crisis(crisis);

        for (auto& p : working_positions_) {
            double return_val = cr.sp500 * p.beta;  // Default

            // Use sector-specific if available
            auto sit = cr.sector_returns.find(p.sector);
            if (sit != cr.sector_returns.end()) {
                return_val = sit->second * p.beta;
            }

            p.price *= (1.0 + return_val);
            p.market_value = p.shares * p.price;
        }
        recalculate_weights();

        ScenarioModification mod;
        mod.action = ScenarioAction::HistoricalReplay;
        mod.crisis = crisis;
        mod.description = crisis_to_string(crisis);
        mod.created = std::chrono::system_clock::now();
        modifications_.push_back(mod);
    }

    // -- Impact Analysis --

    /** Calculate full impact analysis */
    [[nodiscard]] ScenarioImpact calculate_impact() const {
        std::lock_guard<std::mutex> lock(mutex_);
        ScenarioImpact impact;
        impact.scenario_id = "SCEN" + std::to_string(scenario_counter_);
        impact.modifications = modifications_;
        impact.calculated_at = std::chrono::system_clock::now();

        // Portfolio values
        impact.portfolio_value_before = calculate_value(base_positions_);
        impact.portfolio_value_after = calculate_value(working_positions_);
        impact.pnl = impact.portfolio_value_after - impact.portfolio_value_before;
        impact.portfolio_return = (impact.portfolio_value_before > 0) ?
            impact.pnl / impact.portfolio_value_before : 0.0;

        // Position impacts
        for (const auto& base_pos : base_positions_) {
            ScenarioImpact::PositionImpact pi;
            pi.symbol = base_pos.symbol;
            pi.weight_before = base_pos.weight;
            pi.value_before = base_pos.market_value;

            // Find in working portfolio
            auto wit = std::find_if(working_positions_.begin(), working_positions_.end(),
                [&](const WhatIfPosition& p) { return p.symbol == base_pos.symbol; });

            if (wit != working_positions_.end()) {
                pi.weight_after = wit->weight;
                pi.value_after = wit->market_value;
            }
            pi.pnl = pi.value_after - pi.value_before;
            pi.contribution_to_return = (impact.portfolio_value_before > 0) ?
                pi.pnl / impact.portfolio_value_before : 0.0;

            impact.position_impacts.push_back(pi);
        }

        // Risk metrics
        impact.metrics.push_back(calc_metric_impact(
            ImpactMetric::ReturnImpact, 0.0, impact.portfolio_return));
        impact.metrics.push_back(calc_metric_impact(
            ImpactMetric::BetaChange, portfolio_beta(base_positions_),
            portfolio_beta(working_positions_)));
        impact.metrics.push_back(calc_metric_impact(
            ImpactMetric::ConcentrationChange,
            max_weight(base_positions_), max_weight(working_positions_)));

        // VaR estimate (simplified)
        double base_vol = portfolio_volatility(base_positions_);
        double new_vol = portfolio_volatility(working_positions_);
        double base_var = -1.645 * base_vol * std::sqrt(10.0/252.0) *
            impact.portfolio_value_before;
        double new_var = -1.645 * new_vol * std::sqrt(10.0/252.0) *
            impact.portfolio_value_after;
        impact.metrics.push_back(calc_metric_impact(
            ImpactMetric::VaRChange, base_var, new_var));

        // Sector weights
        auto base_sectors = sector_weights(base_positions_);
        auto new_sectors = sector_weights(working_positions_);
        std::set<std::string> all_sectors;
        for (auto& [s, _] : base_sectors) all_sectors.insert(s);
        for (auto& [s, _] : new_sectors) all_sectors.insert(s);
        for (const auto& s : all_sectors) {
            impact.sector_weights[s] = {
                base_sectors.count(s) ? base_sectors[s] : 0.0,
                new_sectors.count(s) ? new_sectors[s] : 0.0
            };
        }

        // Compliance check (simplified)
        double max_w = max_weight(working_positions_);
        if (max_w > 0.25) {
            impact.compliance_passed = false;
            impact.compliance_violations.push_back(
                "Single position exceeds 25% concentration limit");
        }

        // Tax impact estimate
        for (const auto& pi : impact.position_impacts) {
            if (pi.pnl > 0) {
                impact.long_term_gains += pi.pnl * 0.7;  // Assume 70% long-term
                impact.short_term_gains += pi.pnl * 0.3;
            }
        }
        impact.estimated_tax_impact =
            impact.long_term_gains * 0.15 + impact.short_term_gains * 0.37;

        // Transaction costs
        for (const auto& pi : impact.position_impacts) {
            double trade_value = std::abs(pi.value_after - pi.value_before);
            impact.estimated_commission += trade_value * 0.0001;  // 1 bps
            impact.estimated_spread_cost += trade_value * 0.0005; // 5 bps
        }

        return impact;
    }

    // -- Sensitivity Analysis --

    /** Run 2D sensitivity grid analysis */
    [[nodiscard]] SensitivityResult sensitivity_grid(
            MarketVariable x_var, double x_min, double x_max, int x_steps,
            MarketVariable y_var, double y_min, double y_max, int y_steps) const {
        std::lock_guard<std::mutex> lock(mutex_);
        SensitivityResult result;
        result.x_variable = std::to_string(static_cast<int>(x_var));
        result.y_variable = std::to_string(static_cast<int>(y_var));
        result.result_metric = "Portfolio Return";

        // Build ranges
        for (int i = 0; i <= x_steps; ++i) {
            result.x_range.push_back(x_min + (x_max - x_min) * i / x_steps);
        }
        for (int j = 0; j <= y_steps; ++j) {
            result.y_range.push_back(y_min + (y_max - y_min) * j / y_steps);
        }

        result.min_result = 1e10;
        result.max_result = -1e10;

        result.grid.resize(result.x_range.size());
        for (size_t i = 0; i < result.x_range.size(); ++i) {
            result.grid[i].resize(result.y_range.size());
            for (size_t j = 0; j < result.y_range.size(); ++j) {
                // Apply both shocks and calculate return
                double total_return = 0.0;
                for (const auto& p : base_positions_) {
                    double x_impact = calculate_position_shock(p, x_var, result.x_range[i]);
                    double y_impact = calculate_position_shock(p, y_var, result.y_range[j]);
                    double combined = x_impact + y_impact -
                        x_impact * y_impact;  // Avoid double-counting
                    total_return += p.weight * combined;
                }
                result.grid[i][j] = total_return;
                result.min_result = std::min(result.min_result, total_return);
                result.max_result = std::max(result.max_result, total_return);
            }
        }

        return result;
    }

    // -- Undo/Redo --

    /** Undo last modification */
    bool undo() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state = undo_stack_.undo();
        if (!state) return false;
        working_positions_ = *state;
        if (!modifications_.empty()) modifications_.pop_back();
        return true;
    }

    /** Redo last undone modification */
    bool redo() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto state = undo_stack_.redo();
        if (!state) return false;
        working_positions_ = *state;
        return true;
    }

    /** Reset to base portfolio */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        working_positions_ = base_positions_;
        modifications_.clear();
        undo_stack_.clear();
    }

    [[nodiscard]] bool can_undo() const { return undo_stack_.can_undo(); }
    [[nodiscard]] bool can_redo() const { return undo_stack_.can_redo(); }

    // -- Snapshots --

    /** Save current state as a named snapshot */
    std::string save_snapshot(const std::string& name,
                               const std::string& description = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        ScenarioSnapshot snap;
        snap.id = "SNAP" + std::to_string(++scenario_counter_);
        snap.name = name;
        snap.description = description;
        snap.positions = working_positions_;
        snap.modifications = modifications_;
        snap.saved_at = std::chrono::system_clock::now();

        snapshots_[snap.id] = snap;
        return snap.id;
    }

    /** Load a saved snapshot */
    bool load_snapshot(const std::string& snapshot_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = snapshots_.find(snapshot_id);
        if (it == snapshots_.end()) return false;

        save_undo_state();
        working_positions_ = it->second.positions;
        modifications_ = it->second.modifications;
        return true;
    }

    /** List all snapshots */
    [[nodiscard]] std::vector<ScenarioSnapshot> list_snapshots() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ScenarioSnapshot> result;
        for (const auto& [id, snap] : snapshots_) {
            result.push_back(snap);
        }
        return result;
    }

    // -- Comparison --

    /** Compare multiple snapshots */
    [[nodiscard]] ScenarioComparison compare_snapshots(
            const std::vector<std::string>& snapshot_ids) const {
        std::lock_guard<std::mutex> lock(mutex_);
        ScenarioComparison comp;

        for (const auto& id : snapshot_ids) {
            auto it = snapshots_.find(id);
            if (it == snapshots_.end()) continue;

            comp.scenario_names.push_back(it->second.name);

            double base_val = calculate_value(base_positions_);
            double snap_val = calculate_value(it->second.positions);
            double ret = (base_val > 0) ? (snap_val - base_val) / base_val : 0.0;
            comp.portfolio_returns.push_back(ret);
        }

        return comp;
    }

    // -- Reporting --

    /** Generate text impact report */
    [[nodiscard]] std::string format_impact(const ScenarioImpact& impact) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        ss << "=== What-If Scenario Impact ===\n";
        ss << "Modifications: " << impact.modifications.size() << "\n\n";

        ss << "--- Portfolio Impact ---\n";
        ss << "Value Before: $" << impact.portfolio_value_before / 1e6 << "M\n";
        ss << "Value After:  $" << impact.portfolio_value_after / 1e6 << "M\n";
        ss << "P&L:          $" << impact.pnl / 1e6 << "M ("
           << impact.portfolio_return * 100.0 << "%)\n\n";

        ss << "--- Risk Metrics ---\n";
        for (const auto& m : impact.metrics) {
            ss << "  " << metric_to_string(m.metric) << ": "
               << m.before << " -> " << m.after
               << " (delta: " << m.delta << ")\n";
        }

        if (!impact.compliance_violations.empty()) {
            ss << "\n--- Compliance Violations ---\n";
            for (const auto& v : impact.compliance_violations) {
                ss << "  [!] " << v << "\n";
            }
        }

        ss << "\n--- Costs ---\n";
        ss << "Est. Commission: $" << impact.estimated_commission << "\n";
        ss << "Est. Spread Cost: $" << impact.estimated_spread_cost << "\n";
        ss << "Est. Tax Impact: $" << impact.estimated_tax_impact << "\n";

        ss << "\n--- Top Position Impacts ---\n";
        auto sorted = impact.position_impacts;
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) {
                return std::abs(a.pnl) > std::abs(b.pnl);
            });
        for (size_t i = 0; i < std::min(size_t(10), sorted.size()); ++i) {
            ss << "  " << sorted[i].symbol << ": $" << sorted[i].pnl
               << " (w: " << sorted[i].weight_before * 100 << "% -> "
               << sorted[i].weight_after * 100 << "%)\n";
        }

        return ss.str();
    }

    /** Get modification history */
    [[nodiscard]] std::vector<ScenarioModification> modifications() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return modifications_;
    }

private:
    void recalculate_weights() {
        double total = 0.0;
        for (const auto& p : working_positions_) {
            total += p.market_value;
        }
        for (auto& p : working_positions_) {
            p.weight = (total > 0) ? p.market_value / total : 0.0;
        }
    }

    void save_undo_state() {
        undo_stack_.push(working_positions_);
    }

    static double calculate_value(const std::vector<WhatIfPosition>& positions) {
        double total = 0.0;
        for (const auto& p : positions) total += p.market_value;
        return total;
    }

    static double portfolio_beta(const std::vector<WhatIfPosition>& positions) {
        double beta = 0.0;
        for (const auto& p : positions) beta += p.weight * p.beta;
        return beta;
    }

    static double portfolio_volatility(const std::vector<WhatIfPosition>& positions) {
        double vol = 0.0;
        for (const auto& p : positions) {
            vol += p.weight * p.weight * p.volatility * p.volatility;
        }
        return std::sqrt(vol);
    }

    static double max_weight(const std::vector<WhatIfPosition>& positions) {
        double max_w = 0.0;
        for (const auto& p : positions) max_w = std::max(max_w, p.weight);
        return max_w;
    }

    static std::map<std::string, double> sector_weights(
            const std::vector<WhatIfPosition>& positions) {
        std::map<std::string, double> result;
        for (const auto& p : positions) {
            if (!p.sector.empty()) result[p.sector] += p.weight;
        }
        return result;
    }

    static double calculate_position_shock(const WhatIfPosition& pos,
                                            MarketVariable var, double magnitude) {
        switch (var) {
            case MarketVariable::EquityLevel:
                return magnitude * pos.beta;
            case MarketVariable::InterestRate:
                // Duration-sensitive: utilities, REITs, bonds affected more
                if (pos.sector == "Utilities" || pos.sector == "Real Estate")
                    return -magnitude * 0.05;
                return -magnitude * 0.02;
            case MarketVariable::Volatility:
                return -std::abs(magnitude) * 0.01 * pos.beta;
            case MarketVariable::FxRate:
                return magnitude * 0.3;  // Simplified FX sensitivity
            case MarketVariable::CreditSpread:
                return -magnitude * 0.003;
            default:
                return magnitude * 0.5 * pos.beta;
        }
    }

    static MetricImpact calc_metric_impact(ImpactMetric metric,
                                            double before, double after) {
        MetricImpact mi;
        mi.metric = metric;
        mi.before = before;
        mi.after = after;
        mi.delta = after - before;
        mi.delta_pct = (before != 0) ? mi.delta / std::abs(before) * 100.0 : 0.0;
        return mi;
    }

    mutable std::mutex mutex_;
    std::vector<WhatIfPosition> base_positions_;
    std::vector<WhatIfPosition> working_positions_;
    std::vector<ScenarioModification> modifications_;
    UndoStack<std::vector<WhatIfPosition>> undo_stack_;
    std::unordered_map<std::string, ScenarioSnapshot> snapshots_;
    int scenario_counter_;
};

} // namespace ux
} // namespace genie

#endif // GENIE_UX_WHATIF_SCENARIO_HPP
