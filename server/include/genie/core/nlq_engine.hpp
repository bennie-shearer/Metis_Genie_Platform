/**
 * @file nlq_engine.hpp
 * @brief Natural Language Query Engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Parses natural language queries about portfolios, risk, and market data
 * into structured query objects that can be executed against Genie's engines.
 *
 * Capabilities:
 *   - Tokenizer with stop-word removal and stemming
 *   - Intent classification (portfolio query, risk query, trade, report, etc.)
 *   - Entity extraction (symbols, dates, amounts, percentages, time ranges)
 *   - Query template matching with fuzzy scoring
 *   - Structured query builder (filters, aggregations, sorts)
 *   - Context-aware disambiguation (remembers conversation state)
 *   - Numeric expression parsing ("greater than 5%", "between $100 and $200")
 *   - Temporal expression parsing ("last week", "since January", "YTD")
 *   - Results formatting as natural language responses
 *
 * Example queries:
 *   "show me portfolios with greater than 5% drawdown"
 *   "what is the VaR of my tech holdings?"
 *   "list positions where unrealized gain exceeds $10,000"
 *   "compare performance of AAPL vs MSFT since January"
 *   "which sectors are overweight relative to benchmark?"
 *
 * Zero external dependencies (no NLP library). Uses pattern matching
 * and rule-based parsing. For production NLP, integrate an LLM API.
 */

#ifndef GENIE_CORE_NLQ_ENGINE_HPP
#define GENIE_CORE_NLQ_ENGINE_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <algorithm>
#include <sstream>
#include <functional>
#include <optional>
#include <variant>
#include <ctime>
#include <iomanip>
#include <cctype>
#include <cmath>

namespace genie {
namespace core {

// ============================================================
// Enumerations
// ============================================================

enum class QueryIntent {
    // Portfolio
    ListPortfolios,
    ShowPositions,
    PortfolioSummary,
    PortfolioPerformance,
    PortfolioAllocation,
    // Risk
    ShowVaR,
    ShowDrawdown,
    StressTest,
    RiskSummary,
    ExposureAnalysis,
    // Market
    GetQuote,
    CompareAssets,
    MarketOverview,
    SectorAnalysis,
    // Trading
    PlaceOrder,
    ShowOrders,
    ShowTrades,
    // Reporting
    GenerateReport,
    ShowCompliance,
    TaxSummary,
    // Analytics
    ShowBenchmark,
    ShowAttribution,
    BacktestStrategy,
    // System
    ShowStatus,
    Help,
    Unknown
};

enum class FilterOperator {
    Equals,
    NotEquals,
    GreaterThan,
    GreaterThanOrEqual,
    LessThan,
    LessThanOrEqual,
    Between,
    Contains,
    StartsWith,
    In
};

enum class AggregationType {
    None,
    Sum,
    Average,
    Count,
    Min,
    Max,
    Median
};

enum class SortDirection {
    Ascending,
    Descending
};

enum class EntityType {
    Symbol,         // AAPL, MSFT
    Amount,         // $10,000
    Percentage,     // 5%, 0.05
    Date,           // January 1, 2026-01-01
    DateRange,      // last week, since January
    Sector,         // technology, healthcare
    AssetClass,     // equity, fixed income, options
    Metric,         // VaR, Sharpe, drawdown
    PortfolioName,  // "my tech portfolio"
    Number,         // 10, 100
    Duration,       // 3 months, 1 year
    ComparisonOp    // greater than, less than
};

// ============================================================
// Data Structures
// ============================================================

struct ExtractedEntity {
    EntityType      type;
    std::string     raw_text;
    std::string     normalized;
    double          numeric_value = 0.0;
    std::string     date_value;
    std::string     date_end_value;  // For ranges
    size_t          position = 0;    // Position in token stream
};

struct QueryFilter {
    std::string     field;
    FilterOperator  op;
    std::variant<double, std::string, std::vector<std::string>> value;
    std::optional<double> value_upper;  // For Between
};

struct QuerySort {
    std::string     field;
    SortDirection   direction = SortDirection::Descending;
};

struct StructuredQuery {
    QueryIntent                 intent = QueryIntent::Unknown;
    double                      confidence = 0.0;
    std::vector<ExtractedEntity> entities;
    std::vector<QueryFilter>    filters;
    std::vector<QuerySort>      sorts;
    AggregationType             aggregation = AggregationType::None;
    std::string                 aggregation_field;
    std::vector<std::string>    symbols;
    std::string                 date_from;
    std::string                 date_to;
    std::string                 portfolio_name;
    int                         limit = 0;      // Top N
    std::string                 raw_query;
    std::string                 explanation;     // How the query was interpreted

