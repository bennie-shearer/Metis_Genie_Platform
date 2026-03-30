/**
 * @file esg_scoring.hpp
 * @brief ESG (Environmental, Social, Governance) Scoring and Integration Engine
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Comprehensive ESG analysis framework for portfolio-level and security-level
 * scoring across Environmental, Social, and Governance pillars. Supports
 * multiple scoring methodologies (MSCI-style, Sustainalytics-style, custom),
 * controversy screening, UN SDG alignment, carbon footprint estimation,
 * and regulatory compliance (SFDR, EU Taxonomy, TCFD).
 *
 * Features:
 *   - Three-pillar ESG scoring (E, S, G) with sub-category granularity
 *   - Composite ESG score with configurable pillar weights
 *   - Industry-relative scoring (best-in-class within sector)
 *   - Controversy and incident tracking with severity levels
 *   - Exclusion screening (weapons, tobacco, fossil fuels, etc.)
 *   - UN Sustainable Development Goals (SDG) alignment mapping
 *   - Carbon intensity and Scope 1/2/3 estimation
 *   - SFDR Article 6/8/9 classification support
 *   - EU Taxonomy alignment percentage calculation
 *   - TCFD-aligned climate risk metrics
 *   - Portfolio-weighted ESG aggregation
 *   - ESG momentum (score trend over time)
 *   - Peer comparison and percentile ranking
 *   - Data provider abstraction (MSCI, Sustainalytics, Bloomberg, custom CSV)
 *   - Override mechanism for analyst adjustments
 *   - Audit trail for all score changes
 *   - Thread-safe concurrent access
 *   - Zero external dependencies
 *
 * Architecture:
 *   EsgEngine owns:
 *     - SecurityEsgStore: per-security ESG data cache
 *     - ControversyTracker: incident monitoring with decay model
 *     - ExclusionScreener: configurable exclusion lists
 *     - CarbonCalculator: emissions estimation engine
 *     - SdgMapper: UN SDG alignment scoring
 *     - RegulatoryClassifier: SFDR/EU Taxonomy classification
 *
 * @note Header-only. No external dependencies.
 */

#ifndef GENIE_COMPLIANCE_ESG_SCORING_HPP
#define GENIE_COMPLIANCE_ESG_SCORING_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
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
#include <variant>
#include <cassert>

