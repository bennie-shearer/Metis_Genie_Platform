/**
 * @file yahoo_finance.hpp
 * @brief Yahoo Finance data scraper for historical prices
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides historical price data from Yahoo Finance:
 * - Daily, weekly, monthly OHLCV data
 * - Dividend and split history
 * - No API key required (uses public endpoints)
 * - Unlimited requests (reasonable rate limiting recommended)
 * 
 * Note: Yahoo Finance does not have an official API.
 * This uses public download endpoints.
 */
#pragma once
#ifndef GENIE_MARKET_YAHOO_FINANCE_HPP
#define GENIE_MARKET_YAHOO_FINANCE_HPP

#include "../core/http_client.hpp"
#include "../core/logging.hpp"
#include "alpha_vantage.hpp"  // For PriceBar struct
#include "market_data.hpp"    // For Dividend, Split structs
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

/**
 * @brief Yahoo Finance event types
 */
enum class YahooEventType {
    History,    // Price history
    Div,        // Dividends
    Split       // Stock splits
};

/**
 * @brief Yahoo Finance interval
 */
enum class YahooInterval {
    Daily,      // 1d
    Weekly,     // 1wk
    Monthly     // 1mo
};

inline std::string yahoo_interval_to_string(YahooInterval interval) {
    switch (interval) {
        case YahooInterval::Daily: return "1d";
        case YahooInterval::Weekly: return "1wk";
        case YahooInterval::Monthly: return "1mo";
    }
    return "1d";
}

// Dividend and Split structs are now in market_data.hpp

/**
 * @brief Yahoo Finance quote summary
 */
struct YahooQuote {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string currency;
    std::string market_state;  // PRE, REGULAR, POST, CLOSED
    
    double price{0};
    double open{0};
    double high{0};
    double low{0};
    double previous_close{0};
    int64_t volume{0};
    int64_t avg_volume{0};
    
    double bid{0};
    double ask{0};
    int64_t bid_size{0};
    int64_t ask_size{0};
    
    double day_high{0};
    double day_low{0};
    double week_52_high{0};
    double week_52_low{0};
    
    double market_cap{0};
    double pe_ratio{0};
    double eps{0};
    double dividend_yield{0};
    
    std::string earnings_date;
    int64_t shares_outstanding{0};
    
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Yahoo Finance client
 */
class YahooFinanceClient {
public:
    static constexpr const char* DOWNLOAD_URL = "https://query1.finance.yahoo.com/v7/finance/download";
    static constexpr const char* QUOTE_URL = "https://query1.finance.yahoo.com/v7/finance/quote";
    static constexpr const char* CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart";
    
    YahooFinanceClient() {
        // Reasonable rate limiting for Yahoo Finance
        rate_limiter_ = std::make_shared<core::RateLimiter>(10, 1);  // 10 req/sec
        client_.set_rate_limiter(rate_limiter_);
        client_.set_default_header("User-Agent", "Mozilla/5.0");
    }
    
    // === Historical Price Data ===
    
    /**
     * @brief Get historical daily prices
     * @param symbol Stock symbol
     * @param start_date Start date (YYYY-MM-DD)
     * @param end_date End date (YYYY-MM-DD), empty for today
     */
    std::vector<PriceBar> get_historical_prices(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date = "",
        YahooInterval interval = YahooInterval::Daily) {
        
        // Convert dates to Unix timestamps
        time_t period1 = date_to_timestamp(start_date);
        time_t period2 = end_date.empty() ? 
            std::time(nullptr) : date_to_timestamp(end_date);
        
        std::string url = std::string(DOWNLOAD_URL) + "/" + symbol;
        
        auto response = client_.get(url, {
            {"period1", std::to_string(period1)},
            {"period2", std::to_string(period2)},
            {"interval", yahoo_interval_to_string(interval)},
            {"events", "history"},
            {"includeAdjustedClose", "true"}
        });
        
        if (!response.ok()) {
            last_error_ = "Failed to fetch data: " + response.error;
            return {};
        }
        
        return parse_csv_prices(response.body);
    }
    