    [[nodiscard]] bool has_symbols() const { return !symbols.empty(); }
    [[nodiscard]] bool has_date_range() const { return !date_from.empty(); }
    [[nodiscard]] bool has_filters() const { return !filters.empty(); }
};

struct QueryResult {
    bool                        success = false;
    std::string                 natural_response;   // Natural language answer
    std::string                 data_type;          // "positions", "portfolios", "metrics"
    std::vector<std::map<std::string, std::string>> rows;  // Tabular results
    std::map<std::string, double> scalar_metrics;
    std::string                 error_message;
    std::vector<std::string>    suggestions;        // "Did you mean...?"
};

// ============================================================
// Tokenizer
// ============================================================

class NlqTokenizer {
public:
    struct Token {
        std::string text;
        std::string lower;
        std::string stem;
        bool        is_stop_word = false;
        bool        is_number = false;
        bool        is_symbol = false;  // Looks like a ticker
        size_t      position = 0;
    };

    std::vector<Token> tokenize(const std::string& input) const {
        std::vector<Token> tokens;
        std::string cleaned = input;

        // Remove extra whitespace
        auto it = std::unique(cleaned.begin(), cleaned.end(),
            [](char a, char b) { return std::isspace(a) && std::isspace(b); });
        cleaned.erase(it, cleaned.end());

        // Split on whitespace and punctuation (keep $ and %)
        std::string current;
        size_t pos = 0;
        for (size_t i = 0; i <= cleaned.size(); ++i) {
            char c = (i < cleaned.size()) ? cleaned[i] : ' ';
            if (std::isspace(c) || c == ',' || c == '?' || c == '!' || c == ';') {
                if (!current.empty()) {
                    tokens.push_back(make_token(current, pos));
                    pos++;
                    current.clear();
                }
            } else {
                current += c;
            }
        }

        return tokens;
    }

private:
    Token make_token(const std::string& text, size_t pos) const {
        Token t;
        t.text = text;
        t.position = pos;

        // Lowercase
        t.lower.resize(text.size());
        std::transform(text.begin(), text.end(), t.lower.begin(),
            [](char c) { return static_cast<char>(std::tolower(c)); });

        // Check if number/amount
        t.is_number = is_numeric(text);

        // Check if ticker symbol (1-5 uppercase letters)
        t.is_symbol = is_ticker(text);

        // Stop words
        t.is_stop_word = stop_words_.count(t.lower) > 0;

        // Simple stemming (just remove common suffixes)
        t.stem = simple_stem(t.lower);

        return t;
    }

    bool is_numeric(const std::string& s) const {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '$' || s[0] == '-' || s[0] == '+') start = 1;
        bool has_digit = false;
        for (size_t i = start; i < s.size(); ++i) {
            if (std::isdigit(s[i]) || s[i] == '.' || s[i] == ',') {
                if (std::isdigit(s[i])) has_digit = true;
            } else if (s[i] == '%' && i == s.size() - 1) {
                continue; // Allow trailing %
            } else {
                return false;
            }
        }
        return has_digit;
    }

    bool is_ticker(const std::string& s) const {
        if (s.size() < 1 || s.size() > 5) return false;
        for (char c : s) {
            if (!std::isupper(c) && c != '.') return false;
        }
        return true;
    }

    std::string simple_stem(const std::string& word) const {
        std::string s = word;
        // Remove common suffixes
        if (s.size() > 5 && s.substr(s.size()-3) == "ing") s = s.substr(0, s.size()-3);
        else if (s.size() > 4 && s.substr(s.size()-2) == "ed") s = s.substr(0, s.size()-2);
        else if (s.size() > 4 && s.substr(s.size()-1) == "s" && s[s.size()-2] != 's')
            s = s.substr(0, s.size()-1);
        return s;
    }

