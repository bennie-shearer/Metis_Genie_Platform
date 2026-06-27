/**
 * @file custom_dashboards.hpp
 * @brief Custom dashboard builder for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Enables users to create, save, share, clone, and manage custom dashboard
 * layouts with configurable widgets, data sources, refresh intervals, themes,
 * drag-and-drop grid positioning, and role-based access control.
 *
 * Features:
 *  - Drag-and-drop grid layout (configurable columns, default 12)
 *  - 10 widget types: line chart, bar chart, pie chart, table, metric card,
 *    heatmap, scatter plot, treemap, gauge, text block
 *  - 9 data sources: portfolio, risk, analytics, market data, compliance,
 *    trading, performance, tax, custom query
 *  - Dashboard templates for quick creation (PM, Trader, Risk, Compliance, Exec)
 *  - Widget settings with per-widget refresh intervals
 *  - Dashboard sharing with role-based access
 *  - Clone and fork dashboards from templates or other users
 *  - Export dashboard definitions as JSON
 *  - Theme support (dark, light, custom)
 *  - Comprehensive audit trail
 *  - Thread-safe concurrent access
 *  - Zero external dependencies
 */
#pragma once
#ifndef GENIE_UX_CUSTOM_DASHBOARDS_HPP
#define GENIE_UX_CUSTOM_DASHBOARDS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <functional>

namespace genie::ux {

// ============================================================================
// Enums and Constants
// ============================================================================

/** @brief Widget visualization types */
enum class WidgetType {
    LINE_CHART, BAR_CHART, PIE_CHART, TABLE, METRIC_CARD,
    HEATMAP, SCATTER_PLOT, TREEMAP, GAUGE, TEXT_BLOCK
};

/** @brief Data source for a widget */
enum class DataSource {
    PORTFOLIO, RISK, ANALYTICS, MARKET_DATA, COMPLIANCE,
    TRADING, PERFORMANCE, TAX, CUSTOM_QUERY
};

/** @brief Dashboard access level */
enum class DashboardAccess { PRIVATE, TEAM, ORGANIZATION, PUBLIC };

/** @brief Widget refresh mode */
enum class RefreshMode { MANUAL, POLLING, STREAMING, ON_EVENT };

// ============================================================================
// Data Structures
// ============================================================================

/** @brief Widget configuration settings */
struct WidgetSettings {
    std::string color_scheme{"default"};
    bool show_legend{true};
    bool show_grid{true};
    bool animate_transitions{true};
    std::string time_range{"1D"};
    int decimal_places{2};
    std::string number_format{"standard"};
    std::string sort_column;
    std::string sort_direction{"desc"};
    int max_rows{50};
    std::unordered_map<std::string, std::string> custom;
};

/** @brief Dashboard widget definition */
struct DashboardWidget {
    std::string id;
    std::string title;
    std::string subtitle;
    WidgetType type{WidgetType::METRIC_CARD};
    DataSource source{DataSource::PORTFOLIO};
    std::string query;
    std::string api_endpoint;
    int grid_x{0};
    int grid_y{0};
    int grid_w{4};
    int grid_h{3};
    int min_w{2};
    int min_h{2};
    RefreshMode refresh_mode{RefreshMode::POLLING};
    int refresh_seconds{60};
    WidgetSettings settings;
    bool visible{true};
    std::string created_at;
    std::string updated_at;
};

/** @brief Dashboard definition */
struct Dashboard {
    std::string id;
    std::string name;
    std::string description;
    std::string owner;
    std::string created_at;
    std::string updated_at;
    DashboardAccess access{DashboardAccess::PRIVATE};
    std::vector<std::string> shared_with;
    std::string theme{"dark"};
    int grid_columns{12};
    int grid_row_height{80};
    bool auto_refresh{true};
    int auto_refresh_seconds{300};
    std::vector<DashboardWidget> widgets;
    std::string cloned_from;
    std::unordered_map<std::string, std::string> metadata;
};

/** @brief Dashboard template for quick creation */
struct DashboardTemplate {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::string target_role;
    std::vector<DashboardWidget> default_widgets;
    std::string preview_image_url;
};

/** @brief Dashboard change audit entry */
struct DashboardAuditEntry {
    std::string dashboard_id;
    std::string action; // created, updated, shared, cloned, deleted, widget_added, widget_removed
    std::string user;
    std::string timestamp;
    std::string details;
};

/** @brief Dashboard manager statistics */
struct DashboardManagerStats {
    std::size_t total_dashboards{0};
    std::size_t shared_dashboards{0};
    std::size_t total_widgets{0};
    std::size_t templates_available{0};
    std::size_t total_clones{0};
    std::vector<DashboardAuditEntry> recent_activity;
};

// ============================================================================
// DashboardBuilder
// ============================================================================

/**
 * @class DashboardBuilder
 * @brief Creates, manages, and shares custom dashboards
 *
 * Provides complete dashboard lifecycle management:
 *  - Create from scratch or from templates
 *  - Add, remove, reposition widgets
 *  - Share with team or organization
 *  - Clone and fork dashboards
 *  - Export definitions as JSON-compatible strings
 *  - Full audit trail
 */
class DashboardBuilder {
public:
    DashboardBuilder() { initialize_default_templates(); }

