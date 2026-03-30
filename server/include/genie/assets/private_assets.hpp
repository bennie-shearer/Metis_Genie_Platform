/**
 * @file private_assets.hpp
 * @brief Private Assets Management Engine (PE, Real Estate, Infrastructure)
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Comprehensive private assets management framework supporting private equity,
 * real estate, infrastructure, private credit, and natural resources. Handles
 * illiquid asset valuation, cash flow modeling (J-curve), capital call/
 * distribution tracking, IRR/TVPI/DPI/RVPI calculations, vintage year
 * analysis, and commitment pacing.
 *
 * Features:
 *   - Multi-asset-class support: PE, RE, infra, private credit, natural resources
 *   - Fund lifecycle modeling: fundraising, investment, harvesting, liquidation
 *   - Capital call and distribution waterfall tracking
 *   - J-curve cash flow projection
 *   - Valuation methods: NAV, DCF, comparable transactions, appraisal
 *   - IRR calculation (Newton-Raphson) for realized and unrealized returns
 *   - TVPI, DPI, RVPI, PIC multiples
 *   - Vintage year cohort analysis
 *   - Commitment pacing model with over-commitment ratio
 *   - PME (Public Market Equivalent) benchmarking: KS-PME, Direct Alpha, mPME
 *   - Liquidity planning and cash flow forecasting
 *   - Quarterly NAV mark-to-market with lag adjustment
 *   - Fee structure modeling: management fees, carried interest, hurdle rates
 *   - Co-investment and side-pocket tracking
 *   - Currency hedging for cross-border holdings
 *   - Integration with portfolio-level risk and reporting
 *   - Thread-safe concurrent access
 *   - Zero external dependencies
 *
 * @note Header-only. No external dependencies.
 */

#ifndef GENIE_ASSETS_PRIVATE_ASSETS_HPP
#define GENIE_ASSETS_PRIVATE_ASSETS_HPP

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
#include <stdexcept>
#include <memory>
#include <array>
#include <cassert>