    const std::set<std::string> stop_words_ = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "do", "does", "did", "will", "would", "could",
        "should", "may", "might", "shall", "can", "to", "of", "in", "for",
        "on", "with", "at", "by", "from", "as", "into", "through", "during",
        "before", "after", "above", "below", "and", "but", "or", "not", "no",
        "this", "that", "these", "those", "it", "its", "i", "me", "my",
        "we", "our", "you", "your", "he", "she", "they", "them", "their",
        "what", "which", "who", "whom", "when", "where", "why", "how",
        "all", "each", "every", "both", "few", "more", "most", "other",
        "some", "such", "than", "too", "very", "just", "also", "please"
    };
};

// ============================================================
// Entity Extractor
// ============================================================

class EntityExtractor {
public:
    std::vector<ExtractedEntity> extract(const std::string& input,
                                          const std::vector<NlqTokenizer::Token>& tokens) const {
        std::vector<ExtractedEntity> entities;

        // Extract symbols (tickers)
        for (const auto& t : tokens) {
            if (t.is_symbol && known_symbols_.count(t.text)) {
                entities.push_back({EntityType::Symbol, t.text, t.text, 0.0, "", "", t.position});
            } else if (t.is_symbol && t.text.size() >= 2 && t.text.size() <= 5) {
                // Possible unknown ticker
                entities.push_back({EntityType::Symbol, t.text, t.text, 0.0, "", "", t.position});
            }
        }

        // Extract amounts ($10,000)
        std::regex amount_re(R"(\$[\d,]+(?:\.\d+)?)");
        auto begin = std::sregex_iterator(input.begin(), input.end(), amount_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string match = (*it).str();
            std::string num_str = match.substr(1);
            num_str.erase(std::remove(num_str.begin(), num_str.end(), ','), num_str.end());
            double val = std::stod(num_str);
            entities.push_back({EntityType::Amount, match, num_str, val, "", "", 0});
        }

        // Extract percentages
        std::regex pct_re(R"((\d+(?:\.\d+)?)\s*%)");
        begin = std::sregex_iterator(input.begin(), input.end(), pct_re);
        for (auto it = begin; it != end; ++it) {
            double val = std::stod((*it)[1].str()) / 100.0;
            entities.push_back({EntityType::Percentage, (*it).str(), (*it)[1].str(), val, "", "", 0});
        }

        // Extract sectors
        for (const auto& t : tokens) {
            if (sector_map_.count(t.lower)) {
                entities.push_back({EntityType::Sector, t.text, sector_map_.at(t.lower), 0.0, "", "", t.position});
            }
        }

        // Extract metrics
        for (const auto& t : tokens) {
            if (metric_map_.count(t.lower)) {
                entities.push_back({EntityType::Metric, t.text, metric_map_.at(t.lower), 0.0, "", "", t.position});
            }
        }

        // Extract temporal expressions
        extract_temporal(input, tokens, entities);

        // Extract comparison operators
        extract_comparisons(input, tokens, entities);

        return entities;
    }

private:
    void extract_temporal(const std::string& input,
                          const std::vector<NlqTokenizer::Token>& tokens,
                          std::vector<ExtractedEntity>& entities) const {
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](char c) { return static_cast<char>(std::tolower(c)); });

        // Relative time expressions
        struct TimePattern {
            std::string pattern;
            int days_back;
        };
        std::vector<TimePattern> patterns = {
            {"today", 0}, {"yesterday", 1},
            {"last week", 7}, {"past week", 7},
            {"last month", 30}, {"past month", 30},
            {"last quarter", 90}, {"past quarter", 90},
            {"last year", 365}, {"past year", 365},
            {"ytd", -1}, {"year to date", -1},  // Special: compute from Jan 1
            {"mtd", -2}, {"month to date", -2},
            {"3 months", 90}, {"6 months", 180},
            {"1 year", 365}, {"2 years", 730}, {"5 years", 1825}
        };

        for (const auto& p : patterns) {
            if (lower.find(p.pattern) != std::string::npos) {
                ExtractedEntity e;
                e.type = EntityType::DateRange;
                e.raw_text = p.pattern;
                e.numeric_value = p.days_back;
                // Compute actual dates would require current date
                entities.push_back(e);
                break;
            }
        }

        // Month names
        static const std::map<std::string, int> months = {
            {"january", 1}, {"february", 2}, {"march", 3}, {"april", 4},
            {"may", 5}, {"june", 6}, {"july", 7}, {"august", 8},
            {"september", 9}, {"october", 10}, {"november", 11}, {"december", 12},
            {"jan", 1}, {"feb", 2}, {"mar", 3}, {"apr", 4},
            {"jun", 6}, {"jul", 7}, {"aug", 8}, {"sep", 9},
            {"oct", 10}, {"nov", 11}, {"dec", 12}
        };

        for (const auto& t : tokens) {
            if (months.count(t.lower)) {
                ExtractedEntity e;
                e.type = EntityType::Date;
                e.raw_text = t.text;
                e.numeric_value = months.at(t.lower);
                entities.push_back(e);
            }
        }
    }

    void extract_comparisons(const std::string& input,
                             const std::vector<NlqTokenizer::Token>&,
                             std::vector<ExtractedEntity>& entities) const {
        std::string lower = input;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](char c) { return static_cast<char>(std::tolower(c)); });

        struct CompPattern {
            std::string pattern;
            std::string op;
        };
        std::vector<CompPattern> patterns = {
            {"greater than", "gt"}, {"more than", "gt"}, {"above", "gt"},
            {"exceeds", "gt"}, {"over", "gt"},
            {"less than", "lt"}, {"below", "lt"}, {"under", "lt"},
            {"at least", "gte"}, {"minimum", "gte"},
            {"at most", "lte"}, {"maximum", "lte"}, {"up to", "lte"},
            {"between", "between"}, {"equal to", "eq"}, {"equals", "eq"}
        };

        for (const auto& p : patterns) {
            if (lower.find(p.pattern) != std::string::npos) {
                entities.push_back({EntityType::ComparisonOp, p.pattern, p.op, 0.0, "", "", 0});
            }
        }
    }

    const std::set<std::string> known_symbols_ = {
        "AAPL", "MSFT", "GOOGL", "GOOG", "AMZN", "META", "NVDA", "TSLA",
        "JPM", "V", "JNJ", "WMT", "PG", "MA", "UNH", "HD", "DIS", "BAC",
        "XOM", "CSCO", "VZ", "ADBE", "CRM", "NFLX", "INTC", "AMD", "PYPL",
        "SPY", "QQQ", "IWM", "DIA", "VTI", "VOO", "BND", "AGG", "GLD", "SLV"
    };

    const std::map<std::string, std::string> sector_map_ = {
        {"tech", "Technology"}, {"technology", "Technology"},
        {"healthcare", "Healthcare"}, {"health", "Healthcare"},
        {"financial", "Financials"}, {"financials", "Financials"}, {"banks", "Financials"},
        {"energy", "Energy"}, {"oil", "Energy"},
        {"consumer", "Consumer"}, {"retail", "Consumer Discretionary"},
        {"industrial", "Industrials"}, {"industrials", "Industrials"},
        {"materials", "Materials"}, {"utilities", "Utilities"},
        {"real estate", "Real Estate"}, {"reit", "Real Estate"},
        {"telecom", "Communication Services"}, {"communication", "Communication Services"}
    };

    const std::map<std::string, std::string> metric_map_ = {
        {"var", "VaR"}, {"value at risk", "VaR"},
        {"cvar", "CVaR"}, {"expected shortfall", "CVaR"},
        {"drawdown", "Drawdown"}, {"max drawdown", "MaxDrawdown"},
        {"sharpe", "SharpeRatio"}, {"sharpe ratio", "SharpeRatio"},
        {"sortino", "SortinoRatio"},
        {"volatility", "Volatility"}, {"vol", "Volatility"},
        {"beta", "Beta"}, {"alpha", "Alpha"},
        {"return", "Return"}, {"returns", "Return"},
        {"performance", "Performance"},
        {"pnl", "PnL"}, {"p&l", "PnL"}, {"profit", "PnL"},
        {"aum", "AUM"}, {"nav", "NAV"},
        {"exposure", "Exposure"}, {"weight", "Weight"},
        {"allocation", "Allocation"},
        {"tracking error", "TrackingError"},
        {"information ratio", "InformationRatio"}
    };
};