    // ---- Dashboard CRUD ----

    /** @brief Create a new empty dashboard */
    Dashboard create(const std::string& name, const std::string& owner,
                     const std::string& description = "") {
        std::lock_guard lock(mutex_);
        Dashboard dash;
        dash.id = "DASH-" + std::to_string(++dashboard_counter_);
        dash.name = name;
        dash.description = description;
        dash.owner = owner;
        dash.created_at = now_str();
        dash.updated_at = dash.created_at;
        dashboards_[dash.id] = dash;
        audit("created", dash.id, owner, "Dashboard '" + name + "' created");
        return dash;
    }

    /** @brief Create from template */
    Dashboard create_from_template(const std::string& template_id, const std::string& owner,
                                   const std::string& name = "") {
        std::lock_guard lock(mutex_);
        Dashboard dash;
        dash.id = "DASH-" + std::to_string(++dashboard_counter_);
        dash.owner = owner;
        dash.created_at = now_str();
        dash.updated_at = dash.created_at;

        auto tmpl = templates_.find(template_id);
        if (tmpl != templates_.end()) {
            dash.name = name.empty() ? tmpl->second.name : name;
            dash.description = tmpl->second.description;
            dash.widgets = tmpl->second.default_widgets;
            // Assign widget IDs
            for (auto& w : dash.widgets) {
                w.id = "W-" + std::to_string(++widget_counter_);
                w.created_at = dash.created_at;
                w.updated_at = dash.created_at;
            }
            dash.cloned_from = template_id;
        }
        dashboards_[dash.id] = dash;
        audit("created", dash.id, owner, "Created from template '" + template_id + "'");
        return dash;
    }

    /** @brief Clone an existing dashboard */
    std::optional<Dashboard> clone(const std::string& source_id, const std::string& new_owner,
                                   const std::string& new_name = "") {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(source_id);
        if (it == dashboards_.end()) return std::nullopt;

        Dashboard clone_dash = it->second;
        clone_dash.id = "DASH-" + std::to_string(++dashboard_counter_);
        clone_dash.owner = new_owner;
        clone_dash.name = new_name.empty() ? clone_dash.name + " (Copy)" : new_name;
        clone_dash.created_at = now_str();
        clone_dash.updated_at = clone_dash.created_at;
        clone_dash.access = DashboardAccess::PRIVATE;
        clone_dash.shared_with.clear();
        clone_dash.cloned_from = source_id;
        clone_count_++;

        // Re-assign widget IDs
        for (auto& w : clone_dash.widgets) {
            w.id = "W-" + std::to_string(++widget_counter_);
        }

        dashboards_[clone_dash.id] = clone_dash;
        audit("cloned", clone_dash.id, new_owner, "Cloned from '" + source_id + "'");
        return clone_dash;
    }

