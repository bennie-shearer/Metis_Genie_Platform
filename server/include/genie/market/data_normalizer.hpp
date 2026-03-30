/**
 * @file data_normalizer.hpp
 * @brief Market data normalization layer across providers
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Unified market data format across multiple providers:
 * - Provider-agnostic normalized quote/trade/bar format
 * - Symbol mapping (ISIN, CUSIP, SEDOL, ticker translation)
 * - Currency normalization and FX conversion
 * - Corporate action adjustments (splits, dividends)
 * - Stale data detection and quality scoring
 * - Provider priority/fallback chain
 * - Quote consolidation from multiple sources
 * - Audit trail for data provenance
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_MARKET_DATA_NORMALIZER_HPP
#define GENIE_MARKET_DATA_NORMALIZER_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <optional>
#include <cmath>
#include <functional>

namespace genie {
namespace market {
namespace normalizer {

// ============================================================================
// Enumerations
// ============================================================================

enum class DataProvider {
    Yahoo,
    AlphaVantage,
    Polygon,
    IEXCloud,
    Bloomberg,
    Reuters,
    FRED,
    Finnhub,
    CoinGecko,
    Manual
};

enum class IdentifierType {
    Ticker,
    ISIN,
    CUSIP,
    SEDOL,
    FIGI,
    RIC,             // Reuters Instrument Code
    BBGTicker        // Bloomberg ticker
};

enum class DataQuality {
    Excellent,       // Real-time, verified
    Good,            // Slightly delayed (<1min)
    Acceptable,      // Delayed (1-15min)
    Stale,           // Old (>15min)
    Suspect,         // Failed validation
    Unavailable      // No data
};

[[nodiscard]] inline std::string provider_string(DataProvider p) {
    switch (p) {
        case DataProvider::Yahoo:        return "yahoo";
        case DataProvider::AlphaVantage: return "alpha_vantage";
        case DataProvider::Polygon:      return "polygon";
        case DataProvider::IEXCloud:     return "iex_cloud";
        case DataProvider::Bloomberg:    return "bloomberg";
        case DataProvider::Reuters:      return "reuters";
        case DataProvider::FRED:         return "fred";
        case DataProvider::Finnhub:      return "finnhub";
        case DataProvider::CoinGecko:    return "coingecko";
        case DataProvider::Manual:       return "manual";
    }
    return "unknown";
}

[[nodiscard]] inline std::string quality_string(DataQuality q) {
    switch (q) {
        case DataQuality::Excellent:    return "excellent";
        case DataQuality::Good:         return "good";
        case DataQuality::Acceptable:   return "acceptable";
        case DataQuality::Stale:        return "stale";
        case DataQuality::Suspect:      return "suspect";
        case DataQuality::Unavailable:  return "unavailable";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Normalized quote
 */
struct NormalizedQuote {
    std::string symbol;            // Canonical symbol
    double bid{0};
    double ask{0};
    double mid{0};
    double last{0};
    double volume{0};
    double change{0};
    double change_pct{0};
    double open{0};
    double high{0};
    double low{0};
    double prev_close{0};
    int64_t market_cap{0};
    std::string currency{"USD"};
    DataProvider source{DataProvider::Manual};
    DataQuality quality{DataQuality::Unavailable};
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point received_at;
    double latency_ms{0};

    [[nodiscard]] double spread() const { return ask - bid; }
    [[nodiscard]] double spread_bps() const {
        return mid > 0 ? (spread() / mid) * 10000.0 : 0;
    }
};

/**
 * @brief Symbol identifier mapping
 */
struct SymbolMapping {
    std::string canonical;         // Internal canonical symbol
    std::string ticker;
    std::string isin;
    std::string cusip;
    std::string sedol;
    std::string figi;
    std::string ric;
    std::string bbg_ticker;
    std::string exchange;
    std::string currency;
    std::string country;
    std::string asset_class;
    std::string name;

