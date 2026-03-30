/**
 * @file adr.hpp
 * @brief Architecture Decision Records (ADR) Management
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements ADR tracking:
 * - Structured decision records with status lifecycle
 * - Supersedence and amendment tracking
 * - Context, decision, consequences documentation
 * - Export to Markdown and JSON
 * - Query and search capabilities
 * - Compliance and audit integration
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_CORE_ADR_HPP
#define GENIE_CORE_ADR_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <optional>

namespace genie {
namespace core {
namespace adr {

// ============================================================================
// Enumerations
// ============================================================================

enum class AdrStatus { Proposed, Accepted, Deprecated, Superseded, Rejected, Amended };
enum class AdrCategory { Architecture, Security, Performance, DataModel, Integration, Operations, Compliance, Testing };

[[nodiscard]] inline std::string status_name(AdrStatus s) {
    switch (s) {
        case AdrStatus::Proposed:   return "Proposed";
        case AdrStatus::Accepted:   return "Accepted";
        case AdrStatus::Deprecated: return "Deprecated";
        case AdrStatus::Superseded: return "Superseded";
        case AdrStatus::Rejected:   return "Rejected";
        case AdrStatus::Amended:    return "Amended";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string category_name(AdrCategory c) {
    switch (c) {
        case AdrCategory::Architecture:  return "Architecture";
        case AdrCategory::Security:      return "Security";
        case AdrCategory::Performance:   return "Performance";
        case AdrCategory::DataModel:     return "Data Model";
        case AdrCategory::Integration:   return "Integration";
        case AdrCategory::Operations:    return "Operations";
        case AdrCategory::Compliance:    return "Compliance";
        case AdrCategory::Testing:       return "Testing";
    }
    return "Other";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Architecture Decision Record
 */
struct DecisionRecord {
    int number{0};                         // ADR-NNNN
    std::string title;
    AdrStatus status{AdrStatus::Proposed};
    AdrCategory category{AdrCategory::Architecture};
    std::string date;                      // Date proposed
    std::string last_updated;
    std::vector<std::string> authors;
    std::vector<std::string> reviewers;

    // Core content
    std::string context;                   // Why this decision is needed
    std::string decision;                  // What was decided
    std::string consequences;              // What results from the decision
    std::vector<std::string> alternatives; // Considered alternatives

    // Relationships
    int supersedes{0};                     // ADR number this supersedes
    int superseded_by{0};                  // ADR number that supersedes this
    int amends{0};                         // ADR number this amends
    std::vector<int> related;              // Related ADR numbers

    // Metadata
    std::vector<std::string> tags;
    std::string compliance_reference;      // Regulatory reference
    std::string notes;

    /**
     * @brief Export as Markdown
     */
    [[nodiscard]] std::string to_markdown() const {
        std::ostringstream oss;
        oss << "# ADR-" << std::setw(4) << std::setfill('0') << number
            << ": " << title << "\n\n";
        oss << "**Status:** " << status_name(status) << "  \n";
        oss << "**Date:** " << date << "  \n";
        oss << "**Category:** " << category_name(category) << "  \n";

        if (!authors.empty()) {
            oss << "**Authors:** ";
            for (size_t i = 0; i < authors.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << authors[i];
            }
            oss << "  \n";
        }

        if (supersedes > 0) {
            oss << "**Supersedes:** ADR-" << std::setw(4) << std::setfill('0')
                << supersedes << "  \n";
        }
        if (superseded_by > 0) {
            oss << "**Superseded by:** ADR-" << std::setw(4) << std::setfill('0')
                << superseded_by << "  \n";
        }

        oss << "\n## Context\n\n" << context << "\n";
        oss << "\n## Decision\n\n" << decision << "\n";

        if (!alternatives.empty()) {
            oss << "\n## Alternatives Considered\n\n";
            for (size_t i = 0; i < alternatives.size(); ++i) {
                oss << (i + 1) << ". " << alternatives[i] << "\n";
            }
        }

        oss << "\n## Consequences\n\n" << consequences << "\n";

        if (!notes.empty()) {
            oss << "\n## Notes\n\n" << notes << "\n";
        }

        return oss.str();
    }

    /**
     * @brief Export as JSON
     */
    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"number\": " << number << ",\n";
        oss << "  \"title\": \"" << escape(title) << "\",\n";
        oss << "  \"status\": \"" << status_name(status) << "\",\n";
        oss << "  \"category\": \"" << category_name(category) << "\",\n";
        oss << "  \"date\": \"" << date << "\",\n";
        oss << "  \"context\": \"" << escape(context) << "\",\n";
        oss << "  \"decision\": \"" << escape(decision) << "\",\n";
        oss << "  \"consequences\": \"" << escape(consequences) << "\"";

        if (!tags.empty()) {
            oss << ",\n  \"tags\": [";
            for (size_t i = 0; i < tags.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << tags[i] << "\"";
            }
            oss << "]";
        }

