/**
 * @file dashboard.hpp
 * @brief Dashboard customization and keyboard shortcuts
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: User Experience - Dashboard customization and keyboard shortcuts
 */

#ifndef GENIE_UX_DASHBOARD_HPP
#define GENIE_UX_DASHBOARD_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>

namespace genie {
namespace ux {

/**
 * @brief Widget type
 */
enum class WidgetType {
    WATCHLIST,
    CHART,
    POSITIONS,
    ORDERS,
    NEWS,
    MARKET_OVERVIEW,
    ACCOUNT_SUMMARY,
    TRADE_TICKET,
    ALERTS,
    CALENDAR,
    SCREENER,
    HEATMAP,
    CUSTOM
};

/**
 * @brief Widget configuration
 */
struct WidgetConfig {
    std::string id;
    WidgetType type{WidgetType::WATCHLIST};
    std::string title;
    int row{0};
    int col{0};
    int width{1};
    int height{1};
    bool visible{true};
    bool minimized{false};
    std::map<std::string, std::string> settings;
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"id\":\"" << id << "\",";
        json << "\"type\":\"" << widget_type_to_string(type) << "\",";
        json << "\"title\":\"" << title << "\",";
        json << "\"row\":" << row << ",";
        json << "\"col\":" << col << ",";
        json << "\"width\":" << width << ",";
        json << "\"height\":" << height << ",";
        json << "\"visible\":" << (visible ? "true" : "false") << ",";
        json << "\"minimized\":" << (minimized ? "true" : "false") << ",";
        json << "\"settings\":{";
        bool first = true;
        for (const auto& [k, v] : settings) {
            if (!first) json << ",";
            json << "\"" << k << "\":\"" << v << "\"";
            first = false;
        }
        json << "}}";
        return json.str();
    }
    
    static std::string widget_type_to_string(WidgetType t) {
        switch (t) {
            case WidgetType::WATCHLIST: return "watchlist";
            case WidgetType::CHART: return "chart";
            case WidgetType::POSITIONS: return "positions";
            case WidgetType::ORDERS: return "orders";
            case WidgetType::NEWS: return "news";
            case WidgetType::MARKET_OVERVIEW: return "market_overview";
            case WidgetType::ACCOUNT_SUMMARY: return "account_summary";
            case WidgetType::TRADE_TICKET: return "trade_ticket";
            case WidgetType::ALERTS: return "alerts";
            case WidgetType::CALENDAR: return "calendar";
            case WidgetType::SCREENER: return "screener";
            case WidgetType::HEATMAP: return "heatmap";
            case WidgetType::CUSTOM: return "custom";
            default: return "unknown";
        }
    }
    
    static WidgetType widget_type_from_string(const std::string& s) {
        if (s == "watchlist") return WidgetType::WATCHLIST;
        if (s == "chart") return WidgetType::CHART;
        if (s == "positions") return WidgetType::POSITIONS;
        if (s == "orders") return WidgetType::ORDERS;
        if (s == "news") return WidgetType::NEWS;
        if (s == "market_overview") return WidgetType::MARKET_OVERVIEW;
        if (s == "account_summary") return WidgetType::ACCOUNT_SUMMARY;
        if (s == "trade_ticket") return WidgetType::TRADE_TICKET;
        if (s == "alerts") return WidgetType::ALERTS;
        if (s == "calendar") return WidgetType::CALENDAR;
        if (s == "screener") return WidgetType::SCREENER;
        if (s == "heatmap") return WidgetType::HEATMAP;
        return WidgetType::CUSTOM;
    }
};

/**
 * @brief Layout preset
 */
struct LayoutPreset {
    std::string id;
    std::string name;
    std::string description;
    std::vector<WidgetConfig> widgets;
    int grid_columns{12};
    int grid_rows{8};
    std::string theme{"dark"};
};

/**
 * @brief Keyboard shortcut
 */
struct KeyboardShortcut {
    std::string id;
    std::string key;              // e.g., "B", "S", "F1"
    bool ctrl{false};
    bool alt{false};
    bool shift{false};
    std::string action;           // action identifier
    std::string description;
    std::string category;         // trading, navigation, view
    bool enabled{true};
    
    std::string get_key_combo() const {
        std::string combo;
        if (ctrl) combo += "Ctrl+";
        if (alt) combo += "Alt+";
        if (shift) combo += "Shift+";
        combo += key;
        return combo;
    }
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"id\":\"" << id << "\",";
        json << "\"key\":\"" << key << "\",";
        json << "\"ctrl\":" << (ctrl ? "true" : "false") << ",";
        json << "\"alt\":" << (alt ? "true" : "false") << ",";
        json << "\"shift\":" << (shift ? "true" : "false") << ",";
        json << "\"combo\":\"" << get_key_combo() << "\",";
        json << "\"action\":\"" << action << "\",";
        json << "\"description\":\"" << description << "\",";
        json << "\"category\":\"" << category << "\",";
        json << "\"enabled\":" << (enabled ? "true" : "false");
        json << "}";
        return json.str();
    }
};