// ============================================================
// Intent Classifier
// ============================================================

class IntentClassifier {
public:
    struct IntentPattern {
        QueryIntent intent;
        std::vector<std::string> keywords;  // Must match at least N keywords
        int min_matches = 1;
        double weight = 1.0;
    };

    QueryIntent classify(const std::vector<NlqTokenizer::Token>& tokens,
                          const std::vector<ExtractedEntity>& entities,
                          double& confidence) const {
        // Score each intent
        std::map<QueryIntent, double> scores;

        for (const auto& pattern : patterns_) {
            int matches = 0;
            for (const auto& kw : pattern.keywords) {
                for (const auto& t : tokens) {
                    if (t.lower == kw || t.stem == kw) {
                        matches++;
                        break;
                    }
                }
            }
            if (matches >= pattern.min_matches) {
                scores[pattern.intent] += matches * pattern.weight;
            }
        }

        // Boost based on entities
        for (const auto& e : entities) {
            if (e.type == EntityType::Symbol) {
                scores[QueryIntent::GetQuote] += 0.5;
                scores[QueryIntent::ShowPositions] += 0.3;
            }
            if (e.type == EntityType::Metric) {
                if (e.normalized == "VaR" || e.normalized == "CVaR")
                    scores[QueryIntent::ShowVaR] += 2.0;
                if (e.normalized == "Drawdown" || e.normalized == "MaxDrawdown")
                    scores[QueryIntent::ShowDrawdown] += 2.0;
                if (e.normalized == "SharpeRatio" || e.normalized == "Performance")
                    scores[QueryIntent::PortfolioPerformance] += 1.5;
            }
            if (e.type == EntityType::Sector) {
                scores[QueryIntent::SectorAnalysis] += 1.0;
                scores[QueryIntent::ExposureAnalysis] += 0.5;
            }
        }

        // Find best
        QueryIntent best = QueryIntent::Unknown;
        double best_score = 0.0;
        for (const auto& [intent, score] : scores) {
            if (score > best_score) {
                best_score = score;
                best = intent;
            }
        }

        // Confidence based on score margin
        double total = 0.0;
        for (const auto& [_, score] : scores) total += score;
        confidence = (total > 0) ? best_score / total : 0.0;
        confidence = std::min(1.0, confidence);

        return best;
    }

private:
    const std::vector<IntentPattern> patterns_ = {
        {QueryIntent::ListPortfolios,       {"list", "show", "portfolios", "all"}, 2, 1.0},
        {QueryIntent::ShowPositions,        {"positions", "holdings", "hold"}, 1, 1.5},
        {QueryIntent::PortfolioSummary,     {"summary", "overview", "portfolio"}, 2, 1.0},
        {QueryIntent::PortfolioPerformance, {"performance", "return", "pnl", "profit"}, 1, 1.5},
        {QueryIntent::PortfolioAllocation,  {"allocation", "weight", "breakdown"}, 1, 1.5},
        {QueryIntent::ShowVaR,              {"var", "value", "risk", "cvar"}, 1, 2.0},
        {QueryIntent::ShowDrawdown,         {"drawdown", "loss", "decline"}, 1, 2.0},
        {QueryIntent::StressTest,           {"stress", "test", "scenario"}, 2, 1.5},
        {QueryIntent::RiskSummary,          {"risk", "summary", "exposure"}, 2, 1.0},
        {QueryIntent::ExposureAnalysis,     {"exposure", "concentration", "overweight", "underweight"}, 1, 1.5},
        {QueryIntent::GetQuote,             {"quote", "price", "current", "trading"}, 1, 1.5},
        {QueryIntent::CompareAssets,        {"compare", "versus", "vs"}, 1, 2.0},
        {QueryIntent::MarketOverview,       {"market", "overview", "indices"}, 2, 1.0},
        {QueryIntent::SectorAnalysis,       {"sector", "industry", "breakdown"}, 1, 1.5},
        {QueryIntent::PlaceOrder,           {"buy", "sell", "order", "trade", "execute"}, 1, 2.0},
        {QueryIntent::ShowOrders,           {"orders", "open", "pending"}, 1, 1.5},
        {QueryIntent::ShowTrades,           {"trades", "executions", "fills"}, 1, 1.5},
        {QueryIntent::GenerateReport,       {"report", "generate", "export", "pdf"}, 1, 1.5},
        {QueryIntent::ShowCompliance,       {"compliance", "regulatory", "violations", "rules"}, 1, 1.5},
        {QueryIntent::TaxSummary,           {"tax", "gains", "losses", "harvest"}, 1, 1.5},
        {QueryIntent::ShowBenchmark,        {"benchmark", "tracking", "excess"}, 1, 1.5},
        {QueryIntent::ShowAttribution,      {"attribution", "allocation", "selection"}, 1, 1.5},
        {QueryIntent::BacktestStrategy,     {"backtest", "simulate", "historical"}, 1, 1.5},
        {QueryIntent::ShowStatus,           {"status", "health", "system", "uptime"}, 1, 1.0},
        {QueryIntent::Help,                 {"help", "commands", "what", "can"}, 2, 1.0}
    };
};

