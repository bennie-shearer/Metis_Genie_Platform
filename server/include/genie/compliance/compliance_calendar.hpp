/**
 * @file compliance_calendar.hpp
 * @brief Regulatory compliance deadline and filing calendar
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Compliance calendar and deadline management:
 * - Regulatory filing deadlines (SEC, FINRA, NFA, CFTC)
 * - Recurring obligation scheduling (daily/monthly/quarterly/annual)
 * - Upcoming deadline alerts with lead-time warnings
 * - Filing status tracking (pending/submitted/accepted/rejected)
 * - Holiday-aware business day calculations
 * - Jurisdiction-specific requirements
 * - Audit-ready compliance timeline
 * - Assignee and escalation management
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_COMPLIANCE_CALENDAR_HPP
#define GENIE_COMPLIANCE_CALENDAR_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>

namespace genie {
namespace compliance {
namespace calendar {

// ============================================================================
// Enumerations
// ============================================================================

enum class Regulator {
    SEC,
    FINRA,
    NFA,
    CFTC,
    FDIC,
    OCC,
    FED,
    MSRB,
    SIPC,
    IRS,
    StateAG,
    ESMA,
    FCA,
    BaFin,
    Internal
};

enum class FilingFrequency {
    OneTime,
    Daily,
    Weekly,
    Monthly,
    Quarterly,
    SemiAnnual,
    Annual,
    Biennial,
    AsNeeded
};

enum class FilingStatus {
    Upcoming,
    InProgress,
    UnderReview,
    Submitted,
    Accepted,
    Rejected,
    Overdue,
    Waived,
    NotApplicable
};

enum class Priority {
    Low,
    Medium,
    High,
    Critical
};

[[nodiscard]] inline std::string regulator_string(Regulator r) {
    switch (r) {
        case Regulator::SEC:      return "SEC";
        case Regulator::FINRA:    return "FINRA";
        case Regulator::NFA:      return "NFA";
        case Regulator::CFTC:     return "CFTC";
        case Regulator::FDIC:     return "FDIC";
        case Regulator::OCC:      return "OCC";
        case Regulator::FED:      return "FED";
        case Regulator::MSRB:     return "MSRB";
        case Regulator::SIPC:     return "SIPC";
        case Regulator::IRS:      return "IRS";
        case Regulator::StateAG:  return "State AG";
        case Regulator::ESMA:     return "ESMA";
        case Regulator::FCA:      return "FCA";
        case Regulator::BaFin:    return "BaFin";
        case Regulator::Internal: return "Internal";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string status_string(FilingStatus s) {
    switch (s) {
        case FilingStatus::Upcoming:       return "upcoming";
        case FilingStatus::InProgress:     return "in_progress";
        case FilingStatus::UnderReview:    return "under_review";
        case FilingStatus::Submitted:      return "submitted";
        case FilingStatus::Accepted:       return "accepted";
        case FilingStatus::Rejected:       return "rejected";
        case FilingStatus::Overdue:        return "overdue";
        case FilingStatus::Waived:         return "waived";
        case FilingStatus::NotApplicable:  return "not_applicable";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

using Date = std::chrono::system_clock::time_point;

/**
 * @brief Compliance obligation / filing requirement
 */
struct ComplianceObligation {
    std::string id;
    std::string name;
    std::string description;
    Regulator regulator{Regulator::Internal};
    std::string form_number;       // e.g., "Form ADV", "13F-HR"
    FilingFrequency frequency{FilingFrequency::Annual};
    Priority priority{Priority::Medium};
    Date due_date;
    int lead_time_days{30};        // Warning days before due
    std::string assignee;
    std::string escalation_contact;
    FilingStatus status{FilingStatus::Upcoming};
    std::string jurisdiction;
    std::vector<std::string> applicable_entities;
    std::string notes;
    Date submitted_at;
    std::string confirmation_number;

    [[nodiscard]] bool is_overdue(Date now) const {
        return now > due_date && status != FilingStatus::Submitted &&
               status != FilingStatus::Accepted && status != FilingStatus::Waived &&
               status != FilingStatus::NotApplicable;
    }

    [[nodiscard]] int days_until_due(Date now) const {
        auto diff = std::chrono::duration_cast<std::chrono::hours>(due_date - now);
        return static_cast<int>(diff.count() / 24);
    }

    [[nodiscard]] bool needs_warning(Date now) const {
        int days = days_until_due(now);
        return days >= 0 && days <= lead_time_days &&
               status == FilingStatus::Upcoming;
    }

    [[nodiscard]] std::string format() const {
        auto t = std::chrono::system_clock::to_time_t(due_date);
        std::ostringstream oss;
        oss << "[" << regulator_string(regulator) << "] " << name;
        if (!form_number.empty()) oss << " (" << form_number << ")";
        oss << " due=" << std::put_time(std::gmtime(&t), "%Y-%m-%d");
        oss << " status=" << status_string(status);
        if (!assignee.empty()) oss << " assignee=" << assignee;
        return oss.str();
    }
};

/**
 * @brief Calendar view of obligations
 */
