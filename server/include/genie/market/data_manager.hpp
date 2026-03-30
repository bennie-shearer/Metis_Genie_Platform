/**
 * @file data_manager.hpp
 * @brief Unified market data manager for all data sources
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Provides a single interface for market data:
 * - Coordinates multiple data sources (Alpha Vantage, Yahoo Finance)
 * - Automatic source failover
 * - Data storage in SQLite price store
 * - Data validation and quality checks
 * - Caching with staleness detection
 * - Batch operations for efficiency
 */
#pragma once
#ifndef GENIE_MARKET_DATA_MANAGER_HPP
#define GENIE_MARKET_DATA_MANAGER_HPP

#include "alpha_vantage.hpp"
#include "yahoo_finance.hpp"
#include "price_store.hpp"
#include "price_cache.hpp"
#include "price_validator.hpp"
#include "symbol_master.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <mutex>
#include <chrono>
#include <functional>
#include <algorithm>

namespace genie::market {

/**
 * @brief Data source priority
 */
enum class DataSourcePriority {
    AlphaVantageFirst,  // Prefer Alpha Vantage, fallback to Yahoo
    YahooFirst,         // Prefer Yahoo, fallback to Alpha Vantage
    AlphaVantageOnly,   // Only use Alpha Vantage
    YahooOnly           // Only use Yahoo Finance
};

/**
 * @brief Data fetch result
 */
template<typename T>
struct DataResult {
    bool success{false};
    T data;
    std::string source;
    std::string error;
    std::chrono::milliseconds latency{0};
    bool from_cache{false};
};

/**
 * @brief Data manager configuration
 */
struct DataManagerConfig {
    // API keys
    std::string alpha_vantage_key;
    
    // Database
    std::string price_db_path{"prices.db"};
    std::string symbol_db_path{"symbols.db"};
    
    // Source priority
    DataSourcePriority priority{DataSourcePriority::YahooFirst};
    
    // Caching
    bool enable_cache{true};
    int quote_cache_seconds{60};
    int history_cache_seconds{3600};
    
    // Validation
    bool validate_data{true};
    ValidationConfig validation_config;
    
    // Rate limiting
    int alpha_vantage_calls_per_minute{5};
    int yahoo_calls_per_second{10};
};

/**
 * @brief Unified market data manager
 */
class DataManager {
public:
    explicit DataManager(const DataManagerConfig& config = {})
        : config_(config)
        , price_store_(config.price_db_path)
        , symbol_master_(config.symbol_db_path)
        , validator_(config.validation_config) {
        
        // Initialize Alpha Vantage client if key provided
        if (!config.alpha_vantage_key.empty()) {
            alpha_vantage_ = std::make_unique<AlphaVantageClient>(config.alpha_vantage_key);
        }
        
        // Yahoo Finance doesn't need a key
        yahoo_ = std::make_unique<YahooFinanceClient>();
    }
    
    // ========================================================================
    // Real-Time Quotes
    // ========================================================================
    
    /**
     * @brief Get real-time quote for symbol
     */
    DataResult<Quote> get_quote(const std::string& symbol) {
        auto start = std::chrono::steady_clock::now();
        DataResult<Quote> result;
        
        // Check cache first
        if (config_.enable_cache) {
            auto cached = quote_cache_.find(symbol);
            if (cached != quote_cache_.end()) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - cached->second.timestamp).count();
                if (age < config_.quote_cache_seconds) {
                    result.success = true;
                    result.data = cached->second;
                    result.source = "cache";
                    result.from_cache = true;
                    return result;
                }
            }
        }
        
        // Try primary source
        std::optional<Quote> quote;
        std::string source;
        
