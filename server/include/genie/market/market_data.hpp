/**
 * @file market_data.hpp
 * @brief Market data management for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_MARKET_DATA_HPP
#define GENIE_MARKET_DATA_HPP
#include "security.hpp"
#include "../core/random.hpp"
#include "../core/date_utils.hpp"

namespace genie::market {
struct PriceQuote {
    SecurityId security_id;
    TimePoint timestamp;
    Price bid{0}, ask{0}, last{0}, open{0}, high{0}, low{0}, close{0}, prev_close{0};
    Quantity volume{0};
    [[nodiscard]] Price mid() const { return (bid + ask) / 2.0; }
};

/**
 * @brief Unified OHLCV price bar with all required fields
 */
struct PriceBar { 
    TimePoint timestamp; 
    std::string date;              // Date string YYYY-MM-DD
    Price open{0}, high{0}, low{0}, close{0}; 
    Price adj_close{0};            // Adjusted close
    Price adjusted_close{0};       // Alias for adj_close
    Quantity volume{0}; 
    double dividend_amount{0};     // Dividend amount
    double split_coefficient{1.0}; // Split coefficient
    
    [[nodiscard]] bool is_valid() const {
        return (high >= low) && (open > 0) && (close > 0);
    }
    
    void sync_adjusted() {
        if (adjusted_close != 0 && adj_close == 0) adj_close = adjusted_close;
        else if (adj_close != 0 && adjusted_close == 0) adjusted_close = adj_close;
        else if (adj_close == 0 && adjusted_close == 0) adj_close = adjusted_close = close;
    }
};
using PriceHistory = std::vector<PriceBar>;

/**
 * @brief Real-time quote data
 */
struct Quote {
    std::string symbol;
    double price{0};
    double bid{0};
    double ask{0};
    int64_t bid_size{0};
    int64_t ask_size{0};
    double open{0};
    double high{0};
    double low{0};
    double previous_close{0};
    int64_t volume{0};
    std::string latest_trading_day;
    double change{0};
    double change_percent{0};
    std::chrono::system_clock::time_point timestamp;
    
    bool is_valid() const {
        return !symbol.empty() && price > 0;
    }
    
    double mid() const {
        if (bid > 0 && ask > 0) return (bid + ask) / 2.0;
        return price;
    }
    
    double spread() const {
        if (bid > 0 && ask > 0) return ask - bid;
        return 0;
    }
};

/**
 * @brief Dividend record
 */
struct Dividend {
    std::string symbol;
    std::string date;
    double amount{0};
    std::string ex_date;
    std::string payment_date;
    std::string record_date;
    std::string currency{"USD"};
    std::string description;
};

/**
 * @brief Stock split record
 */
struct Split {
    std::string symbol;
    std::string date;
    std::string ratio;  // e.g., "4:1"
    double factor{1.0}; // e.g., 4.0 for 4:1 split
    double from_factor{1.0};  // e.g., 1.0 (split from)
    double to_factor{1.0};    // e.g., 4.0 (split to)
};

struct ReturnSeries {
    SecurityId security_id; Frequency frequency{Frequency::Daily}; std::vector<double> returns;
    [[nodiscard]] size_t size() const { return returns.size(); }
    [[nodiscard]] double mean() const { return math::mean(returns); }
    [[nodiscard]] double stddev() const { return math::stddev(returns); }
    [[nodiscard]] double annualized_vol() const { return math::annualized_volatility(stddev(), frequency); }
};

class MarketDataStore {
    mutable std::shared_mutex mutex_;
    std::unordered_map<SecurityId, SecurityPtr> securities_;
    std::unordered_map<SecurityId, PriceQuote> quotes_;
    std::unordered_map<SecurityId, PriceHistory> histories_;
    std::unordered_map<SecurityId, ReturnSeries> returns_;
public:
    void add_security(SecurityPtr sec) { std::unique_lock lk(mutex_); securities_[sec->id()] = std::move(sec); }
    [[nodiscard]] SecurityPtr get_security(const SecurityId& id) const { std::shared_lock lk(mutex_); auto it = securities_.find(id); return it != securities_.end() ? it->second : nullptr; }
    [[nodiscard]] size_t security_count() const { std::shared_lock lk(mutex_); return securities_.size(); }
    void update_quote(const PriceQuote& q) { std::unique_lock lk(mutex_); quotes_[q.security_id] = q; }
    [[nodiscard]] std::optional<PriceQuote> get_quote(const SecurityId& id) const { std::shared_lock lk(mutex_); auto it = quotes_.find(id); return it != quotes_.end() ? std::optional{it->second} : std::nullopt; }
    [[nodiscard]] Price get_price(const SecurityId& id) const { auto q = get_quote(id); return q ? q->last : 0.0; }
    void set_history(const SecurityId& id, PriceHistory h) { std::unique_lock lk(mutex_); histories_[id] = std::move(h); calc_returns(id); }
    [[nodiscard]] std::vector<double> get_return_vector(const SecurityId& id) const { std::shared_lock lk(mutex_); auto it = returns_.find(id); return it != returns_.end() ? it->second.returns : std::vector<double>{}; }
    [[nodiscard]] double get_volatility(const SecurityId& id) const { std::shared_lock lk(mutex_); auto it = returns_.find(id); return it != returns_.end() ? it->second.annualized_vol() : 0.0; }
private:
    void calc_returns(const SecurityId& id) {
        auto it = histories_.find(id); if (it == histories_.end() || it->second.size() < 2) return;
        ReturnSeries rs; rs.security_id = id; const auto& h = it->second;
        for (size_t i = 1; i < h.size(); ++i) if (h[i-1].adj_close > 0) rs.returns.push_back((h[i].adj_close - h[i-1].adj_close) / h[i-1].adj_close);
        returns_[id] = std::move(rs);
    }
};

class MarketDataService {
    std::shared_ptr<MarketDataStore> store_;
    TimePoint as_of_;
public:
    MarketDataService() : store_(std::make_shared<MarketDataStore>()), as_of_(date_utils::today()) {}
    [[nodiscard]] std::shared_ptr<MarketDataStore> store() const { return store_; }
    void add_security(SecurityPtr sec) { store_->add_security(std::move(sec)); }
    [[nodiscard]] SecurityPtr get_security(const SecurityId& id) const { return store_->get_security(id); }
    [[nodiscard]] Price get_price(const SecurityId& id) const { return store_->get_price(id); }
    void update_price(const SecurityId& id, Price p) { PriceQuote q; q.security_id = id; q.last = q.close = p; q.bid = p * 0.999; q.ask = p * 1.001; store_->update_quote(q); }
    void generate_synthetic_history(const SecurityId& id, size_t days, double init, double ret, double vol) {
        PriceHistory h; h.reserve(days); RandomGenerator gen;
        double dr = ret / 252.0, dv = vol / std::sqrt(252.0), price = init;
        auto date = as_of_ - std::chrono::days(static_cast<int>(days));
        for (size_t i = 0; i < days; ++i) {
            double r = gen.normal(dr, dv), open = price; price *= (1.0 + r);
            PriceBar bar; bar.timestamp = date; bar.open = open; bar.close = bar.adj_close = price;
            bar.high = std::max(open, price) * 1.005; bar.low = std::min(open, price) * 0.995;
            bar.volume = gen.uniform(100000, 1000000); h.push_back(bar);
            date += std::chrono::days(1); if (date_utils::is_weekend(date)) date += std::chrono::days(2);
        }
        store_->set_history(id, std::move(h)); update_price(id, price);
    }
};
} // namespace genie::market
#endif