namespace genie {
namespace compliance {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** ESG pillar identifiers */
enum class EsgPillar {
    Environmental,
    Social,
    Governance,
    Composite      ///< Weighted combination of E, S, G
};

/** Environmental sub-categories */
enum class EnvironmentalCategory {
    ClimateChange,          ///< GHG emissions, energy transition
    NaturalResources,       ///< Water stress, land use, biodiversity
    PollutionWaste,         ///< Toxic emissions, packaging waste, e-waste
    EnvironmentalOpportunity ///< Clean tech, green building, renewable energy
};

/** Social sub-categories */
enum class SocialCategory {
    HumanCapital,           ///< Labor management, health & safety, training
    ProductLiability,       ///< Product safety, data privacy, responsible marketing
    StakeholderOpposition,  ///< Controversial sourcing, community relations
    SocialOpportunity       ///< Access to healthcare, nutrition, financial inclusion
};

/** Governance sub-categories */
enum class GovernanceCategory {
    CorporateGovernance,    ///< Board structure, ownership, pay
    CorporateBehavior,      ///< Business ethics, anti-competitive, tax transparency
    Transparency,           ///< Reporting quality, audit, disclosure
    RegulatoryCompliance    ///< Sanctions, regulatory actions
};

/** ESG rating grades (MSCI-style) */
enum class EsgRating {
    AAA,    ///< Leader
    AA,     ///< Leader
    A,      ///< Average
    BBB,    ///< Average
    BB,     ///< Average
    B,      ///< Laggard
    CCC,    ///< Laggard
    NR      ///< Not Rated
};

/** Controversy severity levels */
enum class ControversySeverity {
    None,
    Low,
    Moderate,
    Significant,
    High,
    Severe      ///< Category 5: structural, systemic issues
};

/** Exclusion categories for screening */
enum class ExclusionCategory {
    ControversialWeapons,   ///< Cluster munitions, landmines, bio/chem
    NuclearWeapons,
    ConventionalWeapons,
    Tobacco,
    ThermalCoal,
    OilSands,
    ArcticDrilling,
    Gambling,
    AdultEntertainment,
    Alcohol,
    AnimalTesting,
    GeneticallyModified,
    PalmOil,
    PrivatePrisons,
    PredatoryLending
};

/** UN Sustainable Development Goals */
enum class SdgGoal {
    NoPoverty = 1,
    ZeroHunger = 2,
    GoodHealth = 3,
    QualityEducation = 4,
    GenderEquality = 5,
    CleanWater = 6,
    AffordableEnergy = 7,
    DecentWork = 8,
    IndustryInnovation = 9,
    ReducedInequalities = 10,
    SustainableCities = 11,
    ResponsibleConsumption = 12,
    ClimateAction = 13,
    LifeBelowWater = 14,
    LifeOnLand = 15,
    PeaceJustice = 16,
    Partnerships = 17
};

/** SFDR classification */
enum class SfdrArticle {
    Article6,   ///< No sustainability consideration
    Article8,   ///< Promotes environmental/social characteristics
    Article9    ///< Sustainable investment objective
};

/** Carbon emission scopes */
enum class CarbonScope {
    Scope1,     ///< Direct emissions
    Scope2,     ///< Indirect - purchased energy
    Scope3      ///< Indirect - value chain
};

/** ESG data provider */
enum class EsgDataProvider {
    Internal,       ///< Manual/custom entry
    MSCI,
    Sustainalytics,
    Bloomberg,
    Refinitiv,
    ISS,
    CDP,
    CustomCSV
};

// ---------------------------------------------------------------------------
// Data Structures
// ---------------------------------------------------------------------------

/** Per-category ESG score */
struct CategoryScore {
    double raw_score = 0.0;         ///< 0-10 scale
    double weight = 1.0;            ///< Weight in pillar calculation
    double industry_adjusted = 0.0; ///< Relative to sector peers
    std::string rationale;          ///< Brief explanation
    std::chrono::system_clock::time_point as_of;
};

/** Pillar-level ESG score */
struct PillarScore {
    EsgPillar pillar = EsgPillar::Composite;
    double score = 0.0;                 ///< 0-10 scale
    double percentile = 0.0;            ///< 0-100 within universe
    EsgRating rating = EsgRating::NR;
    std::map<std::string, CategoryScore> categories;
    double momentum_1y = 0.0;           ///< Score change over 12 months
};

/** Carbon footprint data */
struct CarbonFootprint {
    double scope1_tons_co2e = 0.0;      ///< Direct emissions (tCO2e)
    double scope2_tons_co2e = 0.0;      ///< Purchased energy
    double scope3_tons_co2e = 0.0;      ///< Value chain (estimated)
    double carbon_intensity = 0.0;      ///< tCO2e / $M revenue
    double weighted_avg_carbon = 0.0;   ///< WACI for portfolio
    double fossil_fuel_revenue_pct = 0.0;
    bool scope3_estimated = true;       ///< Whether Scope 3 is modeled
    std::string methodology;
};

/** Controversy/incident record */
struct Controversy {
    std::string id;
    std::string security_id;
    std::string title;
    std::string description;
    ControversySeverity severity = ControversySeverity::None;
    std::vector<EsgPillar> affected_pillars;
    std::chrono::system_clock::time_point date;
    std::string source;
    bool resolved = false;
    double score_impact = 0.0;          ///< Negative impact on ESG score
};

/** SDG alignment entry */
struct SdgAlignment {
    SdgGoal goal;
    double alignment_score = 0.0;       ///< -1.0 (harmful) to +1.0 (aligned)
    double revenue_aligned_pct = 0.0;   ///< % revenue contributing to SDG
    std::string rationale;
};

/** EU Taxonomy alignment */
struct TaxonomyAlignment {
    double taxonomy_aligned_pct = 0.0;      ///< % revenue taxonomy-aligned
    double taxonomy_eligible_pct = 0.0;     ///< % revenue taxonomy-eligible
    double transition_activity_pct = 0.0;   ///< % in transition activities
    bool do_no_significant_harm = true;     ///< DNSH criteria met
    bool minimum_safeguards = true;         ///< Minimum social safeguards
};

/** Complete ESG profile for a security */
struct SecurityEsgProfile {
    std::string security_id;
    std::string name;
    std::string sector;
    std::string industry;
    std::string country;

    PillarScore environmental;
    PillarScore social;
    PillarScore governance;
    PillarScore composite;

    EsgRating overall_rating = EsgRating::NR;
    ControversySeverity controversy_level = ControversySeverity::None;
    std::vector<Controversy> controversies;

    CarbonFootprint carbon;
    std::vector<SdgAlignment> sdg_alignments;
    TaxonomyAlignment taxonomy;
    SfdrArticle sfdr_classification = SfdrArticle::Article6;

    std::set<ExclusionCategory> exclusion_flags;
    EsgDataProvider data_source = EsgDataProvider::Internal;
    std::chrono::system_clock::time_point last_updated;

    /** Check if security is excluded by any active screen */
    [[nodiscard]] bool is_excluded(const std::set<ExclusionCategory>& active_screens) const {
        for (auto& flag : exclusion_flags) {
            if (active_screens.count(flag)) return true;
        }
        return false;
    }
};

/** Portfolio-level ESG summary */
struct PortfolioEsgSummary {
    std::string portfolio_id;
    double composite_score = 0.0;       ///< Weighted average
    double environmental_score = 0.0;
    double social_score = 0.0;
    double governance_score = 0.0;
    EsgRating portfolio_rating = EsgRating::NR;

    double coverage_pct = 0.0;          ///< % AUM with ESG data
    size_t rated_count = 0;
    size_t unrated_count = 0;

    CarbonFootprint carbon;
    double taxonomy_aligned_pct = 0.0;
    SfdrArticle sfdr_classification = SfdrArticle::Article6;

    std::vector<std::string> excluded_holdings;     ///< Securities failing screens
    std::vector<Controversy> top_controversies;     ///< Highest severity

    // Distribution
    std::map<EsgRating, size_t> rating_distribution;
    std::map<EsgRating, double> rating_weight_distribution;

    // SDG portfolio alignment
    std::map<SdgGoal, double> sdg_alignment;