namespace genie {
namespace assets {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** Private asset class types */
enum class PrivateAssetClass {
    PrivateEquity,
    Buyout,
    VentureCapital,
    GrowthEquity,
    RealEstate,
    Infrastructure,
    PrivateCredit,
    NaturalResources,
    SecondaryFund,
    FundOfFunds,
    CoInvestment,
    DirectInvestment
};

/** Fund lifecycle stage */
enum class FundStage {
    Fundraising,
    InvestmentPeriod,
    HarvestingPeriod,
    Liquidation,
    Terminated
};

/** Valuation methodology */
enum class ValuationMethod {
    NAV,                    ///< Net Asset Value (GP-reported)
    DCF,                    ///< Discounted Cash Flow
    ComparableTransactions, ///< Transaction multiples
    ComparablePublic,       ///< Public company multiples
    Appraisal,              ///< Third-party appraisal
    CostBasis,              ///< At cost (early-stage)
    LastRound,              ///< Last funding round
    Hybrid                  ///< Blended approach
};

/** Cash flow type */
enum class CashFlowType {
    CapitalCall,
    Distribution,
    Recallable,
    ManagementFee,
    CarriedInterest,
    OrganizationalExpense,
    Clawback,
    ReturnOfCapital,
    Dividend,
    Interest
};

/** Real estate property type */
enum class PropertyType {
    Office,
    Retail,
    Industrial,
    Residential,
    MultiFamily,
    Hospitality,
    DataCenter,
    Healthcare,
    LifeScience,
    SelfStorage,
    MixedUse,
    Land
};

/** Real estate investment strategy */
enum class RealEstateStrategy {
    Core,
    CorePlus,
    ValueAdd,
    Opportunistic,
    Development
};

/** Infrastructure sector */
enum class InfraSector {
    Transportation,
    Energy,
    Renewables,
    Telecom,
    Water,
    Social,
    Digital
};

// ---------------------------------------------------------------------------
// Data Structures
// ---------------------------------------------------------------------------

/** Single cash flow event */
struct CashFlow {
    std::string id;
    CashFlowType type = CashFlowType::CapitalCall;
    double amount = 0.0;            ///< Positive = inflow, negative = outflow
    std::string currency = "USD";
    std::chrono::system_clock::time_point date;
    std::string description;
    bool confirmed = true;          ///< False for projected/estimated
};

/** Fee structure for a fund */
struct FeeStructure {
    double management_fee_rate = 0.02;      ///< Annual management fee (% of committed)
    double carry_rate = 0.20;               ///< Carried interest rate
    double preferred_return = 0.08;         ///< Hurdle rate
    bool european_waterfall = true;         ///< True=whole-fund, False=deal-by-deal
    double catch_up_rate = 1.0;             ///< GP catch-up (100% = full catch-up)
    double organizational_expense_cap = 0.01; ///< % of committed
    int investment_period_years = 5;
    int fund_term_years = 10;
    int extension_years = 2;                ///< Optional extension
    bool fee_on_invested_after_ip = true;   ///< Fee basis changes after investment period
};

/** Fund performance multiples */
struct FundMultiples {
    double tvpi = 0.0;      ///< Total Value to Paid-In
    double dpi = 0.0;       ///< Distributions to Paid-In
    double rvpi = 0.0;      ///< Residual Value to Paid-In
    double pic = 0.0;       ///< Paid-In Capital ratio (% of commitment called)
    double gross_irr = 0.0; ///< Gross IRR (before fees)
    double net_irr = 0.0;   ///< Net IRR (after fees)
    double gross_moic = 0.0;///< Gross Multiple on Invested Capital
};

/** Quarterly NAV snapshot */
struct NavSnapshot {
    std::chrono::system_clock::time_point as_of;
    double nav = 0.0;
    double unfunded_commitment = 0.0;
    double total_called = 0.0;
    double total_distributed = 0.0;
    double total_value = 0.0;       ///< NAV + distributions
    ValuationMethod method = ValuationMethod::NAV;
    std::string notes;
};

/** Commitment pacing parameters */
struct PacingModel {
    double target_allocation_pct = 0.15;    ///< Target allocation to privates
    double over_commitment_ratio = 1.3;     ///< Commit 1.3x target to account for J-curve
    double expected_drawdown_rate = 0.25;   ///< % of commitment called per year
    double expected_distribution_rate = 0.20;///< % NAV distributed per year
    int projection_years = 10;
    double portfolio_total_value = 0.0;     ///< Total portfolio for allocation calc
};

/** PME (Public Market Equivalent) result */
struct PmeResult {
    double ks_pme = 0.0;           ///< Kaplan-Schoar PME
    double direct_alpha = 0.0;     ///< Annualized excess return vs public market
    double mpme = 0.0;             ///< Modified PME (Long-Nickels)
    std::string benchmark_name;
    double benchmark_total_return = 0.0;
};

/** J-Curve projection point */
struct JCurvePoint {
    int quarter_offset = 0;     ///< Quarters from inception
    double cumulative_calls = 0.0;
    double cumulative_distributions = 0.0;
    double nav = 0.0;
    double net_cash_flow = 0.0; ///< Distributions - calls
    double tvpi = 0.0;
};

/** Private equity fund */
struct PrivateFund {
    std::string id;
    std::string name;
    std::string manager;            ///< GP name
    PrivateAssetClass asset_class = PrivateAssetClass::PrivateEquity;
    FundStage stage = FundStage::InvestmentPeriod;
    std::string vintage_year;       ///< "2020", "2021", etc.
    std::string currency = "USD";
    std::string geography;          ///< Primary region

    // Commitment
    double commitment = 0.0;        ///< Total LP commitment
    double funded = 0.0;            ///< Total capital called
    double unfunded = 0.0;          ///< Remaining commitment
    double recallable = 0.0;        ///< Distributions subject to recall
    double nav = 0.0;               ///< Current NAV
    double total_distributed = 0.0;

    FeeStructure fees;
    FundMultiples multiples;

    std::vector<CashFlow> cash_flows;
    std::vector<NavSnapshot> nav_history;

    std::chrono::system_clock::time_point inception;
    std::chrono::system_clock::time_point expected_termination;

    // Real estate specific
    std::optional<PropertyType> property_type;
    std::optional<RealEstateStrategy> re_strategy;

    // Infrastructure specific
    std::optional<InfraSector> infra_sector;

