/**
 * @file fred_client.hpp
 * @brief Federal Reserve Economic Data (FRED) API client
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * FRED API integration providing:
 * - Economic data series retrieval
 * - Series search and metadata
 * - Categories and releases
 * - Common indicators (GDP, unemployment, inflation, etc.)
 * 
 * API Documentation: https://fred.stlouisfed.org/docs/api/fred/
 */
#pragma once
#ifndef GENIE_MARKET_FRED_CLIENT_HPP
#define GENIE_MARKET_FRED_CLIENT_HPP

#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief FRED configuration
 */
struct FREDConfig {
    std::string api_key;
    int timeout_ms{10000};
    
    static constexpr const char* BASE_URL = "https://api.stlouisfed.org/fred";
    
    bool is_valid() const {
        return !api_key.empty();
    }
};

/**
 * @brief FRED data observation
 */
struct FREDObservation {
    std::string date;
    double value{0};
    bool is_missing{false};          // "." in FRED data
    
    std::string realtime_start;
    std::string realtime_end;
};

/**
 * @brief FRED series metadata
 */
struct FREDSeries {
    std::string id;
    std::string title;
    std::string observation_start;
    std::string observation_end;
    std::string frequency;           // D, W, M, Q, A
    std::string frequency_short;
    std::string units;
    std::string units_short;
    std::string seasonal_adjustment;
    std::string seasonal_adjustment_short;
    std::string last_updated;
    int popularity{0};
    std::string notes;
};

/**
 * @brief FRED release info
 */
struct FREDRelease {
    int id{0};
    std::string name;
    bool press_release{false};
    std::string link;
    std::string notes;
};

/**
 * @brief FRED category
 */
struct FREDCategory {
    int id{0};
    std::string name;
    int parent_id{0};
    std::string notes;
};

/**
 * @brief Common economic indicators
 */
namespace indicators {
    // GDP & Output
    constexpr const char* GDP = "GDP";                      // Real GDP
    constexpr const char* GDPC1 = "GDPC1";                  // Real GDP (chained)
    constexpr const char* GDPPOT = "GDPPOT";                // Real Potential GDP
    
    // Employment
    constexpr const char* UNRATE = "UNRATE";                // Unemployment Rate
    constexpr const char* PAYEMS = "PAYEMS";                // Total Nonfarm Payrolls
    constexpr const char* CIVPART = "CIVPART";              // Labor Force Participation
    constexpr const char* ICSA = "ICSA";                    // Initial Claims
    constexpr const char* CCSA = "CCSA";                    // Continued Claims
    
    // Inflation & Prices
    constexpr const char* CPIAUCSL = "CPIAUCSL";            // CPI All Items
    constexpr const char* CPILFESL = "CPILFESL";            // Core CPI
    constexpr const char* PCEPI = "PCEPI";                  // PCE Price Index
    constexpr const char* PCEPILFE = "PCEPILFE";            // Core PCE
    constexpr const char* PPIFIS = "PPIFIS";                // PPI Final Demand
    
    // Interest Rates
    constexpr const char* FEDFUNDS = "FEDFUNDS";            // Fed Funds Rate
    constexpr const char* DFF = "DFF";                      // Effective Fed Funds (daily)
    constexpr const char* DGS10 = "DGS10";                  // 10-Year Treasury
    constexpr const char* DGS2 = "DGS2";                    // 2-Year Treasury
    constexpr const char* DGS30 = "DGS30";                  // 30-Year Treasury
    constexpr const char* T10Y2Y = "T10Y2Y";                // 10Y-2Y Spread
    constexpr const char* T10YFF = "T10YFF";                // 10Y-FF Spread
    constexpr const char* BAMLH0A0HYM2 = "BAMLH0A0HYM2";    // HY Spread
    
    // Money Supply
    constexpr const char* M1SL = "M1SL";                    // M1 Money Stock
    constexpr const char* M2SL = "M2SL";                    // M2 Money Stock
    constexpr const char* WALCL = "WALCL";                  // Fed Balance Sheet
    
    // Housing
    constexpr const char* HOUST = "HOUST";                  // Housing Starts
    constexpr const char* PERMIT = "PERMIT";                // Building Permits
    constexpr const char* HSN1F = "HSN1F";                  // New Home Sales
    constexpr const char* EXHOSLUSM495S = "EXHOSLUSM495S";  // Existing Home Sales
    constexpr const char* CSUSHPINSA = "CSUSHPINSA";        // Case-Shiller Index
    constexpr const char* MORTGAGE30US = "MORTGAGE30US";    // 30Y Mortgage Rate
    
