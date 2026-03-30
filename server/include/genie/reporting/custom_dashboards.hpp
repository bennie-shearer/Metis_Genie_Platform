/**
 * @file custom_dashboards.hpp
 * @brief Custom dashboard builder for Metis Genie Platform
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Enables users to create, save, and share custom dashboard layouts with
 * configurable widgets, data sources, refresh intervals, and themes.
 */
#pragma once
#ifndef GENIE_CUSTOM_DASHBOARDS_HPP
#define GENIE_CUSTOM_DASHBOARDS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>
#include <sstream>

namespace genie::reporting {

/** @brief Widget type */
enum class WidgetType {
    LINE_CHART, BAR_CHART, PIE_CHART, TABLE, METRIC_CARD,
    HEATMAP, SCATTER_PLOT, TREEMAP, GAUGE, TEXT_BLOCK
};

/** @brief Data source for a widget */
enum class DataSource {
    PORTFOLIO, RISK, ANALYTICS, MARKET_DATA, COMPLIANCE,
    TRADING, PERFORMANCE, TAX, CUSTOM_QUERY
};

/** @brief Dashboard widget definition */
struct DashboardWidget {
    std::string id;
    std::string title;
    WidgetType type{WidgetType::METRIC_CARD};
    DataSource source{DataSource::PORTFOLIO};
    std::string query;
    int grid_x{0};
    int grid_y{0};
    int grid_w{4};
    int grid_h{3};
    int refresh_seconds{60};
    std::unordered_map<std::string, std::string> settings;
};

/** @brief Dashboard definition */
struct Dashboard {
    std::string id;
    std::string name;
    std::string description;
    std::string owner;
    std::string created_at;
    std::string updated_at;
    bool is_shared{false};
    bool is_default{false};
    std::string theme{"dark"};
    int grid_columns{12};
    std::vector<DashboardWidget> widgets;
};

/** @brief Dashboard template for quick creation */
struct DashboardTemplate {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::vector<DashboardWidget> default_widgets;
};

/**
 * @class DashboardBuilder
 * @brief Creates and manages custom dashboards
 */
class DashboardBuilder {
public:
    /** @brief Create a new dashboard */
    Dashboard create(const std::string& name, const std::string& owner) {
        std::lock_guard lock(mutex_);
        Dashboard dash;
        dash.id = "DASH-" + std::to_string(++dashboard_counter_);
        dash.name = name;
        dash.owner = owner;
        dash.created_at = now_str();
        dash.updated_at = dash.created_at;
        dashboards_[dash.id] = dash;
        return dash;
    }

    /** @brief Add a widget to a dashboard */
    bool add_widget(const std::string& dashboard_id, DashboardWidget widget) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(dashboard_id);
        if (it == dashboards_.end()) return false;
        widget.id = "W-" + std::to_string(++widget_counter_);
        it->second.widgets.push_back(std::move(widget));
        it->second.updated_at = now_str();
        return true;
    }

    /** @brief Remove a widget */
    bool remove_widget(const std::string& dashboard_id, const std::string& widget_id) {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(dashboard_id);
        if (it == dashboards_.end()) return false;
        auto& widgets = it->second.widgets;
        auto wid = std::remove_if(widgets.begin(), widgets.end(),
            [&](const DashboardWidget& w) { return w.id == widget_id; });
        if (wid != widgets.end()) {
            widgets.erase(wid, widgets.end());
            it->second.updated_at = now_str();
            return true;
        }
        return false;
    }

    /** @brief Get a dashboard by ID */
    [[nodiscard]] std::optional<Dashboard> get(const std::string& id) const {
        std::lock_guard lock(mutex_);
        auto it = dashboards_.find(id);
        if (it != dashboards_.end()) return it->second;
        return std::nullopt;
    }

    /** @brief List dashboards for a user */
    [[nodiscard]] std::vector<Dashboard> list(const std::string& owner = "") const {
        std::lock_guard lock(mutex_);
        std::vector<Dashboard> result;
        for (const auto& [_, d] : dashboards_) {
            if (owner.empty() || d.owner == owner || d.is_shared) {
                result.push_back(d);
            }
        }
        return result;
    }

    /** @brief Create from template */
    Dashboard create_from_template(const std::string& template_id, const std::string& owner) {
        std::lock_guard lock(mutex_);
        Dashboard dash;
        dash.id = "DASH-" + std::to_string(++dashboard_counter_);
        dash.owner = owner;
        dash.created_at = now_str();
        dash.updated_at = dash.created_at;

        auto tmpl = templates_.find(template_id);
        if (tmpl != templates_.end()) {
            dash.name = tmpl->second.name;
            dash.description = tmpl->second.description;
            dash.widgets = tmpl->second.default_widgets;
        }
        dashboards_[dash.id] = dash;
        return dash;
    }

    /** @brief Register a dashboard template */
    void register_template(DashboardTemplate tmpl) {
        std::lock_guard lock(mutex_);
        templates_[tmpl.id] = std::move(tmpl);
    }

    /** @brief Available templates */
    [[nodiscard]] std::vector<DashboardTemplate> templates() const {
        std::lock_guard lock(mutex_);
        std::vector<DashboardTemplate> result;
        for (const auto& [_, t] : templates_) result.push_back(t);
        return result;
    }

    /** @brief Total dashboards created */
    [[nodiscard]] uint64_t dashboard_count() const { return dashboard_counter_; }

private:
    static std::string now_str() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
        return buf;
    }

    std::unordered_map<std::string, Dashboard> dashboards_;
    std::unordered_map<std::string, DashboardTemplate> templates_;
    mutable std::mutex mutex_;
    uint64_t dashboard_counter_{0};
    uint64_t widget_counter_{0};
};

} // namespace genie::reporting

#endif // GENIE_CUSTOM_DASHBOARDS_HPP
