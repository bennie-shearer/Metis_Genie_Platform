/**
 * @file sec_edgar.hpp
 * @brief SEC EDGAR API client for filing retrieval
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * SEC EDGAR integration providing:
 * - Company filing search and retrieval
 * - 10-K, 10-Q, 8-K filing access
 * - Insider transaction filings (Form 4)
 * - Institutional holdings (13F)
 * - Company facts and financials
 * - XBRL data extraction
 * 
 * API Documentation: https://www.sec.gov/edgar/sec-api-documentation
 */
#pragma once
#ifndef GENIE_MARKET_SEC_EDGAR_HPP
#define GENIE_MARKET_SEC_EDGAR_HPP

#include "../core/http_client.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <regex>

namespace genie::market {

/**
 * @brief SEC EDGAR configuration
 */
struct EDGARConfig {
    std::string user_agent;           // Required: "Company Name email@domain.com"
    int timeout_ms{30000};
    int rate_limit_ms{100};           // SEC requires 10 requests/second max
    
    static constexpr const char* BASE_URL = "https://data.sec.gov";
    static constexpr const char* EDGAR_URL = "https://www.sec.gov/cgi-bin/browse-edgar";
    static constexpr const char* FULL_TEXT_URL = "https://efts.sec.gov/LATEST/search-index";
    
    bool is_valid() const {
        return !user_agent.empty() && 
               user_agent.find("@") != std::string::npos;
    }
};

/**
 * @brief SEC filing type
 */
enum class FilingType {
    Form10K,          // Annual report
    Form10Q,          // Quarterly report
    Form8K,           // Current report (material events)
    Form4,            // Insider transaction
    Form13F,          // Institutional holdings
    FormDEF14A,       // Proxy statement
    FormS1,           // Registration statement
    Form3,            // Initial insider ownership
    Form5,            // Annual insider ownership
    FormSC13G,        // Beneficial ownership
    FormSC13D,        // Beneficial ownership (activist)
    Other
};

inline std::string filing_type_to_string(FilingType type) {
    switch (type) {
        case FilingType::Form10K: return "10-K";
        case FilingType::Form10Q: return "10-Q";
        case FilingType::Form8K: return "8-K";
        case FilingType::Form4: return "4";
        case FilingType::Form13F: return "13F-HR";
        case FilingType::FormDEF14A: return "DEF 14A";
        case FilingType::FormS1: return "S-1";
        case FilingType::Form3: return "3";
        case FilingType::Form5: return "5";
        case FilingType::FormSC13G: return "SC 13G";
        case FilingType::FormSC13D: return "SC 13D";
        default: return "Other";
    }
}

inline FilingType string_to_filing_type(const std::string& s) {
    if (s.find("10-K") != std::string::npos) return FilingType::Form10K;
    if (s.find("10-Q") != std::string::npos) return FilingType::Form10Q;
    if (s.find("8-K") != std::string::npos) return FilingType::Form8K;
    if (s == "4") return FilingType::Form4;
    if (s.find("13F") != std::string::npos) return FilingType::Form13F;
    if (s.find("DEF 14A") != std::string::npos) return FilingType::FormDEF14A;
    if (s.find("S-1") != std::string::npos) return FilingType::FormS1;
    if (s == "3") return FilingType::Form3;
    if (s == "5") return FilingType::Form5;
    if (s.find("SC 13G") != std::string::npos) return FilingType::FormSC13G;
    if (s.find("SC 13D") != std::string::npos) return FilingType::FormSC13D;
    return FilingType::Other;
}

/**
 * @brief SEC filing metadata
 */
struct EDGARFiling {
    std::string accession_number;     // Unique filing ID
    std::string cik;                  // Company CIK
    std::string company_name;
    FilingType form_type;
    std::string form_type_str;
    std::string filing_date;
    std::string report_date;
    std::string primary_document;     // Main document filename
    std::string description;
    int file_count{0};
    int64_t size{0};
    bool is_xbrl{false};
    bool is_inline_xbrl{false};
    