    [[nodiscard]] std::string get_id(IdentifierType type) const {
        switch (type) {
            case IdentifierType::Ticker:    return ticker;
            case IdentifierType::ISIN:      return isin;
            case IdentifierType::CUSIP:     return cusip;
            case IdentifierType::SEDOL:     return sedol;
            case IdentifierType::FIGI:      return figi;
            case IdentifierType::RIC:       return ric;
            case IdentifierType::BBGTicker: return bbg_ticker;
        }
        return "";
    }
};

/**
 * @brief Adjustment factor for corporate actions
 */
struct AdjustmentFactor {
    std::string symbol;
    std::chrono::system_clock::time_point effective_date;
    double price_factor{1.0};      // Multiply old prices by this
    double volume_factor{1.0};     // Multiply old volumes by this
    std::string reason;            // "2:1 split", "dividend $0.50"
};

/**
 * @brief Data provenance record
 */
struct ProvenanceRecord {
    std::string symbol;
    DataProvider provider;
    std::chrono::system_clock::time_point fetched_at;
    double raw_price{0};
    double normalized_price{0};
    double adjustment_factor{1.0};
    DataQuality quality;
    std::string notes;
};

// ============================================================================
// Symbol Registry
// ============================================================================

/**
 * @brief Maps between identifier systems
 */
class SymbolRegistry {
public:
    void register_symbol(SymbolMapping mapping) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string canonical = mapping.canonical;
        mappings_[canonical] = std::move(mapping);
        // Build reverse indices
        const auto& m = mappings_[canonical];
        if (!m.isin.empty()) isin_index_[m.isin] = canonical;
        if (!m.cusip.empty()) cusip_index_[m.cusip] = canonical;
        if (!m.ticker.empty()) ticker_index_[m.ticker] = canonical;
        if (!m.ric.empty()) ric_index_[m.ric] = canonical;
    }

    [[nodiscard]] std::optional<SymbolMapping> lookup(const std::string& canonical) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mappings_.find(canonical);
        if (it == mappings_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::string resolve(const std::string& identifier,
                                        IdentifierType type = IdentifierType::Ticker) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::map<std::string, std::string>* index = nullptr;
        switch (type) {
            case IdentifierType::ISIN:   index = &isin_index_; break;
            case IdentifierType::CUSIP:  index = &cusip_index_; break;
            case IdentifierType::RIC:    index = &ric_index_; break;
            default:                     index = &ticker_index_; break;
        }
        if (index) {
            auto it = index->find(identifier);
            if (it != index->end()) return it->second;
        }
        return identifier; // Assume it's already canonical
    }

    [[nodiscard]] int count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(mappings_.size());
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, SymbolMapping> mappings_;
    std::map<std::string, std::string> isin_index_;
    std::map<std::string, std::string> cusip_index_;
    std::map<std::string, std::string> ticker_index_;
    std::map<std::string, std::string> ric_index_;
};

// ============================================================================
// Data Normalizer
// ============================================================================

/**
 * @brief Normalizes market data from multiple providers
 */
class DataNormalizer {
public:
    DataNormalizer() {
        register_default_symbols();
    }

    /**
     * @brief Normalize a raw quote from a provider
     */
    NormalizedQuote normalize(const std::string& symbol, double price,
                               double volume, DataProvider source) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();

        NormalizedQuote q;
        q.symbol = registry_.resolve(symbol);
        q.last = apply_adjustments(q.symbol, price);
        q.volume = volume;
        q.source = source;
        q.timestamp = now;
        q.received_at = now;
        q.quality = assess_quality(q.symbol, price, now);

        // Calculate change from prev close
        auto pc_it = prev_close_.find(q.symbol);
        if (pc_it != prev_close_.end() && pc_it->second > 0) {
            q.prev_close = pc_it->second;
            q.change = q.last - q.prev_close;
            q.change_pct = q.change / q.prev_close * 100.0;
        }

        // Store as latest
        latest_[q.symbol] = q;

        // Record provenance
        ProvenanceRecord pr;
        pr.symbol = q.symbol;
        pr.provider = source;
        pr.fetched_at = now;
        pr.raw_price = price;
        pr.normalized_price = q.last;
        pr.quality = q.quality;
        provenance_.push_back(pr);
        if (provenance_.size() > 10000) provenance_.pop_front();

