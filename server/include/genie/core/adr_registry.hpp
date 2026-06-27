/**
 * @file adr_registry.hpp
 * @brief Architecture Decision Records (ADR) framework
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Manages architecture decisions as first-class objects:
 * - Structured ADR creation and management
 * - Decision lifecycle (proposed -> accepted/superseded/deprecated)
 * - Impact analysis and cross-referencing
 * - ADR export to Markdown and JSON
 * - Decision audit trail
 * - Template-based ADR generation
 * - Searchable decision catalog
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_ADR_REGISTRY_HPP
#define GENIE_CORE_ADR_REGISTRY_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <optional>
#include <chrono>
#include <set>
#include <functional>

namespace genie {
namespace core {
namespace adr {

// ============================================================================
// Enumerations
// ============================================================================

enum class AdrStatus {
    Proposed,
    Accepted,
    Deprecated,
    Superseded,
    Rejected
};

enum class AdrImpact {
    Low,
    Medium,
    High,
    Critical
};

enum class AdrCategory {
    Architecture,
    Technology,
    Security,
    Performance,
    DataModel,
    Integration,
    Deployment,
    Testing,
    Compliance,
    Trading,
    Risk,
    Infrastructure
};

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] inline std::string adr_status_string(AdrStatus s) {
    switch (s) {
        case AdrStatus::Proposed:   return "Proposed";
        case AdrStatus::Accepted:   return "Accepted";
        case AdrStatus::Deprecated: return "Deprecated";
        case AdrStatus::Superseded: return "Superseded";
        case AdrStatus::Rejected:   return "Rejected";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string adr_impact_string(AdrImpact i) {
    switch (i) {
        case AdrImpact::Low:      return "Low";
        case AdrImpact::Medium:   return "Medium";
        case AdrImpact::High:     return "High";
        case AdrImpact::Critical: return "Critical";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string adr_category_string(AdrCategory c) {
    switch (c) {
        case AdrCategory::Architecture:   return "Architecture";
        case AdrCategory::Technology:     return "Technology";
        case AdrCategory::Security:       return "Security";
        case AdrCategory::Performance:    return "Performance";
        case AdrCategory::DataModel:      return "Data Model";
        case AdrCategory::Integration:    return "Integration";
        case AdrCategory::Deployment:     return "Deployment";
        case AdrCategory::Testing:        return "Testing";
        case AdrCategory::Compliance:     return "Compliance";
        case AdrCategory::Trading:        return "Trading";
        case AdrCategory::Risk:           return "Risk";
        case AdrCategory::Infrastructure: return "Infrastructure";
    }
    return "Unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Considered alternative in an ADR
 */
struct Alternative {
    std::string name;
    std::string description;
    std::vector<std::string> pros;
    std::vector<std::string> cons;
    bool chosen{false};
};

/**
 * @brief Architecture Decision Record
 */
struct Adr {
    int number{0};                 // ADR sequential number
    std::string id;                // e.g., "ADR-0001"
    std::string title;
    AdrStatus status{AdrStatus::Proposed};
    AdrCategory category{AdrCategory::Architecture};
    AdrImpact impact{AdrImpact::Medium};
    
    // Core sections
    std::string context;           // Problem statement
    std::string decision;          // What was decided
    std::string rationale;         // Why this decision
    std::string consequences;      // Positive and negative effects
    
    // Alternatives considered
    std::vector<Alternative> alternatives;
    
    // Metadata
    std::string author;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::string superseded_by;     // ADR ID if superseded
    std::vector<std::string> supersedes;  // ADR IDs this supersedes
    std::vector<std::string> related;     // Related ADR IDs
    std::vector<std::string> tags;
    std::vector<std::string> affected_components;
    