    std::string filing_url() const {
        // Build SEC EDGAR URL
        std::string acc_no = accession_number;
        // Remove dashes for URL
        acc_no.erase(std::remove(acc_no.begin(), acc_no.end(), '-'), acc_no.end());
        
        std::ostringstream oss;
        oss << "https://www.sec.gov/Archives/edgar/data/"
            << cik << "/" << acc_no << "/" << primary_document;
        return oss.str();
    }
    
    std::string index_url() const {
        std::string acc_no = accession_number;
        acc_no.erase(std::remove(acc_no.begin(), acc_no.end(), '-'), acc_no.end());
        
        std::ostringstream oss;
        oss << "https://www.sec.gov/Archives/edgar/data/"
            << cik << "/" << acc_no << "/index.json";
        return oss.str();
    }
};

/**
 * @brief Company info from SEC
 */
struct EDGARCompany {
    std::string cik;
    std::string name;
    std::string ticker;
    std::string sic;                  // Industry code
    std::string sic_description;
    std::string state;
    std::string state_of_inc;
    std::string fiscal_year_end;      // MMDD format
    std::string business_address;
    std::string mailing_address;
    std::string phone;
    
    int fiscal_year_end_month() const {
        if (fiscal_year_end.length() >= 2) {
            try {
                return std::stoi(fiscal_year_end.substr(0, 2));
            } catch (...) {}
        }
        return 12;  // Default to December
    }
};

/**
 * @brief Insider transaction from Form 4
 */
struct EDGARInsiderTransaction {
    std::string filing_date;
    std::string transaction_date;
    std::string cik;
    std::string issuer_cik;
    std::string issuer_name;
    std::string issuer_ticker;
    std::string owner_name;
    std::string owner_cik;
    bool is_director{false};
    bool is_officer{false};
    bool is_ten_percent_owner{false};
    bool is_other{false};
    std::string officer_title;
    
    // Transaction details
    std::string security_title;
    std::string transaction_code;     // A=Award, P=Purchase, S=Sale, etc.
    double shares{0};
    double price_per_share{0};
    std::string acquired_disposed;    // A=Acquired, D=Disposed
    double shares_owned_after{0};
    std::string ownership_type;       // D=Direct, I=Indirect
    
    bool is_purchase() const { return transaction_code == "P"; }
    bool is_sale() const { return transaction_code == "S"; }
    bool is_award() const { return transaction_code == "A"; }
    
    double transaction_value() const { return shares * price_per_share; }
};

/**
 * @brief 13F holding from institutional investor
 */
struct EDGAR13FHolding {
    std::string name_of_issuer;
    std::string title_of_class;
    std::string cusip;
    double value{0};                  // In thousands
    double shares{0};
    std::string put_call;             // PUT, CALL, or empty
    std::string investment_discretion;  // SOLE, SHARED, NONE
    std::string voting_authority_sole{0};
    std::string voting_authority_shared{0};
    std::string voting_authority_none{0};
};

/**
 * @brief Company financial fact
 */
struct EDGARFact {
    std::string label;
    std::string description;
    std::string unit;
    
    struct Value {
        std::string end_date;
        std::string start_date;       // For duration facts
        double val{0};
        std::string accession_number;
        std::string form;
        std::string fiscal_year;
        std::string fiscal_period;    // FY, Q1, Q2, Q3, Q4
        bool is_instant{true};        // vs duration
    };
    
    std::vector<Value> values;
};

/**
 * @brief SEC EDGAR API client
 */
class EDGARClient {
public:
    explicit EDGARClient(const EDGARConfig& config)
        : config_(config)
        , http_(EDGARConfig::BASE_URL) {
        
        http_.set_timeout(config.timeout_ms);
        http_.set_header("User-Agent", config.user_agent);
    }
    
    // ========================================================================
    // Company Lookup
    // ========================================================================
    
    /**
     * @brief Get CIK from ticker
     */
    std::string ticker_to_cik(const std::string& ticker) {
        auto response = make_request("/submissions/company_tickers.json");
        if (!response) return "";
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return "";
        
        std::string upper_ticker = to_upper(ticker);
        
        // Search through company tickers
        for (int i = 0; ; ++i) {
            std::string idx = std::to_string(i);
            if (!json.contains(idx)) break;
            
            const auto& entry = json[idx];
            if (to_upper(entry.get_string("ticker", "")) == upper_ticker) {
                int cik_int = entry.get_int("cik_str", 0);
                return std::to_string(cik_int);
            }
        }
        
        return "";
    }
    
