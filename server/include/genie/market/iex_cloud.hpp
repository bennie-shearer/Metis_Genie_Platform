/**
 * @file iex_cloud.hpp
 * @brief IEX Cloud API client for real-time market data
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * IEX Cloud API integration providing:
 * - Real-time and delayed quotes
 * - Historical price data
 * - Company fundamentals and financials
 * - Market statistics and news
 * - Batch quote requests
 * - SSE streaming support
 * 
 * API Documentation: https://iexcloud.io/docs/api/
 */
#pragma once
#ifndef GENIE_MARKET_IEX_CLOUD_HPP
#define GENIE_MARKET_IEX_CLOUD_HPP

#include "../core/http_client.hpp"
#include "market_data.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::market {

/**
 * @brief IEX Cloud configuration
 */
struct IEXConfig {
    std::string api_token;
    bool sandbox{false};              // Use sandbox environment
    std::string version{"stable"};    // API version
    int timeout_ms{10000};
    int rate_limit_per_second{100};   // Depends on plan
    
    std::string base_url() const {
        if (sandbox) {
            return "https://sandbox.iexapis.com/" + version;
        }
        return "https://cloud.iexapis.com/" + version;
    }
    
    bool is_valid() const {
        return !api_token.empty();
    }
};

/**
 * @brief IEX quote data
 */
struct IEXQuote {
    std::string symbol;
    std::string company_name;
    double latest_price{0};
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double previous_close{0};
    double change{0};
    double change_percent{0};
    int64_t volume{0};
    int64_t avg_volume{0};
    double market_cap{0};
    double pe_ratio{0};
    double week52_high{0};
    double week52_low{0};
    double ytd_change{0};
    std::string latest_source;
    std::string latest_time;
    int64_t latest_update{0};
    bool is_us_market_open{false};
    
    Quote to_quote() const {
        Quote q;
        q.symbol = symbol;
        q.price = latest_price;
        q.open = open;
        q.high = high;
        q.low = low;
        q.previous_close = previous_close;
        q.change = change;
        q.change_percent = change_percent;
        q.volume = volume;
        return q;
    }
};

/**
 * @brief IEX historical price point
 */
struct IEXHistoricalPrice {
    std::string date;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double adjusted_close{0};
    int64_t volume{0};
    double change{0};
    double change_percent{0};
    double change_over_time{0};
    
    PriceBar to_price_bar() const {
        PriceBar bar;
        bar.date = date;
        bar.open = open;
        bar.high = high;
        bar.low = low;
        bar.close = close;
        bar.adjusted_close = adjusted_close > 0 ? adjusted_close : close;
        bar.volume = volume;
        return bar;
    }
};

/**
 * @brief IEX company info
 */
struct IEXCompany {
    std::string symbol;
    std::string company_name;
    std::string exchange;
    std::string industry;
    std::string website;
    std::string description;
    std::string ceo;
    std::string security_name;
    std::string issue_type;
    std::string sector;
    std::string primary_sic_code;
    int employees{0};
    std::string address;
    std::string city;
    std::string state;
    std::string zip;
    std::string country;
    std::string phone;
};

/**
 * @brief IEX key stats
 */
struct IEXStats {
    std::string symbol;
    double market_cap{0};
    double week52_high{0};
    double week52_low{0};
    double week52_change{0};
    int64_t shares_outstanding{0};
    int64_t avg_10_volume{0};
    int64_t avg_30_volume{0};
    double day_200_moving_avg{0};
    double day_50_moving_avg{0};
    int64_t employees{0};
    double ttm_eps{0};
    double ttm_dividend_rate{0};
    double dividend_yield{0};
    std::string next_dividend_date;
    std::string ex_dividend_date;
    std::string next_earnings_date;
    double pe_ratio{0};
    double beta{0};
    double max_change_percent{0};
    double year_5_change_percent{0};
    double year_2_change_percent{0};
    double year_1_change_percent{0};
    double ytd_change_percent{0};
    double month_6_change_percent{0};
    double month_3_change_percent{0};
    double month_1_change_percent{0};
    double day_30_change_percent{0};
    double day_5_change_percent{0};
};