    /**
     * @brief Export as Markdown
     */
    [[nodiscard]] std::string to_markdown() const {
        std::ostringstream oss;
        
        oss << "# " << id << ": " << title << "\n\n";
        oss << "| Field | Value |\n";
        oss << "|-------|-------|\n";
        oss << "| Status | **" << adr_status_string(status) << "** |\n";
        oss << "| Category | " << adr_category_string(category) << " |\n";
        oss << "| Impact | " << adr_impact_string(impact) << " |\n";
        oss << "| Author | " << author << " |\n";
        
        auto t = std::chrono::system_clock::to_time_t(created_at);
        oss << "| Date | ";
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%d");
        oss << " |\n\n";
        
        if (!superseded_by.empty()) {
            oss << "> **Superseded by " << superseded_by << "**\n\n";
        }
        
        oss << "## Context\n\n" << context << "\n\n";
        oss << "## Decision\n\n" << decision << "\n\n";
        
        if (!alternatives.empty()) {
            oss << "## Alternatives Considered\n\n";
            for (const auto& alt : alternatives) {
                oss << "### " << alt.name << (alt.chosen ? " ✅ (Chosen)" : "") << "\n\n";
                oss << alt.description << "\n\n";
                if (!alt.pros.empty()) {
                    oss << "**Pros:**\n";
                    for (const auto& p : alt.pros) oss << "- " << p << "\n";
                    oss << "\n";
                }
                if (!alt.cons.empty()) {
                    oss << "**Cons:**\n";
                    for (const auto& c : alt.cons) oss << "- " << c << "\n";
                    oss << "\n";
                }
            }
        }
        
        if (!rationale.empty())
            oss << "## Rationale\n\n" << rationale << "\n\n";
        
        if (!consequences.empty())
            oss << "## Consequences\n\n" << consequences << "\n\n";
        
        if (!affected_components.empty()) {
            oss << "## Affected Components\n\n";
            for (const auto& c : affected_components) oss << "- " << c << "\n";
            oss << "\n";
        }
        
        if (!related.empty()) {
            oss << "## Related Decisions\n\n";
            for (const auto& r : related) oss << "- " << r << "\n";
            oss << "\n";
        }
        
        return oss.str();
    }
    
    /**
     * @brief Export as JSON
     */
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"id\": \"" << id << "\",\n";
        oss << "  \"number\": " << number << ",\n";
        oss << "  \"title\": \"" << title << "\",\n";
        oss << "  \"status\": \"" << adr_status_string(status) << "\",\n";
        oss << "  \"category\": \"" << adr_category_string(category) << "\",\n";
        oss << "  \"impact\": \"" << adr_impact_string(impact) << "\",\n";
        oss << "  \"author\": \"" << author << "\",\n";
        oss << "  \"context\": \"" << context << "\",\n";
        oss << "  \"decision\": \"" << decision << "\",\n";
        oss << "  \"tags\": [";
        for (size_t i = 0; i < tags.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << "\"" << tags[i] << "\"";
        }
        oss << "]\n";
        oss << "}";
        return oss.str();
    }
};

// ============================================================================
// ADR Registry
// ============================================================================

/**
 * @brief Central registry for Architecture Decision Records
 */
class AdrRegistry {
public:
    AdrRegistry() {
        register_platform_adrs();
    }
    
    /**
     * @brief Create new ADR
     */
    Adr& create(const std::string& title, AdrCategory category = AdrCategory::Architecture) {
        std::lock_guard<std::mutex> lock(mutex_);
        int num = next_number_++;
        
        std::ostringstream id_oss;
        id_oss << "ADR-" << std::setfill('0') << std::setw(4) << num;
        
        Adr adr;
        adr.number = num;
        adr.id = id_oss.str();
        adr.title = title;
        adr.category = category;
        adr.created_at = std::chrono::system_clock::now();
        adr.updated_at = adr.created_at;
        
        adrs_[adr.id] = adr;
        return adrs_[adr.id];
    }
    