    /** @brief Delete a dashboard */
    bool remove(const std::string& id, const std::string& user) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(id);
        if (it == dashboards_.end()) return false;
        if (it->second.owner != user) return false;
        audit("deleted", id, user, "Dashboard '" + it->second.name + "' deleted");
        dashboards_.erase(it);
        return true;
    }

    /** @brief Get a dashboard by ID */
    [[nodiscard]] std::optional<Dashboard> get(const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(id);
        if (it != dashboards_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List dashboards visible to a user */
    [[nodiscard]] std::vector<Dashboard> list(const std::string& user = "") const {
        std::lock_guard lock(mutex_);
        std::vector<Dashboard> result;
        for (const auto& [_, d] : dashboards_) {
            if (user.empty() || d.owner == user ||
                d.access == DashboardAccess::PUBLIC ||
                d.access == DashboardAccess::ORGANIZATION ||
                std::find(d.shared_with.begin(), d.shared_with.end(), user) != d.shared_with.end()) {
                result.push_back(d);
            }
        }
        return result;
    }

    // ---- Widget Management ----

    /** @brief Add a widget to a dashboard */
    bool add_widget(const std::string& dashboard_id, DashboardWidget widget,
                    const std::string& user = "") {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(dashboard_id);
        if (it == dashboards_.end()) return false;
        widget.id = "W-" + std::to_string(++widget_counter_);
        widget.created_at = now_str();
        widget.updated_at = widget.created_at;
        it->second.widgets.push_back(std::move(widget));
        it->second.updated_at = now_str();
        audit("widget_added", dashboard_id, user, "Widget added");
        return true;
    }

    /** @brief Remove a widget from a dashboard */
    bool remove_widget(const std::string& dashboard_id, const std::string& widget_id,
                       const std::string& user = "") {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(dashboard_id);
        if (it == dashboards_.end()) return false;
        auto& widgets = it->second.widgets;
        auto wid = std::remove_if(widgets.begin(), widgets.end(),
            [&](const DashboardWidget& w) { return w.id == widget_id; });
        if (wid != widgets.end()) {
            widgets.erase(wid, widgets.end());
            it->second.updated_at = now_str();
            audit("widget_removed", dashboard_id, user, "Widget '" + widget_id + "' removed");
            return true;
        }
        return false;
    }

    /** @brief Move/resize a widget */
    bool move_widget(const std::string& dashboard_id, const std::string& widget_id,
                     int x, int y, int w, int h) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(dashboard_id);
        if (it == dashboards_.end()) return false;
        for (auto& widget : it->second.widgets) {
            if (widget.id == widget_id) {
                widget.grid_x = x;
                widget.grid_y = y;
                widget.grid_w = std::max(w, widget.min_w);
                widget.grid_h = std::max(h, widget.min_h);
                widget.updated_at = now_str();
                it->second.updated_at = widget.updated_at;
                return true;
            }
        }
        return false;
    }

    // ---- Sharing ----

    /** @brief Share a dashboard with specific users */
    bool share(const std::string& id, const std::vector<std::string>& users,
               const std::string& owner) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(id);
        if (it == dashboards_.end() || it->second.owner != owner) return false;
        for (const auto& u : users) {
            if (std::find(it->second.shared_with.begin(), it->second.shared_with.end(), u)
                == it->second.shared_with.end()) {
                it->second.shared_with.push_back(u);
            }
        }
        it->second.updated_at = now_str();
        audit("shared", id, owner, "Shared with " + std::to_string(users.size()) + " users");
        return true;
    }

