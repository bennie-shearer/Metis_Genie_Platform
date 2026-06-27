/**
 * @file finnhub_client.hpp
 * @brief Finnhub API client for news, sentiment, and market data
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Finnhub integration providing:
 * - Company news and market news
 * - Social sentiment analysis
 * - Insider transactions
 * - Earnings calendars
 * - IPO and merger calendars
 * - Real-time quotes
 * 
 * API Documentation: https://finnhub.io/docs/api
 */
#pragma once
#ifndef GENIE_MARKET_FINNHUB_CLIENT_HPP
#define GENIE_MARKET_FINNHUB_CLIENT_HPP

#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace genie::market {

/**
 * @brief Finnhub configuration
 */
struct FinnhubConfig {
    std::string api_key;
    int timeout_ms{10000};
    
    static constexpr const char* BASE_URL = "https://finnhub.io/api/v1";
    
    bool is_valid() const {
        return !api_key.empty();
    }
};

/**
 * @brief News article
 */
struct FinnhubNews {
    std::string category;
    int64_t datetime{0};              // Unix timestamp
    std::string headline;
    int id{0};
    std::string image;
    std::string related;              // Comma-separated symbols
    std::string source;
    std::string summary;
    std::string url;
    
    std::string formatted_date() const {
        std::time_t t = static_cast<std::time_t>(datetime);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M");
        return oss.str();
    }
    
    std::vector<std::string> related_symbols() const {
        std::vector<std::string> result;
        std::istringstream iss(related);
        std::string token;
        while (std::getline(iss, token, ',')) {
            if (!token.empty()) {
                result.push_back(token);
            }
        }
        return result;
    }
};

/**
 * @brief Social sentiment data
 */
struct FinnhubSentiment {
    std::string symbol;
    double reddit_mention{0};
    double reddit_positive_mention{0};
    double reddit_negative_mention{0};
    double twitter_mention{0};
    double twitter_positive_mention{0};
    double twitter_negative_mention{0};
    double score{0};
    int buzz{0};
    int at_time{0};                   // Unix timestamp
    
    double reddit_sentiment() const {
        if (reddit_mention <= 0) return 0;
        return (reddit_positive_mention - reddit_negative_mention) / reddit_mention;
    }
    
    double twitter_sentiment() const {
        if (twitter_mention <= 0) return 0;
        return (twitter_positive_mention - twitter_negative_mention) / twitter_mention;
    }
    
    double overall_sentiment() const {
        double total = reddit_mention + twitter_mention;
        if (total <= 0) return 0;
        double positive = reddit_positive_mention + twitter_positive_mention;
        double negative = reddit_negative_mention + twitter_negative_mention;
        return (positive - negative) / total;
    }
};

/**
 * @brief Insider transaction
 */
struct FinnhubInsiderTransaction {
    std::string symbol;
    std::string name;                 // Insider name
    int share{0};                     // Number of shares
    int change{0};                    // Change in shares
    std::string transaction_code;     // P=Purchase, S=Sale, etc.
    std::string transaction_date;
    double transaction_price{0};
    std::string filing_date;
    bool is_derivative{false};
    
    bool is_purchase() const {
        return transaction_code == "P" || transaction_code == "A";
    }
    
    bool is_sale() const {
        return transaction_code == "S" || transaction_code == "D";
    }
    
    double transaction_value() const {
        return std::abs(change) * transaction_price;
    }
};

/**
 * @brief Earnings announcement
 */
struct FinnhubEarnings {
    std::string symbol;
    std::string date;
    double eps_actual{0};
    double eps_estimate{0};
    double revenue_actual{0};
    double revenue_estimate{0};
    std::string hour;                 // bmo, amc, dmh
    int quarter{0};
    int year{0};
    
    double eps_surprise() const {
        return eps_actual - eps_estimate;
    }
    
    double eps_surprise_pct() const {
        if (eps_estimate == 0) return 0;
        return (eps_actual - eps_estimate) / std::abs(eps_estimate) * 100;
    }
    