    // Consumer
    constexpr const char* RSXFS = "RSXFS";                  // Retail Sales
    constexpr const char* UMCSENT = "UMCSENT";              // Consumer Sentiment
    constexpr const char* PCE = "PCE";                      // Personal Consumption
    constexpr const char* PSAVERT = "PSAVERT";              // Personal Savings Rate
    
    // Manufacturing
    constexpr const char* INDPRO = "INDPRO";                // Industrial Production
    constexpr const char* DGORDER = "DGORDER";              // Durable Goods Orders
    constexpr const char* MANEMP = "MANEMP";                // Manufacturing Employment
    constexpr const char* IPMAN = "IPMAN";                  // Manufacturing IP
    
    // Trade
    constexpr const char* BOPGSTB = "BOPGSTB";              // Trade Balance
    constexpr const char* DTWEXBGS = "DTWEXBGS";            // Trade Weighted USD
    
    // Financial
    constexpr const char* SP500 = "SP500";                  // S&P 500
    constexpr const char* VIXCLS = "VIXCLS";                // VIX
    constexpr const char* WILLSMLCAP = "WILLSMLCAP";        // Wilshire Small Cap
    
    // Leading Indicators
    constexpr const char* USSLIND = "USSLIND";              // Leading Index
    constexpr const char* USREC = "USREC";                  // Recession Indicator
}

/**
 * @brief FRED API client
 */
class FREDClient {
public:
    explicit FREDClient(const FREDConfig& config)
        : config_(config)
        , http_client_(FREDConfig::BASE_URL) {
        
        http_client_.set_timeout(config.timeout_ms);
    }
    
    // ========================================================================
    // Series Data
    // ========================================================================
    
    /**
     * @brief Get observations for a series
     */
    std::vector<FREDObservation> get_series(
        const std::string& series_id,
        const std::string& start_date = "",
        const std::string& end_date = "",
        const std::string& frequency = "") {
        
        std::vector<FREDObservation> observations;
        
        std::map<std::string, std::string> params = {
            {"series_id", series_id}
        };
        
        if (!start_date.empty()) {
            params["observation_start"] = start_date;
        }
        if (!end_date.empty()) {
            params["observation_end"] = end_date;
        }
        if (!frequency.empty()) {
            params["frequency"] = frequency;
        }
        
        auto response = request("/series/observations", params);
        if (!response) return observations;
        
        auto& json = *response;
        if (!json.contains("observations") || !json["observations"].is_array()) {
            return observations;
        }
        
        for (const auto& item : json["observations"]) {
            FREDObservation obs;
            obs.date = item.value("date", "");
            obs.realtime_start = item.value("realtime_start", "");
            obs.realtime_end = item.value("realtime_end", "");
            
            std::string value_str = item.value("value", ".");
            if (value_str == ".") {
                obs.is_missing = true;
                obs.value = 0;
            } else {
                try {
                    obs.value = std::stod(value_str);
                } catch (...) {
                    obs.is_missing = true;
                }
            }
            
            observations.push_back(obs);
        }
        
        return observations;
    }
    
    /**
     * @brief Get latest value for a series
     */
    std::optional<FREDObservation> get_latest(const std::string& series_id) {
        auto params = std::map<std::string, std::string>{
            {"series_id", series_id},
            {"sort_order", "desc"},
            {"limit", "1"}
        };
        
        auto response = request("/series/observations", params);
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("observations") || !json["observations"].is_array() ||
            json["observations"].empty()) {
            return std::nullopt;
        }
        
        const auto& item = json["observations"][0];
        FREDObservation obs;
        obs.date = item.value("date", "");
        
        std::string value_str = item.value("value", ".");
        if (value_str == ".") {
            obs.is_missing = true;
        } else {
            try {
                obs.value = std::stod(value_str);
            } catch (...) {
                obs.is_missing = true;
            }
        }
        