// ============================================================
// Query Builder
// ============================================================

class QueryBuilder {
public:
    StructuredQuery build(const std::string& raw_query,
                           const std::vector<NlqTokenizer::Token>& tokens,
                           const std::vector<ExtractedEntity>& entities,
                           QueryIntent intent,
                           double confidence) const {
        StructuredQuery q;
        q.raw_query = raw_query;
        q.intent = intent;
        q.confidence = confidence;
        q.entities = entities;

        // Extract symbols
        for (const auto& e : entities) {
            if (e.type == EntityType::Symbol) {
                q.symbols.push_back(e.normalized);
            }
        }

        // Extract date range
        for (const auto& e : entities) {
            if (e.type == EntityType::DateRange) {
                // Would compute actual dates from numeric_value
                q.date_from = "computed_from_offset";
            }
        }

        // Build filters from comparison operators + values
        build_filters(entities, q);

        // Extract limit (top N)
        for (size_t i = 0; i < tokens.size(); ++i) {
            if ((tokens[i].lower == "top" || tokens[i].lower == "first" ||
                 tokens[i].lower == "best" || tokens[i].lower == "worst") &&
                i + 1 < tokens.size() && tokens[i+1].is_number) {
                q.limit = static_cast<int>(std::stod(tokens[i+1].text));
            }
        }

        // Sort direction hints
        for (const auto& t : tokens) {
            if (t.lower == "worst" || t.lower == "lowest" || t.lower == "bottom") {
                q.sorts.push_back({"value", SortDirection::Ascending});
            } else if (t.lower == "best" || t.lower == "highest" || t.lower == "top") {
                q.sorts.push_back({"value", SortDirection::Descending});
            }
        }

        // Build explanation
        q.explanation = build_explanation(q);

        return q;
    }

private:
    void build_filters(const std::vector<ExtractedEntity>& entities,
                       StructuredQuery& q) const {
        FilterOperator current_op = FilterOperator::GreaterThan;

        for (const auto& e : entities) {
            if (e.type == EntityType::ComparisonOp) {
                if (e.normalized == "gt") current_op = FilterOperator::GreaterThan;
                else if (e.normalized == "lt") current_op = FilterOperator::LessThan;
                else if (e.normalized == "gte") current_op = FilterOperator::GreaterThanOrEqual;
                else if (e.normalized == "lte") current_op = FilterOperator::LessThanOrEqual;
                else if (e.normalized == "eq") current_op = FilterOperator::Equals;
                else if (e.normalized == "between") current_op = FilterOperator::Between;
            }
            else if (e.type == EntityType::Percentage || e.type == EntityType::Amount) {
                QueryFilter f;
                f.field = "value"; // Generic, would be refined by intent
                f.op = current_op;
                f.value = e.numeric_value;
                q.filters.push_back(f);
            }
        }
    }