        if (should_use_alpha_vantage_first()) {
            quote = try_alpha_vantage_quote(symbol);
            source = "alpha_vantage";
            
            if (!quote && config_.priority != DataSourcePriority::AlphaVantageOnly) {
                quote = try_yahoo_quote(symbol);
                source = "yahoo_finance";
            }
        } else {
            quote = try_yahoo_quote(symbol);
            source = "yahoo_finance";
            
            if (!quote && config_.priority != DataSourcePriority::YahooOnly) {
                quote = try_alpha_vantage_quote(symbol);
                source = "alpha_vantage";
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (quote) {
            result.success = true;
            result.data = *quote;
            result.source = source;
            
            // Cache the result
            if (config_.enable_cache) {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                quote_cache_[symbol] = *quote;
            }
        } else {
            result.error = last_error_;
        }
        
        return result;
    }
    
    /**
     * @brief Get quotes for multiple symbols
     */
    std::map<std::string, DataResult<Quote>> get_quotes(const std::vector<std::string>& symbols) {
        std::map<std::string, DataResult<Quote>> results;
        
        for (const auto& symbol : symbols) {
            results[symbol] = get_quote(symbol);
        }
        
        return results;
    }
    
    // ========================================================================
    // Historical Data
    // ========================================================================
    
    /**
     * @brief Get historical daily prices
     */
    DataResult<std::vector<PriceBar>> get_historical_prices(
        const std::string& symbol,
        const std::string& start_date = "",
        const std::string& end_date = "",
        bool use_adjusted = true) {
        
        auto start = std::chrono::steady_clock::now();
        DataResult<std::vector<PriceBar>> result;
        
        // Check database first
        auto stored = price_store_.get_prices(symbol, start_date, end_date);
        if (!stored.empty()) {
            // Convert StoredPriceBar to PriceBar
            std::vector<PriceBar> bars;
            for (const auto& sp : stored) {
                PriceBar bar;
                bar.date = sp.date;
                bar.open = sp.open;
                bar.high = sp.high;
                bar.low = sp.low;
                bar.close = sp.close;
                bar.adjusted_close = sp.adjusted_close;
                bar.volume = sp.volume;
                bar.dividend_amount = sp.dividend;
                bar.split_coefficient = sp.split_factor;
                bars.push_back(bar);
            }
            
            result.success = true;
            result.data = bars;
            result.source = "database";
            result.from_cache = true;
            return result;
        }
        
        // Fetch from API
        std::vector<PriceBar> bars;
        std::string source;
        
        if (should_use_alpha_vantage_first()) {
            bars = try_alpha_vantage_history(symbol, use_adjusted);
            source = "alpha_vantage";
            
            if (bars.empty() && config_.priority != DataSourcePriority::AlphaVantageOnly) {
                bars = try_yahoo_history(symbol, start_date, end_date);
                source = "yahoo_finance";
            }
        } else {
            bars = try_yahoo_history(symbol, start_date, end_date);
            source = "yahoo_finance";
            
            if (bars.empty() && config_.priority != DataSourcePriority::YahooOnly) {
                bars = try_alpha_vantage_history(symbol, use_adjusted);
                source = "alpha_vantage";
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        result.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!bars.empty()) {
            // Validate data
            if (config_.validate_data) {
                auto validation = validator_.validate_series(symbol, bars);
                if (!validation.is_valid) {
                    // Log warnings but still return data
                    last_warning_ = "Data validation warnings for " + symbol;
                }
            }
            
            // Filter by date range if specified
            if (!start_date.empty() || !end_date.empty()) {
                bars.erase(std::remove_if(bars.begin(), bars.end(),
                    [&](const PriceBar& bar) {
                        if (!start_date.empty() && bar.date < start_date) return true;
                        if (!end_date.empty() && bar.date > end_date) return true;
                        return false;
                    }), bars.end());
            }
            
            // Store in database
            DataSource ds = (source == "alpha_vantage") ? 
                DataSource::AlphaVantage : DataSource::YahooFinance;
            price_store_.store_prices(symbol, bars, ds);
            
            result.success = true;
            result.data = bars;
            result.source = source;
        } else {
            result.error = last_error_;
        }
        
        return result;
    }
    
    /**
     * @brief Get recent N days of prices
     */
    DataResult<std::vector<PriceBar>> get_recent_prices(const std::string& symbol, int days = 100) {
        // Calculate start date
        auto now = std::chrono::system_clock::now();
        auto start_time = now - std::chrono::hours(24 * days);
        auto start_t = std::chrono::system_clock::to_time_t(start_time);
        std::tm tm = *std::localtime(&start_t);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        
        return get_historical_prices(symbol, oss.str(), "");
    }
    
    /**
     * @brief Backfill historical data for symbol
     */
    int backfill_history(const std::string& symbol, [[maybe_unused]] int years = 5) {
        auto bars = try_yahoo_history(symbol, "", "");
        if (bars.empty() && alpha_vantage_) {
            bars = alpha_vantage_->get_daily_adjusted(symbol, OutputSize::Full);
        }
        
        if (!bars.empty()) {
            return price_store_.store_prices(symbol, bars, DataSource::YahooFinance);
        }
        
        return 0;
    }
    
    // ========================================================================
    // Dividends and Splits
    // ========================================================================
    
    /**
     * @brief Get dividend history
     */
    DataResult<std::vector<Dividend>> get_dividends(const std::string& symbol,
                                                     const std::string& start_date = "") {
        DataResult<std::vector<Dividend>> result;
        
        // Try Yahoo Finance (free)
        if (yahoo_) {
            auto divs = yahoo_->get_dividends(symbol, start_date);
            if (!divs.empty()) {
                result.success = true;
                result.data = divs;
                result.source = "yahoo_finance";
                
                // Store in database
                for (const auto& div : divs) {
                    price_store_.store_dividend(symbol, div);
                }
                
                return result;
            }
        }
        
        // Check database
        auto stored = price_store_.get_dividends(symbol, start_date, "");
        if (!stored.empty()) {
            result.success = true;
            result.data = stored;
            result.source = "database";
            result.from_cache = true;
            return result;
        }
        
        result.error = "No dividend data found for " + symbol;
        return result;
    }
    
    /**
     * @brief Get stock split history
     */
    DataResult<std::vector<Split>> get_splits(const std::string& symbol,
                                               const std::string& start_date = "") {
        DataResult<std::vector<Split>> result;
        
        // Try Yahoo Finance (free)
        if (yahoo_) {
            auto splits = yahoo_->get_splits(symbol, start_date);
            if (!splits.empty()) {
                result.success = true;
                result.data = splits;
                result.source = "yahoo_finance";
                
                // Store in database
                for (const auto& split : splits) {
                    price_store_.store_split(symbol, split);
                }
                
                return result;
            }
        }
        
        // Check database
        auto stored = price_store_.get_splits(symbol, start_date, "");
        if (!stored.empty()) {
            result.success = true;
            result.data = stored;
            result.source = "database";
            result.from_cache = true;
            return result;
        }
        
        result.error = "No split data found for " + symbol;
        return result;
    }
    
    // ========================================================================
    // Symbol Lookup
    // ========================================================================
    
    /**
     * @brief Search for symbols
     */
    DataResult<std::vector<SymbolMatch>> search_symbols(const std::string& query) {
        DataResult<std::vector<SymbolMatch>> result;
        
        // Try Alpha Vantage symbol search
        if (alpha_vantage_) {
            auto matches = alpha_vantage_->search_symbols(query);
            if (!matches.empty()) {
                result.success = true;
                result.data = matches;
                result.source = "alpha_vantage";
                return result;
            }
        }
        
        // Fallback to local symbol master
        auto local = symbol_master_.search(query);
        if (!local.empty()) {
            // Convert to SymbolMatch format
            for (const auto& sym : local) {
                SymbolMatch match;
                match.symbol = sym.symbol;
                match.name = sym.name;
                match.type = asset_type_to_string(sym.asset_type);
                match.region = "US";
                match.currency = sym.currency;
                result.data.push_back(match);
            }
            result.success = true;
            result.source = "local";
        }
        
        return result;
    }
    
    /**
     * @brief Get company overview/fundamentals
     */
    DataResult<CompanyOverview> get_company_info(const std::string& symbol) {
        DataResult<CompanyOverview> result;
        
        if (alpha_vantage_) {
            auto overview = alpha_vantage_->get_company_overview(symbol);
            if (overview) {
                result.success = true;
                result.data = *overview;
                result.source = "alpha_vantage";
                return result;
            }
        }
        
        result.error = "Company info requires Alpha Vantage API key";
        return result;
    }
    
    // ========================================================================
    // Batch Operations
    // ========================================================================
    
    /**
     * @brief Refresh quotes for watchlist
     */
    int refresh_watchlist(const std::vector<std::string>& symbols) {
        int refreshed = 0;
        
        for (const auto& symbol : symbols) {
            auto result = get_quote(symbol);
            if (result.success) {
                refreshed++;
            }
        }
        
        return refreshed;
    }
    
    /**
     * @brief Backfill history for multiple symbols
     */
    std::map<std::string, int> backfill_multiple(const std::vector<std::string>& symbols) {
        std::map<std::string, int> results;
        
        for (const auto& symbol : symbols) {
            results[symbol] = backfill_history(symbol);
        }
        
        return results;
    }
    
    // ========================================================================
    // Cache Management
    // ========================================================================
    
    /**
     * @brief Clear quote cache
     */
    void clear_quote_cache() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        quote_cache_.clear();
    }
    
    /**
     * @brief Invalidate cached quote
     */
    void invalidate_quote(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        quote_cache_.erase(symbol);
    }
    
    /**
     * @brief Get cached quote age in seconds
     */
    int get_quote_age(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = quote_cache_.find(symbol);
        if (it == quote_cache_.end()) return -1;
        
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - it->second.timestamp).count();
        return static_cast<int>(age);
    }
    
