/**
 * @file alpha_vantage.hpp
 * @brief Alpha Vantage API client for real stock quotes and data
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides integration with Alpha Vantage API:
 * - Real-time and delayed stock quotes
 * - Historical daily/weekly/monthly prices
 * - Intraday data (1min, 5min, 15min, 30min, 60min)
 * - Forex rates
 * - Cryptocurrency prices
 * - Technical indicators
 * 
 * Free tier: 500 API calls/day, 5 calls/minute
 */
#pragma once
#ifndef GENIE_MARKET_ALPHA_VANTAGE_HPP
#define GENIE_MARKET_ALPHA_VANTAGE_HPP

#include "../core/http_client.hpp"
#include "market_data.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace genie::market {

// PriceBar is now defined in market_data.hpp

// Quote struct is now defined in market_data.hpp

/**
 * @brief Forex rate
 */
struct ForexRate {
    std::string from_currency;
    std::string to_currency;
    double exchange_rate{0};
    double bid_price{0};
    double ask_price{0};
    std::string last_refreshed;
    std::string timezone;
};

/**
 * @brief Company overview/fundamentals
 */
struct CompanyOverview {
    std::string symbol;
    std::string name;
    std::string description;
    std::string exchange;
    std::string currency;
    std::string country;
    std::string sector;
    std::string industry;
    double market_cap{0};
    double pe_ratio{0};
    double peg_ratio{0};
    double book_value{0};
    double dividend_per_share{0};
    double dividend_yield{0};
    double eps{0};
    double revenue_per_share{0};
    double profit_margin{0};
    double beta{0};
    double high_52week{0};
    double low_52week{0};
    double moving_avg_50day{0};
    double moving_avg_200day{0};
    int64_t shares_outstanding{0};
};

/**
 * @brief Search result
 */
struct SymbolMatch {
    std::string symbol;
    std::string name;
    std::string type;
    std::string region;
    std::string market_open;
    std::string market_close;
    std::string timezone;
    std::string currency;
    double match_score{0};
};

/**
 * @brief Interval for intraday data
 */
enum class Interval {
    Min1,
    Min5,
    Min15,
    Min30,
    Min60
};

inline std::string interval_to_string(Interval interval) {
    switch (interval) {
        case Interval::Min1: return "1min";
        case Interval::Min5: return "5min";
        case Interval::Min15: return "15min";
        case Interval::Min30: return "30min";
        case Interval::Min60: return "60min";
    }
    return "5min";
}

/**
 * @brief Output size for historical data
 */
enum class OutputSize {
    Compact,    // Last 100 data points
    Full        // Full historical data (20+ years)
};

/**
 * @brief Alpha Vantage API client
 */
class AlphaVantageClient {
public:
    static constexpr const char* BASE_URL = "https://www.alphavantage.co";
    static constexpr int FREE_TIER_CALLS_PER_MINUTE = 5;
    static constexpr int FREE_TIER_CALLS_PER_DAY = 500;
    
    explicit AlphaVantageClient(const std::string& api_key)
        : api_key_(api_key)
        , rate_limiter_(std::make_shared<core::RateLimiter>(
            FREE_TIER_CALLS_PER_MINUTE, 60)) {
        client_.set_base_url(BASE_URL);
        client_.set_rate_limiter(rate_limiter_);
    }
    
    // === Quote Functions ===
    