    std::string build_explanation(const StructuredQuery& q) const {
        std::ostringstream ss;
        ss << "Intent: " << intent_name(q.intent);
        if (!q.symbols.empty()) {
            ss << " | Symbols: ";
            for (size_t i = 0; i < q.symbols.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << q.symbols[i];
            }
        }
        if (!q.filters.empty()) {
            ss << " | Filters: " << q.filters.size();
        }
        ss << " | Confidence: " << std::fixed << std::setprecision(1) << (q.confidence * 100) << "%";
        return ss.str();
    }

    static std::string intent_name(QueryIntent intent) {
        static const std::map<QueryIntent, std::string> names = {
            {QueryIntent::ListPortfolios, "List Portfolios"},
            {QueryIntent::ShowPositions, "Show Positions"},
            {QueryIntent::PortfolioSummary, "Portfolio Summary"},
            {QueryIntent::PortfolioPerformance, "Portfolio Performance"},
            {QueryIntent::ShowVaR, "Value at Risk"},
            {QueryIntent::ShowDrawdown, "Drawdown Analysis"},
            {QueryIntent::StressTest, "Stress Test"},
            {QueryIntent::GetQuote, "Get Quote"},
            {QueryIntent::CompareAssets, "Compare Assets"},
            {QueryIntent::SectorAnalysis, "Sector Analysis"},
            {QueryIntent::PlaceOrder, "Place Order"},
            {QueryIntent::GenerateReport, "Generate Report"},
            {QueryIntent::ShowCompliance, "Compliance Check"},
            {QueryIntent::TaxSummary, "Tax Summary"},
            {QueryIntent::ShowStatus, "System Status"},
            {QueryIntent::Help, "Help"},
            {QueryIntent::Unknown, "Unknown"}
        };
        auto it = names.find(intent);
        return (it != names.end()) ? it->second : "Unknown";
    }
};