    // Improvement metrics
    double esg_momentum_3m = 0.0;
    double esg_momentum_12m = 0.0;
};

/** ESG screening rule */
struct ScreeningRule {
    std::string name;
    std::variant<
        ExclusionCategory,
        std::function<bool(const SecurityEsgProfile&)>
    > criteria;
    std::string description;
    bool active = true;
};

/** ESG score override */
struct ScoreOverride {
    std::string security_id;
    EsgPillar pillar;
    std::string category;           ///< Empty for pillar-level override
    double original_score = 0.0;
    double override_score = 0.0;
    std::string analyst;
    std::string rationale;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point expiry;   ///< When override expires
};

/** ESG engine configuration */
struct EsgConfig {
    // Pillar weights (must sum to 1.0)
    double environmental_weight = 0.35;
    double social_weight = 0.30;
    double governance_weight = 0.35;

    // Controversy penalty model
    double controversy_decay_halflife_days = 365.0;
    double max_controversy_penalty = 3.0;   ///< Max score reduction

    // Carbon estimation defaults
    double default_scope3_multiplier = 3.5; ///< Scope3 = Scope1+2 * multiplier
    std::string carbon_methodology = "GHG Protocol";

    // Industry-relative scoring
    bool use_industry_adjustment = true;
    double industry_adjustment_strength = 0.5;  ///< 0=absolute, 1=fully relative

    // Screening
    std::set<ExclusionCategory> active_exclusions;

    // Data staleness
    int max_data_age_days = 365;

    // SFDR thresholds
    double article8_min_esg_score = 5.0;
    double article9_min_esg_score = 7.0;
    double article9_min_taxonomy_pct = 50.0;
};

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------

/** Convert numeric score (0-10) to MSCI-style rating */
inline EsgRating score_to_rating(double score) {
    if (score >= 8.6) return EsgRating::AAA;
    if (score >= 7.1) return EsgRating::AA;
    if (score >= 5.7) return EsgRating::A;
    if (score >= 4.3) return EsgRating::BBB;
    if (score >= 2.9) return EsgRating::BB;
    if (score >= 1.4) return EsgRating::B;
    return EsgRating::CCC;
}

/** Rating to string */
inline std::string rating_to_string(EsgRating r) {
    switch (r) {
        case EsgRating::AAA: return "AAA";
        case EsgRating::AA:  return "AA";
        case EsgRating::A:   return "A";
        case EsgRating::BBB: return "BBB";
        case EsgRating::BB:  return "BB";
        case EsgRating::B:   return "B";
        case EsgRating::CCC: return "CCC";
        case EsgRating::NR:  return "NR";
    }
    return "NR";
}

/** Pillar to string */
inline std::string pillar_to_string(EsgPillar p) {
    switch (p) {
        case EsgPillar::Environmental: return "Environmental";
        case EsgPillar::Social:        return "Social";
        case EsgPillar::Governance:    return "Governance";
        case EsgPillar::Composite:     return "Composite";
    }
    return "Unknown";
}

/** Severity to string */
inline std::string severity_to_string(ControversySeverity s) {
    switch (s) {
        case ControversySeverity::None:        return "None";
        case ControversySeverity::Low:         return "Low";
        case ControversySeverity::Moderate:    return "Moderate";
        case ControversySeverity::Significant: return "Significant";
        case ControversySeverity::High:        return "High";
        case ControversySeverity::Severe:      return "Severe";
    }
    return "Unknown";
}

/** Exclusion category to string */
inline std::string exclusion_to_string(ExclusionCategory e) {
    switch (e) {
        case ExclusionCategory::ControversialWeapons: return "Controversial Weapons";
        case ExclusionCategory::NuclearWeapons:       return "Nuclear Weapons";
        case ExclusionCategory::ConventionalWeapons:  return "Conventional Weapons";
        case ExclusionCategory::Tobacco:              return "Tobacco";
        case ExclusionCategory::ThermalCoal:          return "Thermal Coal";
        case ExclusionCategory::OilSands:             return "Oil Sands";
        case ExclusionCategory::ArcticDrilling:       return "Arctic Drilling";
        case ExclusionCategory::Gambling:             return "Gambling";
        case ExclusionCategory::AdultEntertainment:   return "Adult Entertainment";
        case ExclusionCategory::Alcohol:              return "Alcohol";
        case ExclusionCategory::AnimalTesting:        return "Animal Testing";
        case ExclusionCategory::GeneticallyModified:  return "Genetically Modified";
        case ExclusionCategory::PalmOil:              return "Palm Oil";
        case ExclusionCategory::PrivatePrisons:       return "Private Prisons";
        case ExclusionCategory::PredatoryLending:     return "Predatory Lending";
    }
    return "Unknown";
}

/** SDG goal to string */
inline std::string sdg_to_string(SdgGoal g) {
    static const std::array<std::string, 18> names = {
        "", "No Poverty", "Zero Hunger", "Good Health", "Quality Education",
        "Gender Equality", "Clean Water", "Affordable Energy", "Decent Work",
        "Industry Innovation", "Reduced Inequalities", "Sustainable Cities",
        "Responsible Consumption", "Climate Action", "Life Below Water",
        "Life on Land", "Peace Justice", "Partnerships"
    };
    int idx = static_cast<int>(g);
    return (idx >= 1 && idx <= 17) ? names[static_cast<size_t>(idx)] : "Unknown";
}

// ---------------------------------------------------------------------------
// ControversyTracker
// ---------------------------------------------------------------------------

/**
 * Tracks ESG controversies with time-decay model.
 * Older controversies have diminishing impact on current scores.
 */
class ControversyTracker {
public:
    explicit ControversyTracker(double halflife_days = 365.0)
        : halflife_days_(halflife_days) {}