    /**
     * @brief Get ADR by ID
     */
    [[nodiscard]] std::optional<Adr> get(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = adrs_.find(id);
        if (it == adrs_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Update ADR status
     */
    bool update_status(const std::string& id, AdrStatus new_status) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = adrs_.find(id);
        if (it == adrs_.end()) return false;
        it->second.status = new_status;
        it->second.updated_at = std::chrono::system_clock::now();
        return true;
    }
    
    /**
     * @brief Supersede an ADR with a new one
     */
    bool supersede(const std::string& old_id, const std::string& new_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto old_it = adrs_.find(old_id);
        auto new_it = adrs_.find(new_id);
        if (old_it == adrs_.end() || new_it == adrs_.end()) return false;
        
        old_it->second.status = AdrStatus::Superseded;
        old_it->second.superseded_by = new_id;
        old_it->second.updated_at = std::chrono::system_clock::now();
        
        new_it->second.supersedes.push_back(old_id);
        new_it->second.updated_at = std::chrono::system_clock::now();
        return true;
    }
    
    /**
     * @brief List all ADRs
     */
    [[nodiscard]] std::vector<Adr> list(std::optional<AdrStatus> status_filter = std::nullopt,
                                          std::optional<AdrCategory> category_filter = std::nullopt) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Adr> result;
        for (const auto& [id, adr] : adrs_) {
            if (status_filter && adr.status != *status_filter) continue;
            if (category_filter && adr.category != *category_filter) continue;
            result.push_back(adr);
        }
        std::sort(result.begin(), result.end(),
                  [](const Adr& a, const Adr& b) { return a.number < b.number; });
        return result;
    }
    
    /**
     * @brief Search ADRs by keyword
     */
    [[nodiscard]] std::vector<Adr> search(const std::string& keyword) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string kw_lower = keyword;
        std::transform(kw_lower.begin(), kw_lower.end(), kw_lower.begin(), ::tolower);
        