/**
 * @brief User preferences
 */
struct UserPreferences {
    std::string user_id;
    std::string theme{"dark"};                     // dark, light, custom
    std::string default_layout{"trading"};
    std::string default_chart_type{"candle"};      // candle, line, bar, heikin_ashi
    std::string default_timeframe{"1d"};
    int refresh_interval_ms{1000};
    bool sound_enabled{true};
    bool notifications_enabled{true};
    bool confirm_orders{true};
    std::string font_size{"medium"};               // small, medium, large
    std::string number_format{"us"};               // us, eu
    std::string date_format{"MM/DD/YYYY"};
    std::vector<std::string> favorite_symbols;
    std::map<std::string, std::string> custom_settings;
    
    std::string to_json() const {
        std::ostringstream json;
        json << "{";
        json << "\"user_id\":\"" << user_id << "\",";
        json << "\"theme\":\"" << theme << "\",";
        json << "\"default_layout\":\"" << default_layout << "\",";
        json << "\"default_chart_type\":\"" << default_chart_type << "\",";
        json << "\"default_timeframe\":\"" << default_timeframe << "\",";
        json << "\"refresh_interval_ms\":" << refresh_interval_ms << ",";
        json << "\"sound_enabled\":" << (sound_enabled ? "true" : "false") << ",";
        json << "\"notifications_enabled\":" << (notifications_enabled ? "true" : "false") << ",";
        json << "\"confirm_orders\":" << (confirm_orders ? "true" : "false") << ",";
        json << "\"font_size\":\"" << font_size << "\",";
        json << "\"number_format\":\"" << number_format << "\",";
        json << "\"date_format\":\"" << date_format << "\",";
        json << "\"favorite_symbols\":[";
        for (size_t i = 0; i < favorite_symbols.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << favorite_symbols[i] << "\"";
        }
        json << "]}";
        return json.str();
    }
};

/**
 * @brief Dashboard manager
 */
class DashboardManager {
public:
    /**
     * @brief Get default layout presets
     */
    static std::vector<LayoutPreset> get_default_presets() {
        std::vector<LayoutPreset> presets;
        
        // Trading layout
        LayoutPreset trading;
        trading.id = "trading";
        trading.name = "Trading";
        trading.description = "Optimized for active trading with order entry";
        trading.widgets = {
            {"w1", WidgetType::CHART, "Chart", 0, 0, 8, 5, true, false, {{"symbol", "SPY"}}},
            {"w2", WidgetType::TRADE_TICKET, "Trade", 0, 8, 4, 3, true, false, {}},
            {"w3", WidgetType::POSITIONS, "Positions", 5, 0, 6, 3, true, false, {}},
            {"w4", WidgetType::ORDERS, "Orders", 5, 6, 6, 3, true, false, {}},
            {"w5", WidgetType::WATCHLIST, "Watchlist", 3, 8, 4, 2, true, false, {}}
        };
        presets.push_back(trading);
        
        // Analysis layout
        LayoutPreset analysis;
        analysis.id = "analysis";
        analysis.name = "Analysis";
        analysis.description = "Multi-chart layout for technical analysis";
        analysis.widgets = {
            {"w1", WidgetType::CHART, "Daily", 0, 0, 6, 4, true, false, {{"symbol", "SPY"}, {"timeframe", "1d"}}},
            {"w2", WidgetType::CHART, "Weekly", 0, 6, 6, 4, true, false, {{"symbol", "SPY"}, {"timeframe", "1w"}}},
            {"w3", WidgetType::CHART, "Hourly", 4, 0, 6, 4, true, false, {{"symbol", "SPY"}, {"timeframe", "1h"}}},
            {"w4", WidgetType::SCREENER, "Screener", 4, 6, 6, 4, true, false, {}}
        };
        presets.push_back(analysis);
        
        // Portfolio layout
        LayoutPreset portfolio;
        portfolio.id = "portfolio";
        portfolio.name = "Portfolio";
        portfolio.description = "Portfolio overview and management";
        portfolio.widgets = {
            {"w1", WidgetType::ACCOUNT_SUMMARY, "Account", 0, 0, 12, 2, true, false, {}},
            {"w2", WidgetType::POSITIONS, "Positions", 2, 0, 8, 3, true, false, {}},
            {"w3", WidgetType::HEATMAP, "Allocation", 2, 8, 4, 3, true, false, {}},
            {"w4", WidgetType::CHART, "Performance", 5, 0, 8, 3, true, false, {{"type", "portfolio"}}},
            {"w5", WidgetType::NEWS, "News", 5, 8, 4, 3, true, false, {}}
        };
        presets.push_back(portfolio);
        
        // News layout
        LayoutPreset news;
        news.id = "news";
        news.name = "News & Research";
        news.description = "Focus on market news and research";
        news.widgets = {
            {"w1", WidgetType::NEWS, "Headlines", 0, 0, 6, 6, true, false, {}},
            {"w2", WidgetType::CALENDAR, "Calendar", 0, 6, 6, 3, true, false, {}},
            {"w3", WidgetType::MARKET_OVERVIEW, "Markets", 3, 6, 6, 3, true, false, {}},
            {"w4", WidgetType::WATCHLIST, "Watchlist", 6, 0, 12, 2, true, false, {}}
        };
        presets.push_back(news);
        
        return presets;
    }
    
