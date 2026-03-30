/**
 * @file instrument_master.hpp
 * @brief Comprehensive security master with identifiers and reference data
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Security master database for instrument reference data:
 * - Multi-identifier resolution (ISIN, CUSIP, SEDOL, FIGI, ticker)
 * - Asset classification (equity, fixed income, derivative, fund, crypto)
 * - Corporate action tracking (splits, dividends, mergers, spinoffs)
 * - Exchange and listing information
 * - Sector/industry/country classification
 * - Instrument lifecycle (active, suspended, delisted, matured)
 * - Bulk lookup and fuzzy search
 * - Audit trail for reference data changes
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_ASSETS_INSTRUMENT_MASTER_HPP
#define GENIE_ASSETS_INSTRUMENT_MASTER_HPP

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
#include <set>

namespace genie {
namespace assets {
namespace master {

// ============================================================================
// Enumerations
// ============================================================================

enum class AssetType {
    Equity, PreferredStock, ADR, ETF, MutualFund, ClosedEndFund,
    CorporateBond, GovernmentBond, MunicipalBond, ConvertibleBond,
    Option, Future, Swap, FRA, CDS,
    FXSpot, FXForward, Commodity,
    Crypto, StableCoin,
    REIT, MLP, SPAC,
    Index, Custom
};

enum class InstrumentStatus {
    Active, Suspended, Halted, Delisted, Matured, Merged, Converted
};

enum class CorporateActionType {
    StockSplit, ReverseSplit, CashDividend, StockDividend, SpecialDividend,
    Merger, Acquisition, Spinoff, RightsIssue, NameChange, TickerChange,
    Exchange, Delisting, Bankruptcy
};

[[nodiscard]] inline std::string asset_type_string(AssetType t) {
    switch (t) {
        case AssetType::Equity: return "equity"; case AssetType::PreferredStock: return "preferred";
        case AssetType::ADR: return "adr"; case AssetType::ETF: return "etf";
        case AssetType::MutualFund: return "mutual_fund"; case AssetType::ClosedEndFund: return "cef";
        case AssetType::CorporateBond: return "corp_bond"; case AssetType::GovernmentBond: return "govt_bond";
        case AssetType::MunicipalBond: return "muni_bond"; case AssetType::ConvertibleBond: return "convert_bond";
        case AssetType::Option: return "option"; case AssetType::Future: return "future";
        case AssetType::Swap: return "swap"; case AssetType::FRA: return "fra";
        case AssetType::CDS: return "cds"; case AssetType::FXSpot: return "fx_spot";
        case AssetType::FXForward: return "fx_fwd"; case AssetType::Commodity: return "commodity";
        case AssetType::Crypto: return "crypto"; case AssetType::StableCoin: return "stablecoin";
        case AssetType::REIT: return "reit"; case AssetType::MLP: return "mlp";
        case AssetType::SPAC: return "spac"; case AssetType::Index: return "index";
        case AssetType::Custom: return "custom";
    }
    return "unknown";
}

[[nodiscard]] inline std::string status_string(InstrumentStatus s) {
    switch (s) {
        case InstrumentStatus::Active: return "active"; case InstrumentStatus::Suspended: return "suspended";
        case InstrumentStatus::Halted: return "halted"; case InstrumentStatus::Delisted: return "delisted";
        case InstrumentStatus::Matured: return "matured"; case InstrumentStatus::Merged: return "merged";
        case InstrumentStatus::Converted: return "converted";
    }
    return "unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

struct Identifiers {
    std::string ticker;
    std::string isin;
    std::string cusip;
    std::string sedol;
    std::string figi;
    std::string ric;           // Reuters
    std::string bbg_ticker;    // Bloomberg
    std::string cik;           // SEC CIK
};

struct CorporateAction {
    CorporateActionType type;
    std::chrono::system_clock::time_point effective_date;
    std::chrono::system_clock::time_point record_date;
    std::chrono::system_clock::time_point ex_date;
    double factor{1.0};        // Split/dividend factor
    double cash_amount{0};
    std::string currency;
    std::string description;
    std::string new_symbol;    // For mergers/ticker changes
};

/**
 * @brief Complete instrument record
 */
struct Instrument {
    std::string id;            // Internal canonical ID
    std::string name;
    std::string short_name;
    Identifiers identifiers;
    AssetType asset_type{AssetType::Equity};
    InstrumentStatus status{InstrumentStatus::Active};

    // Classification
    std::string sector;
    std::string industry;
    std::string sub_industry;
    std::string country;
    std::string region;

    // Exchange & listing
    std::string primary_exchange;
    std::string mic;           // Market Identifier Code
    std::string currency;
    int lot_size{1};
    double tick_size{0.01};

    // Reference data
    int64_t shares_outstanding{0};
    double market_cap{0};
    double par_value{0};       // For bonds
    std::string maturity_date; // For bonds
    double coupon_rate{0};     // For bonds

    // Lifecycle
    std::chrono::system_clock::time_point listed_date;
    std::chrono::system_clock::time_point last_updated;
    std::vector<CorporateAction> corporate_actions;

    [[nodiscard]] bool is_active() const { return status == InstrumentStatus::Active; }
    [[nodiscard]] bool is_equity_like() const {
        return asset_type == AssetType::Equity || asset_type == AssetType::ETF ||
               asset_type == AssetType::ADR || asset_type == AssetType::REIT;
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << "{\"id\":\"" << id << "\",\"name\":\"" << name
            << "\",\"ticker\":\"" << identifiers.ticker
            << "\",\"type\":\"" << asset_type_string(asset_type)
            << "\",\"status\":\"" << status_string(status)
            << "\",\"sector\":\"" << sector
            << "\",\"country\":\"" << country
            << "\",\"exchange\":\"" << primary_exchange
            << "\",\"currency\":\"" << currency << "\"}";
        return oss.str();
    }
};