    // ========================================================================
    // Database Access
    // ========================================================================
    
    /**
     * @brief Get price store for direct access
     */
    PriceStore& price_store() { return price_store_; }
    const PriceStore& price_store() const { return price_store_; }
    
    /**
     * @brief Get symbol master for direct access
     */
    SymbolMaster& symbol_master() { return symbol_master_; }
    const SymbolMaster& symbol_master() const { return symbol_master_; }
    
    /**
     * @brief Get validator for direct access
     */
    PriceValidator& validator() { return validator_; }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    const DataManagerConfig& config() const { return config_; }
    
    void set_alpha_vantage_key(const std::string& key) {
        config_.alpha_vantage_key = key;
        if (!key.empty()) {
            alpha_vantage_ = std::make_unique<AlphaVantageClient>(key);
        } else {
            alpha_vantage_.reset();
        }
    }
    
    void set_source_priority(DataSourcePriority priority) {
        config_.priority = priority;
    }
    
    const std::string& last_error() const { return last_error_; }
    const std::string& last_warning() const { return last_warning_; }
    
    /**
     * @brief Get data source status
     */
    struct SourceStatus {
        bool alpha_vantage_available{false};
        int alpha_vantage_remaining_calls{0};
        bool yahoo_available{true};
        int cached_quotes{0};
        int stored_symbols{0};
        int stored_price_bars{0};
    };
    