        std::vector<Adr> result;
        for (const auto& [id, adr] : adrs_) {
            std::string searchable = adr.title + " " + adr.context + " " + 
                                      adr.decision + " " + adr.rationale;
            std::transform(searchable.begin(), searchable.end(), searchable.begin(), ::tolower);
            if (searchable.find(kw_lower) != std::string::npos) {
                result.push_back(adr);
            }
        }
        return result;
    }
    
    /**
     * @brief Export all ADRs to Markdown index
     */
    [[nodiscard]] std::string export_index_markdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "# Architecture Decision Records\n\n";
        oss << "## Metis Genie Platform Investment Management Platform v5.3.1\n\n";
        oss << "| ADR | Title | Status | Category | Impact |\n";
        oss << "|-----|-------|--------|----------|--------|\n";
        
        std::vector<const Adr*> sorted;
        for (const auto& [id, adr] : adrs_) sorted.push_back(&adr);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Adr* a, const Adr* b) { return a->number < b->number; });
        
        for (const auto* adr : sorted) {
            oss << "| " << adr->id << " | " << adr->title 
                << " | " << adr_status_string(adr->status)
                << " | " << adr_category_string(adr->category)
                << " | " << adr_impact_string(adr->impact) << " |\n";
        }
        
        oss << "\n---\n\n";
        
        // Full ADR content
        for (const auto* adr : sorted) {
            oss << adr->to_markdown() << "\n---\n\n";
        }
        
        return oss.str();
    }
    
    /**
     * @brief Statistics
     */
    struct Stats {
        int total{0};
        int accepted{0};
        int proposed{0};
        int deprecated{0};
        int superseded{0};
        int rejected{0};
        std::map<std::string, int> by_category;
        std::map<std::string, int> by_impact;
    };
    
    [[nodiscard]] Stats statistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats s;
        s.total = static_cast<int>(adrs_.size());
        for (const auto& [id, adr] : adrs_) {
            switch (adr.status) {
                case AdrStatus::Accepted:   ++s.accepted; break;
                case AdrStatus::Proposed:   ++s.proposed; break;
                case AdrStatus::Deprecated: ++s.deprecated; break;
                case AdrStatus::Superseded: ++s.superseded; break;
                case AdrStatus::Rejected:   ++s.rejected; break;
            }
            ++s.by_category[adr_category_string(adr.category)];
            ++s.by_impact[adr_impact_string(adr.impact)];
        }
        return s;
    }
    
    [[nodiscard]] int count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(adrs_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Adr> adrs_;
    int next_number_{1};
    
    void register_platform_adrs() {
        // ADR-0001: Header-Only Architecture
        {
            auto& adr = create("Header-Only C++ Library Architecture", AdrCategory::Architecture);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::Critical;
            adr.author = "Bennie Shearer";
            adr.context = "The platform needs a build architecture that minimizes compilation complexity, "
                          "eliminates link-time issues across platforms (Windows/Linux/macOS), and enables "
                          "rapid prototyping without complex build system configuration.";
            adr.decision = "Implement the entire platform as a header-only C++ library. All implementation "
                           "code resides in .hpp files with inline definitions. The only compiled unit is "
                           "main.cpp and the test suite.";
            adr.rationale = "Header-only libraries provide zero-friction integration, eliminate platform-specific "
                            "linker configuration, and allow single-file inclusion for any module.";
            adr.consequences = "Positive: Simple builds, easy integration, single-include usage. "
                               "Negative: Longer compile times, potential ODR violations if not careful, "
                               "all code exposed in headers.";
            adr.alternatives = {
                {"Static Library", "Compile each module to .a/.lib", {"Faster incremental builds", "Code hiding"}, {"Complex CMake setup", "Platform-specific linking"}, false},
                {"Shared Library", "Compile to .so/.dll/.dylib", {"Runtime flexibility", "Smaller executables"}, {"DLL hell", "Symbol export complexity"}, false},
                {"Header-Only", "All code in .hpp files", {"Zero build complexity", "Single include"}, {"Longer full builds"}, true}
            };
            adr.tags = {"build", "architecture", "c++20"};
            adr.affected_components = {"All modules"};
        }
        
        // ADR-0002: Zero External Dependencies
        {
            auto& adr = create("Zero External Dependencies Policy", AdrCategory::Technology);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::High;
            adr.author = "Bennie Shearer";
            adr.context = "Enterprise environments often restrict third-party dependencies due to licensing, "
                          "security review, and supply chain concerns. The platform must compile and run "
                          "with only the C++ standard library.";
            adr.decision = "No external library dependencies. All functionality implemented using only "
                           "the C++20 standard library. Platform APIs (sockets, threads) use OS-level "
                           "APIs with cross-platform abstractions.";
            adr.rationale = "Eliminates dependency management, license compliance concerns, version conflicts, "
                            "and supply chain security risks.";
            adr.consequences = "Positive: No dependency vulnerabilities, simple deployment, no package manager needed. "
                               "Negative: More implementation effort, no battle-tested crypto or HTTP libraries.";
            adr.tags = {"dependencies", "security", "policy"};
            adr.affected_components = {"All modules"};
        }
        
        // ADR-0003: Cross-Platform via C++20 Standard
        {
            auto& adr = create("C++20 Standard for Cross-Platform Support", AdrCategory::Technology);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::High;
            adr.author = "Bennie Shearer";
            adr.context = "The platform must run on Windows (MinGW/MSVC), Linux (GCC), and macOS (Clang) "
                          "without platform-specific code branches where possible.";
            adr.decision = "Target C++20 standard as the minimum language version. Use std::filesystem, "
                           "std::chrono, std::jthread, concepts, and ranges where supported. Use "
                           "preprocessor guards only for OS-level API differences (sockets, signals).";
            adr.rationale = "C++20 provides sufficient standard library support for most platform-independent "
                            "operations, reducing the need for platform-specific code.";
            adr.consequences = "Positive: Single codebase for all platforms, modern language features. "
                               "Negative: Requires recent compiler versions, some C++20 features vary by compiler.";
            adr.tags = {"c++20", "cross-platform", "compatibility"};
            adr.affected_components = {"Core", "Build System"};
        }
        
        // ADR-0004: Event-Driven Message Bus
        {
            auto& adr = create("Event-Driven Message Bus for Module Communication", AdrCategory::Architecture);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::High;
            adr.author = "Bennie Shearer";
            adr.context = "Modules (trading, risk, portfolio, market data) need to communicate state changes "
                          "without tight coupling. Direct function calls create circular dependencies.";
            adr.decision = "Implement a central message bus with publish/subscribe pattern, topic-based routing, "
                           "and dead letter queue for failed message handling.";
            adr.rationale = "Pub/sub decouples producers from consumers, enables event sourcing for audit trails, "
                            "and supports future microservices decomposition.";
            adr.consequences = "Positive: Loose coupling, event sourcing, audit trail. "
                               "Negative: Eventual consistency, debugging complexity, message ordering.";
            adr.tags = {"messaging", "events", "decoupling"};
            adr.affected_components = {"Core", "Trading", "Risk", "Portfolio", "Market Data"};
        }
        
        // ADR-0005: Database Migration Framework
        {
            auto& adr = create("Schema Migration Framework for Data Persistence", AdrCategory::DataModel);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::Medium;
            adr.author = "Bennie Shearer";
            adr.context = "The platform requires versioned schema management for its persistence layer. "
                          "Manual SQL scripts are error-prone and don't support rollback.";
            adr.decision = "Implement a migration framework with numbered migrations, dependency resolution, "
                           "up/down support, seed data management, and dry-run capability.";
            adr.rationale = "Migrations enable reproducible schema evolution, safe rollbacks, and automated "
                            "deployment pipelines.";
            adr.tags = {"database", "migrations", "schema"};
            adr.affected_components = {"Persistence", "Core"};
        }
        
        // ADR-0006: Cryptocurrency Trading Module
        {
            auto& adr = create("Cryptocurrency Trading Support", AdrCategory::Trading);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::High;
            adr.author = "Bennie Shearer";
            adr.context = "Institutional portfolios increasingly include digital assets. The platform needs "
                          "native support for cryptocurrency trading, wallet management, DeFi positions, "
                          "and staking alongside traditional assets.";
            adr.decision = "Add comprehensive crypto module supporting multiple exchanges, wallet management, "
                           "gas fee estimation, staking, DeFi positions, bridge transfers, and FATF "
                           "Travel Rule compliance.";
            adr.rationale = "Native crypto support enables unified portfolio view, consistent risk management, "
                            "and regulatory compliance across traditional and digital assets.";
            adr.tags = {"crypto", "trading", "defi", "compliance"};
            adr.affected_components = {"Trading", "Portfolio", "Compliance", "Risk"};
        }
        
        // ADR-0007: OpenAPI Documentation
        {
            auto& adr = create("OpenAPI 3.0 Auto-Generated Documentation", AdrCategory::Integration);
            adr.status = AdrStatus::Accepted;
            adr.impact = AdrImpact::Medium;
            adr.author = "Bennie Shearer";
            adr.context = "API consumers need machine-readable documentation for client generation, testing, "
                          "and integration. Manual documentation drifts from implementation.";
            adr.decision = "Generate OpenAPI 3.0 specifications from registered endpoint metadata. Include "
                           "Swagger UI HTML export for interactive documentation.";
            adr.rationale = "Auto-generation keeps docs in sync with code. OpenAPI is the industry standard "
                            "for REST API documentation.";
            adr.tags = {"api", "documentation", "openapi", "swagger"};
            adr.affected_components = {"Net", "REST API"};
        }
    }
};

} // namespace adr
} // namespace core
} // namespace genie

#endif // GENIE_CORE_ADR_REGISTRY_HPP