    /**
     * @brief Get real-time quote for a symbol
     */
    std::optional<Quote> get_quote(const std::string& symbol) {
        auto response = client_.get("/query", {
            {"function", "GLOBAL_QUOTE"},
            {"symbol", symbol},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return std::nullopt;
        }
        
        auto json = core::JsonParser::parse(response.body);
        auto global_quote = json["Global Quote"];
        
        if (global_quote.is_null()) {
            // Check for error message
            if (json.has("Note")) {
                last_error_ = "Rate limit: " + json["Note"].as_string();
            } else if (json.has("Error Message")) {
                last_error_ = json["Error Message"].as_string();
            } else {
                last_error_ = "No quote data returned";
            }
            return std::nullopt;
        }
        
        Quote q;
        q.symbol = global_quote["01. symbol"].as_string();
        q.open = std::stod(global_quote["02. open"].as_string());
        q.high = std::stod(global_quote["03. high"].as_string());
        q.low = std::stod(global_quote["04. low"].as_string());
        q.price = std::stod(global_quote["05. price"].as_string());
        q.volume = static_cast<int64_t>(std::stod(global_quote["06. volume"].as_string()));
        q.latest_trading_day = global_quote["07. latest trading day"].as_string();
        q.previous_close = std::stod(global_quote["08. previous close"].as_string());
        q.change = std::stod(global_quote["09. change"].as_string());
        
        std::string pct = global_quote["10. change percent"].as_string();
        if (!pct.empty() && pct.back() == '%') pct.pop_back();
        q.change_percent = std::stod(pct);
        
        q.timestamp = std::chrono::system_clock::now();
        
        return q;
    }
    
    /**
     * @brief Get quotes for multiple symbols
     */
    std::map<std::string, Quote> get_quotes(const std::vector<std::string>& symbols) {
        std::map<std::string, Quote> result;
        for (const auto& symbol : symbols) {
            auto quote = get_quote(symbol);
            if (quote) {
                result[symbol] = *quote;
            }
        }
        return result;
    }
    
    // === Historical Data Functions ===
    
    /**
     * @brief Get daily adjusted prices
     */
    std::vector<PriceBar> get_daily_adjusted(const std::string& symbol,
                                              OutputSize size = OutputSize::Compact) {
        auto response = client_.get("/query", {
            {"function", "TIME_SERIES_DAILY_ADJUSTED"},
            {"symbol", symbol},
            {"outputsize", size == OutputSize::Full ? "full" : "compact"},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_daily_adjusted(response.body);
    }
    
    /**
     * @brief Get daily prices (non-adjusted)
     */
    std::vector<PriceBar> get_daily(const std::string& symbol,
                                    OutputSize size = OutputSize::Compact) {
        auto response = client_.get("/query", {
            {"function", "TIME_SERIES_DAILY"},
            {"symbol", symbol},
            {"outputsize", size == OutputSize::Full ? "full" : "compact"},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_daily(response.body);
    }
    
    /**
     * @brief Get intraday prices
     */
    std::vector<PriceBar> get_intraday(const std::string& symbol,
                                        Interval interval = Interval::Min5,
                                        OutputSize size = OutputSize::Compact) {
        auto response = client_.get("/query", {
            {"function", "TIME_SERIES_INTRADAY"},
            {"symbol", symbol},
            {"interval", interval_to_string(interval)},
            {"outputsize", size == OutputSize::Full ? "full" : "compact"},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_intraday(response.body, interval);
    }
    
    /**
     * @brief Get weekly adjusted prices
     */
    std::vector<PriceBar> get_weekly_adjusted(const std::string& symbol) {
        auto response = client_.get("/query", {
            {"function", "TIME_SERIES_WEEKLY_ADJUSTED"},
            {"symbol", symbol},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_weekly_adjusted(response.body);
    }
    
    /**
     * @brief Get monthly adjusted prices
     */
    std::vector<PriceBar> get_monthly_adjusted(const std::string& symbol) {
        auto response = client_.get("/query", {
            {"function", "TIME_SERIES_MONTHLY_ADJUSTED"},
            {"symbol", symbol},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_monthly_adjusted(response.body);
    }
    
    // === Forex Functions ===
    
    /**
     * @brief Get forex exchange rate
     */
    std::optional<ForexRate> get_forex_rate(const std::string& from_currency,
                                            const std::string& to_currency) {
        auto response = client_.get("/query", {
            {"function", "CURRENCY_EXCHANGE_RATE"},
            {"from_currency", from_currency},
            {"to_currency", to_currency},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return std::nullopt;
        }
        
        auto json = core::JsonParser::parse(response.body);
        auto rate_data = json["Realtime Currency Exchange Rate"];
        
        if (rate_data.is_null()) {
            last_error_ = "No forex data returned";
            return std::nullopt;
        }
        
        ForexRate rate;
        rate.from_currency = rate_data["1. From_Currency Code"].as_string();
        rate.to_currency = rate_data["3. To_Currency Code"].as_string();
        rate.exchange_rate = std::stod(rate_data["5. Exchange Rate"].as_string());
        rate.bid_price = std::stod(rate_data["8. Bid Price"].as_string());
        rate.ask_price = std::stod(rate_data["9. Ask Price"].as_string());
        rate.last_refreshed = rate_data["6. Last Refreshed"].as_string();
        rate.timezone = rate_data["7. Time Zone"].as_string();
        
        return rate;
    }
    
    /**
     * @brief Get forex daily prices
     */
    std::vector<PriceBar> get_forex_daily(const std::string& from_symbol,
                                          const std::string& to_symbol,
                                          OutputSize size = OutputSize::Compact) {
        auto response = client_.get("/query", {
            {"function", "FX_DAILY"},
            {"from_symbol", from_symbol},
            {"to_symbol", to_symbol},
            {"outputsize", size == OutputSize::Full ? "full" : "compact"},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        return parse_fx_daily(response.body);
    }
    
    // === Search Functions ===
    
    /**
     * @brief Search for symbols by keywords
     */
    std::vector<SymbolMatch> search_symbols(const std::string& keywords) {
        auto response = client_.get("/query", {
            {"function", "SYMBOL_SEARCH"},
            {"keywords", keywords},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return {};
        }
        
        auto json = core::JsonParser::parse(response.body);
        auto matches = json["bestMatches"];
        
        std::vector<SymbolMatch> result;
        if (matches.is_array()) {
            for (size_t i = 0; i < matches.size(); ++i) {
                const auto& m = matches[i];
                SymbolMatch sm;
                sm.symbol = m["1. symbol"].as_string();
                sm.name = m["2. name"].as_string();
                sm.type = m["3. type"].as_string();
                sm.region = m["4. region"].as_string();
                sm.market_open = m["5. marketOpen"].as_string();
                sm.market_close = m["6. marketClose"].as_string();
                sm.timezone = m["7. timezone"].as_string();
                sm.currency = m["8. currency"].as_string();
                sm.match_score = std::stod(m["9. matchScore"].as_string());
                result.push_back(sm);
            }
        }
        
        return result;
    }
    
    // === Fundamental Data ===
    
    /**
     * @brief Get company overview/fundamentals
     */
    std::optional<CompanyOverview> get_company_overview(const std::string& symbol) {
        auto response = client_.get("/query", {
            {"function", "OVERVIEW"},
            {"symbol", symbol},
            {"apikey", api_key_}
        });
        
        if (!response.ok()) {
            last_error_ = "API error: " + response.error;
            return std::nullopt;
        }
        
        auto json = core::JsonParser::parse(response.body);
        
        if (!json.has("Symbol")) {
            last_error_ = "No company data returned";
            return std::nullopt;
        }
        
        CompanyOverview co;
        co.symbol = json["Symbol"].as_string();
        co.name = json["Name"].as_string();
        co.description = json["Description"].as_string();
        co.exchange = json["Exchange"].as_string();
        co.currency = json["Currency"].as_string();
        co.country = json["Country"].as_string();
        co.sector = json["Sector"].as_string();
        co.industry = json["Industry"].as_string();
        
        // Parse numeric fields with error handling
        auto safe_stod = [](const std::string& s) -> double {
            if (s.empty() || s == "None" || s == "-") return 0;
            try { return std::stod(s); }
            catch (...) { return 0; }
        };
        
        auto safe_stoll = [](const std::string& s) -> int64_t {
            if (s.empty() || s == "None" || s == "-") return 0;
            try { return std::stoll(s); }
            catch (...) { return 0; }
        };
        
        co.market_cap = safe_stod(json["MarketCapitalization"].as_string());
        co.pe_ratio = safe_stod(json["PERatio"].as_string());
        co.peg_ratio = safe_stod(json["PEGRatio"].as_string());
        co.book_value = safe_stod(json["BookValue"].as_string());
        co.dividend_per_share = safe_stod(json["DividendPerShare"].as_string());
        co.dividend_yield = safe_stod(json["DividendYield"].as_string());
        co.eps = safe_stod(json["EPS"].as_string());
        co.revenue_per_share = safe_stod(json["RevenuePerShareTTM"].as_string());
        co.profit_margin = safe_stod(json["ProfitMargin"].as_string());
        co.beta = safe_stod(json["Beta"].as_string());
        co.high_52week = safe_stod(json["52WeekHigh"].as_string());
        co.low_52week = safe_stod(json["52WeekLow"].as_string());
        co.moving_avg_50day = safe_stod(json["50DayMovingAverage"].as_string());
        co.moving_avg_200day = safe_stod(json["200DayMovingAverage"].as_string());
        co.shares_outstanding = safe_stoll(json["SharesOutstanding"].as_string());
        
        return co;
    }
    
    // === Utility Functions ===
    
    const std::string& last_error() const { return last_error_; }
    
    int remaining_calls() const {
        return rate_limiter_ ? rate_limiter_->remaining() : 0;
    }
    
    void set_api_key(const std::string& key) {
        api_key_ = key;
    }
    
    core::HttpClient& http_client() { return client_; }

private:
    std::string api_key_;
    core::HttpClient client_;
    std::shared_ptr<core::RateLimiter> rate_limiter_;
    std::string last_error_;
    
    // === Parsing Helpers ===
    
    std::vector<PriceBar> parse_daily_adjusted(const std::string& json_str) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        auto time_series = json["Time Series (Daily)"];
        if (!time_series.is_object()) return result;
        
        for (const auto& [date, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = date;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = std::stod(data["5. adjusted close"].as_string());
            bar.volume = static_cast<int64_t>(std::stod(data["6. volume"].as_string()));
            bar.dividend_amount = std::stod(data["7. dividend amount"].as_string());
            bar.split_coefficient = std::stod(data["8. split coefficient"].as_string());
            result.push_back(bar);
        }
        
        // Sort by date descending (most recent first)
        std::sort(result.begin(), result.end(), 
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<PriceBar> parse_daily(const std::string& json_str) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        auto time_series = json["Time Series (Daily)"];
        if (!time_series.is_object()) return result;
        
        for (const auto& [date, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = date;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = bar.close;
            bar.volume = static_cast<int64_t>(std::stod(data["5. volume"].as_string()));
            result.push_back(bar);
        }
        
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<PriceBar> parse_intraday(const std::string& json_str, Interval interval) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        std::string key = "Time Series (" + interval_to_string(interval) + ")";
        auto time_series = json[key];
        if (!time_series.is_object()) return result;
        
        for (const auto& [datetime, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = datetime;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = bar.close;
            bar.volume = static_cast<int64_t>(std::stod(data["5. volume"].as_string()));
            result.push_back(bar);
        }
        
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<PriceBar> parse_weekly_adjusted(const std::string& json_str) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        auto time_series = json["Weekly Adjusted Time Series"];
        if (!time_series.is_object()) return result;
        
        for (const auto& [date, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = date;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = std::stod(data["5. adjusted close"].as_string());
            bar.volume = static_cast<int64_t>(std::stod(data["6. volume"].as_string()));
            bar.dividend_amount = std::stod(data["7. dividend amount"].as_string());
            result.push_back(bar);
        }
        
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<PriceBar> parse_monthly_adjusted(const std::string& json_str) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        auto time_series = json["Monthly Adjusted Time Series"];
        if (!time_series.is_object()) return result;
        
        for (const auto& [date, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = date;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = std::stod(data["5. adjusted close"].as_string());
            bar.volume = static_cast<int64_t>(std::stod(data["6. volume"].as_string()));
            bar.dividend_amount = std::stod(data["7. dividend amount"].as_string());
            result.push_back(bar);
        }
        
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<PriceBar> parse_fx_daily(const std::string& json_str) {
        std::vector<PriceBar> result;
        auto json = core::JsonParser::parse(json_str);
        
        if (check_for_error(json)) return result;
        
        auto time_series = json["Time Series FX (Daily)"];
        if (!time_series.is_object()) return result;
        
        for (const auto& [date, data] : time_series.as_object()) {
            PriceBar bar;
            bar.date = date;
            bar.open = std::stod(data["1. open"].as_string());
            bar.high = std::stod(data["2. high"].as_string());
            bar.low = std::stod(data["3. low"].as_string());
            bar.close = std::stod(data["4. close"].as_string());
            bar.adjusted_close = bar.close;
            bar.volume = 0; // FX has no volume
            result.push_back(bar);
        }
        
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    bool check_for_error(const core::JsonValue& json) {
        if (json.has("Note")) {
            last_error_ = "Rate limit: " + json["Note"].as_string();
            return true;
        }
        if (json.has("Error Message")) {
            last_error_ = json["Error Message"].as_string();
            return true;
        }
        if (json.has("Information")) {
            last_error_ = json["Information"].as_string();
            return true;
        }
        return false;
    }
};

} // namespace genie::market

#endif // GENIE_MARKET_ALPHA_VANTAGE_HPP
