/**
 * @file tradier_client.hpp
 * @brief Tradier API client for market data and options
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides Tradier market data integration:
 *   - Real-time and delayed quotes
 *   - Options chains with Greeks
 *   - Historical price data (daily, weekly, monthly)
 *   - Options expirations
 *   - Market clock and calendar
 *   - Symbol lookup / search
 *   - Time & sales (tick data)
 *
 * Order management is handled by TradierBroker in broker_abstraction.hpp.
 * This client focuses on market data endpoints.
 *
 * API Documentation: https://documentation.tradier.com/
 *
 * Rate Limits:
 *   - Production: 120 requests/minute
 *   - Sandbox: 60 requests/minute
 */
#pragma once
#ifndef GENIE_TRADING_TRADIER_CLIENT_HPP
#define GENIE_TRADING_TRADIER_CLIENT_HPP

#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <memory>

namespace genie::trading {

// ============================================================================
// Data Types
// ============================================================================

/**
 * @brief Tradier quote data
 */
struct TradierQuote {
    std::string symbol;
    std::string description;
    std::string exchange;
    std::string type;  // stock, option, etf, index
    double last{0};
    double change{0};
    double change_pct{0};
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double prevclose{0};
    int64_t volume{0};
    int64_t average_volume{0};
    double bid{0};
    double ask{0};
    int bid_size{0};
    int ask_size{0};
    double week_52_high{0};
    double week_52_low{0};
    std::string last_trade_time;
};

/**
 * @brief Options contract
 */
struct TradierOption {
    std::string symbol;          // OCC symbol
    std::string underlying;
    std::string description;
    std::string option_type;     // "call" or "put"
    std::string expiration_date;
    double strike{0};
    double last{0};
    double bid{0};
    double ask{0};
    double change{0};
    int64_t volume{0};
    int64_t open_interest{0};
    // Greeks
    double delta{0};
    double gamma{0};
    double theta{0};
    double vega{0};
    double rho{0};
    double implied_volatility{0};
    double mid_iv{0};
};

/**
 * @brief Historical bar
 */
struct TradierBar {
    std::string date;
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    int64_t volume{0};
};

/**
 * @brief Tradier-specific market clock status
 *
 * Distinct from genie::trading::MarketClock (broker_interface.hpp) which
 * is the generic broker abstraction type.  This struct matches Tradier's
 * /v1/markets/clock JSON response verbatim.
 */
struct TradierClock {
    std::string date;
    std::string description;
    std::string state;       // "premarket", "open", "postmarket", "closed"
    std::string next_state;
    std::string next_change;
    int timestamp{0};
};

/**
 * @brief Trading calendar day
 */
struct CalendarDay {
    std::string date;
    std::string status;  // "open", "closed"
    std::string description;
    struct Hours {
        std::string open;
        std::string close;
    } premarket, market, postmarket;
};

// ============================================================================
// Tradier Client
// ============================================================================

/**
 * @brief Tradier API configuration
 */
struct TradierConfig {
    std::string access_token;
    bool sandbox{true};
    int requests_per_minute{120};

    std::string base_url() const {
        return sandbox ? "https://sandbox.tradier.com"
                       : "https://api.tradier.com";
    }

    bool is_valid() const {
        return !access_token.empty();
    }
};

/**
 * @brief Tradier market data client
 */
class TradierClient {
public:
    explicit TradierClient(const TradierConfig& config)
        : config_(config)
        , rate_limiter_(std::make_shared<core::RateLimiter>(
            config.requests_per_minute, 60)) {

        http_.set_base_url(config_.base_url());
        http_.set_default_header("Authorization", "Bearer " + config_.access_token);
        http_.set_default_header("Accept", "application/json");
        http_.set_rate_limiter(rate_limiter_);
    }

    // === Quotes ===

    /**
     * @brief Get real-time quote for a single symbol
     */
    std::optional<TradierQuote> get_quote(const std::string& symbol) {
        auto quotes = get_quotes({symbol});
        if (!quotes.empty()) return quotes[0];
        return std::nullopt;
    }