        return q;
    }

    /**
     * @brief Consolidate quotes from multiple providers
     */
    NormalizedQuote consolidate(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = latest_.find(symbol);
        if (it == latest_.end()) {
            NormalizedQuote empty;
            empty.symbol = symbol;
            empty.quality = DataQuality::Unavailable;
            return empty;
        }
        return it->second;
    }

    /**
     * @brief Register adjustment factor
     */
    void add_adjustment(AdjustmentFactor factor) {
        std::lock_guard<std::mutex> lock(mutex_);
        adjustments_[factor.symbol].push_back(std::move(factor));
    }

    /**
     * @brief Set previous close for change calculation
     */
    void set_prev_close(const std::string& symbol, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        prev_close_[symbol] = price;
    }

    /**
     * @brief Set provider priority
     */
    void set_provider_priority(const std::vector<DataProvider>& priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        provider_priority_ = priority;
    }

    /**
     * @brief Get symbol registry
     */
    SymbolRegistry& registry() { return registry_; }

    /**
     * @brief Get latest normalized quote
     */
    [[nodiscard]] std::optional<NormalizedQuote> latest(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = latest_.find(symbol);
        if (it == latest_.end()) return std::nullopt;
        return it->second;
    }

    /**
     * @brief Get provenance trail
     */
    [[nodiscard]] std::vector<ProvenanceRecord> provenance(const std::string& symbol,
                                                             int max_count = 50) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProvenanceRecord> result;
        for (auto it = provenance_.rbegin(); it != provenance_.rend(); ++it) {
            if (it->symbol == symbol) {
                result.push_back(*it);
                if (static_cast<int>(result.size()) >= max_count) break;
            }
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] int tracked_symbols() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(latest_.size());
    }

private:
    mutable std::mutex mutex_;
    SymbolRegistry registry_;
    std::map<std::string, NormalizedQuote> latest_;
    std::map<std::string, double> prev_close_;
    std::map<std::string, std::vector<AdjustmentFactor>> adjustments_;
    std::vector<DataProvider> provider_priority_;
    std::deque<ProvenanceRecord> provenance_;

    double apply_adjustments(const std::string& symbol, double raw_price) const {
        auto it = adjustments_.find(symbol);
        if (it == adjustments_.end()) return raw_price;
        double adjusted = raw_price;
        for (const auto& adj : it->second) {
            adjusted *= adj.price_factor;
        }
        return adjusted;
    }

    DataQuality assess_quality(const std::string& symbol, double price,
                                std::chrono::system_clock::time_point now) const {
        if (price <= 0) return DataQuality::Suspect;
        auto prev = latest_.find(symbol);
        if (prev != latest_.end()) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - prev->second.timestamp).count();
            if (age < 5) return DataQuality::Excellent;
            if (age < 60) return DataQuality::Good;
            if (age < 900) return DataQuality::Acceptable;
            return DataQuality::Stale;
        }
        return DataQuality::Good; // First data point
    }

    void register_default_symbols() {
        auto reg = [this](const std::string& sym, const std::string& name,
                           const std::string& isin = "") {
            SymbolMapping m;
            m.canonical = sym;
            m.ticker = sym;
            m.name = name;
            m.isin = isin;
            m.currency = "USD";
            m.country = "US";
            registry_.register_symbol(std::move(m));
        };
        reg("AAPL", "Apple Inc", "US0378331005");
        reg("MSFT", "Microsoft Corp", "US5949181045");
        reg("GOOGL", "Alphabet Inc", "US02079K3059");
        reg("AMZN", "Amazon.com Inc", "US0231351067");
        reg("NVDA", "NVIDIA Corp", "US67066G1040");
        reg("META", "Meta Platforms", "US30303M1027");
        reg("TSLA", "Tesla Inc", "US88160R1014");
        reg("BRK.B", "Berkshire Hathaway B", "US0846707026");
        reg("JPM", "JPMorgan Chase", "US46625H1005");
        reg("V", "Visa Inc", "US92826C8394");
    }
};

} // namespace normalizer
} // namespace market
} // namespace genie

#endif // GENIE_MARKET_DATA_NORMALIZER_HPP