    /**
     * @brief Get default keyboard shortcuts
     */
    static std::vector<KeyboardShortcut> get_default_shortcuts() {
        std::vector<KeyboardShortcut> shortcuts;
        
        // Trading shortcuts
        shortcuts.push_back({"buy", "B", false, false, false, "open_buy_ticket", "Open buy order ticket", "trading", true});
        shortcuts.push_back({"sell", "S", false, false, false, "open_sell_ticket", "Open sell order ticket", "trading", true});
        shortcuts.push_back({"flatten", "F", true, false, false, "flatten_position", "Flatten current position", "trading", true});
        shortcuts.push_back({"cancel_all", "C", true, false, true, "cancel_all_orders", "Cancel all open orders", "trading", true});
        shortcuts.push_back({"market_buy", "B", true, false, false, "market_buy", "Submit market buy order", "trading", true});
        shortcuts.push_back({"market_sell", "S", true, false, false, "market_sell", "Submit market sell order", "trading", true});
        
        // Navigation shortcuts
        shortcuts.push_back({"next_symbol", "ArrowDown", false, true, false, "next_symbol", "Next symbol in watchlist", "navigation", true});
        shortcuts.push_back({"prev_symbol", "ArrowUp", false, true, false, "prev_symbol", "Previous symbol in watchlist", "navigation", true});
        shortcuts.push_back({"search", "/", false, false, false, "focus_search", "Focus symbol search", "navigation", true});
        shortcuts.push_back({"command", "K", true, false, false, "open_command", "Open command palette", "navigation", true});
        
        // View shortcuts
        shortcuts.push_back({"toggle_sidebar", "\\", true, false, false, "toggle_sidebar", "Toggle sidebar", "view", true});
        shortcuts.push_back({"fullscreen_chart", "F", false, false, false, "fullscreen_chart", "Toggle chart fullscreen", "view", true});
        shortcuts.push_back({"zoom_in", "+", true, false, false, "zoom_in", "Zoom in chart", "view", true});
        shortcuts.push_back({"zoom_out", "-", true, false, false, "zoom_out", "Zoom out chart", "view", true});
        
        // Timeframe shortcuts
        shortcuts.push_back({"tf_1m", "1", false, false, false, "set_tf_1m", "Set 1 minute timeframe", "chart", true});
        shortcuts.push_back({"tf_5m", "2", false, false, false, "set_tf_5m", "Set 5 minute timeframe", "chart", true});
        shortcuts.push_back({"tf_15m", "3", false, false, false, "set_tf_15m", "Set 15 minute timeframe", "chart", true});
        shortcuts.push_back({"tf_1h", "4", false, false, false, "set_tf_1h", "Set 1 hour timeframe", "chart", true});
        shortcuts.push_back({"tf_4h", "5", false, false, false, "set_tf_4h", "Set 4 hour timeframe", "chart", true});
        shortcuts.push_back({"tf_1d", "6", false, false, false, "set_tf_1d", "Set 1 day timeframe", "chart", true});
        shortcuts.push_back({"tf_1w", "7", false, false, false, "set_tf_1w", "Set 1 week timeframe", "chart", true});
        
        // Drawing tools
        shortcuts.push_back({"draw_line", "L", false, true, false, "draw_line", "Draw trend line", "drawing", true});
        shortcuts.push_back({"draw_fib", "F", false, true, false, "draw_fibonacci", "Draw Fibonacci retracement", "drawing", true});
        shortcuts.push_back({"draw_rect", "R", false, true, false, "draw_rectangle", "Draw rectangle", "drawing", true});
        shortcuts.push_back({"draw_text", "T", false, true, false, "draw_text", "Add text annotation", "drawing", true});
        shortcuts.push_back({"clear_drawings", "Delete", true, false, false, "clear_drawings", "Clear all drawings", "drawing", true});
        
        // Help
        shortcuts.push_back({"help", "F1", false, false, false, "show_help", "Show keyboard shortcuts", "help", true});
        shortcuts.push_back({"settings", ",", true, false, false, "open_settings", "Open settings", "help", true});
        
        return shortcuts;
    }
    