    /**
     * @brief Get quotes for multiple symbols
     */
    std::vector<TradierQuote> get_quotes(const std::vector<std::string>& symbols) {
        std::string sym_list;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) sym_list += ",";
            sym_list += symbols[i];
        }

        auto response = http_.get("/v1/markets/quotes", {{"symbols", sym_list}});
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TradierQuote> result;

        if (!json.has("quotes") || !json["quotes"].has("quote")) return result;

        auto quote_data = json["quotes"]["quote"];
        std::vector<core::JsonValue> items;
        if (quote_data.is_array()) {
            items = quote_data.array();
        } else if (quote_data.is_object()) {
            items.push_back(quote_data);
        }

        for (const auto& q : items) {
            TradierQuote quote;
            quote.symbol = q.get_string("symbol");
            quote.description = q.get_string("description");
            quote.exchange = q.get_string("exch");
            quote.type = q.get_string("type");
            quote.last = q.get_double("last");
            quote.change = q.get_double("change");
            quote.change_pct = q.get_double("change_percentage");
            quote.open = q.get_double("open");
            quote.high = q.get_double("high");
            quote.low = q.get_double("low");
            quote.close = q.get_double("close");
            quote.prevclose = q.get_double("prevclose");
            quote.volume = static_cast<int64_t>(q.get_double("volume"));
            quote.average_volume = static_cast<int64_t>(q.get_double("average_volume"));
            quote.bid = q.get_double("bid");
            quote.ask = q.get_double("ask");
            quote.bid_size = q.get_int("bidsize");
            quote.ask_size = q.get_int("asksize");
            quote.week_52_high = q.get_double("week_52_high");
            quote.week_52_low = q.get_double("week_52_low");
            quote.last_trade_time = q.get_string("trade_date");
            result.push_back(quote);
        }