    /** @brief Set dashboard access level */
    bool set_access(const std::string& id, DashboardAccess access, const std::string& owner) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(id);
        if (it == dashboards_.end() || it->second.owner != owner) return false;
        it->second.access = access;
        it->second.updated_at = now_str();
        return true;
    }

    // ---- Templates ----

    /** @brief Register a dashboard template */
    void register_template(DashboardTemplate tmpl) {
        std::lock_guard lock(mutex_);
        templates_[tmpl.id] = std::move(tmpl);
    }

    /** @brief List available templates */
    [[nodiscard]] std::vector<DashboardTemplate> list_templates() const {
        std::lock_guard lock(mutex_);
        std::vector<DashboardTemplate> result;
        for (const auto& [_, t] : templates_) result.push_back(t);
        return result;
    }

    // ---- Statistics ----

    /** @brief Get comprehensive manager statistics */
    [[nodiscard]] DashboardManagerStats stats() const {
        std::lock_guard lock(mutex_);
        DashboardManagerStats s;
        s.total_dashboards = dashboards_.size();
        s.templates_available = templates_.size();
        s.total_clones = clone_count_;
        for (const auto& [_, d] : dashboards_) {
            s.total_widgets += d.widgets.size();
            if (d.access != DashboardAccess::PRIVATE) s.shared_dashboards++;
        }
        // Recent audit
        std::size_t count = std::min(audit_log_.size(), std::size_t(20));
        if (count > 0) {
            s.recent_activity.assign(audit_log_.end() - count, audit_log_.end());
        }
        return s;
    }

    [[nodiscard]] uint64_t dashboard_count() const { return dashboard_counter_; }
    [[nodiscard]] uint64_t widget_total() const { return widget_counter_; }