    SourceStatus get_status() const {
        SourceStatus status;
        
        status.alpha_vantage_available = (alpha_vantage_ != nullptr);
        if (alpha_vantage_) {
            status.alpha_vantage_remaining_calls = alpha_vantage_->remaining_calls();
        }
        
        status.yahoo_available = (yahoo_ != nullptr);
        
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            status.cached_quotes = static_cast<int>(quote_cache_.size());
        }
        
        auto stats = price_store_.get_stats();
        status.stored_symbols = static_cast<int>(stats["total_symbols"]);
        status.stored_price_bars = static_cast<int>(stats["total_price_bars"]);
        
        return status;
    }

private:
    DataManagerConfig config_;
    
    std::unique_ptr<AlphaVantageClient> alpha_vantage_;
    std::unique_ptr<YahooFinanceClient> yahoo_;
    
    PriceStore price_store_;
    SymbolMaster symbol_master_;
    PriceValidator validator_;
    
    std::map<std::string, Quote> quote_cache_;
    mutable std::mutex cache_mutex_;
    
    std::string last_error_;
    std::string last_warning_;
    
    bool should_use_alpha_vantage_first() const {
        return config_.priority == DataSourcePriority::AlphaVantageFirst ||
               config_.priority == DataSourcePriority::AlphaVantageOnly;
    }
    
    std::optional<Quote> try_alpha_vantage_quote(const std::string& symbol) {
        if (!alpha_vantage_) {
            last_error_ = "Alpha Vantage not configured";
            return std::nullopt;
        }
        
        auto quote = alpha_vantage_->get_quote(symbol);
        if (!quote) {
            last_error_ = alpha_vantage_->last_error();
        }
        return quote;
    }
    
    std::optional<Quote> try_yahoo_quote(const std::string& symbol) {
        if (!yahoo_) {
            last_error_ = "Yahoo Finance not available";
            return std::nullopt;
        }
        
        auto yq = yahoo_->get_quote(symbol);
        if (!yq) {
            last_error_ = yahoo_->last_error();
            return std::nullopt;
        }
        
        // Convert YahooQuote to Quote
        Quote quote;
        quote.symbol = yq->symbol;
        quote.price = yq->price;
        quote.open = yq->open;
        quote.high = yq->high;
        quote.low = yq->low;
        quote.previous_close = yq->previous_close;
        quote.volume = yq->volume;
        quote.change = yq->price - yq->previous_close;
        if (yq->previous_close > 0) {
            quote.change_percent = (quote.change / yq->previous_close) * 100;
        }
        quote.timestamp = yq->timestamp;
        
        return quote;
    }
    
    std::vector<PriceBar> try_alpha_vantage_history(const std::string& symbol, bool adjusted) {
        if (!alpha_vantage_) {
            last_error_ = "Alpha Vantage not configured";
            return {};
        }
        
        auto bars = adjusted ? 
            alpha_vantage_->get_daily_adjusted(symbol, OutputSize::Full) :
            alpha_vantage_->get_daily(symbol, OutputSize::Full);
        
        if (bars.empty()) {
            last_error_ = alpha_vantage_->last_error();
        }
        
        return bars;
    }
    
    std::vector<PriceBar> try_yahoo_history(const std::string& symbol,
                                            const std::string& start,
                                            const std::string& end) {
        if (!yahoo_) {
            last_error_ = "Yahoo Finance not available";
            return {};
        }
        
        std::string start_date = start.empty() ? "2000-01-01" : start;
        auto bars = yahoo_->get_historical_prices(symbol, start_date, end);
        
        if (bars.empty()) {
            last_error_ = yahoo_->last_error();
        }
        
        return bars;
    }
};

} // namespace genie::market

#endif // GENIE_MARKET_DATA_MANAGER_HPP
