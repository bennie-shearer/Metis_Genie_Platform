/**
 * @file symbol_autocomplete.hpp
 * @brief Symbol autocomplete with database integration
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: User Experience - Symbol autocomplete from database
 */

#ifndef GENIE_UX_SYMBOL_AUTOCOMPLETE_HPP
#define GENIE_UX_SYMBOL_AUTOCOMPLETE_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <memory>
#include <chrono>
#include <cctype>

namespace genie {
namespace ux {

/**
 * @brief Symbol match result
 */
struct SymbolMatch {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string type;          // stock, etf, mutual_fund, crypto, forex
    double relevance_score{0.0};
    int64_t avg_volume{0};
    double market_cap{0.0};
    bool is_tradeable{true};
    
    bool operator<(const SymbolMatch& other) const {
        return relevance_score > other.relevance_score; // Higher score first
    }
};

/**
 * @brief Trie node for prefix matching
 */
struct TrieNode {
    std::map<char, std::unique_ptr<TrieNode>> children;
    std::vector<std::string> symbols;  // Symbols at this node
    bool is_word_end{false};
};

/**
 * @brief Symbol database entry
 */
struct SymbolEntry {
    std::string symbol;
    std::string name;
    std::string exchange;
    std::string type;
    std::string sector;
    std::string industry;
    int64_t avg_volume{0};
    double market_cap{0.0};
    bool is_active{true};
    std::vector<std::string> aliases;
    std::chrono::system_clock::time_point last_updated;
};

/**
 * @brief Symbol autocomplete engine
 */
class SymbolAutocomplete {
public:
    struct Config {
        size_t max_results{10};
        size_t min_query_length{1};
        bool include_inactive{false};
        bool boost_by_volume{true};
        bool boost_by_market_cap{true};
        double exact_match_boost{10.0};
        double prefix_match_boost{5.0};
        double name_match_boost{2.0};
        std::vector<std::string> preferred_exchanges{"NYSE", "NASDAQ"};
    };
    
    explicit SymbolAutocomplete(const Config& config)
        : config_(config), symbol_trie_(std::make_unique<TrieNode>()),
          name_trie_(std::make_unique<TrieNode>()) {}
    
    /**
     * @brief Add symbol to autocomplete index
     */
    void add_symbol(const SymbolEntry& entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        symbols_[entry.symbol] = entry;
        
        // Add to symbol trie
        add_to_trie(symbol_trie_.get(), to_upper(entry.symbol), entry.symbol);
        
        // Add to name trie (tokenized)
        auto tokens = tokenize(entry.name);
        for (const auto& token : tokens) {
            add_to_trie(name_trie_.get(), to_upper(token), entry.symbol);
        }
        
        // Add aliases
        for (const auto& alias : entry.aliases) {
            add_to_trie(symbol_trie_.get(), to_upper(alias), entry.symbol);
        }
    }
    
    /**
     * @brief Remove symbol from index
     */
    void remove_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex_);
        symbols_.erase(symbol);
        // Note: Trie cleanup is deferred for performance
    }
    
    /**
     * @brief Search for symbols matching query
     */
    std::vector<SymbolMatch> search(const std::string& query,
                                     size_t max_results = 0) const {
        if (query.length() < config_.min_query_length) {
            return {};
        }
        
        if (max_results == 0) max_results = config_.max_results;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string upper_query = to_upper(query);
        std::unordered_map<std::string, double> scores;
        
        // Search symbol trie
        auto symbol_matches = search_trie(symbol_trie_.get(), upper_query);
        for (const auto& sym : symbol_matches) {
            double score = calculate_relevance(sym, upper_query, true);
            scores[sym] = std::max(scores[sym], score);
        }
        
        // Search name trie
        auto name_matches = search_trie(name_trie_.get(), upper_query);
        for (const auto& sym : name_matches) {
            double score = calculate_relevance(sym, upper_query, false);
            scores[sym] = std::max(scores[sym], score);
        }
        
        // Build results
        std::vector<SymbolMatch> results;
        for (const auto& [symbol, score] : scores) {
            auto it = symbols_.find(symbol);
            if (it == symbols_.end()) continue;
            if (!config_.include_inactive && !it->second.is_active) continue;
            
            SymbolMatch match;
            match.symbol = it->second.symbol;
            match.name = it->second.name;
            match.exchange = it->second.exchange;
            match.type = it->second.type;
            match.avg_volume = it->second.avg_volume;
            match.market_cap = it->second.market_cap;
            match.relevance_score = score;
            match.is_tradeable = it->second.is_active;
            
            results.push_back(match);
        }
        
        // Sort by relevance
        std::sort(results.begin(), results.end());
        
        // Limit results
        if (results.size() > max_results) {
            results.resize(max_results);
        }
        
        return results;
    }
    