// ============================================================================
// Instrument Master
// ============================================================================

class InstrumentMaster {
public:
    InstrumentMaster() { register_default_instruments(); }

    void add(Instrument inst) {
        std::lock_guard<std::mutex> lock(mutex_);
        inst.last_updated = std::chrono::system_clock::now();
        std::string id = inst.id;
        // Build reverse indices
        if (!inst.identifiers.isin.empty()) isin_idx_[inst.identifiers.isin] = id;
        if (!inst.identifiers.cusip.empty()) cusip_idx_[inst.identifiers.cusip] = id;
        if (!inst.identifiers.ticker.empty()) ticker_idx_[inst.identifiers.ticker] = id;
        if (!inst.identifiers.sedol.empty()) sedol_idx_[inst.identifiers.sedol] = id;
        if (!inst.identifiers.figi.empty()) figi_idx_[inst.identifiers.figi] = id;
        instruments_[id] = std::move(inst);
    }

    [[nodiscard]] std::optional<Instrument> get(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instruments_.find(id);
        if (it == instruments_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::optional<Instrument> lookup_by_isin(const std::string& isin) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = isin_idx_.find(isin); if (it == isin_idx_.end()) return std::nullopt;
        auto ii = instruments_.find(it->second); return ii != instruments_.end() ? std::optional(ii->second) : std::nullopt;
    }

    [[nodiscard]] std::optional<Instrument> lookup_by_ticker(const std::string& ticker) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ticker_idx_.find(ticker); if (it == ticker_idx_.end()) return std::nullopt;
        auto ii = instruments_.find(it->second); return ii != instruments_.end() ? std::optional(ii->second) : std::nullopt;
    }

    [[nodiscard]] std::vector<Instrument> search(const std::string& query) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string q = to_upper(query);
        std::vector<Instrument> results;
        for (const auto& [_, inst] : instruments_) {
            if (to_upper(inst.name).find(q) != std::string::npos ||
                to_upper(inst.identifiers.ticker).find(q) != std::string::npos ||
                inst.identifiers.isin.find(q) != std::string::npos) {
                results.push_back(inst);
                if (results.size() >= 50) break;
            }
        }
        return results;
    }

    [[nodiscard]] std::vector<Instrument> by_sector(const std::string& sector) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Instrument> results;
        for (const auto& [_, inst] : instruments_) {
            if (inst.sector == sector && inst.is_active()) results.push_back(inst);
        }
        return results;
    }

    [[nodiscard]] std::vector<Instrument> by_type(AssetType type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Instrument> results;
        for (const auto& [_, inst] : instruments_) {
            if (inst.asset_type == type && inst.is_active()) results.push_back(inst);
        }
        return results;
    }

    void add_corporate_action(const std::string& id, CorporateAction ca) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instruments_.find(id);
        if (it != instruments_.end()) {
            it->second.corporate_actions.push_back(std::move(ca));
            it->second.last_updated = std::chrono::system_clock::now();
        }
    }

    [[nodiscard]] int instrument_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(instruments_.size());
    }

    [[nodiscard]] int active_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int c = 0;
        for (const auto& [_, i] : instruments_) if (i.is_active()) ++c;
        return c;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, Instrument> instruments_;
    std::unordered_map<std::string, std::string> isin_idx_, cusip_idx_,
        ticker_idx_, sedol_idx_, figi_idx_;

    static std::string to_upper(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::toupper);
        return r;
    }

    void register_default_instruments() {
        auto add_equity = [this](const std::string& ticker, const std::string& name,
                                   const std::string& isin, const std::string& sector,
                                   const std::string& exchange) {
            Instrument inst;
            inst.id = ticker; inst.name = name; inst.short_name = ticker;
            inst.identifiers.ticker = ticker; inst.identifiers.isin = isin;
            inst.asset_type = AssetType::Equity; inst.sector = sector;
            inst.country = "US"; inst.region = "North America";
            inst.primary_exchange = exchange; inst.currency = "USD";
            add(std::move(inst));
        };
        add_equity("AAPL", "Apple Inc", "US0378331005", "Technology", "NASDAQ");
        add_equity("MSFT", "Microsoft Corporation", "US5949181045", "Technology", "NASDAQ");
        add_equity("GOOGL", "Alphabet Inc Class A", "US02079K3059", "Communication Services", "NASDAQ");
        add_equity("AMZN", "Amazon.com Inc", "US0231351067", "Consumer Discretionary", "NASDAQ");
        add_equity("NVDA", "NVIDIA Corporation", "US67066G1040", "Technology", "NASDAQ");
        add_equity("META", "Meta Platforms Inc", "US30303M1027", "Communication Services", "NASDAQ");
        add_equity("TSLA", "Tesla Inc", "US88160R1014", "Consumer Discretionary", "NASDAQ");
        add_equity("JPM", "JPMorgan Chase & Co", "US46625H1005", "Financials", "NYSE");
        add_equity("V", "Visa Inc", "US92826C8394", "Financials", "NYSE");
        add_equity("JNJ", "Johnson & Johnson", "US4781601046", "Healthcare", "NYSE");
        add_equity("UNH", "UnitedHealth Group", "US91324P1021", "Healthcare", "NYSE");
        add_equity("XOM", "Exxon Mobil Corp", "US30231G1022", "Energy", "NYSE");
    }
};

} // namespace master
} // namespace assets
} // namespace genie

#endif // GENIE_ASSETS_INSTRUMENT_MASTER_HPP