    /**
     * @brief Generate JavaScript for keyboard handler
     */
    static std::string generate_keyboard_handler_js(const std::vector<KeyboardShortcut>& shortcuts) {
        std::ostringstream js;
        
        js << "const KeyboardHandler = {\n";
        js << "  shortcuts: " << shortcuts_to_json(shortcuts) << ",\n\n";
        
        js << "  init() {\n";
        js << "    document.addEventListener('keydown', (e) => this.handle(e));\n";
        js << "  },\n\n";
        
        js << "  handle(e) {\n";
        js << "    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;\n";
        js << "    const combo = this.getCombo(e);\n";
        js << "    const shortcut = this.shortcuts.find(s => s.combo === combo && s.enabled);\n";
        js << "    if (shortcut) {\n";
        js << "      e.preventDefault();\n";
        js << "      this.execute(shortcut.action);\n";
        js << "    }\n";
        js << "  },\n\n";
        
        js << "  getCombo(e) {\n";
        js << "    let combo = '';\n";
        js << "    if (e.ctrlKey || e.metaKey) combo += 'Ctrl+';\n";
        js << "    if (e.altKey) combo += 'Alt+';\n";
        js << "    if (e.shiftKey) combo += 'Shift+';\n";
        js << "    combo += e.key;\n";
        js << "    return combo;\n";
        js << "  },\n\n";
        
        js << "  execute(action) {\n";
        js << "    if (typeof App !== 'undefined' && typeof App[action] === 'function') {\n";
        js << "      App[action]();\n";
        js << "    } else {\n";
        js << "      console.log('Action:', action);\n";
        js << "    }\n";
        js << "  }\n";
        js << "};\n";
        
        return js.str();
    }
    
private:
    static std::string shortcuts_to_json(const std::vector<KeyboardShortcut>& shortcuts) {
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < shortcuts.size(); ++i) {
            if (i > 0) json << ",";
            json << shortcuts[i].to_json();
        }
        json << "]";
        return json.str();
    }
};

/**
 * @brief User settings store
 */
class UserSettingsStore {
public:
    explicit UserSettingsStore(const std::string& db_path = "settings.db")
        : db_path_(db_path) {}
    
    /**
     * @brief Save user preferences
     */
    bool save_preferences(const UserPreferences& prefs) {
        std::lock_guard<std::mutex> lock(mutex_);
        preferences_[prefs.user_id] = prefs;
        return true;
    }
    
    /**
     * @brief Get user preferences
     */
    std::optional<UserPreferences> get_preferences(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = preferences_.find(user_id);
        if (it != preferences_.end()) {
            return it->second;
        }
        // Return defaults
        UserPreferences defaults;
        defaults.user_id = user_id;
        return defaults;
    }
    
    /**
     * @brief Save custom layout
     */
    bool save_layout(const std::string& user_id, const LayoutPreset& layout) {
        std::lock_guard<std::mutex> lock(mutex_);
        user_layouts_[user_id][layout.id] = layout;
        return true;
    }
    
    /**
     * @brief Get user layouts
     */
    std::vector<LayoutPreset> get_user_layouts(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LayoutPreset> result;
        
        // Add default presets
        auto defaults = DashboardManager::get_default_presets();
        for (const auto& p : defaults) {
            result.push_back(p);
        }
        
        // Add user custom layouts
        auto it = user_layouts_.find(user_id);
        if (it != user_layouts_.end()) {
            for (const auto& [id, layout] : it->second) {
                result.push_back(layout);
            }
        }
        
        return result;
    }
    
    /**
     * @brief Save keyboard shortcuts
     */
    bool save_shortcuts(const std::string& user_id, const std::vector<KeyboardShortcut>& shortcuts) {
        std::lock_guard<std::mutex> lock(mutex_);
        user_shortcuts_[user_id] = shortcuts;
        return true;
    }
    
    /**
     * @brief Get user keyboard shortcuts
     */
    std::vector<KeyboardShortcut> get_shortcuts(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_shortcuts_.find(user_id);
        if (it != user_shortcuts_.end()) {
            return it->second;
        }
        return DashboardManager::get_default_shortcuts();
    }
    
private:
    std::string db_path_;
    mutable std::mutex mutex_;
    std::map<std::string, UserPreferences> preferences_;
    std::map<std::string, std::map<std::string, LayoutPreset>> user_layouts_;
    std::map<std::string, std::vector<KeyboardShortcut>> user_shortcuts_;
};

} // namespace ux
} // namespace genie

#endif // GENIE_UX_DASHBOARD_HPP