        oss << "\n}";
        return oss.str();
    }

private:
    [[nodiscard]] static std::string escape(const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c == '"') r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else r += c;
        }
        return r;
    }
};

// ============================================================================
// ADR Repository
// ============================================================================

/**
 * @brief ADR management system
 */
class AdrRepository {
public:
    AdrRepository() = default;

    /**
     * @brief Add a new ADR
     */
    int add(DecisionRecord adr) {
        std::lock_guard<std::mutex> lock(mutex_);
        adr.number = next_number_++;
        if (adr.date.empty()) adr.date = now_string();
        adr.last_updated = adr.date;
        records_[adr.number] = std::move(adr);
        return adr.number;
    }

    /**
     * @brief Get ADR by number
     */
    [[nodiscard]] std::optional<DecisionRecord> get(int number) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(number);
        if (it == records_.end()) return std::nullopt;
        return it->second;
    }

    /**
     * @brief Update ADR status
     */
    bool update_status(int number, AdrStatus status) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(number);
        if (it == records_.end()) return false;
        it->second.status = status;
        it->second.last_updated = now_string();
        return true;
    }

    /**
     * @brief Supersede an ADR with a new one
     */
    bool supersede(int old_number, int new_number) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto old_it = records_.find(old_number);
        auto new_it = records_.find(new_number);
        if (old_it == records_.end() || new_it == records_.end()) return false;

        old_it->second.status = AdrStatus::Superseded;
        old_it->second.superseded_by = new_number;
        old_it->second.last_updated = now_string();
        new_it->second.supersedes = old_number;
        new_it->second.last_updated = now_string();
        return true;
    }

    /**
     * @brief List all ADRs
     */
    [[nodiscard]] std::vector<DecisionRecord> list_all() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DecisionRecord> result;
        for (const auto& [_, r] : records_) result.push_back(r);
        return result;
    }

    /**
     * @brief Filter by status
     */
    [[nodiscard]] std::vector<DecisionRecord> by_status(AdrStatus status) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DecisionRecord> result;
        for (const auto& [_, r] : records_) {
            if (r.status == status) result.push_back(r);
        }
        return result;
    }

    /**
     * @brief Filter by category
     */
    [[nodiscard]] std::vector<DecisionRecord> by_category(AdrCategory category) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DecisionRecord> result;
        for (const auto& [_, r] : records_) {
            if (r.category == category) result.push_back(r);
        }
        return result;
    }

    /**
     * @brief Search ADRs by keyword
     */
    [[nodiscard]] std::vector<DecisionRecord> search(const std::string& keyword) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string lower_kw = to_lower(keyword);
        std::vector<DecisionRecord> result;
        for (const auto& [_, r] : records_) {
            if (contains_lower(r.title, lower_kw) ||
                contains_lower(r.context, lower_kw) ||
                contains_lower(r.decision, lower_kw) ||
                contains_lower(r.consequences, lower_kw)) {
                result.push_back(r);
            }
        }
        return result;
    }

    /**
     * @brief Export all as Markdown
     */
    [[nodiscard]] std::string export_markdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "# Architecture Decision Records\n\n";
        oss << "## Index\n\n";
        oss << "| # | Title | Status | Category | Date |\n";
        oss << "|---|-------|--------|----------|------|\n";
        for (const auto& [num, r] : records_) {
            oss << "| ADR-" << std::setw(4) << std::setfill('0') << num
                << " | " << r.title
                << " | " << status_name(r.status)
                << " | " << category_name(r.category)
                << " | " << r.date << " |\n";
        }
        oss << "\n---\n\n";
        for (const auto& [_, r] : records_) {
            oss << r.to_markdown() << "\n---\n\n";
        }
        return oss.str();
    }

    /**
     * @brief Count by status
     */
    [[nodiscard]] std::map<AdrStatus, int> status_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<AdrStatus, int> counts;
        for (const auto& [_, r] : records_) {
            counts[r.status]++;
        }
        return counts;
    }

    /**
     * @brief Pre-populate with platform ADRs
     */
    void populate_platform_adrs() {
        // ADR-0001: Header-Only Architecture
        DecisionRecord adr1;
        adr1.title = "Header-Only C++ Architecture";
        adr1.status = AdrStatus::Accepted;
        adr1.category = AdrCategory::Architecture;
        adr1.authors = {"Bennie Shearer"};
        adr1.context = "The platform needs to be easily distributed and compiled across "
                       "Windows, Linux, and macOS without complex build dependencies.";
        adr1.decision = "Implement the entire server as a header-only C++20 library with "
                        "zero external dependencies. All functionality is contained in .hpp files "
                        "under the include/genie directory tree.";
        adr1.alternatives = {
            "Traditional compilation unit (.cpp/.hpp) architecture",
            "CMake-based build with external dependency management",
            "Hybrid approach with precompiled core and header-only extensions"
        };
        adr1.consequences = "Simplifies distribution and compilation. Increases compile times for "
                           "full builds. Requires careful template design. Enables single-header "
                           "inclusion for any module.";
        adr1.tags = {"architecture", "cross-platform", "zero-dependencies"};
        add(adr1);

        // ADR-0002: REST API Design
        DecisionRecord adr2;
        adr2.title = "RESTful API with JSON Responses";
        adr2.status = AdrStatus::Accepted;
        adr2.category = AdrCategory::Integration;
        adr2.authors = {"Bennie Shearer"};
        adr2.context = "The platform needs a web API for client communication with support "
                       "for both synchronous requests and real-time data feeds.";
        adr2.decision = "Use REST API with JSON for request/response with WebSocket for "
                        "real-time streaming. API versioning via URL path (/api/v1/).";
        adr2.alternatives = {
            "GraphQL API",
            "gRPC with Protocol Buffers",
            "SOAP/XML web services"
        };
        adr2.consequences = "Wide client compatibility. Human-readable payloads. Standard HTTP "
                           "tooling support. WebSocket adds real-time capability without polling.";
        adr2.tags = {"api", "rest", "websocket"};
        add(adr2);

        // ADR-0003: Event-Driven Message Bus
        DecisionRecord adr3;
        adr3.title = "Internal Event-Driven Message Bus";
        adr3.status = AdrStatus::Accepted;
        adr3.category = AdrCategory::Architecture;
        adr3.authors = {"Bennie Shearer"};
        adr3.context = "Components need to communicate asynchronously without tight coupling. "
                       "Risk events, trade notifications, and market data updates must propagate "
                       "across modules.";
        adr3.decision = "Implement an in-process pub/sub message bus with topic-based routing, "
                        "dead letter queue, and circuit breaker pattern. No external message "
                        "broker dependency.";
        adr3.alternatives = {
            "RabbitMQ or Kafka integration",
            "Direct observer pattern between components",
            "Shared memory event queue"
        };
        adr3.consequences = "Zero-dependency event system. In-process only (no distributed messaging). "
                           "Circuit breaker prevents cascading failures. Dead letter queue captures "
                           "undeliverable messages for debugging.";
        adr3.tags = {"messaging", "events", "pub-sub"};
        add(adr3);

        // ADR-0004: Cryptocurrency Support
        DecisionRecord adr4;
        adr4.title = "Cryptocurrency Trading Module";
        adr4.status = AdrStatus::Accepted;
        adr4.category = AdrCategory::Architecture;
        adr4.authors = {"Bennie Shearer"};
        adr4.context = "Investment portfolios increasingly include digital assets. The platform "
                       "needs native cryptocurrency trading, wallet management, and DeFi integration.";
        adr4.decision = "Add a dedicated crypto_trading module with exchange adapters, wallet management, "
                        "gas estimation, staking, DeFi position tracking, travel rule compliance, "
                        "and multi-chain support.";
        adr4.alternatives = {
            "Third-party crypto API integration only",
            "Treat crypto as standard asset class without specialized module",
            "Separate microservice for crypto operations"
        };
        adr4.consequences = "Full-featured crypto support within the monolithic architecture. "
                           "Maintains zero-dependency principle. Requires ongoing updates for "
                           "new chains and protocols.";
        adr4.tags = {"crypto", "trading", "defi", "wallet"};
        add(adr4);

        // ADR-0005: Database Migration Framework
        DecisionRecord adr5;
        adr5.title = "Versioned Database Migration System";
        adr5.status = AdrStatus::Accepted;
        adr5.category = AdrCategory::DataModel;
        adr5.authors = {"Bennie Shearer"};
        adr5.context = "Schema evolution needs to be tracked, repeatable, and reversible across "
                       "development, staging, and production environments.";
        adr5.decision = "Implement a built-in migration engine with numbered up/down migrations, "
                        "schema builder DSL, checksum validation, and dry-run capability.";
        adr5.alternatives = {
            "Flyway or Liquibase integration",
            "Manual SQL scripts with version tracking",
            "ORM-based auto-migration"
        };
        adr5.consequences = "Self-contained migration system. Consistent schema management. "
                           "SQL export capability for DBA review. No external tool dependencies.";
        adr5.tags = {"database", "migration", "schema"};
        add(adr5);
    }

private:
    mutable std::mutex mutex_;
    std::map<int, DecisionRecord> records_;
    int next_number_{1};

    [[nodiscard]] static std::string now_string() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%d");
        return oss.str();
    }

    [[nodiscard]] static std::string to_lower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }

    [[nodiscard]] static bool contains_lower(const std::string& text, const std::string& keyword) {
        return to_lower(text).find(keyword) != std::string::npos;
    }
};

} // namespace adr
} // namespace core
} // namespace genie

#endif // GENIE_CORE_ADR_HPP