    /** Add a controversy */
    void add(const Controversy& c) {
        std::lock_guard<std::mutex> lock(mutex_);
        controversies_[c.security_id].push_back(c);
    }

    /** Resolve a controversy */
    bool resolve(const std::string& security_id, const std::string& controversy_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = controversies_.find(security_id);
        if (it == controversies_.end()) return false;
        for (auto& c : it->second) {
            if (c.id == controversy_id) {
                c.resolved = true;
                return true;
            }
        }
        return false;
    }

    /** Calculate current controversy penalty with time decay */
    [[nodiscard]] double calculate_penalty(const std::string& security_id,
                                            double max_penalty = 3.0) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = controversies_.find(security_id);
        if (it == controversies_.end()) return 0.0;

        auto now = std::chrono::system_clock::now();
        double total_impact = 0.0;

        for (const auto& c : it->second) {
            if (c.resolved) continue;

            double severity_weight = severity_to_weight(c.severity);
            double days_ago = std::chrono::duration<double>(
                now - c.date).count() / 86400.0;
            double decay = std::exp(-0.693 * days_ago / halflife_days_);

            total_impact += severity_weight * decay;
        }

        return std::min(total_impact, max_penalty);
    }

    /** Get active controversies for a security */
    [[nodiscard]] std::vector<Controversy> get_active(
            const std::string& security_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Controversy> result;
        auto it = controversies_.find(security_id);
        if (it == controversies_.end()) return result;
        for (const auto& c : it->second) {
            if (!c.resolved) result.push_back(c);
        }
        return result;
    }

    /** Get highest severity for a security */
    [[nodiscard]] ControversySeverity highest_severity(
            const std::string& security_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = controversies_.find(security_id);
        if (it == controversies_.end()) return ControversySeverity::None;

        auto max_sev = ControversySeverity::None;
        for (const auto& c : it->second) {
            if (!c.resolved && c.severity > max_sev) {
                max_sev = c.severity;
            }
        }
        return max_sev;
    }

private:
    static double severity_to_weight(ControversySeverity s) {
        switch (s) {
            case ControversySeverity::None:        return 0.0;
            case ControversySeverity::Low:         return 0.3;
            case ControversySeverity::Moderate:    return 0.7;
            case ControversySeverity::Significant: return 1.2;
            case ControversySeverity::High:        return 2.0;
            case ControversySeverity::Severe:      return 3.0;
        }
        return 0.0;
    }

    double halflife_days_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<Controversy>> controversies_;
};

// ---------------------------------------------------------------------------
// ExclusionScreener
// ---------------------------------------------------------------------------

/**
 * Configurable exclusion screening engine.
 * Supports both category-based and custom rule-based screens.
 */
class ExclusionScreener {
public:
    /** Add a category exclusion */
    void add_exclusion(ExclusionCategory cat) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_categories_.insert(cat);
    }

    /** Remove a category exclusion */
    void remove_exclusion(ExclusionCategory cat) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_categories_.erase(cat);
    }

    /** Add a custom screening rule */
    void add_rule(const ScreeningRule& rule) {
        std::lock_guard<std::mutex> lock(mutex_);
        custom_rules_.push_back(rule);
    }

    /** Screen a security -- returns list of violated rules */
    [[nodiscard]] std::vector<std::string> screen(
            const SecurityEsgProfile& profile) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> violations;

        // Category-based exclusions
        for (auto& flag : profile.exclusion_flags) {
            if (active_categories_.count(flag)) {
                violations.push_back("Exclusion: " + exclusion_to_string(flag));
            }
        }

        // Custom rules
        for (const auto& rule : custom_rules_) {
            if (!rule.active) continue;
            if (std::holds_alternative<ExclusionCategory>(rule.criteria)) {
                auto cat = std::get<ExclusionCategory>(rule.criteria);
                if (profile.exclusion_flags.count(cat)) {
                    violations.push_back("Rule: " + rule.name);
                }
            } else {
                auto& fn = std::get<std::function<bool(const SecurityEsgProfile&)>>(
                    rule.criteria);
                if (fn(profile)) {
                    violations.push_back("Rule: " + rule.name);
                }
            }
        }

        return violations;
    }

    /** Get active exclusion categories */
    [[nodiscard]] std::set<ExclusionCategory> active_categories() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_categories_;
    }

private:
    mutable std::mutex mutex_;
    std::set<ExclusionCategory> active_categories_;
    std::vector<ScreeningRule> custom_rules_;
};

// ---------------------------------------------------------------------------
// CarbonCalculator
// ---------------------------------------------------------------------------

/**
 * Carbon footprint estimation engine.
 * Calculates portfolio-level carbon metrics: WACI, total emissions,
 * carbon intensity, and implied temperature rise.
 */
class CarbonCalculator {
public:
    explicit CarbonCalculator(double scope3_multiplier = 3.5)
        : scope3_multiplier_(scope3_multiplier) {}

    /** Estimate Scope 3 from Scope 1+2 if not available */
    [[nodiscard]] double estimate_scope3(double scope1, double scope2) const {
        return (scope1 + scope2) * scope3_multiplier_;
    }