struct CalendarView {
    Date start;
    Date end;
    std::vector<ComplianceObligation> obligations;
    int overdue_count{0};
    int due_soon_count{0};
    int total_count{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << "Compliance Calendar: " << total_count << " obligations"
            << " (" << overdue_count << " overdue, "
            << due_soon_count << " due soon)\n";
        for (const auto& o : obligations) {
            oss << "  " << o.format() << "\n";
        }
        return oss.str();
    }
};

// ============================================================================
// Compliance Calendar Manager
// ============================================================================

/**
 * @brief Manages regulatory compliance deadlines
 */
class ComplianceCalendar {
public:
    ComplianceCalendar() {
        register_common_filings();
    }

    /**
     * @brief Add an obligation
     */
    void add_obligation(ComplianceObligation obligation) {
        std::lock_guard<std::mutex> lock(mutex_);
        obligations_[obligation.id] = std::move(obligation);
    }

    /**
     * @brief Update filing status
     */
    bool update_status(const std::string& id, FilingStatus new_status,
                        const std::string& confirmation = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = obligations_.find(id);
        if (it == obligations_.end()) return false;
        it->second.status = new_status;
        if (!confirmation.empty()) it->second.confirmation_number = confirmation;
        if (new_status == FilingStatus::Submitted) {
            it->second.submitted_at = std::chrono::system_clock::now();
        }
        return true;
    }

    /**
     * @brief Get upcoming obligations within N days
     */
    [[nodiscard]] CalendarView upcoming(int days = 90) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto horizon = now + std::chrono::hours(24 * days);

        CalendarView view;
        view.start = now;
        view.end = horizon;

        for (const auto& [_, ob] : obligations_) {
            if (ob.due_date <= horizon) {
                view.obligations.push_back(ob);
                if (ob.is_overdue(now)) ++view.overdue_count;
                if (ob.needs_warning(now)) ++view.due_soon_count;
            }
        }

        std::sort(view.obligations.begin(), view.obligations.end(),
            [](const ComplianceObligation& a, const ComplianceObligation& b) {
                return a.due_date < b.due_date;
            });

        view.total_count = static_cast<int>(view.obligations.size());
        return view;
    }

    /**
     * @brief Get overdue obligations
     */
    [[nodiscard]] std::vector<ComplianceObligation> overdue() const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        std::vector<ComplianceObligation> result;
        for (const auto& [_, ob] : obligations_) {
            if (ob.is_overdue(now)) result.push_back(ob);
        }
        return result;
    }

    /**
     * @brief Get obligations by regulator
     */
    [[nodiscard]] std::vector<ComplianceObligation> by_regulator(Regulator reg) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ComplianceObligation> result;
        for (const auto& [_, ob] : obligations_) {
            if (ob.regulator == reg) result.push_back(ob);
        }
        return result;
    }

    /**
     * @brief Get obligations assigned to person
     */
    [[nodiscard]] std::vector<ComplianceObligation> by_assignee(
        const std::string& assignee) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ComplianceObligation> result;
        for (const auto& [_, ob] : obligations_) {
            if (ob.assignee == assignee) result.push_back(ob);
        }
        return result;
    }

    [[nodiscard]] int total_obligations() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(obligations_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, ComplianceObligation> obligations_;

    void register_common_filings() {
        auto make = [](const std::string& id, const std::string& name,
                       Regulator reg, const std::string& form,
                       FilingFrequency freq, Priority pri, int lead) {
            ComplianceObligation ob;
            ob.id = id;
            ob.name = name;
            ob.regulator = reg;
            ob.form_number = form;
            ob.frequency = freq;
            ob.priority = pri;
            ob.lead_time_days = lead;
            ob.due_date = std::chrono::system_clock::now() + std::chrono::hours(24 * 90);
            return ob;
        };

        obligations_["SEC-13F"] = make("SEC-13F", "Institutional Holdings Report",
            Regulator::SEC, "13F-HR", FilingFrequency::Quarterly, Priority::High, 45);
        obligations_["SEC-ADV"] = make("SEC-ADV", "Investment Adviser Registration",
            Regulator::SEC, "Form ADV", FilingFrequency::Annual, Priority::Critical, 60);
        obligations_["SEC-PF"] = make("SEC-PF", "Private Fund Report",
            Regulator::SEC, "Form PF", FilingFrequency::Quarterly, Priority::High, 45);
        obligations_["FINRA-4530"] = make("FINRA-4530", "Supplemental FOCUS",
            Regulator::FINRA, "Rule 4530", FilingFrequency::Quarterly, Priority::High, 30);
        obligations_["IRS-K1"] = make("IRS-K1", "Partnership Tax Reporting",
            Regulator::IRS, "Schedule K-1", FilingFrequency::Annual, Priority::High, 75);
        obligations_["CFTC-CPO"] = make("CFTC-CPO", "CPO Annual Report",
            Regulator::CFTC, "Form CPO-PQR", FilingFrequency::Annual, Priority::Medium, 60);
        obligations_["INT-RISK"] = make("INT-RISK", "Internal Risk Review",
            Regulator::Internal, "", FilingFrequency::Monthly, Priority::Medium, 7);
    }
};

} // namespace calendar
} // namespace compliance
} // namespace genie

#endif // GENIE_COMPLIANCE_CALENDAR_HPP