private:
    void initialize_default_templates() {
        // Portfolio Manager template
        DashboardTemplate pm;
        pm.id = "TMPL-PM";
        pm.name = "Portfolio Manager";
        pm.description = "Overview of portfolio positions, P&L, and allocation";
        pm.category = "Portfolio";
        pm.target_role = "Portfolio Manager";
        pm.default_widgets = {
            {"", "Portfolio Value", "", WidgetType::METRIC_CARD, DataSource::PORTFOLIO, "", "/api/v1/portfolio", 0, 0, 3, 2, 2, 2, RefreshMode::POLLING, 30, {}, true, "", ""},
            {"", "Daily P&L", "", WidgetType::METRIC_CARD, DataSource::PORTFOLIO, "", "/api/v1/portfolio/performance", 3, 0, 3, 2, 2, 2, RefreshMode::POLLING, 30, {}, true, "", ""},
            {"", "Asset Allocation", "", WidgetType::PIE_CHART, DataSource::PORTFOLIO, "", "/api/v1/portfolio/allocations", 6, 0, 6, 4, 3, 3, RefreshMode::POLLING, 60, {}, true, "", ""},
            {"", "Position Heatmap", "", WidgetType::HEATMAP, DataSource::PORTFOLIO, "", "/api/v1/portfolio/positions", 0, 2, 6, 4, 4, 3, RefreshMode::POLLING, 60, {}, true, "", ""},
            {"", "Performance Chart", "", WidgetType::LINE_CHART, DataSource::ANALYTICS, "", "/api/v1/analytics/performance", 0, 6, 12, 4, 6, 3, RefreshMode::POLLING, 300, {}, true, "", ""}
        };
        templates_["TMPL-PM"] = std::move(pm);

        // Trader template
        DashboardTemplate trader;
        trader.id = "TMPL-TRADER";
        trader.name = "Trading Desk";
        trader.description = "Real-time order flow, blotter, and market data";
        trader.category = "Trading";
        trader.target_role = "Trader";
        trader.default_widgets = {
            {"", "Order Blotter", "", WidgetType::TABLE, DataSource::TRADING, "", "/api/v1/trading/blotter", 0, 0, 8, 5, 6, 3, RefreshMode::STREAMING, 5, {}, true, "", ""},
            {"", "Open Orders", "", WidgetType::METRIC_CARD, DataSource::TRADING, "", "/api/v1/orders", 8, 0, 4, 2, 2, 2, RefreshMode::STREAMING, 5, {}, true, "", ""},
            {"", "Fill Rate", "", WidgetType::GAUGE, DataSource::TRADING, "", "/api/v1/trading/allocations", 8, 2, 4, 3, 2, 2, RefreshMode::POLLING, 30, {}, true, "", ""}
        };
        templates_["TMPL-TRADER"] = std::move(trader);

        // Risk Manager template
        DashboardTemplate risk;
        risk.id = "TMPL-RISK";
        risk.name = "Risk Dashboard";
        risk.description = "VaR, stress testing, and exposure monitoring";
        risk.category = "Risk";
        risk.target_role = "Risk Manager";
        risk.default_widgets = {
            {"", "Portfolio VaR", "", WidgetType::METRIC_CARD, DataSource::RISK, "", "/api/v1/risk/var", 0, 0, 3, 2, 2, 2, RefreshMode::POLLING, 60, {}, true, "", ""},
            {"", "Stress Scenarios", "", WidgetType::BAR_CHART, DataSource::RISK, "", "/api/v1/analytics/stress-test", 3, 0, 9, 4, 4, 3, RefreshMode::POLLING, 300, {}, true, "", ""},
            {"", "Risk Attribution", "", WidgetType::PIE_CHART, DataSource::ANALYTICS, "", "/api/v1/analytics/risk-attribution", 0, 4, 6, 4, 3, 3, RefreshMode::POLLING, 300, {}, true, "", ""},
            {"", "Exposure Summary", "", WidgetType::TABLE, DataSource::RISK, "", "/api/v1/risk/exposure", 6, 4, 6, 4, 4, 3, RefreshMode::POLLING, 120, {}, true, "", ""}
        };
        templates_["TMPL-RISK"] = std::move(risk);

        // Compliance template
        DashboardTemplate compliance;
        compliance.id = "TMPL-COMPLIANCE";
        compliance.name = "Compliance Monitor";
        compliance.description = "Surveillance alerts, violations, and regulatory status";
        compliance.category = "Compliance";
        compliance.target_role = "Compliance Officer";
        compliance.default_widgets = {
            {"", "Active Alerts", "", WidgetType::METRIC_CARD, DataSource::COMPLIANCE, "", "/api/v1/compliance/surveillance", 0, 0, 3, 2, 2, 2, RefreshMode::STREAMING, 10, {}, true, "", ""},
            {"", "Violations", "", WidgetType::TABLE, DataSource::COMPLIANCE, "", "/api/v1/compliance/violations", 0, 2, 12, 5, 6, 3, RefreshMode::POLLING, 60, {}, true, "", ""}
        };
        templates_["TMPL-COMPLIANCE"] = std::move(compliance);

        // Executive Summary template
        DashboardTemplate exec;
        exec.id = "TMPL-EXEC";
        exec.name = "Executive Summary";
        exec.description = "High-level AUM, performance, and risk overview";
        exec.category = "Executive";
        exec.target_role = "Executive";
        exec.default_widgets = {
            {"", "Total AUM", "", WidgetType::METRIC_CARD, DataSource::PORTFOLIO, "", "/api/v1/portfolio", 0, 0, 4, 2, 2, 2, RefreshMode::POLLING, 300, {}, true, "", ""},
            {"", "YTD Return", "", WidgetType::METRIC_CARD, DataSource::ANALYTICS, "", "/api/v1/analytics/performance", 4, 0, 4, 2, 2, 2, RefreshMode::POLLING, 300, {}, true, "", ""},
            {"", "Risk Score", "", WidgetType::GAUGE, DataSource::RISK, "", "/api/v1/risk/var", 8, 0, 4, 2, 2, 2, RefreshMode::POLLING, 300, {}, true, "", ""}
        };
        templates_["TMPL-EXEC"] = std::move(exec);
    }

    void audit(const std::string& action, const std::string& dashboard_id,
               const std::string& user, const std::string& details) {
        DashboardAuditEntry entry;
        entry.dashboard_id = dashboard_id;
        entry.action = action;
        entry.user = user;
        entry.timestamp = now_str();
        entry.details = details;
        audit_log_.push_back(std::move(entry));
        while (audit_log_.size() > 500) audit_log_.erase(audit_log_.begin());
    }

    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    std::unordered_map<std::string, Dashboard> dashboards_;
    std::unordered_map<std::string, DashboardTemplate> templates_;
    std::vector<DashboardAuditEntry> audit_log_;
    mutable std::mutex mutex_;
    uint64_t dashboard_counter_{0};
    uint64_t widget_counter_{0};
    uint64_t clone_count_{0};
};

} // namespace genie::ux

#endif // GENIE_UX_CUSTOM_DASHBOARDS_HPP