/**
 * @brief IEX news article
 */
struct IEXNews {
    std::string datetime;
    std::string headline;
    std::string source;
    std::string url;
    std::string summary;
    std::string related;
    std::string image;
    std::string lang;
    bool has_paywall{false};
};

/**
 * @brief IEX earnings data
 */
struct IEXEarnings {
    double actual_eps{0};
    double consensus_eps{0};
    double eps_surprise{0};
    double eps_surprise_pct{0};
    std::string announce_time;
    std::string fiscal_period;
    std::string fiscal_end_date;
    std::string report_date;
    double revenue{0};
    double revenue_surprise{0};
};

/**
 * @brief IEX Cloud API client
 */
class IEXCloudClient {
public:
    explicit IEXCloudClient(const IEXConfig& config)
        : config_(config)
        , http_client_(config.base_url()) {
        
        http_client_.set_timeout(config.timeout_ms);
    }
    
    // ========================================================================
    // Quote Data
    // ========================================================================
    
    /**
     * @brief Get real-time quote
     */
    std::optional<IEXQuote> get_quote(const std::string& symbol) {
        auto response = request("/stock/" + symbol + "/quote");
        if (!response) return std::nullopt;
        
        return parse_quote(*response);
    }
    
    /**
     * @brief Get batch quotes
     */
    std::map<std::string, IEXQuote> get_quotes(const std::vector<std::string>& symbols) {
        std::map<std::string, IEXQuote> result;
        if (symbols.empty()) return result;
        
        // Build comma-separated symbol list
        std::ostringstream oss;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) oss << ",";
            oss << symbols[i];
        }
        
        auto response = request("/stock/market/batch", {
            {"symbols", oss.str()},
            {"types", "quote"}
        });
        
        if (!response) return result;
        
        // Parse batch response
        // Format: {"AAPL": {"quote": {...}}, "MSFT": {"quote": {...}}}
        auto& json = *response;
        for (const auto& sym : symbols) {
            if (json.contains(sym) && json[sym].contains("quote")) {
                auto quote = parse_quote_from_json(json[sym]["quote"]);
                if (quote) {
                    result[sym] = *quote;
                }
            }
        }
        
        return result;
    }
    
    /**
     * @brief Get delayed quote (15-min delay, lower cost)
     */
    std::optional<IEXQuote> get_delayed_quote(const std::string& symbol) {
        auto response = request("/stock/" + symbol + "/delayed-quote");
        if (!response) return std::nullopt;
        
        IEXQuote quote;
        quote.symbol = symbol;
        
        auto& json = *response;
        quote.latest_price = json.value("delayedPrice", 0.0);
        quote.high = json.value("high", 0.0);
        quote.low = json.value("low", 0.0);
        quote.volume = json.value("delayedSize", 0);
        quote.latest_time = json.value("processedTime", "");
        
        return quote;
    }
    
    // ========================================================================
    // Historical Data
    // ========================================================================
    
    /**
     * @brief Get historical prices
     * @param range: 5d, 1m, 3m, 6m, ytd, 1y, 2y, 5y, max
     */
    std::vector<IEXHistoricalPrice> get_historical(
        const std::string& symbol,
        const std::string& range = "1m") {
        
        std::vector<IEXHistoricalPrice> prices;
        
        auto response = request("/stock/" + symbol + "/chart/" + range);
        if (!response || !response->is_array()) return prices;
        
        for (const auto& item : *response) {
            IEXHistoricalPrice price;
            price.date = item.value("date", "");
            price.open = item.value("open", 0.0);
            price.high = item.value("high", 0.0);
            price.low = item.value("low", 0.0);
            price.close = item.value("close", 0.0);
            price.adjusted_close = item.value("fClose", price.close);
            price.volume = item.value("volume", 0);
            price.change = item.value("change", 0.0);
            price.change_percent = item.value("changePercent", 0.0);
            price.change_over_time = item.value("changeOverTime", 0.0);
            
            prices.push_back(price);
        }
        
        return prices;
    }
    
    /**
     * @brief Get historical prices as PriceBars
     */
    std::vector<PriceBar> get_price_bars(
        const std::string& symbol,
        const std::string& range = "1m") {
        
        auto hist = get_historical(symbol, range);
        std::vector<PriceBar> bars;
        bars.reserve(hist.size());
        
        for (const auto& h : hist) {
            bars.push_back(h.to_price_bar());
        }
        
        return bars;
    }
    
    /**
     * @brief Get intraday prices
     */
    std::vector<IEXHistoricalPrice> get_intraday(const std::string& symbol) {
        return get_historical(symbol, "1d");
    }
    
    // ========================================================================
    // Company Information
    // ========================================================================
    
    /**
     * @brief Get company info
     */
    std::optional<IEXCompany> get_company(const std::string& symbol) {
        auto response = request("/stock/" + symbol + "/company");
        if (!response) return std::nullopt;
        
        IEXCompany company;
        auto& json = *response;
        
        company.symbol = json.value("symbol", symbol);
        company.company_name = json.value("companyName", "");
        company.exchange = json.value("exchange", "");
        company.industry = json.value("industry", "");
        company.website = json.value("website", "");
        company.description = json.value("description", "");
        company.ceo = json.value("CEO", "");
        company.security_name = json.value("securityName", "");
        company.issue_type = json.value("issueType", "");
        company.sector = json.value("sector", "");
        company.primary_sic_code = json.value("primarySicCode", "");
        company.employees = json.value("employees", 0);
        company.address = json.value("address", "");
        company.city = json.value("city", "");
        company.state = json.value("state", "");
        company.zip = json.value("zip", "");
        company.country = json.value("country", "");
        company.phone = json.value("phone", "");
        
        return company;
    }
    
    /**
     * @brief Get key statistics
     */
    std::optional<IEXStats> get_stats(const std::string& symbol) {
        auto response = request("/stock/" + symbol + "/stats");
        if (!response) return std::nullopt;
        
        IEXStats stats;
        auto& json = *response;
        
        stats.symbol = symbol;
        stats.market_cap = json.value("marketcap", 0.0);
        stats.week52_high = json.value("week52high", 0.0);
        stats.week52_low = json.value("week52low", 0.0);
        stats.week52_change = json.value("week52change", 0.0);
        stats.shares_outstanding = json.value("sharesOutstanding", 0);
        stats.avg_10_volume = json.value("avg10Volume", 0);
        stats.avg_30_volume = json.value("avg30Volume", 0);
        stats.day_200_moving_avg = json.value("day200MovingAvg", 0.0);
        stats.day_50_moving_avg = json.value("day50MovingAvg", 0.0);
        stats.ttm_eps = json.value("ttmEPS", 0.0);
        stats.ttm_dividend_rate = json.value("ttmDividendRate", 0.0);
        stats.dividend_yield = json.value("dividendYield", 0.0);
        stats.next_dividend_date = json.value("nextDividendDate", "");
        stats.ex_dividend_date = json.value("exDividendDate", "");
        stats.next_earnings_date = json.value("nextEarningsDate", "");
        stats.pe_ratio = json.value("peRatio", 0.0);
        stats.beta = json.value("beta", 0.0);
        stats.ytd_change_percent = json.value("ytdChangePercent", 0.0);
        stats.year_1_change_percent = json.value("year1ChangePercent", 0.0);
        stats.month_6_change_percent = json.value("month6ChangePercent", 0.0);
        stats.month_3_change_percent = json.value("month3ChangePercent", 0.0);
        stats.month_1_change_percent = json.value("month1ChangePercent", 0.0);
        
        return stats;
    }
    
    // ========================================================================
    // News
    // ========================================================================
    
    /**
     * @brief Get company news
     */
    std::vector<IEXNews> get_news(const std::string& symbol, int count = 10) {
        std::vector<IEXNews> news;
        
        auto response = request("/stock/" + symbol + "/news/last/" + std::to_string(count));
        if (!response || !response->is_array()) return news;
        
        for (const auto& item : *response) {
            IEXNews article;
            article.datetime = item.value("datetime", "");
            article.headline = item.value("headline", "");
            article.source = item.value("source", "");
            article.url = item.value("url", "");
            article.summary = item.value("summary", "");
            article.related = item.value("related", "");
            article.image = item.value("image", "");
            article.lang = item.value("lang", "en");
            article.has_paywall = item.value("hasPaywall", false);
            
            news.push_back(article);
        }
        
        return news;
    }
    
    /**
     * @brief Get market news
     */
    std::vector<IEXNews> get_market_news(int count = 10) {
        std::vector<IEXNews> news;
        
        auto response = request("/stock/market/news/last/" + std::to_string(count));
        if (!response || !response->is_array()) return news;
        
        for (const auto& item : *response) {
            IEXNews article;
            article.datetime = item.value("datetime", "");
            article.headline = item.value("headline", "");
            article.source = item.value("source", "");
            article.url = item.value("url", "");
            article.summary = item.value("summary", "");
            article.related = item.value("related", "");
            
            news.push_back(article);
        }
        
        return news;
    }
    
    // ========================================================================
    // Earnings
    // ========================================================================
    
    /**
     * @brief Get earnings data
     */
    std::vector<IEXEarnings> get_earnings(const std::string& symbol, int quarters = 4) {
        std::vector<IEXEarnings> earnings;
        
        auto response = request("/stock/" + symbol + "/earnings/" + std::to_string(quarters));
        if (!response) return earnings;
        
        auto& json = *response;
        if (!json.contains("earnings") || !json["earnings"].is_array()) {
            return earnings;
        }
        
        for (const auto& item : json["earnings"]) {
            IEXEarnings e;
            e.actual_eps = item.value("actualEPS", 0.0);
            e.consensus_eps = item.value("consensusEPS", 0.0);
            e.eps_surprise = item.value("EPSSurpriseDollar", 0.0);
            e.eps_surprise_pct = item.value("EPSSurprisePct", 0.0);
            e.announce_time = item.value("announceTime", "");
            e.fiscal_period = item.value("fiscalPeriod", "");
            e.fiscal_end_date = item.value("fiscalEndDate", "");
            e.report_date = item.value("reportDate", "");
            
            earnings.push_back(e);
        }
        
        return earnings;
    }
    
    // ========================================================================
    // Dividends & Splits
    // ========================================================================
    
    /**
     * @brief Get dividends
     */
    std::vector<Dividend> get_dividends(const std::string& symbol, 
                                         const std::string& range = "1y") {
        std::vector<Dividend> dividends;
        
        auto response = request("/stock/" + symbol + "/dividends/" + range);
        if (!response || !response->is_array()) return dividends;
        
        for (const auto& item : *response) {
            Dividend div;
            div.symbol = symbol;
            div.date = item.value("exDate", "");
            div.amount = item.value("amount", 0.0);
            div.currency = item.value("currency", "USD");
            div.description = item.value("description", "");
            
            dividends.push_back(div);
        }
        
        return dividends;
    }
    
    /**
     * @brief Get stock splits
     */
    std::vector<Split> get_splits(const std::string& symbol,
                                   const std::string& range = "5y") {
        std::vector<Split> splits;
        
        auto response = request("/stock/" + symbol + "/splits/" + range);
        if (!response || !response->is_array()) return splits;
        
        for (const auto& item : *response) {
            Split split;
            split.symbol = symbol;
            split.date = item.value("exDate", "");
            split.ratio = item.value("ratio", 1.0);
            split.from_factor = item.value("fromFactor", 1.0);
            split.to_factor = item.value("toFactor", 1.0);
            
            splits.push_back(split);
        }
        
        return splits;
    }
    
    // ========================================================================
    // Market Status
    // ========================================================================
    
    /**
     * @brief Check if US market is open
     */
    bool is_market_open() {
        auto response = request("/stock/market/batch", {
            {"symbols", "SPY"},
            {"types", "quote"}
        });
        
        if (!response) return false;
        
        if (response->contains("SPY") && (*response)["SPY"].contains("quote")) {
            return (*response)["SPY"]["quote"].value("isUSMarketOpen", false);
        }
        
        return false;
    }
    
    // ========================================================================
    // Symbol Search
    // ========================================================================
    
    /**
     * @brief Search for symbols
     */
    std::vector<std::pair<std::string, std::string>> search(const std::string& query) {
        std::vector<std::pair<std::string, std::string>> results;
        
        auto response = request("/search/" + query);
        if (!response || !response->is_array()) return results;
        
        for (const auto& item : *response) {
            std::string symbol = item.value("symbol", "");
            std::string name = item.value("securityName", "");
            if (!symbol.empty()) {
                results.emplace_back(symbol, name);
            }
        }
        
        return results;
    }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    const IEXConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }
    
    /**
     * @brief Get API usage stats (message count)
     */
    std::optional<int64_t> get_message_budget() {
        auto response = request("/account/usage");
        if (!response) return std::nullopt;
        
        return response->value("monthlyUsage", 0);
    }