    double revenue_surprise() const {
        return revenue_actual - revenue_estimate;
    }
    
    bool is_beat() const {
        return eps_actual > eps_estimate;
    }
};

/**
 * @brief IPO event
 */
struct FinnhubIPO {
    std::string symbol;
    std::string name;
    std::string date;
    std::string exchange;
    double price{0};
    int64_t shares{0};
    double offering_amount{0};
    std::string status;
};

/**
 * @brief Quote data
 */
struct FinnhubQuote {
    std::string symbol;
    double current{0};                // Current price
    double change{0};                 // Change
    double percent_change{0};         // Percent change
    double high{0};                   // Day high
    double low{0};                    // Day low
    double open{0};                   // Open price
    double previous_close{0};         // Previous close
    int64_t timestamp{0};             // Unix timestamp
    
    bool is_valid() const {
        return current > 0;
    }
};

/**
 * @brief Company profile
 */
struct FinnhubProfile {
    std::string ticker;
    std::string name;
    std::string country;
    std::string currency;
    std::string exchange;
    std::string ipo;                  // IPO date
    double market_cap{0};
    double shares_outstanding{0};
    std::string logo;
    std::string phone;
    std::string weburl;
    std::string finnhub_industry;
    std::string gics_industry;
    std::string naics_industry;
};

/**
 * @brief Recommendation trend
 */
struct FinnhubRecommendation {
    std::string period;
    int strong_buy{0};
    int buy{0};
    int hold{0};
    int sell{0};
    int strong_sell{0};
    
    int total() const {
        return strong_buy + buy + hold + sell + strong_sell;
    }
    
    double consensus_score() const {
        int t = total();
        if (t == 0) return 0;
        // Scale: Strong Buy=5, Buy=4, Hold=3, Sell=2, Strong Sell=1
        double weighted = strong_buy * 5 + buy * 4 + hold * 3 + sell * 2 + strong_sell * 1;
        return weighted / t;
    }
    
    std::string consensus() const {
        double score = consensus_score();
        if (score >= 4.5) return "Strong Buy";
        if (score >= 3.5) return "Buy";
        if (score >= 2.5) return "Hold";
        if (score >= 1.5) return "Sell";
        return "Strong Sell";
    }
};

/**
 * @brief Finnhub API client
 */
class FinnhubClient {
public:
    explicit FinnhubClient(const FinnhubConfig& config)
        : config_(config)
        , http_(FinnhubConfig::BASE_URL) {
        
        http_.set_timeout(config.timeout_ms);
    }
    
    // ========================================================================
    // News
    // ========================================================================
    
    /**
     * @brief Get company news
     */
    std::vector<FinnhubNews> get_company_news(
        const std::string& symbol,
        const std::string& from = "",
        const std::string& to = "") {
        
        std::vector<FinnhubNews> result;
        
        std::map<std::string, std::string> params = {{"symbol", symbol}};
        
        if (!from.empty()) params["from"] = from;
        if (!to.empty()) params["to"] = to;
        
        // Default to last 7 days
        if (from.empty() && to.empty()) {
            auto now = std::chrono::system_clock::now();
            auto week_ago = now - std::chrono::hours(24 * 7);
            
            auto now_t = std::chrono::system_clock::to_time_t(now);
            auto week_ago_t = std::chrono::system_clock::to_time_t(week_ago);
            
            std::ostringstream from_oss, to_oss;
            from_oss << std::put_time(std::localtime(&week_ago_t), "%Y-%m-%d");
            to_oss << std::put_time(std::localtime(&now_t), "%Y-%m-%d");
            
            params["from"] = from_oss.str();
            params["to"] = to_oss.str();
        }
        
        auto response = make_request("/company-news", params);
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_array()) return result;
        