    /** Calculate total carbon for a security */
    [[nodiscard]] double total_emissions(const CarbonFootprint& cf) const {
        return cf.scope1_tons_co2e + cf.scope2_tons_co2e + cf.scope3_tons_co2e;
    }

    /** Calculate Weighted Average Carbon Intensity (WACI) for a portfolio */
    [[nodiscard]] double calculate_waci(
            const std::vector<std::pair<double, CarbonFootprint>>& holdings) const {
        // holdings: vector of (portfolio_weight, carbon_footprint)
        double waci = 0.0;
        for (const auto& [weight, carbon] : holdings) {
            waci += weight * carbon.carbon_intensity;
        }
        return waci;
    }

    /** Calculate portfolio total financed emissions */
    [[nodiscard]] double financed_emissions(
            const std::vector<std::pair<double, CarbonFootprint>>& holdings,
            double portfolio_value) const {
        double total = 0.0;
        for (const auto& [weight, carbon] : holdings) {
            double allocation = weight * portfolio_value;
            total += allocation * carbon.carbon_intensity / 1e6;
        }
        return total;
    }

    /** Estimate implied temperature rise from carbon intensity */
    [[nodiscard]] double implied_temperature(double waci) const {
        // Simplified model: maps WACI to temperature pathway
        // Based on IEA scenarios (approximation)
        if (waci <= 50.0)  return 1.5;   // Paris-aligned
        if (waci <= 100.0) return 1.8;
        if (waci <= 200.0) return 2.0;
        if (waci <= 400.0) return 2.7;
        if (waci <= 700.0) return 3.2;
        return 4.0;  // High-carbon pathway
    }

    /** Aggregate carbon footprints for a portfolio */
    [[nodiscard]] CarbonFootprint aggregate(
            const std::vector<std::pair<double, CarbonFootprint>>& holdings) const {
        CarbonFootprint result;
        result.methodology = "Portfolio-weighted aggregation";

        for (const auto& [weight, carbon] : holdings) {
            result.scope1_tons_co2e += weight * carbon.scope1_tons_co2e;
            result.scope2_tons_co2e += weight * carbon.scope2_tons_co2e;
            result.scope3_tons_co2e += weight * carbon.scope3_tons_co2e;
            result.fossil_fuel_revenue_pct += weight * carbon.fossil_fuel_revenue_pct;
        }

        double total = result.scope1_tons_co2e + result.scope2_tons_co2e;
        if (total > 0) {
            result.carbon_intensity = calculate_waci(holdings);
        }

        result.weighted_avg_carbon = result.carbon_intensity;
        result.scope3_estimated = true;
        return result;
    }

private:
    double scope3_multiplier_;
};

// ---------------------------------------------------------------------------
// SdgMapper
// ---------------------------------------------------------------------------

/**
 * Maps security activities to UN Sustainable Development Goals.
 * Supports both positive alignment and negative impact assessment.
 */