        return result;
    }

    // === Options ===

    /**
     * @brief Get options chain for a symbol
     */
    std::vector<TradierOption> get_option_chain(
            const std::string& symbol,
            const std::string& expiration,
            const std::string& option_type = "") {

        std::map<std::string, std::string> params = {
            {"symbol", symbol},
            {"expiration", expiration},
            {"greeks", "true"}
        };
        if (!option_type.empty()) params["type"] = option_type;

        auto response = http_.get("/v1/markets/options/chains", params);
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TradierOption> result;

        if (!json.has("options") || !json["options"].has("option")) return result;

        auto opt_data = json["options"]["option"];
        std::vector<core::JsonValue> items;
        if (opt_data.is_array()) items = opt_data.array();
        else if (opt_data.is_object()) items.push_back(opt_data);

        for (const auto& o : items) {
            TradierOption opt;
            opt.symbol = o.get_string("symbol");
            opt.underlying = o.get_string("underlying");
            opt.description = o.get_string("description");
            opt.option_type = o.get_string("option_type");
            opt.expiration_date = o.get_string("expiration_date");
            opt.strike = o.get_double("strike");
            opt.last = o.get_double("last");
            opt.bid = o.get_double("bid");
            opt.ask = o.get_double("ask");
            opt.change = o.get_double("change");
            opt.volume = static_cast<int64_t>(o.get_double("volume"));
            opt.open_interest = static_cast<int64_t>(o.get_double("open_interest"));

            if (o.has("greeks")) {
                const auto& g = o["greeks"];
                opt.delta = g.get_double("delta");
                opt.gamma = g.get_double("gamma");
                opt.theta = g.get_double("theta");
                opt.vega = g.get_double("vega");
                opt.rho = g.get_double("rho");
                opt.implied_volatility = g.get_double("mid_iv");
                opt.mid_iv = g.get_double("mid_iv");
            }

            result.push_back(opt);
        }

        return result;
    }

    /**
     * @brief Get available expiration dates for a symbol
     */
    std::vector<std::string> get_option_expirations(const std::string& symbol,
                                                     bool include_all = false) {
        std::map<std::string, std::string> params = {{"symbol", symbol}};
        if (include_all) params["includeAllRoots"] = "true";

        auto response = http_.get("/v1/markets/options/expirations", params);
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<std::string> result;

        if (json.has("expirations") && json["expirations"].has("date")) {
            auto dates = json["expirations"]["date"];
            if (dates.is_array()) {
                for (size_t i = 0; i < dates.size(); ++i) {
                    result.push_back(dates[i].as_string());
                }
            } else if (dates.is_string()) {
                result.push_back(dates.as_string());
            }
        }

        return result;
    }

    /**
     * @brief Get available strike prices for a symbol/expiration
     */
    std::vector<double> get_option_strikes(const std::string& symbol,
                                            const std::string& expiration) {
        auto response = http_.get("/v1/markets/options/strikes", {
            {"symbol", symbol},
            {"expiration", expiration}
        });
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<double> result;

        if (json.has("strikes") && json["strikes"].has("strike")) {
            auto strikes = json["strikes"]["strike"];
            if (strikes.is_array()) {
                for (size_t i = 0; i < strikes.size(); ++i) {
                    result.push_back(strikes[i].as_number());
                }
            } else if (strikes.is_number()) {
                result.push_back(strikes.as_number());
            }
        }

        return result;
    }

    // === Historical Data ===

    /**
     * @brief Get historical price data
     * @param interval "daily", "weekly", "monthly"
     */
    std::vector<TradierBar> get_history(const std::string& symbol,
                                         const std::string& interval = "daily",
                                         const std::string& start = "",
                                         const std::string& end = "") {
        std::map<std::string, std::string> params = {
            {"symbol", symbol},
            {"interval", interval}
        };
        if (!start.empty()) params["start"] = start;
        if (!end.empty()) params["end"] = end;

        auto response = http_.get("/v1/markets/history", params);
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TradierBar> result;

        if (!json.has("history") || !json["history"].has("day")) return result;

        auto day_data = json["history"]["day"];
        std::vector<core::JsonValue> items;
        if (day_data.is_array()) items = day_data.array();
        else if (day_data.is_object()) items.push_back(day_data);

        for (const auto& d : items) {
            TradierBar bar;
            bar.date = d.get_string("date");
            bar.open = d.get_double("open");
            bar.high = d.get_double("high");
            bar.low = d.get_double("low");
            bar.close = d.get_double("close");
            bar.volume = static_cast<int64_t>(d.get_double("volume"));
            result.push_back(bar);
        }

        return result;
    }

    // === Market Info ===

    /**
     * @brief Get market clock (pre-market, open, post-market, closed)
     */
    std::optional<TradierClock> get_clock() {
        auto response = http_.get("/v1/markets/clock");
        if (!response.ok()) return std::nullopt;

        auto json = core::JsonParser::parse(response.body);
        if (!json.has("clock")) return std::nullopt;

        const auto& c = json["clock"];
        TradierClock clock;
        clock.date = c.get_string("date");
        clock.description = c.get_string("description");
        clock.state = c.get_string("state");
        clock.next_state = c.get_string("next_state");
        clock.next_change = c.get_string("next_change");
        clock.timestamp = c.get_int("timestamp");
        return clock;
    }

    /**
     * @brief Get market calendar for a month
     */
    std::vector<CalendarDay> get_calendar(int month = 0, int year = 0) {
        std::map<std::string, std::string> params;
        if (month > 0) params["month"] = std::to_string(month);
        if (year > 0) params["year"] = std::to_string(year);

        auto response = http_.get("/v1/markets/calendar", params);
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<CalendarDay> result;

        if (!json.has("calendar") || !json["calendar"].has("days") ||
            !json["calendar"]["days"].has("day")) return result;

        auto day_data = json["calendar"]["days"]["day"];
        std::vector<core::JsonValue> items;
        if (day_data.is_array()) items = day_data.array();
        else if (day_data.is_object()) items.push_back(day_data);

        for (const auto& d : items) {
            CalendarDay day;
            day.date = d.get_string("date");
            day.status = d.get_string("status");
            day.description = d.get_string("description");

            if (d.has("premarket")) {
                day.premarket.open = d["premarket"].get_string("start");
                day.premarket.close = d["premarket"].get_string("end");
            }
            if (d.has("open")) {
                day.market.open = d["open"].get_string("start");
                day.market.close = d["open"].get_string("end");
            }
            if (d.has("postmarket")) {
                day.postmarket.open = d["postmarket"].get_string("start");
                day.postmarket.close = d["postmarket"].get_string("end");
            }

            result.push_back(day);
        }

        return result;
    }

    /**
     * @brief Search for symbols
     */
    std::vector<TradierQuote> search_symbols(const std::string& query, bool indexes = true) {
        auto response = http_.get("/v1/markets/search", {
            {"q", query},
            {"indexes", indexes ? "true" : "false"}
        });
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TradierQuote> result;

        if (!json.has("securities") || !json["securities"].has("security")) return result;

        auto sec_data = json["securities"]["security"];
        std::vector<core::JsonValue> items;
        if (sec_data.is_array()) items = sec_data.array();
        else if (sec_data.is_object()) items.push_back(sec_data);

        for (const auto& s : items) {
            TradierQuote q;
            q.symbol = s.get_string("symbol");
            q.description = s.get_string("description");
            q.exchange = s.get_string("exchange");
            q.type = s.get_string("type");
            result.push_back(q);
        }

        return result;
    }

    /**
     * @brief Symbol lookup by name
     */
    std::vector<TradierQuote> lookup_symbol(const std::string& query) {
        auto response = http_.get("/v1/markets/lookup", {{"q", query}});
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TradierQuote> result;

        if (!json.has("securities") || !json["securities"].has("security")) return result;

        auto sec_data = json["securities"]["security"];
        std::vector<core::JsonValue> items;
        if (sec_data.is_array()) items = sec_data.array();
        else if (sec_data.is_object()) items.push_back(sec_data);

        for (const auto& s : items) {
            TradierQuote q;
            q.symbol = s.get_string("symbol");
            q.description = s.get_string("description");
            q.exchange = s.get_string("exchange");
            q.type = s.get_string("type");
            result.push_back(q);
        }

        return result;
    }

    // === Time & Sales ===

    /**
     * @brief Get time and sales (tick data) for a symbol
     */
    struct TimeSale {
        std::string time;
        double price{0};
        int64_t size{0};
        std::string exchange;
    };

    std::vector<TimeSale> get_timesales(const std::string& symbol,
                                         const std::string& interval = "tick",
                                         const std::string& start = "",
                                         const std::string& end = "") {
        std::map<std::string, std::string> params = {
            {"symbol", symbol},
            {"interval", interval}
        };
        if (!start.empty()) params["start"] = start;
        if (!end.empty()) params["end"] = end;

        auto response = http_.get("/v1/markets/timesales", params);
        if (!response.ok()) return {};

        auto json = core::JsonParser::parse(response.body);
        std::vector<TimeSale> result;

        if (!json.has("series") || !json["series"].has("data")) return result;

        auto ts_data = json["series"]["data"];
        std::vector<core::JsonValue> items;
        if (ts_data.is_array()) items = ts_data.array();
        else if (ts_data.is_object()) items.push_back(ts_data);

        for (const auto& t : items) {
            TimeSale ts;
            ts.time = t.get_string("time");
            ts.price = t.get_double("price");
            ts.size = static_cast<int64_t>(t.get_double("volume", 0));
            result.push_back(ts);
        }

        return result;
    }

    // === Accessors ===

    const TradierConfig& config() const { return config_; }
    bool is_configured() const { return config_.is_valid(); }

private:
    TradierConfig config_;
    core::HttpClient http_;
    std::shared_ptr<core::RateLimiter> rate_limiter_;
};

} // namespace genie::trading

#endif // GENIE_TRADING_TRADIER_CLIENT_HPP