    /**
     * @brief Get last N days of prices
     */
    std::vector<PriceBar> get_recent_prices(const std::string& symbol, int days = 100) {
        auto now = std::chrono::system_clock::now();
        auto start = now - std::chrono::hours(24 * days);
        
        time_t period1 = std::chrono::system_clock::to_time_t(start);
        time_t period2 = std::chrono::system_clock::to_time_t(now);
        
        std::string url = std::string(DOWNLOAD_URL) + "/" + symbol;
        
        auto response = client_.get(url, {
            {"period1", std::to_string(period1)},
            {"period2", std::to_string(period2)},
            {"interval", "1d"},
            {"events", "history"},
            {"includeAdjustedClose", "true"}
        });
        
        if (!response.ok()) {
            last_error_ = "Failed to fetch data: " + response.error;
            return {};
        }
        
        return parse_csv_prices(response.body);
    }
    
    /**
     * @brief Get all available historical data (max ~20 years)
     */
    std::vector<PriceBar> get_full_history(const std::string& symbol,
                                           YahooInterval interval = YahooInterval::Daily) {
        // Start from 1970 to get all available data
        return get_historical_prices(symbol, "1970-01-01", "", interval);
    }
    
    // === Dividend Data ===
    
    /**
     * @brief Get dividend history
     */
    std::vector<Dividend> get_dividends(const std::string& symbol,
                                        const std::string& start_date = "2000-01-01") {
        time_t period1 = date_to_timestamp(start_date);
        time_t period2 = std::time(nullptr);
        
        std::string url = std::string(DOWNLOAD_URL) + "/" + symbol;
        
        auto response = client_.get(url, {
            {"period1", std::to_string(period1)},
            {"period2", std::to_string(period2)},
            {"interval", "1d"},
            {"events", "div"}
        });
        
        if (!response.ok()) {
            last_error_ = "Failed to fetch dividends: " + response.error;
            return {};
        }
        
        return parse_csv_dividends(response.body, symbol);
    }
    
    // === Split Data ===
    
    /**
     * @brief Get stock split history
     */
    std::vector<Split> get_splits(const std::string& symbol,
                                  const std::string& start_date = "2000-01-01") {
        time_t period1 = date_to_timestamp(start_date);
        time_t period2 = std::time(nullptr);
        
        std::string url = std::string(DOWNLOAD_URL) + "/" + symbol;
        
        auto response = client_.get(url, {
            {"period1", std::to_string(period1)},
            {"period2", std::to_string(period2)},
            {"interval", "1d"},
            {"events", "split"}
        });
        
        if (!response.ok()) {
            last_error_ = "Failed to fetch splits: " + response.error;
            return {};
        }
        
        return parse_csv_splits(response.body, symbol);
    }
    
    // === Quote Data ===
    
    /**
     * @brief Get current quote using chart endpoint
     */
    std::optional<YahooQuote> get_quote(const std::string& symbol) {
        std::string url = std::string(CHART_URL) + "/" + symbol;
        
        auto response = client_.get(url, {
            {"interval", "1d"},
            {"range", "1d"}
        });
        
        if (!response.ok()) {
            last_error_ = "Failed to fetch quote: " + response.error;
            return std::nullopt;
        }
        
        return parse_chart_quote(response.body, symbol);
    }
    
    /**
     * @brief Get quotes for multiple symbols
     */
    std::map<std::string, YahooQuote> get_quotes(const std::vector<std::string>& symbols) {
        std::map<std::string, YahooQuote> result;
        
        // Yahoo allows batch quotes with comma-separated symbols
        std::string symbol_list;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) symbol_list += ",";
            symbol_list += symbols[i];
        }
        
        auto response = client_.get(QUOTE_URL, {
            {"symbols", symbol_list}
        });
        
        if (!response.ok()) {
            // Fall back to individual quotes
            for (const auto& sym : symbols) {
                auto quote = get_quote(sym);
                if (quote) {
                    result[sym] = *quote;
                }
            }
            return result;
        }
        
        // Parse batch response
        auto json = core::JsonParser::parse(response.body);
        auto quote_response = json["quoteResponse"];
        if (quote_response.has("result")) {
            auto results = quote_response["result"];
            if (results.is_array()) {
                for (size_t i = 0; i < results.size(); ++i) {
                    auto quote = parse_quote_json(results[i]);
                    if (quote) {
                        result[quote->symbol] = *quote;
                    }
                }
            }
        }
        