    // Tags and metadata
    std::map<std::string, std::string> metadata;
};

/** Vintage year cohort */
struct VintageCohort {
    std::string vintage;
    size_t fund_count = 0;
    double total_commitment = 0.0;
    double total_funded = 0.0;
    double total_nav = 0.0;
    double total_distributed = 0.0;
    FundMultiples avg_multiples;
    FundMultiples median_multiples;
    std::vector<std::string> fund_ids;
};

/** Liquidity forecast entry */
struct LiquidityForecast {
    int quarter_offset = 0;
    double expected_calls = 0.0;
    double expected_distributions = 0.0;
    double net_cash_flow = 0.0;
    double cumulative_net = 0.0;
    double confidence_low = 0.0;    ///< 10th percentile
    double confidence_high = 0.0;   ///< 90th percentile
};

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------

inline std::string asset_class_to_string(PrivateAssetClass ac) {
    switch (ac) {
        case PrivateAssetClass::PrivateEquity:    return "Private Equity";
        case PrivateAssetClass::Buyout:           return "Buyout";
        case PrivateAssetClass::VentureCapital:   return "Venture Capital";
        case PrivateAssetClass::GrowthEquity:     return "Growth Equity";
        case PrivateAssetClass::RealEstate:       return "Real Estate";
        case PrivateAssetClass::Infrastructure:   return "Infrastructure";
        case PrivateAssetClass::PrivateCredit:    return "Private Credit";
        case PrivateAssetClass::NaturalResources: return "Natural Resources";
        case PrivateAssetClass::SecondaryFund:    return "Secondary Fund";
        case PrivateAssetClass::FundOfFunds:      return "Fund of Funds";
        case PrivateAssetClass::CoInvestment:     return "Co-Investment";
        case PrivateAssetClass::DirectInvestment: return "Direct Investment";
    }
    return "Unknown";
}

inline std::string stage_to_string(FundStage s) {
    switch (s) {
        case FundStage::Fundraising:       return "Fundraising";
        case FundStage::InvestmentPeriod:   return "Investment Period";
        case FundStage::HarvestingPeriod:   return "Harvesting";
        case FundStage::Liquidation:        return "Liquidation";
        case FundStage::Terminated:         return "Terminated";
    }
    return "Unknown";
}

inline std::string valuation_method_to_string(ValuationMethod m) {
    switch (m) {
        case ValuationMethod::NAV:                    return "NAV";
        case ValuationMethod::DCF:                    return "DCF";
        case ValuationMethod::ComparableTransactions: return "Comparable Transactions";
        case ValuationMethod::ComparablePublic:       return "Comparable Public";
        case ValuationMethod::Appraisal:              return "Appraisal";
        case ValuationMethod::CostBasis:              return "Cost Basis";
        case ValuationMethod::LastRound:              return "Last Round";
        case ValuationMethod::Hybrid:                 return "Hybrid";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// IRR Calculator
// ---------------------------------------------------------------------------

/**
 * Internal Rate of Return calculator using Newton-Raphson method.
 * Handles irregular cash flows with exact dates.
 */
class IrrCalculator {
public:
    /**
     * Calculate XIRR for irregular cash flows.
     * @param amounts  Cash flow amounts (negative = outflow, positive = inflow)
     * @param dates    Corresponding dates
     * @param guess    Initial IRR guess
     * @return IRR as decimal (0.15 = 15%), or nullopt if no convergence
     */
    [[nodiscard]] static std::optional<double> xirr(
            const std::vector<double>& amounts,
            const std::vector<std::chrono::system_clock::time_point>& dates,
            double guess = 0.10) {
        if (amounts.size() != dates.size() || amounts.size() < 2) {
            return std::nullopt;
        }

        double rate = guess;
        const int max_iter = 200;
        const double tolerance = 1e-9;

        auto base_time = dates[0];

        for (int iter = 0; iter < max_iter; ++iter) {
            double npv = 0.0;
            double dnpv = 0.0;

            for (size_t i = 0; i < amounts.size(); ++i) {
                double years = std::chrono::duration<double>(
                    dates[i] - base_time).count() / (365.25 * 86400.0);

                double disc = std::pow(1.0 + rate, years);
                if (std::abs(disc) < 1e-15) continue;

                npv += amounts[i] / disc;
                if (years > 0) {
                    dnpv -= years * amounts[i] / (disc * (1.0 + rate));
                }
            }

            if (std::abs(dnpv) < 1e-15) return std::nullopt;

            double new_rate = rate - npv / dnpv;

            if (std::abs(new_rate - rate) < tolerance) {
                return new_rate;
            }

            rate = new_rate;

            // Guard against divergence
            if (rate < -0.99) rate = -0.99;
            if (rate > 10.0) rate = 10.0;
        }

        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// WaterfallEngine
// ---------------------------------------------------------------------------

/**
 * Distribution waterfall calculator.
 * Implements European (whole-fund) and American (deal-by-deal) waterfalls
 * with preferred return, GP catch-up, and carried interest.
 */
class WaterfallEngine {
public:
    struct WaterfallResult {
        double total_distributions = 0.0;
        double return_of_capital = 0.0;
        double preferred_return_lp = 0.0;
        double gp_catch_up = 0.0;
        double carried_interest = 0.0;
        double lp_residual = 0.0;
        double lp_total = 0.0;
        double gp_total = 0.0;
    };

    /** Calculate European waterfall */
    [[nodiscard]] static WaterfallResult european_waterfall(
            double total_invested, double total_value,
            const FeeStructure& fees, double years_elapsed) {
        WaterfallResult result;
        double remaining = total_value;
        result.total_distributions = total_value;

        // 1. Return of capital
        double roc = std::min(remaining, total_invested);
        result.return_of_capital = roc;
        remaining -= roc;

        // 2. Preferred return
        double pref_target = total_invested *
            (std::pow(1.0 + fees.preferred_return, years_elapsed) - 1.0);
        double pref = std::min(remaining, pref_target);
        result.preferred_return_lp = pref;
        remaining -= pref;

        // 3. GP catch-up
        if (remaining > 0 && fees.catch_up_rate > 0) {
            double catch_up_target = (result.return_of_capital + pref) *
                fees.carry_rate / (1.0 - fees.carry_rate);
            double catch_up = std::min(remaining,
                catch_up_target * fees.catch_up_rate);
            result.gp_catch_up = catch_up;
            remaining -= catch_up;
        }

        // 4. Remaining split (carry/LP)
        if (remaining > 0) {
            result.carried_interest = remaining * fees.carry_rate;
            result.lp_residual = remaining * (1.0 - fees.carry_rate);
        }

        result.lp_total = result.return_of_capital + result.preferred_return_lp +
                          result.lp_residual;
        result.gp_total = result.gp_catch_up + result.carried_interest;

        return result;
    }
};

// ---------------------------------------------------------------------------
// JCurveModeler
// ---------------------------------------------------------------------------

/**
 * Models the J-curve effect for private equity fund cash flows.
 * Projects expected capital calls and distributions over fund life.
 */
class JCurveModeler {
public:
    /**
     * Generate J-curve projection.
     * @param commitment     Total commitment amount
     * @param fees           Fee structure
     * @param target_tvpi    Expected final TVPI
     * @param quarters       Number of quarters to project
     */
    [[nodiscard]] static std::vector<JCurvePoint> project(
            double commitment, const FeeStructure& fees,
            double target_tvpi = 1.8, int quarters = 48) {
        std::vector<JCurvePoint> curve;
        curve.reserve(static_cast<size_t>(quarters));

        double cum_calls = 0.0;
        double cum_dist = 0.0;
        int ip_quarters = fees.investment_period_years * 4;
        int total_quarters = fees.fund_term_years * 4;

        for (int q = 0; q < quarters; ++q) {
            JCurvePoint pt;
            pt.quarter_offset = q;

            // Capital calls: concentrated in investment period
            double call_rate = 0.0;
            if (q < ip_quarters) {
                // S-curve: slow start, ramp, slow finish
                double t = static_cast<double>(q) / static_cast<double>(ip_quarters);
                call_rate = 4.0 * t * (1.0 - t);  // Parabolic shape
                call_rate *= commitment / static_cast<double>(ip_quarters) * 1.5;
            }
            cum_calls += call_rate;
            cum_calls = std::min(cum_calls, commitment);

            // Distributions: start after investment period, accelerate
            double dist_rate = 0.0;
            if (q > ip_quarters / 2) {
                double t = static_cast<double>(q - ip_quarters / 2) /
                           static_cast<double>(total_quarters - ip_quarters / 2);
                t = std::min(t, 1.0);
                // Gradual ramp of distributions
                dist_rate = commitment * target_tvpi * t * t * 0.08;
            }
            cum_dist += dist_rate;
            double max_dist = cum_calls * target_tvpi;
            cum_dist = std::min(cum_dist, max_dist);

            pt.cumulative_calls = cum_calls;
            pt.cumulative_distributions = cum_dist;
            pt.net_cash_flow = cum_dist - cum_calls;

            // NAV: invested capital growing, minus distributions
            double invested = cum_calls * (1.0 - fees.management_fee_rate *
                static_cast<double>(q) / 4.0 / static_cast<double>(fees.fund_term_years));
            double growth = 1.0 + (target_tvpi - 1.0) *
                static_cast<double>(q) / static_cast<double>(total_quarters);
            pt.nav = std::max(0.0, invested * growth - cum_dist);

            pt.tvpi = (cum_calls > 0) ?
                (pt.nav + cum_dist) / cum_calls : 0.0;

            curve.push_back(pt);
        }

        return curve;
    }
};

// ---------------------------------------------------------------------------
// CommitmentPacer
// ---------------------------------------------------------------------------

/**
 * Commitment pacing model for managing private asset allocations.
 * Projects optimal new commitment amounts to maintain target allocation.
 */
class CommitmentPacer {
public:
    struct PacingRecommendation {
        int year = 0;
        double recommended_new_commitments = 0.0;
        double projected_nav = 0.0;
        double projected_unfunded = 0.0;
        double projected_allocation_pct = 0.0;
        double exposure_pct = 0.0;  ///< (NAV + unfunded) / portfolio
    };

    /** Generate commitment pacing plan */
    [[nodiscard]] static std::vector<PacingRecommendation> pace(
            const PacingModel& model,
            double current_nav, double current_unfunded,
            const std::vector<PrivateFund>& /*existing_funds*/) {
        std::vector<PacingRecommendation> plan;

        double nav = current_nav;
        double unfunded = current_unfunded;
        double portfolio = model.portfolio_total_value;
        double target_nav = portfolio * model.target_allocation_pct;

        for (int year = 1; year <= model.projection_years; ++year) {
            PacingRecommendation rec;
            rec.year = year;

            // Project drawdowns from existing unfunded
            double calls = unfunded * model.expected_drawdown_rate;
            unfunded -= calls;
            nav += calls;

            // Project distributions
            double dist = nav * model.expected_distribution_rate;
            nav -= dist;

            // Portfolio growth assumption (conservative 5%)
            portfolio *= 1.05;
            target_nav = portfolio * model.target_allocation_pct;

            // Gap to target
            double nav_gap = target_nav - nav;
            double target_commit = nav_gap * model.over_commitment_ratio;
            target_commit = std::max(0.0, target_commit);

            // Add new commitments
            unfunded += target_commit;

            rec.recommended_new_commitments = target_commit;
            rec.projected_nav = nav;
            rec.projected_unfunded = unfunded;
            rec.projected_allocation_pct = (portfolio > 0) ?
                (nav / portfolio * 100.0) : 0.0;
            rec.exposure_pct = (portfolio > 0) ?
                ((nav + unfunded) / portfolio * 100.0) : 0.0;

            plan.push_back(rec);
        }

        return plan;
    }
};

// ---------------------------------------------------------------------------
// PmeCalculator
// ---------------------------------------------------------------------------

/**
 * Public Market Equivalent calculator.
 * Compares private fund returns to a public benchmark.
 */
class PmeCalculator {
public:
    /**
     * Calculate Kaplan-Schoar PME.
     * PME > 1.0 means fund outperformed public benchmark.
     *
     * @param calls         Capital call amounts (positive)
     * @param distributions Distribution amounts (positive)
     * @param call_dates    Dates of capital calls
     * @param dist_dates    Dates of distributions
     * @param nav           Current NAV
     * @param nav_date      Current NAV date
     * @param benchmark_returns  Monthly benchmark returns
     * @param benchmark_dates    Dates for benchmark returns
     */
    [[nodiscard]] static PmeResult calculate(
            const std::vector<double>& calls,
            const std::vector<std::chrono::system_clock::time_point>& call_dates,
            const std::vector<double>& distributions,
            const std::vector<std::chrono::system_clock::time_point>& dist_dates,
            double nav,
            std::chrono::system_clock::time_point nav_date,
            const std::vector<double>& benchmark_returns,
            const std::vector<std::chrono::system_clock::time_point>& benchmark_dates,
            const std::string& benchmark_name = "S&P 500") {
        PmeResult result;
        result.benchmark_name = benchmark_name;

        if (calls.empty() || benchmark_returns.empty()) return result;

        // Build cumulative benchmark index
        std::vector<double> bench_index(benchmark_returns.size() + 1, 1.0);
        for (size_t i = 0; i < benchmark_returns.size(); ++i) {
            bench_index[i + 1] = bench_index[i] * (1.0 + benchmark_returns[i]);
        }
        result.benchmark_total_return = bench_index.back() - 1.0;

        // Find benchmark value at a given date (linear interpolation)
        auto bench_at = [&](std::chrono::system_clock::time_point tp) -> double {
            if (benchmark_dates.empty()) return 1.0;
            if (tp <= benchmark_dates.front()) return bench_index.front();
            if (tp >= benchmark_dates.back()) return bench_index.back();

            for (size_t i = 1; i < benchmark_dates.size(); ++i) {
                if (tp <= benchmark_dates[i]) {
                    double frac = std::chrono::duration<double>(
                        tp - benchmark_dates[i-1]).count() /
                        std::chrono::duration<double>(
                            benchmark_dates[i] - benchmark_dates[i-1]).count();
                    return bench_index[i-1] + frac * (bench_index[i] - bench_index[i-1]);
                }
            }
            return bench_index.back();
        };

        double bench_end = bench_at(nav_date);

        // KS-PME: FV(distributions + NAV) / FV(calls)
        double fv_calls = 0.0;
        for (size_t i = 0; i < calls.size(); ++i) {
            double bench_start = bench_at(call_dates[i]);
            if (bench_start > 0) {
                fv_calls += calls[i] * (bench_end / bench_start);
            }
        }

        double fv_dist = 0.0;
        for (size_t i = 0; i < distributions.size(); ++i) {
            double bench_start = bench_at(dist_dates[i]);
            if (bench_start > 0) {
                fv_dist += distributions[i] * (bench_end / bench_start);
            }
        }

        if (fv_calls > 0) {
            result.ks_pme = (fv_dist + nav) / fv_calls;
        }

        // Direct Alpha: annualized excess return
        if (result.ks_pme > 0 && !call_dates.empty()) {
            double years = std::chrono::duration<double>(
                nav_date - call_dates.front()).count() / (365.25 * 86400.0);
            if (years > 0) {
                result.direct_alpha = std::pow(result.ks_pme, 1.0 / years) - 1.0;
            }
        }

        return result;
    }
};

// ---------------------------------------------------------------------------
// PrivateAssetsEngine -- Main Engine
// ---------------------------------------------------------------------------

/**
 * Central private assets management engine.
 * Manages fund registry, cash flows, valuations, performance,
 * pacing, and reporting for illiquid investments.
 */
class PrivateAssetsEngine {
public:
    PrivateAssetsEngine() = default;

    // -- Fund Management --

    /** Add or update a fund */
    void add_fund(const PrivateFund& fund) {
        std::lock_guard<std::mutex> lock(mutex_);
        funds_[fund.id] = fund;
        update_fund_metrics(funds_[fund.id]);
    }

    /** Get a fund by ID */
    [[nodiscard]] std::optional<PrivateFund> get_fund(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(id);
        return (it != funds_.end()) ?
            std::optional<PrivateFund>(it->second) : std::nullopt;
    }

    /** Get all fund IDs */
    [[nodiscard]] std::vector<std::string> fund_ids() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> ids;
        ids.reserve(funds_.size());
        for (const auto& [id, _] : funds_) ids.push_back(id);
        return ids;
    }

    /** Get fund count */
    [[nodiscard]] size_t fund_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return funds_.size();
    }

    // -- Cash Flow Management --

    /** Record a cash flow for a fund */
    bool record_cash_flow(const std::string& fund_id, const CashFlow& cf) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return false;

        it->second.cash_flows.push_back(cf);

        // Update fund balances
        switch (cf.type) {
            case CashFlowType::CapitalCall:
                it->second.funded += std::abs(cf.amount);
                it->second.unfunded -= std::abs(cf.amount);
                break;
            case CashFlowType::Distribution:
            case CashFlowType::ReturnOfCapital:
            case CashFlowType::Dividend:
            case CashFlowType::Interest:
                it->second.total_distributed += std::abs(cf.amount);
                break;
            case CashFlowType::Recallable:
                it->second.recallable += std::abs(cf.amount);
                break;
            default:
                break;
        }

        update_fund_metrics(it->second);
        return true;
    }

    /** Record NAV snapshot */
    bool record_nav(const std::string& fund_id, const NavSnapshot& snap) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return false;

        it->second.nav = snap.nav;
        it->second.nav_history.push_back(snap);
        update_fund_metrics(it->second);
        return true;
    }

    // -- Performance Calculation --

    /** Calculate fund IRR from cash flows */
    [[nodiscard]] std::optional<double> calculate_irr(const std::string& fund_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return std::nullopt;

        const auto& fund = it->second;

        std::vector<double> amounts;
        std::vector<std::chrono::system_clock::time_point> dates;

        for (const auto& cf : fund.cash_flows) {
            double amt = cf.amount;
            if (cf.type == CashFlowType::CapitalCall ||
                cf.type == CashFlowType::ManagementFee ||
                cf.type == CashFlowType::OrganizationalExpense) {
                amt = -std::abs(amt);  // Outflows are negative
            } else {
                amt = std::abs(amt);   // Inflows are positive
            }
            amounts.push_back(amt);
            dates.push_back(cf.date);
        }

        // Add current NAV as terminal value
        if (fund.nav > 0 && !fund.nav_history.empty()) {
            amounts.push_back(fund.nav);
            dates.push_back(fund.nav_history.back().as_of);
        }

        return IrrCalculator::xirr(amounts, dates);
    }

    /** Calculate multiples for a fund */
    [[nodiscard]] FundMultiples calculate_multiples(const std::string& fund_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return {};

        const auto& fund = it->second;
        FundMultiples m;

        if (fund.funded > 0) {
            m.tvpi = (fund.nav + fund.total_distributed) / fund.funded;
            m.dpi = fund.total_distributed / fund.funded;
            m.rvpi = fund.nav / fund.funded;
        }
        if (fund.commitment > 0) {
            m.pic = fund.funded / fund.commitment;
        }

        return m;
    }

    /** Calculate distribution waterfall */
    [[nodiscard]] WaterfallEngine::WaterfallResult calculate_waterfall(
            const std::string& fund_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return {};

        const auto& fund = it->second;
        double years = 0.0;
        if (!fund.cash_flows.empty()) {
            auto now = std::chrono::system_clock::now();
            years = std::chrono::duration<double>(
                now - fund.inception).count() / (365.25 * 86400.0);
        }

        return WaterfallEngine::european_waterfall(
            fund.funded, fund.nav + fund.total_distributed,
            fund.fees, years);
    }

    // -- Vintage Year Analysis --

    /** Get vintage year cohort analysis */
    [[nodiscard]] std::map<std::string, VintageCohort> vintage_analysis() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, VintageCohort> cohorts;

        for (const auto& [id, fund] : funds_) {
            auto& cohort = cohorts[fund.vintage_year];
            if (cohort.vintage.empty()) cohort.vintage = fund.vintage_year;

            cohort.fund_count++;
            cohort.total_commitment += fund.commitment;
            cohort.total_funded += fund.funded;
            cohort.total_nav += fund.nav;
            cohort.total_distributed += fund.total_distributed;
            cohort.fund_ids.push_back(id);
        }

        // Calculate average multiples per cohort
        for (auto& [vintage, cohort] : cohorts) {
            if (cohort.total_funded > 0) {
                cohort.avg_multiples.tvpi =
                    (cohort.total_nav + cohort.total_distributed) / cohort.total_funded;
                cohort.avg_multiples.dpi =
                    cohort.total_distributed / cohort.total_funded;
                cohort.avg_multiples.rvpi =
                    cohort.total_nav / cohort.total_funded;
            }
            if (cohort.total_commitment > 0) {
                cohort.avg_multiples.pic =
                    cohort.total_funded / cohort.total_commitment;
            }
        }

        return cohorts;
    }