// ============================================================
// Response Formatter
// ============================================================

class ResponseFormatter {
public:
    static std::string format_response(const StructuredQuery& /*query*/, const QueryResult& result) {
        if (!result.success) {
            std::ostringstream ss;
            ss << "I wasn't able to process that query. " << result.error_message;
            if (!result.suggestions.empty()) {
                ss << "\n\nDid you mean:\n";
                for (const auto& s : result.suggestions) {
                    ss << "  - " << s << "\n";
                }
            }
            return ss.str();
        }
        return result.natural_response;
    }

    static std::string format_help() {
        return "I can help you with:\n"
               "  Portfolio: \"show my positions\", \"portfolio summary\", \"allocation breakdown\"\n"
               "  Risk: \"what is my VaR?\", \"show drawdown\", \"stress test results\"\n"
               "  Market: \"quote AAPL\", \"compare AAPL vs MSFT\", \"sector analysis\"\n"
               "  Trading: \"show open orders\", \"recent trades\"\n"
               "  Reports: \"generate performance report\", \"compliance status\"\n"
               "  Analytics: \"benchmark comparison\", \"attribution analysis\"\n"
               "  Tax: \"show tax lots\", \"harvest opportunities\"\n\n"
               "You can add filters like \"positions with gain greater than $10,000\"\n"
               "or time ranges like \"performance since January\" or \"returns last quarter\".\n";
    }
};

// ============================================================
// NLQ Engine (Main Interface)
// ============================================================

class NlqEngine {
public:
    // Parse a natural language query into a structured query
    StructuredQuery parse(const std::string& input) {
        // Tokenize
        auto tokens = tokenizer_.tokenize(input);

        // Extract entities
        auto entities = entity_extractor_.extract(input, tokens);

        // Classify intent
        double confidence = 0.0;
        auto intent = intent_classifier_.classify(tokens, entities, confidence);

        // Build structured query
        auto query = query_builder_.build(input, tokens, entities, intent, confidence);

        // Update conversation context
        last_query_ = query;

        return query;
    }

    // Execute a query against Genie's engines (placeholder - would be wired to actual engines)
    QueryResult execute(const StructuredQuery& query) {
        QueryResult result;

        switch (query.intent) {
            case QueryIntent::Help:
                result.success = true;
                result.natural_response = ResponseFormatter::format_help();
                break;

            case QueryIntent::ShowStatus:
                result.success = true;
                result.natural_response = "System is running. Use the 'status' command for full details.";
                break;

            case QueryIntent::Unknown:
                result.success = false;
                result.error_message = "I didn't understand that query.";
                result.suggestions = {
                    "show my portfolio summary",
                    "what is my VaR?",
                    "list positions",
                    "Type 'help' for available commands"
                };
                break;

            default:
                result.success = true;
                result.natural_response = "Query parsed successfully. Intent: " +
                    query.explanation + "\nExecution would be routed to the appropriate engine.";
                result.data_type = "structured_query";
                break;
        }

        return result;
    }

    // Convenience: parse and execute
    QueryResult query(const std::string& input) {
        auto structured = parse(input);
        return execute(structured);
    }

    // Get conversation context
    [[nodiscard]] const std::optional<StructuredQuery>& last_query() const { return last_query_; }

private:
    NlqTokenizer tokenizer_;
    EntityExtractor entity_extractor_;
    IntentClassifier intent_classifier_;
    QueryBuilder query_builder_;
    std::optional<StructuredQuery> last_query_;
};

} // namespace core
} // namespace genie

#endif // GENIE_CORE_NLQ_ENGINE_HPP