    /**
     * @brief Get popular symbols (most traded)
     */
    std::vector<SymbolMatch> get_popular(size_t count = 10) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::pair<std::string, int64_t>> by_volume;
        for (const auto& [symbol, entry] : symbols_) {
            if (entry.is_active) {
                by_volume.emplace_back(symbol, entry.avg_volume);
            }
        }
        
        std::sort(by_volume.begin(), by_volume.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;
                  });
        
        std::vector<SymbolMatch> results;
        for (size_t i = 0; i < std::min(count, by_volume.size()); ++i) {
            auto it = symbols_.find(by_volume[i].first);
            if (it != symbols_.end()) {
                SymbolMatch match;
                match.symbol = it->second.symbol;
                match.name = it->second.name;
                match.exchange = it->second.exchange;
                match.type = it->second.type;
                match.avg_volume = it->second.avg_volume;
                match.market_cap = it->second.market_cap;
                match.relevance_score = static_cast<double>(it->second.avg_volume);
                results.push_back(match);
            }
        }
        
        return results;
    }
    
    /**
     * @brief Get symbols by sector
     */
    std::vector<SymbolMatch> get_by_sector(const std::string& sector,
                                            size_t max_results = 20) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<SymbolMatch> results;
        std::string upper_sector = to_upper(sector);
        
        for (const auto& [symbol, entry] : symbols_) {
            if (!entry.is_active) continue;
            if (to_upper(entry.sector) != upper_sector) continue;
            
            SymbolMatch match;
            match.symbol = entry.symbol;
            match.name = entry.name;
            match.exchange = entry.exchange;
            match.type = entry.type;
            match.avg_volume = entry.avg_volume;
            match.market_cap = entry.market_cap;
            match.relevance_score = entry.market_cap;
            results.push_back(match);
        }
        
        std::sort(results.begin(), results.end());
        if (results.size() > max_results) {
            results.resize(max_results);
        }
        
        return results;
    }
    
    /**
     * @brief Bulk load symbols
     */
    void bulk_load(const std::vector<SymbolEntry>& entries) {
        for (const auto& entry : entries) {
            add_symbol(entry);
        }
    }
    
    /**
     * @brief Get statistics
     */
    std::map<std::string, int64_t> get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::map<std::string, int64_t> stats;
        stats["total_symbols"] = static_cast<int64_t>(symbols_.size());
        
        int64_t active = 0, stocks = 0, etfs = 0, crypto = 0;
        for (const auto& [sym, entry] : symbols_) {
            if (entry.is_active) active++;
            if (entry.type == "stock") stocks++;
            else if (entry.type == "etf") etfs++;
            else if (entry.type == "crypto") crypto++;
        }
        
        stats["active_symbols"] = active;
        stats["stocks"] = stocks;
        stats["etfs"] = etfs;
        stats["crypto"] = crypto;
        
        return stats;
    }
    
    /**
     * @brief Clear all data
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        symbols_.clear();
        symbol_trie_ = std::make_unique<TrieNode>();
        name_trie_ = std::make_unique<TrieNode>();
    }

private:
    Config config_;
    std::unordered_map<std::string, SymbolEntry> symbols_;
    std::unique_ptr<TrieNode> symbol_trie_;
    std::unique_ptr<TrieNode> name_trie_;
    mutable std::mutex mutex_;
    
    static std::string to_upper(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return result;
    }
    
    static std::vector<std::string> tokenize(const std::string& s) {
        std::vector<std::string> tokens;
        std::string current;
        
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                current += c;
            } else if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        
        return tokens;
    }
    
    void add_to_trie(TrieNode* node, const std::string& key,
                     const std::string& symbol) {
        for (char c : key) {
            if (!node->children[c]) {
                node->children[c] = std::make_unique<TrieNode>();
            }
            node = node->children[c].get();
            
            // Add symbol at each prefix level for faster lookups
            auto& syms = node->symbols;
            if (std::find(syms.begin(), syms.end(), symbol) == syms.end()) {
                syms.push_back(symbol);
            }
        }
        node->is_word_end = true;
    }
    
    std::vector<std::string> search_trie(const TrieNode* node,
                                          const std::string& prefix) const {
        for (char c : prefix) {
            auto it = node->children.find(c);
            if (it == node->children.end()) {
                return {};
            }
            node = it->second.get();
        }
        return node->symbols;
    }
    
    double calculate_relevance(const std::string& symbol,
                               const std::string& query,
                               bool is_symbol_match) const {
        auto it = symbols_.find(symbol);
        if (it == symbols_.end()) return 0.0;
        
        const auto& entry = it->second;
        double score = 1.0;
        
        std::string upper_symbol = to_upper(entry.symbol);
        
        // Exact match bonus
        if (upper_symbol == query) {
            score *= config_.exact_match_boost;
        }
        // Prefix match bonus
        else if (upper_symbol.find(query) == 0) {
            score *= config_.prefix_match_boost;
        }
        // Name match
        else if (!is_symbol_match) {
            score *= config_.name_match_boost;
        }
        
        // Volume boost
        if (config_.boost_by_volume && entry.avg_volume > 0) {
            score *= (1.0 + std::log10(static_cast<double>(entry.avg_volume)) / 10.0);
        }
        
        // Market cap boost
        if (config_.boost_by_market_cap && entry.market_cap > 0) {
            score *= (1.0 + std::log10(entry.market_cap) / 20.0);
        }
        
        // Preferred exchange boost
        for (const auto& exch : config_.preferred_exchanges) {
            if (entry.exchange == exch) {
                score *= 1.5;
                break;
            }
        }
        
        // Shorter symbols ranked higher for same prefix
        score *= (10.0 / (10.0 + entry.symbol.length()));
        
        return score;
    }
};

/**
 * @brief SQL-backed symbol database
 */