    /**
     * @brief Get company info
     */
    std::optional<EDGARCompany> get_company(const std::string& cik_or_ticker) {
        std::string cik = normalize_cik(cik_or_ticker);
        if (cik.empty()) {
            // Try as ticker
            cik = ticker_to_cik(cik_or_ticker);
        }
        if (cik.empty()) return std::nullopt;
        
        auto response = make_request("/submissions/CIK" + pad_cik(cik) + ".json");
        if (!response) return std::nullopt;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object()) return std::nullopt;
        
        EDGARCompany company;
        company.cik = cik;
        company.name = json.get_string("name", "");
        
        if (json.contains("tickers") && json["tickers"].is_array() && 
            !json["tickers"].array().empty()) {
            company.ticker = json["tickers"].array()[0].to_string();
        }
        
        company.sic = json.get_string("sic", "");
        company.sic_description = json.get_string("sicDescription", "");
        company.state = json.get_string("stateOfIncorporation", "");
        company.fiscal_year_end = json.get_string("fiscalYearEnd", "");
        
        if (json.contains("addresses")) {
            if (json["addresses"].contains("business")) {
                company.business_address = format_address(json["addresses"]["business"]);
            }
            if (json["addresses"].contains("mailing")) {
                company.mailing_address = format_address(json["addresses"]["mailing"]);
            }
        }
        
        company.phone = json.get_string("phone", "");
        
        return company;
    }
    
    // ========================================================================
    // Filing Search
    // ========================================================================
    
    /**
     * @brief Get recent filings for a company
     */
    std::vector<EDGARFiling> get_filings(
        const std::string& cik_or_ticker,
        int count = 40,
        const std::string& form_type = "") {
        
        std::vector<EDGARFiling> result;
        
        std::string cik = normalize_cik(cik_or_ticker);
        if (cik.empty()) {
            cik = ticker_to_cik(cik_or_ticker);
        }
        if (cik.empty()) return result;
        
        auto response = make_request("/submissions/CIK" + pad_cik(cik) + ".json");
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object() || !json.contains("filings")) return result;
        
        const auto& filings = json["filings"];
        if (!filings.contains("recent")) return result;
        
        const auto& recent = filings["recent"];
        
        // Get arrays
        auto acc_nums = recent.contains("accessionNumber") ? recent["accessionNumber"].array() : std::vector<core::JsonValue>();
        auto forms = recent.contains("form") ? recent["form"].array() : std::vector<core::JsonValue>();
        auto dates = recent.contains("filingDate") ? recent["filingDate"].array() : std::vector<core::JsonValue>();
        auto primary_docs = recent.contains("primaryDocument") ? recent["primaryDocument"].array() : std::vector<core::JsonValue>();
        auto descriptions = recent.contains("primaryDocDescription") ? recent["primaryDocDescription"].array() : std::vector<core::JsonValue>();
        auto report_dates = recent.contains("reportDate") ? recent["reportDate"].array() : std::vector<core::JsonValue>();
        auto is_xbrl = recent.contains("isXBRL") ? recent["isXBRL"].array() : std::vector<core::JsonValue>();
        auto is_inline_xbrl = recent.contains("isInlineXBRL") ? recent["isInlineXBRL"].array() : std::vector<core::JsonValue>();
        
        size_t n = std::min({acc_nums.size(), forms.size(), dates.size()});
        