        return result;
    }
    
    // === Utility Functions ===
    
    const std::string& last_error() const { return last_error_; }
    
    core::HttpClient& http_client() { return client_; }
    
    /**
     * @brief Validate symbol exists
     */
    bool symbol_exists(const std::string& symbol) {
        auto prices = get_recent_prices(symbol, 5);
        return !prices.empty();
    }

private:
    core::HttpClient client_;
    std::shared_ptr<core::RateLimiter> rate_limiter_;
    std::string last_error_;
    
    // === Parsing Helpers ===
    
    std::vector<PriceBar> parse_csv_prices(const std::string& csv) {
        std::vector<PriceBar> result;
        std::istringstream stream(csv);
        std::string line;
        
        // Skip header
        std::getline(stream, line);
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            auto fields = split_csv_line(line);
            if (fields.size() < 7) continue;
            
            try {
                PriceBar bar;
                bar.date = fields[0];
                bar.open = safe_stod(fields[1]);
                bar.high = safe_stod(fields[2]);
                bar.low = safe_stod(fields[3]);
                bar.close = safe_stod(fields[4]);
                bar.adjusted_close = safe_stod(fields[5]);
                bar.volume = safe_stoll(fields[6]);
                
                // Calculate split coefficient if we have both close and adj close
                if (bar.close > 0 && bar.adjusted_close > 0) {
                    bar.split_coefficient = bar.close / bar.adjusted_close;
                }
                
                if (bar.is_valid()) {
                    result.push_back(bar);
                }
            } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Price bar parse error: " + std::string(e.what()));
                continue;
            } catch (...) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Unknown price bar parse error");
                continue;
            }
        }
        
        // Sort newest first
        std::sort(result.begin(), result.end(),
            [](const PriceBar& a, const PriceBar& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<Dividend> parse_csv_dividends(const std::string& csv, const std::string& symbol = "") {
        std::vector<Dividend> result;
        std::istringstream stream(csv);
        std::string line;
        
        // Skip header
        std::getline(stream, line);
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            auto fields = split_csv_line(line);
            if (fields.size() < 2) continue;
            
            try {
                Dividend div;
                div.symbol = symbol;
                div.date = fields[0];
                div.amount = safe_stod(fields[1]);
                div.ex_date = fields[0];  // Yahoo uses ex-date as the date
                
                if (div.amount > 0) {
                    result.push_back(div);
                }
            } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Dividend parse error: " + std::string(e.what()));
                continue;
            } catch (...) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Unknown dividend parse error");
                continue;
            }
        }
        
        // Sort newest first
        std::sort(result.begin(), result.end(),
            [](const Dividend& a, const Dividend& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::vector<Split> parse_csv_splits(const std::string& csv, const std::string& symbol = "") {
        std::vector<Split> result;
        std::istringstream stream(csv);
        std::string line;
        
        // Skip header
        std::getline(stream, line);
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            auto fields = split_csv_line(line);
            if (fields.size() < 2) continue;
            
            try {
                Split split;
                split.symbol = symbol;
                split.date = fields[0];
                split.ratio = fields[1];
                
                // Parse ratio like "4:1" to get factor
                auto colon_pos = split.ratio.find(':');
                if (colon_pos != std::string::npos) {
                    double num = std::stod(split.ratio.substr(0, colon_pos));
                    double den = std::stod(split.ratio.substr(colon_pos + 1));
                    if (den > 0) {
                        split.factor = num / den;
                    }
                }
                
                result.push_back(split);
            } catch (const std::exception& e) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Split parse error: " + std::string(e.what()));
                continue;
            } catch (...) {
                Logger::instance().log(LogLevel::DEBUG, "YahooFinance", "Unknown split parse error");
                continue;
            }
        }
        
        // Sort newest first
        std::sort(result.begin(), result.end(),
            [](const Split& a, const Split& b) { return a.date > b.date; });
        
        return result;
    }
    
    std::optional<YahooQuote> parse_chart_quote(const std::string& json_str,
                                                const std::string& symbol) {
        auto json = core::JsonParser::parse(json_str);
        auto chart = json["chart"];
        
        if (!chart.has("result")) {
            last_error_ = "No chart data";
            return std::nullopt;
        }
        
        auto results = chart["result"];
        if (!results.is_array() || results.size() == 0) {
            return std::nullopt;
        }
        
        auto result = results[0];
        auto meta = result["meta"];
        
        YahooQuote quote;
        quote.symbol = symbol;
        quote.exchange = meta["exchangeName"].as_string();
        quote.currency = meta["currency"].as_string();
        quote.market_state = meta["marketState"].as_string();
        quote.price = meta["regularMarketPrice"].as_number();
        quote.previous_close = meta["chartPreviousClose"].as_number();
        quote.week_52_high = meta["fiftyTwoWeekHigh"].as_number();
        quote.week_52_low = meta["fiftyTwoWeekLow"].as_number();
        
        // Get OHLCV from indicators
        auto indicators = result["indicators"];
        if (indicators.has("quote") && indicators["quote"].is_array()) {
            auto quote_data = indicators["quote"][0];
            
            if (quote_data.has("open") && quote_data["open"].is_array() && 
                quote_data["open"].size() > 0) {
                auto opens = quote_data["open"];
                auto highs = quote_data["high"];
                auto lows = quote_data["low"];
                auto closes = quote_data["close"];
                auto volumes = quote_data["volume"];
                
                size_t last = opens.size() - 1;
                quote.open = opens[last].as_number();
                quote.high = highs[last].as_number();
                quote.low = lows[last].as_number();
                quote.volume = static_cast<int64_t>(volumes[last].as_number());
            }
        }
        
        quote.timestamp = std::chrono::system_clock::now();
        
        return quote;
    }
    
    std::optional<YahooQuote> parse_quote_json(const core::JsonValue& json) {
        if (!json.has("symbol")) return std::nullopt;
        
        YahooQuote quote;
        quote.symbol = json["symbol"].as_string();
        quote.name = json["shortName"].as_string();
        quote.exchange = json["exchange"].as_string();
        quote.currency = json["currency"].as_string();
        quote.market_state = json["marketState"].as_string();
        
        quote.price = json["regularMarketPrice"].as_number();
        quote.open = json["regularMarketOpen"].as_number();
        quote.high = json["regularMarketDayHigh"].as_number();
        quote.low = json["regularMarketDayLow"].as_number();
        quote.previous_close = json["regularMarketPreviousClose"].as_number();
        quote.volume = static_cast<int64_t>(json["regularMarketVolume"].as_number());
        quote.avg_volume = static_cast<int64_t>(json["averageDailyVolume3Month"].as_number());
        
        quote.bid = json["bid"].as_number();
        quote.ask = json["ask"].as_number();
        quote.bid_size = json["bidSize"].as_int();
        quote.ask_size = json["askSize"].as_int();
        
        quote.week_52_high = json["fiftyTwoWeekHigh"].as_number();
        quote.week_52_low = json["fiftyTwoWeekLow"].as_number();
        
        quote.market_cap = json["marketCap"].as_number();
        quote.pe_ratio = json["trailingPE"].as_number();
        quote.eps = json["epsTrailingTwelveMonths"].as_number();
        
        quote.shares_outstanding = static_cast<int64_t>(json["sharesOutstanding"].as_number());
        
        quote.timestamp = std::chrono::system_clock::now();
        
        return quote;
    }
    
    // === Utility Helpers ===
    
    std::vector<std::string> split_csv_line(const std::string& line) {
        std::vector<std::string> fields;
        std::string field;
        bool in_quotes = false;
        
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(field);
        
        return fields;
    }
    
    time_t date_to_timestamp(const std::string& date) {
        std::tm tm = {};
        std::istringstream ss(date);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        return std::mktime(&tm);
    }
    
    double safe_stod(const std::string& s) {
        if (s.empty() || s == "null" || s == "N/A") return 0;
        try { return std::stod(s); }
        catch (...) { return 0; }
    }
    
    int64_t safe_stoll(const std::string& s) {
        if (s.empty() || s == "null" || s == "N/A") return 0;
        try { return std::stoll(s); }
        catch (...) { return 0; }
    }
};

} // namespace genie::market

#endif // GENIE_MARKET_YAHOO_FINANCE_HPP