        return obs;
    }
    
    /**
     * @brief Get series metadata
     */
    std::optional<FREDSeries> get_series_info(const std::string& series_id) {
        auto response = request("/series", {{"series_id", series_id}});
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("seriess") || !json["seriess"].is_array() ||
            json["seriess"].empty()) {
            return std::nullopt;
        }
        
        const auto& item = json["seriess"][0];
        FREDSeries series;
        
        series.id = item.value("id", "");
        series.title = item.value("title", "");
        series.observation_start = item.value("observation_start", "");
        series.observation_end = item.value("observation_end", "");
        series.frequency = item.value("frequency", "");
        series.frequency_short = item.value("frequency_short", "");
        series.units = item.value("units", "");
        series.units_short = item.value("units_short", "");
        series.seasonal_adjustment = item.value("seasonal_adjustment", "");
        series.seasonal_adjustment_short = item.value("seasonal_adjustment_short", "");
        series.last_updated = item.value("last_updated", "");
        series.popularity = item.value("popularity", 0);
        series.notes = item.value("notes", "");
        
        return series;
    }
    
    // ========================================================================
    // Search
    // ========================================================================
    
    /**
     * @brief Search for series
     */
    std::vector<FREDSeries> search(const std::string& query, int limit = 20) {
        std::vector<FREDSeries> results;
        
        auto response = request("/series/search", {
            {"search_text", query},
            {"limit", std::to_string(limit)}
        });
        
        if (!response) return results;
        
        auto& json = *response;
        if (!json.contains("seriess") || !json["seriess"].is_array()) {
            return results;
        }
        
        for (const auto& item : json["seriess"]) {
            FREDSeries series;
            series.id = item.value("id", "");
            series.title = item.value("title", "");
            series.frequency = item.value("frequency", "");
            series.units = item.value("units", "");
            series.seasonal_adjustment = item.value("seasonal_adjustment", "");
            series.popularity = item.value("popularity", 0);
            
            results.push_back(series);
        }
        
        return results;
    }
    
    // ========================================================================
    // Categories
    // ========================================================================
    
    /**
     * @brief Get category info
     */
    std::optional<FREDCategory> get_category(int category_id) {
        auto response = request("/category", {
            {"category_id", std::to_string(category_id)}
        });
        
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("categories") || !json["categories"].is_array() ||
            json["categories"].empty()) {
            return std::nullopt;
        }
        
        const auto& item = json["categories"][0];
        FREDCategory cat;
        cat.id = item.value("id", 0);
        cat.name = item.value("name", "");
        cat.parent_id = item.value("parent_id", 0);
        cat.notes = item.value("notes", "");
        
        return cat;
    }
    
    /**
     * @brief Get child categories
     */
    std::vector<FREDCategory> get_child_categories(int category_id) {
        std::vector<FREDCategory> categories;
        
        auto response = request("/category/children", {
            {"category_id", std::to_string(category_id)}
        });
        
        if (!response) return categories;
        
        auto& json = *response;
        if (!json.contains("categories") || !json["categories"].is_array()) {
            return categories;
        }
        
        for (const auto& item : json["categories"]) {
            FREDCategory cat;
            cat.id = item.value("id", 0);
            cat.name = item.value("name", "");
            cat.parent_id = item.value("parent_id", 0);
            
            categories.push_back(cat);
        }
        
        return categories;
    }
    
    /**
     * @brief Get series in a category
     */
    std::vector<FREDSeries> get_category_series(int category_id, int limit = 100) {
        std::vector<FREDSeries> series;
        
        auto response = request("/category/series", {
            {"category_id", std::to_string(category_id)},
            {"limit", std::to_string(limit)}
        });
        
        if (!response) return series;
        
        auto& json = *response;
        if (!json.contains("seriess") || !json["seriess"].is_array()) {
            return series;
        }
        
        for (const auto& item : json["seriess"]) {
            FREDSeries s;
            s.id = item.value("id", "");
            s.title = item.value("title", "");
            s.frequency = item.value("frequency", "");
            s.units = item.value("units", "");
            s.popularity = item.value("popularity", 0);
            
            series.push_back(s);
        }
        
        return series;
    }
    
    // ========================================================================
    // Releases
    // ========================================================================
    
    /**
     * @brief Get release info
     */
    std::optional<FREDRelease> get_release(int release_id) {
        auto response = request("/release", {
            {"release_id", std::to_string(release_id)}
        });
        
        if (!response) return std::nullopt;
        
        auto& json = *response;
        if (!json.contains("releases") || !json["releases"].is_array() ||
            json["releases"].empty()) {
            return std::nullopt;
        }
        
        const auto& item = json["releases"][0];
        FREDRelease release;
        release.id = item.value("id", 0);
        release.name = item.value("name", "");
        release.press_release = item.value("press_release", false);
        release.link = item.value("link", "");
        release.notes = item.value("notes", "");
        
        return release;
    }
    
    // ========================================================================
    // Convenience Methods for Common Indicators
    // ========================================================================
    
    /**
     * @brief Get current unemployment rate
     */
    std::optional<double> get_unemployment_rate() {
        auto obs = get_latest(indicators::UNRATE);
        if (obs && !obs->is_missing) return obs->value;
        return std::nullopt;
    }
    
    /**
     * @brief Get current Fed Funds rate
     */
    std::optional<double> get_fed_funds_rate() {
        auto obs = get_latest(indicators::FEDFUNDS);
        if (obs && !obs->is_missing) return obs->value;
        return std::nullopt;
    }
    
    /**
     * @brief Get current 10-year Treasury yield
     */
    std::optional<double> get_10y_treasury() {
        auto obs = get_latest(indicators::DGS10);
        if (obs && !obs->is_missing) return obs->value;
        return std::nullopt;
    }
    
    /**
     * @brief Get current CPI inflation
     */
    std::optional<double> get_cpi() {
        auto obs = get_latest(indicators::CPIAUCSL);
        if (obs && !obs->is_missing) return obs->value;
        return std::nullopt;
    }
    
    /**
     * @brief Get yield curve (10Y-2Y spread)
     */
    std::optional<double> get_yield_spread() {
        auto obs = get_latest(indicators::T10Y2Y);
        if (obs && !obs->is_missing) return obs->value;
        return std::nullopt;
    }
    
    /**
     * @brief Check recession indicator
     */
    std::optional<bool> is_recession() {
        auto obs = get_latest(indicators::USREC);
        if (obs && !obs->is_missing) return obs->value > 0.5;
        return std::nullopt;
    }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    const FREDConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }

private:
    FREDConfig config_;
    core::HttpClient http_client_;
    std::string last_error_;
    
    std::optional<core::JsonValue> request(
        const std::string& endpoint,
        const std::map<std::string, std::string>& params) {
        
        // Build URL
        std::ostringstream url;
        url << endpoint << "?api_key=" << config_.api_key << "&file_type=json";
        
        for (const auto& [key, value] : params) {
            url << "&" << key << "=" << core::url_encode(value);
        }
        
        auto response = http_client_.get(url.str());
        
        if (!response.success) {
            last_error_ = response.error;
            return std::nullopt;
        }
        
        try {
            return core::JsonParser::parse(response.body);
        }
        catch (const std::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return std::nullopt;
        }
    }
};

/**
 * @brief Create FRED client from environment variable
 */
inline std::unique_ptr<FREDClient> create_fred_client_from_env() {
    const char* key = std::getenv("FRED_API_KEY");
    if (!key) return nullptr;
    
    FREDConfig config;
    config.api_key = key;
    
    return std::make_unique<FREDClient>(config);
}

/**
 * @brief Economic indicator snapshot
 */
struct EconomicSnapshot {
    double unemployment_rate{0};
    double fed_funds_rate{0};
    double treasury_10y{0};
    double treasury_2y{0};
    double yield_spread{0};
    double cpi_yoy{0};
    double gdp_growth{0};
    bool recession{false};
    std::string as_of_date;
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Economic Snapshot (" << as_of_date << "):\n";
        oss << "  Unemployment: " << unemployment_rate << "%\n";
        oss << "  Fed Funds: " << fed_funds_rate << "%\n";
        oss << "  10Y Treasury: " << treasury_10y << "%\n";
        oss << "  2Y Treasury: " << treasury_2y << "%\n";
        oss << "  Yield Spread: " << yield_spread << " bps\n";
        oss << "  CPI: " << cpi_yoy << "%\n";
        oss << "  Recession: " << (recession ? "Yes" : "No") << "\n";
        return oss.str();
    }
};

/**
 * @brief Get current economic snapshot
 */
inline EconomicSnapshot get_economic_snapshot(FREDClient& client) {
    EconomicSnapshot snap;
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream date_oss;
    date_oss << std::put_time(std::localtime(&time), "%Y-%m-%d");
    snap.as_of_date = date_oss.str();
    
    if (auto v = client.get_unemployment_rate()) snap.unemployment_rate = *v;
    if (auto v = client.get_fed_funds_rate()) snap.fed_funds_rate = *v;
    if (auto v = client.get_10y_treasury()) snap.treasury_10y = *v;
    if (auto v = client.get_yield_spread()) snap.yield_spread = *v * 100;  // Convert to bps
    if (auto v = client.is_recession()) snap.recession = *v;
    
    // Get 2Y Treasury
    if (auto obs = client.get_latest(indicators::DGS2)) {
        if (!obs->is_missing) snap.treasury_2y = obs->value;
    }
    
    return snap;
}

} // namespace genie::market

#endif // GENIE_MARKET_FRED_CLIENT_HPP