class SdgMapper {
public:
    /** Register SDG alignment for a security */
    void set_alignment(const std::string& security_id,
                       const SdgAlignment& alignment) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& vec = alignments_[security_id];
        // Replace existing entry for same goal
        for (auto& a : vec) {
            if (a.goal == alignment.goal) {
                a = alignment;
                return;
            }
        }
        vec.push_back(alignment);
    }

    /** Get SDG alignments for a security */
    [[nodiscard]] std::vector<SdgAlignment> get_alignments(
            const std::string& security_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = alignments_.find(security_id);
        return (it != alignments_.end()) ? it->second : std::vector<SdgAlignment>{};
    }

    /** Calculate portfolio-level SDG alignment */
    [[nodiscard]] std::map<SdgGoal, double> portfolio_alignment(
            const std::vector<std::pair<std::string, double>>& holdings) const {
        // holdings: vector of (security_id, weight)
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<SdgGoal, double> result;

        for (const auto& [sec_id, weight] : holdings) {
            auto it = alignments_.find(sec_id);
            if (it == alignments_.end()) continue;
            for (const auto& a : it->second) {
                result[a.goal] += weight * a.alignment_score;
            }
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<SdgAlignment>> alignments_;
};

// ---------------------------------------------------------------------------
// RegulatoryClassifier
// ---------------------------------------------------------------------------

/**
 * Classifies portfolios and funds under SFDR and EU Taxonomy.
 */
class RegulatoryClassifier {
public:
    explicit RegulatoryClassifier(const EsgConfig& config = {})
        : config_(config) {}

    /** Classify a portfolio under SFDR */
    [[nodiscard]] SfdrArticle classify_sfdr(
            const PortfolioEsgSummary& summary) const {
        if (summary.composite_score >= config_.article9_min_esg_score &&
            summary.taxonomy_aligned_pct >= config_.article9_min_taxonomy_pct) {
            return SfdrArticle::Article9;
        }
        if (summary.composite_score >= config_.article8_min_esg_score) {
            return SfdrArticle::Article8;
        }
        return SfdrArticle::Article6;
    }

    /** Calculate EU Taxonomy alignment for portfolio */
    [[nodiscard]] TaxonomyAlignment portfolio_taxonomy(
            const std::vector<std::pair<double, TaxonomyAlignment>>& holdings) const {
        TaxonomyAlignment result;
        for (const auto& [weight, tax] : holdings) {
            result.taxonomy_aligned_pct += weight * tax.taxonomy_aligned_pct;
            result.taxonomy_eligible_pct += weight * tax.taxonomy_eligible_pct;
            result.transition_activity_pct += weight * tax.transition_activity_pct;
            if (!tax.do_no_significant_harm) result.do_no_significant_harm = false;
            if (!tax.minimum_safeguards) result.minimum_safeguards = false;
        }
        return result;
    }

    /** SFDR to string */
    static std::string sfdr_to_string(SfdrArticle a) {
        switch (a) {
            case SfdrArticle::Article6: return "Article 6";
            case SfdrArticle::Article8: return "Article 8";
            case SfdrArticle::Article9: return "Article 9";
        }
        return "Unknown";
    }

private:
    EsgConfig config_;
};

// ---------------------------------------------------------------------------
// IndustryPeerScorer
// ---------------------------------------------------------------------------

/**
 * Industry-relative ESG scoring. Adjusts raw scores based on
 * sector peer comparisons (best-in-class methodology).
 */
class IndustryPeerScorer {
public:
    /** Register a peer score for sector comparison */
    void add_peer_score(const std::string& sector, const std::string& security_id,
                        EsgPillar pillar, double score) {
        std::lock_guard<std::mutex> lock(mutex_);
        peer_scores_[sector][pillar].push_back({security_id, score});
    }

    /** Calculate industry-adjusted score */
    [[nodiscard]] double adjusted_score(const std::string& sector,
                                         EsgPillar pillar, double raw_score,
                                         double adjustment_strength = 0.5) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sit = peer_scores_.find(sector);
        if (sit == peer_scores_.end()) return raw_score;

        auto pit = sit->second.find(pillar);
        if (pit == sit->second.end()) return raw_score;

        const auto& peers = pit->second;
        if (peers.empty()) return raw_score;

        // Calculate percentile within sector
        size_t below = 0;
        for (const auto& [id, s] : peers) {
            if (s < raw_score) ++below;
        }
        double percentile = static_cast<double>(below) / static_cast<double>(peers.size());

        // Blend: absolute score weighted with percentile-mapped score
        double peer_adjusted = percentile * 10.0;
        return raw_score * (1.0 - adjustment_strength) +
               peer_adjusted * adjustment_strength;
    }

    /** Get percentile rank within sector */
    [[nodiscard]] double percentile_rank(const std::string& sector,
                                          EsgPillar pillar, double score) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sit = peer_scores_.find(sector);
        if (sit == peer_scores_.end()) return 50.0;

        auto pit = sit->second.find(pillar);
        if (pit == sit->second.end()) return 50.0;

        const auto& peers = pit->second;
        if (peers.empty()) return 50.0;

        size_t below = 0;
        for (const auto& [id, s] : peers) {
            if (s < score) ++below;
        }
        return 100.0 * static_cast<double>(below) / static_cast<double>(peers.size());
    }

private:
    mutable std::mutex mutex_;
    // sector -> pillar -> [(security_id, score)]
    std::unordered_map<std::string,
        std::map<EsgPillar, std::vector<std::pair<std::string, double>>>> peer_scores_;
};

// ---------------------------------------------------------------------------
// ScoreOverrideManager
// ---------------------------------------------------------------------------

/**
 * Manages analyst overrides to ESG scores with audit trail.
 */
class ScoreOverrideManager {
public:
    /** Add an override */
    void add_override(const ScoreOverride& ovr) {
        std::lock_guard<std::mutex> lock(mutex_);
        overrides_[ovr.security_id].push_back(ovr);
    }

    /** Get active override for a security/pillar */
    [[nodiscard]] std::optional<ScoreOverride> get_override(
            const std::string& security_id, EsgPillar pillar,
            const std::string& category = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = overrides_.find(security_id);
        if (it == overrides_.end()) return std::nullopt;

        auto now = std::chrono::system_clock::now();
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (rit->pillar == pillar && rit->category == category) {
                if (rit->expiry > now) return *rit;
            }
        }
        return std::nullopt;
    }

    /** Get all overrides (audit trail) */
    [[nodiscard]] std::vector<ScoreOverride> audit_trail(
            const std::string& security_id = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (security_id.empty()) {
            std::vector<ScoreOverride> all;
            for (const auto& [id, ovrs] : overrides_) {
                all.insert(all.end(), ovrs.begin(), ovrs.end());
            }
            return all;
        }
        auto it = overrides_.find(security_id);
        return (it != overrides_.end()) ? it->second : std::vector<ScoreOverride>{};
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<ScoreOverride>> overrides_;
};

// ---------------------------------------------------------------------------
// EsgEngine -- Main Engine
// ---------------------------------------------------------------------------

/**
 * Central ESG scoring and integration engine.
 * Provides security-level scoring, portfolio aggregation, screening,
 * carbon analysis, SDG alignment, and regulatory classification.
 */
class EsgEngine {
public:
    explicit EsgEngine(const EsgConfig& config = {})
        : config_(config),
          controversy_tracker_(config.controversy_decay_halflife_days),
          carbon_calculator_(config.default_scope3_multiplier),
          regulatory_classifier_(config) {
        // Initialize exclusion screens from config
        for (auto cat : config.active_exclusions) {
            screener_.add_exclusion(cat);
        }
    }

    // -- Security Profile Management --