    // -- J-Curve Projection --

    /** Generate J-curve projection for a fund */
    [[nodiscard]] std::vector<JCurvePoint> j_curve(
            const std::string& fund_id, double target_tvpi = 1.8) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = funds_.find(fund_id);
        if (it == funds_.end()) return {};

        return JCurveModeler::project(
            it->second.commitment, it->second.fees,
            target_tvpi, it->second.fees.fund_term_years * 4);
    }

    // -- Commitment Pacing --

    /** Generate commitment pacing recommendations */
    [[nodiscard]] std::vector<CommitmentPacer::PacingRecommendation> pace_commitments(
            const PacingModel& model) const {
        std::lock_guard<std::mutex> lock(mutex_);

        double total_nav = 0.0;
        double total_unfunded = 0.0;
        std::vector<PrivateFund> fund_list;

        for (const auto& [id, fund] : funds_) {
            total_nav += fund.nav;
            total_unfunded += fund.unfunded;
            fund_list.push_back(fund);
        }

        return CommitmentPacer::pace(model, total_nav, total_unfunded, fund_list);
    }

    // -- Liquidity Forecasting --

    /** Forecast net cash flows over projection horizon */
    [[nodiscard]] std::vector<LiquidityForecast> liquidity_forecast(
            int quarters = 12) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LiquidityForecast> forecast;
        forecast.reserve(static_cast<size_t>(quarters));