private:
    IEXConfig config_;
    core::HttpClient http_client_;
    std::string last_error_;
    
    std::optional<core::JsonValue> request(
        const std::string& endpoint,
        const std::map<std::string, std::string>& params = {}) {
        
        // Build URL with token
        std::ostringstream url;
        url << endpoint << "?token=" << config_.api_token;
        
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
    
    std::optional<IEXQuote> parse_quote(const core::JsonValue& json) {
        return parse_quote_from_json(json);
    }
    
    std::optional<IEXQuote> parse_quote_from_json(const core::JsonValue& json) {
        IEXQuote quote;
        
        quote.symbol = json.value("symbol", "");
        quote.company_name = json.value("companyName", "");
        quote.latest_price = json.value("latestPrice", 0.0);
        quote.open = json.value("open", 0.0);
        quote.high = json.value("high", 0.0);
        quote.low = json.value("low", 0.0);
        quote.close = json.value("close", 0.0);
        quote.previous_close = json.value("previousClose", 0.0);
        quote.change = json.value("change", 0.0);
        quote.change_percent = json.value("changePercent", 0.0) * 100;
        quote.volume = json.value("volume", 0);
        quote.avg_volume = json.value("avgTotalVolume", 0);
        quote.market_cap = json.value("marketCap", 0.0);
        quote.pe_ratio = json.value("peRatio", 0.0);
        quote.week52_high = json.value("week52High", 0.0);
        quote.week52_low = json.value("week52Low", 0.0);
        quote.ytd_change = json.value("ytdChange", 0.0);
        quote.latest_source = json.value("latestSource", "");
        quote.latest_time = json.value("latestTime", "");
        quote.latest_update = json.value("latestUpdate", 0);
        quote.is_us_market_open = json.value("isUSMarketOpen", false);
        
        return quote;
    }
};

/**
 * @brief Create IEX client from environment variable
 */
inline std::unique_ptr<IEXCloudClient> create_iex_client_from_env() {
    const char* token = std::getenv("IEX_TOKEN");
    if (!token) {
        token = std::getenv("IEX_CLOUD_API_TOKEN");
    }
    
    if (!token) return nullptr;
    
    IEXConfig config;
    config.api_token = token;
    
    // Check for sandbox mode
    const char* sandbox = std::getenv("IEX_SANDBOX");
    if (sandbox && std::string(sandbox) == "true") {
        config.sandbox = true;
    }
    
    return std::make_unique<IEXCloudClient>(config);
}

} // namespace genie::market

#endif // GENIE_MARKET_IEX_CLOUD_HPP