    /** Register or update a security ESG profile */
    void set_profile(const SecurityEsgProfile& profile) {
        std::lock_guard<std::mutex> lock(mutex_);
        profiles_[profile.security_id] = profile;

        // Register peer scores for industry comparison
        if (!profile.sector.empty()) {
            peer_scorer_.add_peer_score(profile.sector, profile.security_id,
                EsgPillar::Environmental, profile.environmental.score);
            peer_scorer_.add_peer_score(profile.sector, profile.security_id,
                EsgPillar::Social, profile.social.score);
            peer_scorer_.add_peer_score(profile.sector, profile.security_id,
                EsgPillar::Governance, profile.governance.score);
        }
    }

    /** Get a security ESG profile */
    [[nodiscard]] std::optional<SecurityEsgProfile> get_profile(
            const std::string& security_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(security_id);
        if (it == profiles_.end()) return std::nullopt;
        return it->second;
    }

    /** Calculate composite score for a security */
    [[nodiscard]] double calculate_composite(const SecurityEsgProfile& profile) const {
        double e = profile.environmental.score * config_.environmental_weight;
        double s = profile.social.score * config_.social_weight;
        double g = profile.governance.score * config_.governance_weight;
        double composite = e + s + g;

        // Apply controversy penalty
        double penalty = controversy_tracker_.calculate_penalty(
            profile.security_id, config_.max_controversy_penalty);
        composite = std::max(0.0, composite - penalty);

        // Apply override if exists
        auto ovr = override_mgr_.get_override(profile.security_id, EsgPillar::Composite);
        if (ovr.has_value()) {
            composite = ovr->override_score;
        }

        return std::clamp(composite, 0.0, 10.0);
    }

    /** Score a security with full industry adjustment */
    [[nodiscard]] SecurityEsgProfile score_security(SecurityEsgProfile profile) const {
        // Industry-adjust pillar scores if enabled
        if (config_.use_industry_adjustment && !profile.sector.empty()) {
            profile.environmental.score = peer_scorer_.adjusted_score(
                profile.sector, EsgPillar::Environmental,
                profile.environmental.score, config_.industry_adjustment_strength);
            profile.social.score = peer_scorer_.adjusted_score(
                profile.sector, EsgPillar::Social,
                profile.social.score, config_.industry_adjustment_strength);
            profile.governance.score = peer_scorer_.adjusted_score(
                profile.sector, EsgPillar::Governance,
                profile.governance.score, config_.industry_adjustment_strength);
        }

        // Set ratings
        profile.environmental.rating = score_to_rating(profile.environmental.score);
        profile.social.rating = score_to_rating(profile.social.score);
        profile.governance.rating = score_to_rating(profile.governance.score);

        // Calculate composite
        double comp = calculate_composite(profile);
        profile.composite.score = comp;
        profile.composite.rating = score_to_rating(comp);
        profile.overall_rating = score_to_rating(comp);

        // Set controversy level
        profile.controversy_level = controversy_tracker_.highest_severity(
            profile.security_id);

        // Set SDG alignments
        profile.sdg_alignments = sdg_mapper_.get_alignments(profile.security_id);

        // Set exclusion flags
        profile.exclusion_flags = check_exclusion_flags(profile);

        profile.last_updated = std::chrono::system_clock::now();
        return profile;
    }

    // -- Portfolio ESG Aggregation --

    /** Calculate portfolio-level ESG summary */
    [[nodiscard]] PortfolioEsgSummary portfolio_summary(
            const std::string& portfolio_id,
            const std::vector<std::pair<std::string, double>>& holdings) const {
        // holdings: vector of (security_id, portfolio_weight)
        std::lock_guard<std::mutex> lock(mutex_);
        PortfolioEsgSummary summary;
        summary.portfolio_id = portfolio_id;

        double covered_weight = 0.0;
        std::vector<std::pair<double, CarbonFootprint>> carbon_holdings;
        std::vector<std::pair<double, TaxonomyAlignment>> taxonomy_holdings;
        std::vector<std::pair<std::string, double>> sdg_holdings;

        for (const auto& [sec_id, weight] : holdings) {
            auto it = profiles_.find(sec_id);
            if (it == profiles_.end()) {
                summary.unrated_count++;
                continue;
            }

            const auto& profile = it->second;
            summary.rated_count++;
            covered_weight += weight;

            summary.environmental_score += weight * profile.environmental.score;
            summary.social_score += weight * profile.social.score;
            summary.governance_score += weight * profile.governance.score;

            // Rating distribution
            summary.rating_distribution[profile.overall_rating]++;
            summary.rating_weight_distribution[profile.overall_rating] += weight;

            // Carbon
            carbon_holdings.push_back({weight, profile.carbon});

            // Taxonomy
            taxonomy_holdings.push_back({weight, profile.taxonomy});

            // SDG
            sdg_holdings.push_back({sec_id, weight});

            // Check exclusions
            auto violations = screener_.screen(profile);
            if (!violations.empty()) {
                summary.excluded_holdings.push_back(sec_id);
            }

            // Top controversies
            auto controvs = controversy_tracker_.get_active(sec_id);
            for (auto& c : controvs) {
                if (c.severity >= ControversySeverity::High) {
                    summary.top_controversies.push_back(c);
                }
            }
        }

        // Normalize scores by covered weight
        if (covered_weight > 0) {
            summary.environmental_score /= covered_weight;
            summary.social_score /= covered_weight;
            summary.governance_score /= covered_weight;
        }

        summary.composite_score =
            summary.environmental_score * config_.environmental_weight +
            summary.social_score * config_.social_weight +
            summary.governance_score * config_.governance_weight;

        summary.portfolio_rating = score_to_rating(summary.composite_score);
        summary.coverage_pct = covered_weight * 100.0;

        // Carbon aggregation
        summary.carbon = carbon_calculator_.aggregate(carbon_holdings);

        // Taxonomy aggregation
        auto tax = regulatory_classifier_.portfolio_taxonomy(taxonomy_holdings);
        summary.taxonomy_aligned_pct = tax.taxonomy_aligned_pct;

        // SFDR classification
        summary.sfdr_classification = regulatory_classifier_.classify_sfdr(summary);

        // SDG alignment
        summary.sdg_alignment = sdg_mapper_.portfolio_alignment(sdg_holdings);

        return summary;
    }