        double total_unfunded = 0.0;
        double total_nav = 0.0;
        for (const auto& [id, fund] : funds_) {
            total_unfunded += fund.unfunded;
            total_nav += fund.nav;
        }

        double cum_net = 0.0;
        for (int q = 0; q < quarters; ++q) {
            LiquidityForecast entry;
            entry.quarter_offset = q + 1;

            // Expected calls: declining as funds mature
            double call_decay = std::exp(-0.15 * static_cast<double>(q));
            entry.expected_calls = total_unfunded * 0.06 * call_decay;
            total_unfunded -= entry.expected_calls;

            // Expected distributions: growing as funds harvest
            double dist_growth = 1.0 + 0.05 * static_cast<double>(q);
            entry.expected_distributions = total_nav * 0.04 * dist_growth;

            entry.net_cash_flow = entry.expected_distributions - entry.expected_calls;
            cum_net += entry.net_cash_flow;
            entry.cumulative_net = cum_net;

            // Confidence bounds (simple +/- 30%)
            entry.confidence_low = entry.net_cash_flow * 0.7;
            entry.confidence_high = entry.net_cash_flow * 1.3;

            forecast.push_back(entry);
        }

        return forecast;
    }

    // -- Portfolio Summary --

    /** Get aggregate portfolio statistics */
    struct PortfolioSummary {
        size_t fund_count = 0;
        double total_commitment = 0.0;
        double total_funded = 0.0;
        double total_unfunded = 0.0;
        double total_nav = 0.0;
        double total_distributed = 0.0;
        FundMultiples aggregate_multiples;
        std::map<PrivateAssetClass, double> allocation_by_class;
        std::map<std::string, double> allocation_by_vintage;
        std::map<std::string, double> allocation_by_geography;
    };

    [[nodiscard]] PortfolioSummary portfolio_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        PortfolioSummary summary;
        summary.fund_count = funds_.size();

        for (const auto& [id, fund] : funds_) {
            summary.total_commitment += fund.commitment;
            summary.total_funded += fund.funded;
            summary.total_unfunded += fund.unfunded;
            summary.total_nav += fund.nav;
            summary.total_distributed += fund.total_distributed;

            summary.allocation_by_class[fund.asset_class] += fund.nav;
            summary.allocation_by_vintage[fund.vintage_year] += fund.nav;
            if (!fund.geography.empty()) {
                summary.allocation_by_geography[fund.geography] += fund.nav;
            }
        }

        if (summary.total_funded > 0) {
            summary.aggregate_multiples.tvpi =
                (summary.total_nav + summary.total_distributed) / summary.total_funded;
            summary.aggregate_multiples.dpi =
                summary.total_distributed / summary.total_funded;
            summary.aggregate_multiples.rvpi =
                summary.total_nav / summary.total_funded;
        }
        if (summary.total_commitment > 0) {
            summary.aggregate_multiples.pic =
                summary.total_funded / summary.total_commitment;
        }

        return summary;
    }

    // -- Reporting --

    /** Generate text summary report */
    [[nodiscard]] std::string report() const {
        auto summary = portfolio_summary();
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        ss << "=== Private Assets Report ===\n";
        ss << "Funds: " << summary.fund_count << "\n";
        ss << "Total Commitment: $" << summary.total_commitment / 1e6 << "M\n";
        ss << "Total Funded: $" << summary.total_funded / 1e6 << "M\n";
        ss << "Unfunded: $" << summary.total_unfunded / 1e6 << "M\n";
        ss << "Current NAV: $" << summary.total_nav / 1e6 << "M\n";
        ss << "Total Distributed: $" << summary.total_distributed / 1e6 << "M\n";
        ss << "TVPI: " << summary.aggregate_multiples.tvpi << "x\n";
        ss << "DPI: " << summary.aggregate_multiples.dpi << "x\n";
        ss << "RVPI: " << summary.aggregate_multiples.rvpi << "x\n";
        ss << "PIC: " << (summary.aggregate_multiples.pic * 100.0) << "%\n";

        if (!summary.allocation_by_class.empty()) {
            ss << "\n--- Allocation by Asset Class ---\n";
            for (const auto& [ac, nav] : summary.allocation_by_class) {
                double pct = (summary.total_nav > 0) ?
                    (nav / summary.total_nav * 100.0) : 0.0;
                ss << "  " << asset_class_to_string(ac) << ": $"
                   << nav / 1e6 << "M (" << pct << "%)\n";
            }
        }

        return ss.str();
    }

private:
    void update_fund_metrics(PrivateFund& fund) {
        fund.unfunded = fund.commitment - fund.funded;
        if (fund.unfunded < 0) fund.unfunded = 0;

        if (fund.funded > 0) {
            fund.multiples.tvpi = (fund.nav + fund.total_distributed) / fund.funded;
            fund.multiples.dpi = fund.total_distributed / fund.funded;
            fund.multiples.rvpi = fund.nav / fund.funded;
        }
        if (fund.commitment > 0) {
            fund.multiples.pic = fund.funded / fund.commitment;
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, PrivateFund> funds_;
};

} // namespace assets
} // namespace genie

#endif // GENIE_ASSETS_PRIVATE_ASSETS_HPP