class SymbolDatabase {
public:
    explicit SymbolDatabase(const std::string& db_path = "symbols.db")
        : db_path_(db_path) {}
    
    /**
     * @brief Initialize database schema
     */
    bool initialize() {
        // Schema would be created here
        initialized_ = true;
        return true;
    }
    
    /**
     * @brief Load symbols into autocomplete
     */
    size_t load_into(SymbolAutocomplete& autocomplete) {
        // In production, this would query the database
        // For now, load common symbols
        std::vector<SymbolEntry> entries = get_default_symbols();
        autocomplete.bulk_load(entries);
        return entries.size();
    }
    
    /**
     * @brief Get default symbol set
     */
    static std::vector<SymbolEntry> get_default_symbols() {
        std::vector<SymbolEntry> entries;
        
        // Major indices and ETFs
        entries.push_back({"SPY", "SPDR S&P 500 ETF Trust", "NYSE", "etf", "Index", "", 80000000, 400000000000.0, true, {"SP500", "S&P"}});
        entries.push_back({"QQQ", "Invesco QQQ Trust", "NASDAQ", "etf", "Index", "", 50000000, 180000000000.0, true, {"NASDAQ100"}});
        entries.push_back({"IWM", "iShares Russell 2000 ETF", "NYSE", "etf", "Index", "", 25000000, 50000000000.0, true, {"RUSSELL"}});
        entries.push_back({"DIA", "SPDR Dow Jones Industrial Average ETF", "NYSE", "etf", "Index", "", 3000000, 30000000000.0, true, {"DOW"}});
        entries.push_back({"VTI", "Vanguard Total Stock Market ETF", "NYSE", "etf", "Index", "", 4000000, 300000000000.0, true, {}});
        
        // Mega caps - Technology
        entries.push_back({"AAPL", "Apple Inc.", "NASDAQ", "stock", "Technology", "Consumer Electronics", 60000000, 3000000000000.0, true, {"APPLE"}});
        entries.push_back({"MSFT", "Microsoft Corporation", "NASDAQ", "stock", "Technology", "Software", 25000000, 2800000000000.0, true, {"MICROSOFT"}});
        entries.push_back({"GOOGL", "Alphabet Inc. Class A", "NASDAQ", "stock", "Technology", "Internet", 20000000, 1800000000000.0, true, {"GOOGLE"}});
        entries.push_back({"GOOG", "Alphabet Inc. Class C", "NASDAQ", "stock", "Technology", "Internet", 15000000, 1800000000000.0, true, {"GOOGLE"}});
        entries.push_back({"AMZN", "Amazon.com Inc.", "NASDAQ", "stock", "Technology", "E-Commerce", 35000000, 1500000000000.0, true, {"AMAZON"}});
        entries.push_back({"META", "Meta Platforms Inc.", "NASDAQ", "stock", "Technology", "Social Media", 20000000, 900000000000.0, true, {"FACEBOOK", "FB"}});
        entries.push_back({"NVDA", "NVIDIA Corporation", "NASDAQ", "stock", "Technology", "Semiconductors", 45000000, 1200000000000.0, true, {"NVIDIA"}});
        entries.push_back({"TSLA", "Tesla Inc.", "NASDAQ", "stock", "Technology", "Electric Vehicles", 100000000, 800000000000.0, true, {"TESLA"}});
        
        // Finance
        entries.push_back({"JPM", "JPMorgan Chase & Co.", "NYSE", "stock", "Finance", "Banking", 10000000, 450000000000.0, true, {"JPMORGAN"}});
        entries.push_back({"BAC", "Bank of America Corporation", "NYSE", "stock", "Finance", "Banking", 35000000, 250000000000.0, true, {}});
        entries.push_back({"WFC", "Wells Fargo & Company", "NYSE", "stock", "Finance", "Banking", 15000000, 180000000000.0, true, {}});
        entries.push_back({"GS", "Goldman Sachs Group Inc.", "NYSE", "stock", "Finance", "Investment Banking", 3000000, 120000000000.0, true, {"GOLDMAN"}});
        entries.push_back({"V", "Visa Inc.", "NYSE", "stock", "Finance", "Payments", 7000000, 500000000000.0, true, {"VISA"}});
        entries.push_back({"MA", "Mastercard Incorporated", "NYSE", "stock", "Finance", "Payments", 4000000, 400000000000.0, true, {"MASTERCARD"}});
        
        // Healthcare
        entries.push_back({"JNJ", "Johnson & Johnson", "NYSE", "stock", "Healthcare", "Pharmaceuticals", 6000000, 400000000000.0, true, {}});
        entries.push_back({"UNH", "UnitedHealth Group Inc.", "NYSE", "stock", "Healthcare", "Insurance", 3000000, 500000000000.0, true, {}});
        entries.push_back({"PFE", "Pfizer Inc.", "NYSE", "stock", "Healthcare", "Pharmaceuticals", 25000000, 200000000000.0, true, {"PFIZER"}});
        entries.push_back({"ABBV", "AbbVie Inc.", "NYSE", "stock", "Healthcare", "Pharmaceuticals", 5000000, 250000000000.0, true, {}});
        entries.push_back({"MRK", "Merck & Co. Inc.", "NYSE", "stock", "Healthcare", "Pharmaceuticals", 8000000, 280000000000.0, true, {"MERCK"}});
        
        // Consumer
        entries.push_back({"WMT", "Walmart Inc.", "NYSE", "stock", "Consumer", "Retail", 7000000, 400000000000.0, true, {"WALMART"}});
        entries.push_back({"PG", "Procter & Gamble Company", "NYSE", "stock", "Consumer", "Consumer Goods", 6000000, 350000000000.0, true, {}});
        entries.push_back({"KO", "Coca-Cola Company", "NYSE", "stock", "Consumer", "Beverages", 12000000, 260000000000.0, true, {"COKE", "COCACOLA"}});
        entries.push_back({"PEP", "PepsiCo Inc.", "NASDAQ", "stock", "Consumer", "Beverages", 5000000, 240000000000.0, true, {"PEPSI"}});
        entries.push_back({"COST", "Costco Wholesale Corporation", "NASDAQ", "stock", "Consumer", "Retail", 2000000, 250000000000.0, true, {"COSTCO"}});
        entries.push_back({"HD", "Home Depot Inc.", "NYSE", "stock", "Consumer", "Retail", 4000000, 350000000000.0, true, {"HOMEDEPOT"}});
        
        // Energy
        entries.push_back({"XOM", "Exxon Mobil Corporation", "NYSE", "stock", "Energy", "Oil & Gas", 15000000, 450000000000.0, true, {"EXXON"}});
        entries.push_back({"CVX", "Chevron Corporation", "NYSE", "stock", "Energy", "Oil & Gas", 7000000, 300000000000.0, true, {"CHEVRON"}});
        
        // Industrials
        entries.push_back({"CAT", "Caterpillar Inc.", "NYSE", "stock", "Industrials", "Machinery", 3000000, 150000000000.0, true, {}});
        entries.push_back({"BA", "Boeing Company", "NYSE", "stock", "Industrials", "Aerospace", 5000000, 130000000000.0, true, {"BOEING"}});
        entries.push_back({"GE", "General Electric Company", "NYSE", "stock", "Industrials", "Conglomerate", 8000000, 120000000000.0, true, {}});
        
        // Crypto (if supported)
        entries.push_back({"BTC-USD", "Bitcoin USD", "CRYPTO", "crypto", "Cryptocurrency", "", 30000000000, 800000000000.0, true, {"BTC", "BITCOIN"}});
        entries.push_back({"ETH-USD", "Ethereum USD", "CRYPTO", "crypto", "Cryptocurrency", "", 15000000000, 300000000000.0, true, {"ETH", "ETHEREUM"}});
        
        return entries;
    }

private:
    std::string db_path_;
    bool initialized_{false};
};

} // namespace ux
} // namespace genie

#endif // GENIE_UX_SYMBOL_AUTOCOMPLETE_HPP