        for (size_t i = 0; i < n && result.size() < static_cast<size_t>(count); ++i) {
            std::string form = forms[i].to_string();
            
            // Filter by form type if specified
            if (!form_type.empty() && form.find(form_type) == std::string::npos) {
                continue;
            }
            
            EDGARFiling filing;
            filing.cik = cik;
            filing.company_name = json.get_string("name", "");
            filing.accession_number = acc_nums[i].to_string();
            filing.form_type_str = form;
            filing.form_type = string_to_filing_type(form);
            filing.filing_date = dates[i].to_string();
            
            if (i < primary_docs.size()) {
                filing.primary_document = primary_docs[i].to_string();
            }
            if (i < descriptions.size()) {
                filing.description = descriptions[i].to_string();
            }
            if (i < report_dates.size()) {
                filing.report_date = report_dates[i].to_string();
            }
            if (i < is_xbrl.size()) {
                filing.is_xbrl = is_xbrl[i].get_bool(false) || is_xbrl[i].get_int(0) == 1;
            }
            if (i < is_inline_xbrl.size()) {
                filing.is_inline_xbrl = is_inline_xbrl[i].get_bool(false) || is_inline_xbrl[i].get_int(0) == 1;
            }
            
            result.push_back(filing);
        }
        
        return result;
    }
    
    /**
     * @brief Get specific filing types
     */
    std::vector<EDGARFiling> get_10k_filings(const std::string& cik_or_ticker, int count = 5) {
        return get_filings(cik_or_ticker, count, "10-K");
    }
    
    std::vector<EDGARFiling> get_10q_filings(const std::string& cik_or_ticker, int count = 8) {
        return get_filings(cik_or_ticker, count, "10-Q");
    }
    
    std::vector<EDGARFiling> get_8k_filings(const std::string& cik_or_ticker, int count = 20) {
        return get_filings(cik_or_ticker, count, "8-K");
    }
    
    std::vector<EDGARFiling> get_insider_filings(const std::string& cik_or_ticker, int count = 50) {
        return get_filings(cik_or_ticker, count, "4");
    }
    
    // ========================================================================
    // Company Facts (XBRL Financial Data)
    // ========================================================================
    
    /**
     * @brief Get company facts (XBRL data)
     */
    std::map<std::string, EDGARFact> get_company_facts(const std::string& cik_or_ticker) {
        std::map<std::string, EDGARFact> result;
        
        std::string cik = normalize_cik(cik_or_ticker);
        if (cik.empty()) {
            cik = ticker_to_cik(cik_or_ticker);
        }
        if (cik.empty()) return result;
        
        auto response = make_request("/api/xbrl/companyfacts/CIK" + pad_cik(cik) + ".json");
        if (!response) return result;
        
        auto json = core::JsonParser::parse(*response);
        if (!json.is_object() || !json.contains("facts")) return result;
        
        const auto& facts = json["facts"];
        
        // Parse US-GAAP facts
        if (facts.contains("us-gaap")) {
            parse_facts(facts["us-gaap"], "us-gaap", result);
        }
        
        // Parse DEI facts
        if (facts.contains("dei")) {
            parse_facts(facts["dei"], "dei", result);
        }
        
        return result;
    }
    
    /**
     * @brief Get specific financial metric
     */
    std::vector<EDGARFact::Value> get_fact(
        const std::string& cik_or_ticker,
        const std::string& fact_name) {
        
        auto facts = get_company_facts(cik_or_ticker);
        auto it = facts.find(fact_name);
        if (it != facts.end()) {
            return it->second.values;
        }
        return {};
    }
    
    /**
     * @brief Get revenue history
     */
    std::vector<EDGARFact::Value> get_revenue(const std::string& cik_or_ticker) {
        return get_fact(cik_or_ticker, "Revenues");
    }
    
    /**
     * @brief Get net income history
     */
    std::vector<EDGARFact::Value> get_net_income(const std::string& cik_or_ticker) {
        return get_fact(cik_or_ticker, "NetIncomeLoss");
    }
    
    /**
     * @brief Get total assets
     */
    std::vector<EDGARFact::Value> get_total_assets(const std::string& cik_or_ticker) {
        return get_fact(cik_or_ticker, "Assets");
    }
    
    /**
     * @brief Get EPS
     */
    std::vector<EDGARFact::Value> get_eps(const std::string& cik_or_ticker) {
        return get_fact(cik_or_ticker, "EarningsPerShareBasic");
    }
    
    // ========================================================================
    // Filing Content
    // ========================================================================
    
    /**
     * @brief Get filing document content
     */
    std::optional<std::string> get_filing_content(const EDGARFiling& filing) {
        std::string acc_no = filing.accession_number;
        acc_no.erase(std::remove(acc_no.begin(), acc_no.end(), '-'), acc_no.end());
        
        std::string url = "/Archives/edgar/data/" + filing.cik + "/" + 
                         acc_no + "/" + filing.primary_document;
        
        // Use SEC archives directly
        core::HttpClient archives_http("https://www.sec.gov");
        archives_http.set_timeout(config_.timeout_ms);
        archives_http.set_header("User-Agent", config_.user_agent);
        
        auto response = archives_http.get(url);
        if (!response.success || response.status_code != 200) {
            last_error_ = response.error;
            return std::nullopt;
        }
        
        return response.body;
    }
    
    // ========================================================================
    // Status
    // ========================================================================
    
    const EDGARConfig& config() const { return config_; }
    const std::string& last_error() const { return last_error_; }