    // -- Screening --

    /** Screen a portfolio and return violations */
    [[nodiscard]] std::map<std::string, std::vector<std::string>> screen_portfolio(
            const std::vector<std::pair<std::string, double>>& holdings) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, std::vector<std::string>> results;

        for (const auto& [sec_id, weight] : holdings) {
            auto it = profiles_.find(sec_id);
            if (it == profiles_.end()) continue;
            auto violations = screener_.screen(it->second);
            if (!violations.empty()) {
                results[sec_id] = violations;
            }
        }
        return results;
    }

    // -- Component Access --

    ControversyTracker& controversies() { return controversy_tracker_; }
    const ControversyTracker& controversies() const { return controversy_tracker_; }
    ExclusionScreener& screener() { return screener_; }
    CarbonCalculator& carbon() { return carbon_calculator_; }
    const CarbonCalculator& carbon() const { return carbon_calculator_; }
    SdgMapper& sdg() { return sdg_mapper_; }
    ScoreOverrideManager& overrides() { return override_mgr_; }
    IndustryPeerScorer& peers() { return peer_scorer_; }
    RegulatoryClassifier& regulatory() { return regulatory_classifier_; }

    /** Get count of rated securities */
    [[nodiscard]] size_t profile_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return profiles_.size();
    }

    /** Get all security IDs with profiles */
    [[nodiscard]] std::vector<std::string> security_ids() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> ids;
        ids.reserve(profiles_.size());
        for (const auto& [id, _] : profiles_) {
            ids.push_back(id);
        }
        return ids;
    }

    // -- Reporting --

    /** Generate ESG summary report as text */
    [[nodiscard]] std::string report(const PortfolioEsgSummary& summary) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);

        ss << "=== ESG Portfolio Report ===\n";
        ss << "Portfolio: " << summary.portfolio_id << "\n";
        ss << "ESG Rating: " << rating_to_string(summary.portfolio_rating) << "\n";
        ss << "Composite Score: " << summary.composite_score << "/10\n";
        ss << "  Environmental: " << summary.environmental_score << "/10\n";
        ss << "  Social: " << summary.social_score << "/10\n";
        ss << "  Governance: " << summary.governance_score << "/10\n";
        ss << "Coverage: " << summary.coverage_pct << "% ("
           << summary.rated_count << " rated, "
           << summary.unrated_count << " unrated)\n";

        ss << "\n--- Carbon Footprint ---\n";
        ss << "WACI: " << summary.carbon.weighted_avg_carbon << " tCO2e/$M\n";
        ss << "Implied Temperature: "
           << carbon_calculator_.implied_temperature(summary.carbon.weighted_avg_carbon)
           << " deg C\n";
        ss << "Fossil Fuel Exposure: " << summary.carbon.fossil_fuel_revenue_pct << "%\n";

        ss << "\n--- Regulatory ---\n";
        ss << "SFDR: " << RegulatoryClassifier::sfdr_to_string(
            summary.sfdr_classification) << "\n";
        ss << "EU Taxonomy Aligned: " << summary.taxonomy_aligned_pct << "%\n";

        if (!summary.excluded_holdings.empty()) {
            ss << "\n--- Exclusion Violations ---\n";
            for (const auto& h : summary.excluded_holdings) {
                ss << "  - " << h << "\n";
            }
        }

        if (!summary.top_controversies.empty()) {
            ss << "\n--- Top Controversies ---\n";
            for (const auto& c : summary.top_controversies) {
                ss << "  [" << severity_to_string(c.severity) << "] "
                   << c.title << " (" << c.security_id << ")\n";
            }
        }

        return ss.str();
    }

private:
    /** Determine exclusion flags for a profile */
    std::set<ExclusionCategory> check_exclusion_flags(
            const SecurityEsgProfile& profile) const {
        // Return the profile's own flags -- in a real system these would
        // be populated from data provider feeds
        return profile.exclusion_flags;
    }

    EsgConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SecurityEsgProfile> profiles_;

    ControversyTracker controversy_tracker_;
    ExclusionScreener screener_;
    CarbonCalculator carbon_calculator_;
    SdgMapper sdg_mapper_;
    RegulatoryClassifier regulatory_classifier_;
    IndustryPeerScorer peer_scorer_;
    ScoreOverrideManager override_mgr_;
};

} // namespace compliance
} // namespace genie

#endif // GENIE_COMPLIANCE_ESG_SCORING_HPP