        for (const auto& item : json.array()) {
            FinnhubNews news;
            news.category = item.get_string("category", "");
            news.datetime = item.get_int("datetime", 0);
            news.headline = item.get_string("headline", "");
            news.id = item.get_int("id", 0);
            news.image = item.get_string("image", "");
            news.related = item.get_string("related", "");
            news.source = item.get_string("source", "");
            news.summary = item.get_string("summary", "");
            news.url = item.get_string("url", "");
            
            result.push_back(news);
        }
        
        return result;
    }
    
    /**
     * @brief Get market-wide news
     */
    std::vector<FinnhubNews> get_market_news(const std::string& category = "general") {
        std::vector<FinnhubNews> result;
        
        auto response = make_request("/news", {{"category", category}});
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_array()) return result;
        
        for (const auto& item : json.array()) {
            FinnhubNews news;
            news.category = item.get_string("category", category);
            news.datetime = item.get_int("datetime", 0);
            news.headline = item.get_string("headline", "");
            news.id = item.get_int("id", 0);
            news.image = item.get_string("image", "");
            news.related = item.get_string("related", "");
            news.source = item.get_string("source", "");
            news.summary = item.get_string("summary", "");
            news.url = item.get_string("url", "");
            
            result.push_back(news);
        }
        
        return result;
    }
    
    // ========================================================================
    // Sentiment
    // ========================================================================
    
    /**
     * @brief Get social sentiment
     */
    std::optional<FinnhubSentiment> get_social_sentiment(const std::string& symbol) {
        auto response = make_request("/stock/social-sentiment", {{"symbol", symbol}});
        if (!response) return std::nullopt;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return std::nullopt;
        
        FinnhubSentiment sentiment;
        sentiment.symbol = json.get_string("symbol", symbol);
        
        if (json.contains("reddit") && json["reddit"].is_array() && !json["reddit"].array().empty()) {
            const auto& reddit = json["reddit"].array().back();
            sentiment.reddit_mention = reddit.get_double("mention", 0);
            sentiment.reddit_positive_mention = reddit.get_double("positiveMention", 0);
            sentiment.reddit_negative_mention = reddit.get_double("negativeMention", 0);
            sentiment.at_time = reddit.get_int("atTime", 0);
        }
        
        if (json.contains("twitter") && json["twitter"].is_array() && !json["twitter"].array().empty()) {
            const auto& twitter = json["twitter"].array().back();
            sentiment.twitter_mention = twitter.get_double("mention", 0);
            sentiment.twitter_positive_mention = twitter.get_double("positiveMention", 0);
            sentiment.twitter_negative_mention = twitter.get_double("negativeMention", 0);
        }
        
        return sentiment;
    }
    
    /**
     * @brief Get news sentiment
     */
    struct NewsSentiment {
        std::string symbol;
        double buzz_articles_in_last_week{0};
        double weekly_average{0};
        double buzz{0};
        double company_news_score{0};
        double sector_average_news_score{0};
        double sector_average_bullish_percent{0};
        double bullish_percent{0};
        double bearish_percent{0};
    };
    
    std::optional<NewsSentiment> get_news_sentiment(const std::string& symbol) {
        auto response = make_request("/news-sentiment", {{"symbol", symbol}});
        if (!response) return std::nullopt;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return std::nullopt;
        
        NewsSentiment sentiment;
        sentiment.symbol = json.get_string("symbol", symbol);
        
        if (json.contains("buzz")) {
            const auto& buzz = json["buzz"];
            sentiment.buzz_articles_in_last_week = buzz.get_double("articlesInLastWeek", 0);
            sentiment.weekly_average = buzz.get_double("weeklyAverage", 0);
            sentiment.buzz = buzz.get_double("buzz", 0);
        }
        
        if (json.contains("sentiment")) {
            const auto& sent = json["sentiment"];
            sentiment.bullish_percent = sent.get_double("bullishPercent", 0);
            sentiment.bearish_percent = sent.get_double("bearishPercent", 0);
        }
        
        sentiment.company_news_score = json.get_double("companyNewsScore", 0);
        sentiment.sector_average_news_score = json.get_double("sectorAverageNewsScore", 0);
        sentiment.sector_average_bullish_percent = json.get_double("sectorAverageBullishPercent", 0);
        
        return sentiment;
    }
    
    // ========================================================================
    // Insider Trading
    // ========================================================================
    
    /**
     * @brief Get insider transactions
     */
    std::vector<FinnhubInsiderTransaction> get_insider_transactions(const std::string& symbol) {
        std::vector<FinnhubInsiderTransaction> result;
        
        auto response = make_request("/stock/insider-transactions", {{"symbol", symbol}});
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object() || !json.contains("data")) return result;
        
        for (const auto& item : json["data"].array()) {
            FinnhubInsiderTransaction tx;
            tx.symbol = item.get_string("symbol", symbol);
            tx.name = item.get_string("name", "");
            tx.share = item.get_int("share", 0);
            tx.change = item.get_int("change", 0);
            tx.transaction_code = item.get_string("transactionCode", "");
            tx.transaction_date = item.get_string("transactionDate", "");
            tx.transaction_price = item.get_double("transactionPrice", 0);
            tx.filing_date = item.get_string("filingDate", "");
            tx.is_derivative = item.get_bool("isDerivative", false);
            
            result.push_back(tx);
        }
        
        return result;
    }
    
    // ========================================================================
    // Earnings
    // ========================================================================
    
    /**
     * @brief Get earnings calendar
     */
    std::vector<FinnhubEarnings> get_earnings_calendar(
        const std::string& from = "",
        const std::string& to = "",
        const std::string& symbol = "") {
        
        std::vector<FinnhubEarnings> result;
        
        std::map<std::string, std::string> params;
        if (!from.empty()) params["from"] = from;
        if (!to.empty()) params["to"] = to;
        if (!symbol.empty()) params["symbol"] = symbol;
        
        auto response = make_request("/calendar/earnings", params);
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object() || !json.contains("earningsCalendar")) return result;
        
        for (const auto& item : json["earningsCalendar"].array()) {
            FinnhubEarnings earnings;
            earnings.symbol = item.get_string("symbol", "");
            earnings.date = item.get_string("date", "");
            earnings.eps_actual = item.get_double("epsActual", 0);
            earnings.eps_estimate = item.get_double("epsEstimate", 0);
            earnings.revenue_actual = item.get_double("revenueActual", 0);
            earnings.revenue_estimate = item.get_double("revenueEstimate", 0);
            earnings.hour = item.get_string("hour", "");
            earnings.quarter = item.get_int("quarter", 0);
            earnings.year = item.get_int("year", 0);
            
            result.push_back(earnings);
        }
        
        return result;
    }
    
    // ========================================================================
    // IPO Calendar
    // ========================================================================
    
    /**
     * @brief Get IPO calendar
     */
    std::vector<FinnhubIPO> get_ipo_calendar(
        const std::string& from = "",
        const std::string& to = "") {
        
        std::vector<FinnhubIPO> result;
        
        std::map<std::string, std::string> params;
        if (!from.empty()) params["from"] = from;
        if (!to.empty()) params["to"] = to;
        
        auto response = make_request("/calendar/ipo", params);
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object() || !json.contains("ipoCalendar")) return result;
        
        for (const auto& item : json["ipoCalendar"].array()) {
            FinnhubIPO ipo;
            ipo.symbol = item.get_string("symbol", "");
            ipo.name = item.get_string("name", "");
            ipo.date = item.get_string("date", "");
            ipo.exchange = item.get_string("exchange", "");
            ipo.price = item.get_double("price", 0);
            ipo.shares = item.get_int("numberOfShares", 0);
            ipo.offering_amount = item.get_double("totalSharesValue", 0);
            ipo.status = item.get_string("status", "");
            
            result.push_back(ipo);
        }
        
        return result;
    }
    
    // ========================================================================
    // Quotes
    // ========================================================================
    
    /**
     * @brief Get real-time quote
     */
    std::optional<FinnhubQuote> get_quote(const std::string& symbol) {
        auto response = make_request("/quote", {{"symbol", symbol}});
        if (!response) return std::nullopt;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return std::nullopt;
        
        FinnhubQuote quote;
        quote.symbol = symbol;
        quote.current = json.get_double("c", 0);
        quote.change = json.get_double("d", 0);
        quote.percent_change = json.get_double("dp", 0);
        quote.high = json.get_double("h", 0);
        quote.low = json.get_double("l", 0);
        quote.open = json.get_double("o", 0);
        quote.previous_close = json.get_double("pc", 0);
        quote.timestamp = json.get_int("t", 0);
        
        return quote;
    }
    
    // ========================================================================
    // Company Data
    // ========================================================================
    
    /**
     * @brief Get company profile
     */
    std::optional<FinnhubProfile> get_profile(const std::string& symbol) {
        auto response = make_request("/stock/profile2", {{"symbol", symbol}});
        if (!response) return std::nullopt;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return std::nullopt;
        
        FinnhubProfile profile;
        profile.ticker = json.get_string("ticker", symbol);
        profile.name = json.get_string("name", "");
        profile.country = json.get_string("country", "");
        profile.currency = json.get_string("currency", "");
        profile.exchange = json.get_string("exchange", "");
        profile.ipo = json.get_string("ipo", "");
        profile.market_cap = json.get_double("marketCapitalization", 0);
        profile.shares_outstanding = json.get_double("shareOutstanding", 0);
        profile.logo = json.get_string("logo", "");
        profile.phone = json.get_string("phone", "");
        profile.weburl = json.get_string("weburl", "");
        profile.finnhub_industry = json.get_string("finnhubIndustry", "");
        
        return profile;
    }
    
    /**
     * @brief Get recommendation trends
     */
    std::vector<FinnhubRecommendation> get_recommendations(const std::string& symbol) {
        std::vector<FinnhubRecommendation> result;
        
        auto response = make_request("/stock/recommendation", {{"symbol", symbol}});
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_array()) return result;
        
        for (const auto& item : json.array()) {
            FinnhubRecommendation rec;
            rec.period = item.get_string("period", "");
            rec.strong_buy = item.get_int("strongBuy", 0);
            rec.buy = item.get_int("buy", 0);
            rec.hold = item.get_int("hold", 0);
            rec.sell = item.get_int("sell", 0);
            rec.strong_sell = item.get_int("strongSell", 0);
            
            result.push_back(rec);
        }
        
        return result;
    }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    const FinnhubConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }

private:
    FinnhubConfig config_;
    core::HttpClient http_;
    std::string last_error_;
    
    std::optional<std::string> make_request(
        const std::string& endpoint,
        const std::map<std::string, std::string>& params = {}) {
        
        std::ostringstream url;
        url << endpoint << "?token=" << config_.api_key;
        
        for (const auto& [key, value] : params) {
            url << "&" << key << "=" << core::HttpClient::url_encode(value);
        }
        
        auto response = http_.get(url.str());
        
        if (!response.success) {
            last_error_ = response.error;
            return std::nullopt;
        }
        
        if (response.status_code != 200) {
            last_error_ = "HTTP " + std::to_string(response.status_code);
            return std::nullopt;
        }
        
        return response.body;
    }
};

/**
 * @brief Create Finnhub client from environment
 */
inline std::unique_ptr<FinnhubClient> create_finnhub_client_from_env() {
    FinnhubConfig config;
    
    if (const char* key = std::getenv("FINNHUB_API_KEY")) {
        config.api_key = key;
    }
    
    if (!config.is_valid()) {
        return nullptr;
    }
    
    return std::make_unique<FinnhubClient>(config);
}

} // namespace genie::market

#endif // GENIE_MARKET_FINNHUB_CLIENT_HPP