private:
    EDGARConfig config_;
    core::HttpClient http_;
    std::string last_error_;
    
    std::optional<std::string> make_request(const std::string& endpoint) {
        // Rate limiting
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.rate_limit_ms));
        
        auto response = http_.get(endpoint);
        
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
    
    std::string normalize_cik(const std::string& input) {
        // Check if it's a CIK (numeric)
        bool is_numeric = !input.empty() && std::all_of(input.begin(), input.end(), ::isdigit);
        return is_numeric ? input : "";
    }
    
    std::string pad_cik(const std::string& cik) {
        // Pad CIK to 10 digits
        std::string padded = cik;
        while (padded.length() < 10) {
            padded = "0" + padded;
        }
        return padded;
    }
    
    std::string to_upper(const std::string& s) {
        std::string result = s;
        for (char& c : result) {
            c = std::toupper(static_cast<unsigned char>(c));
        }
        return result;
    }
    
    std::string format_address(const core::JsonValue& addr) {
        std::ostringstream oss;
        oss << addr.get_string("street1", "");
        if (auto street2 = addr.get_string("street2", ""); !street2.empty()) {
            oss << ", " << street2;
        }
        oss << ", " << addr.get_string("city", "")
            << ", " << addr.get_string("stateOrCountry", "")
            << " " << addr.get_string("zipCode", "");
        return oss.str();
    }
    
    void parse_facts(const core::JsonValue& namespace_facts,
                    [[maybe_unused]] const std::string& ns,
                    std::map<std::string, EDGARFact>& result) {
        
        if (!namespace_facts.is_object()) return;
        
        for (const auto& [fact_name, fact_data] : namespace_facts.object()) {
            EDGARFact fact;
            fact.label = fact_data.get_string("label", fact_name);
            fact.description = fact_data.get_string("description", "");
            
            if (fact_data.contains("units")) {
                for (const auto& [unit, values] : fact_data["units"].object()) {
                    fact.unit = unit;
                    
                    if (values.is_array()) {
                        for (const auto& val : values.array()) {
                            EDGARFact::Value v;
                            v.end_date = val.get_string("end", "");
                            v.start_date = val.get_string("start", "");
                            v.val = val.get_double("val", 0);
                            v.accession_number = val.get_string("accn", "");
                            v.form = val.get_string("form", "");
                            v.fiscal_year = val.get_string("fy", "");
                            v.fiscal_period = val.get_string("fp", "");
                            v.is_instant = v.start_date.empty();
                            
                            fact.values.push_back(v);
                        }
                    }
                }
            }
            
            result[fact_name] = fact;
        }
    }
};

/**
 * @brief Create EDGAR client from environment
 */
inline std::unique_ptr<EDGARClient> create_edgar_client_from_env() {
    EDGARConfig config;
    
    if (const char* ua = std::getenv("SEC_USER_AGENT")) {
        config.user_agent = ua;
    } else {
        // Default - should be replaced with actual company/email
        config.user_agent = "Metis Genie Platform contact@example.com";
    }
    
    if (!config.is_valid()) {
        return nullptr;
    }
    
    return std::make_unique<EDGARClient>(config);
}

} // namespace genie::market

#endif // GENIE_MARKET_SEC_EDGAR_HPP
