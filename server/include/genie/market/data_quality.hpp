/**
 * @file data_quality.hpp
 * @brief Data quality and corporate action handling
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Production-grade data quality management:
 * - Price spike detection and filtering
 * - Corporate action adjustment engine
 * - Split-adjusted price calculation
 * - Dividend-adjusted returns
 * - Data source failover logic
 * - Gap detection and filling
 * - Outlier handling
 */
#pragma once
#ifndef GENIE_MARKET_DATA_QUALITY_HPP
#define GENIE_MARKET_DATA_QUALITY_HPP

#include "price_store.hpp"
#include "alpha_vantage.hpp"
#include "yahoo_finance.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace genie::market {

// ============================================================================
// Price Spike Detection
// ============================================================================

/**
 * @brief Spike detection configuration
 */
struct SpikeConfig {
    double max_daily_change_pct{25.0};   // Max single-day % change
    double max_gap_pct{15.0};            // Max overnight gap %
    double zscore_threshold{4.0};        // Standard deviations for outlier
    int lookback_days{20};               // Days for rolling statistics
    bool use_volume_filter{true};        // Consider volume in detection
    double min_volume_ratio{0.1};        // Min volume vs average
};

/**
 * @brief Detected spike
 */
struct DetectedSpike {
    std::string symbol;
    std::string date;
    double price{0};
    double previous_price{0};
    double change_pct{0};
    double zscore{0};
    double volume{0};
    double avg_volume{0};
    std::string reason;
    bool is_suspicious{true};
    bool is_filtered{false};
};

/**
 * @brief Spike detection result
 */
struct SpikeDetectionResult {
    std::string symbol;
    int total_bars{0};
    int spikes_detected{0};
    int spikes_filtered{0};
    std::vector<DetectedSpike> spikes;
    std::vector<PriceBar> cleaned_data;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "Spike Detection for " << symbol << ":\n";
        oss << "  Total bars: " << total_bars << "\n";
        oss << "  Spikes detected: " << spikes_detected << "\n";
        oss << "  Spikes filtered: " << spikes_filtered << "\n";
        return oss.str();
    }
};

/**
 * @brief Price spike detector
 */
class SpikeDetector {
public:
    explicit SpikeDetector(const SpikeConfig& config = {})
        : config_(config) {}
    
    /**
     * @brief Detect spikes in price series
     */
    SpikeDetectionResult detect(
        const std::string& symbol,
        const std::vector<PriceBar>& bars,
        bool filter_spikes = false) {
        
        SpikeDetectionResult result;
        result.symbol = symbol;
        result.total_bars = static_cast<int>(bars.size());
        
        if (bars.size() < 2) {
            result.cleaned_data = bars;
            return result;
        }
        
        // Calculate rolling statistics
        std::vector<double> returns;
        std::vector<double> volumes;
        
        for (size_t i = 1; i < bars.size(); ++i) {
            if (bars[i-1].close > 0) {
                returns.push_back((bars[i].close - bars[i-1].close) / bars[i-1].close);
            }
            volumes.push_back(static_cast<double>(bars[i].volume));
        }
        
        // Process each bar
        result.cleaned_data.push_back(bars[0]);
        
        for (size_t i = 1; i < bars.size(); ++i) {
            bool is_spike = false;
            DetectedSpike spike;
            spike.symbol = symbol;
            spike.date = bars[i].date;
            spike.price = bars[i].close;
            spike.previous_price = bars[i-1].close;
            spike.volume = static_cast<double>(bars[i].volume);
            
            // Calculate change
            if (bars[i-1].close > 0) {
                spike.change_pct = (bars[i].close - bars[i-1].close) / 
                                   bars[i-1].close * 100;
            }
            
            // Check absolute change
            if (std::abs(spike.change_pct) > config_.max_daily_change_pct) {
                spike.reason = "Exceeds max daily change";
                spike.is_suspicious = true;
                is_spike = true;
            }
            
            // Check overnight gap (open vs previous close)
            if (bars[i-1].close > 0) {
                double gap_pct = (bars[i].open - bars[i-1].close) / 
                                 bars[i-1].close * 100;
                if (std::abs(gap_pct) > config_.max_gap_pct) {
                    spike.reason = "Large overnight gap";
                    spike.is_suspicious = true;
                    is_spike = true;
                }
            }
            
            // Z-score check with rolling window
            if (i >= static_cast<size_t>(config_.lookback_days)) {
                double mean = 0, std_dev = 0;
                int count = 0;
                
                for (size_t j = i - config_.lookback_days; j < i - 1; ++j) {
                    if (j < returns.size()) {
                        mean += returns[j];
                        count++;
                    }
                }
                mean /= count;
                
                for (size_t j = i - config_.lookback_days; j < i - 1; ++j) {
                    if (j < returns.size()) {
                        std_dev += (returns[j] - mean) * (returns[j] - mean);
                    }
                }
                std_dev = std::sqrt(std_dev / (count - 1));
                
                if (std_dev > 0 && i - 1 < returns.size()) {
                    spike.zscore = std::abs((returns[i-1] - mean) / std_dev);
                    if (spike.zscore > config_.zscore_threshold) {
                        spike.reason = "Z-score outlier";
                        spike.is_suspicious = true;
                        is_spike = true;
                    }
                }
            }
            
            // Volume filter
            if (config_.use_volume_filter && i >= static_cast<size_t>(config_.lookback_days)) {
                double avg_vol = 0;
                int count = 0;
                for (size_t j = i - config_.lookback_days; j < i; ++j) {
                    if (j < volumes.size()) {
                        avg_vol += volumes[j];
                        count++;
                    }
                }
                spike.avg_volume = avg_vol / count;
                
                // Suspicious if huge move on low volume
                if (spike.avg_volume > 0 && 
                    spike.volume < spike.avg_volume * config_.min_volume_ratio &&
                    std::abs(spike.change_pct) > 10) {
                    spike.reason = "Large move on low volume";
                    spike.is_suspicious = true;
                    is_spike = true;
                }
            }
            
            if (is_spike) {
                result.spikes.push_back(spike);
                result.spikes_detected++;
                
                if (filter_spikes) {
                    // Replace with interpolated value
                    PriceBar cleaned = bars[i];
                    cleaned.close = bars[i-1].close;
                    cleaned.adjusted_close = bars[i-1].adjusted_close;
                    result.cleaned_data.push_back(cleaned);
                    result.spikes_filtered++;
                    result.spikes.back().is_filtered = true;
                } else {
                    result.cleaned_data.push_back(bars[i]);
                }
            } else {
                result.cleaned_data.push_back(bars[i]);
            }
        }
        
        return result;
    }
    
    const SpikeConfig& config() const { return config_; }
    void set_config(const SpikeConfig& config) { config_ = config; }

private:
    SpikeConfig config_;
};

// ============================================================================
// Corporate Action Engine
// ============================================================================

/**
 * @brief Corporate action type
 */
enum class CorporateActionType {
    Split,
    ReverseSplit,
    CashDividend,
    StockDividend,
    Spinoff,
    Merger,
    SymbolChange,
    Delisting
};

/**
 * @brief Corporate action record
 */
struct DataQualityCorporateAction {
    std::string symbol;
    std::string date;
    CorporateActionType type;
    
    // For splits
    double split_from{1};             // e.g., 1 (for 4:1 split)
    double split_to{1};               // e.g., 4
    
    // For dividends
    double dividend_amount{0};
    std::string dividend_currency{"USD"};
    std::string ex_date;
    std::string record_date;
    std::string payment_date;
    
    // For spinoffs/mergers
    std::string new_symbol;
    double share_ratio{0};
    
    double adjustment_factor() const {
        if (type == CorporateActionType::Split || 
            type == CorporateActionType::ReverseSplit) {
            return split_from / split_to;
        }
        return 1.0;
    }
};

/**
 * @brief Corporate action adjustment engine
 */
class CorporateActionEngine {
public:
    /**
     * @brief Add corporate action
     */
    void add_action(const DataQualityCorporateAction& action) {
        actions_[action.symbol].push_back(action);
        
        // Sort by date descending
        auto& vec = actions_[action.symbol];
        std::sort(vec.begin(), vec.end(),
            [](const DataQualityCorporateAction& a, const DataQualityCorporateAction& b) {
                return a.date > b.date;
            });
    }
    
    /**
     * @brief Import splits from market data
     */
    void import_splits(const std::string& symbol, const std::vector<Split>& splits) {
        for (const auto& split : splits) {
            DataQualityCorporateAction action;
            action.symbol = symbol;
            action.date = split.date;
            // Use factor (numeric) instead of ratio (string) for comparison
            action.type = split.factor > 1 ? 
                CorporateActionType::Split : CorporateActionType::ReverseSplit;
            action.split_from = 1;
            action.split_to = split.factor;
            
            add_action(action);
        }
    }
    
    /**
     * @brief Import dividends from market data
     */
    void import_dividends(const std::string& symbol, const std::vector<Dividend>& dividends) {
        for (const auto& div : dividends) {
            DataQualityCorporateAction action;
            action.symbol = symbol;
            action.date = div.date;
            action.type = CorporateActionType::CashDividend;
            action.dividend_amount = div.amount;
            action.ex_date = div.date;
            
            add_action(action);
        }
    }
    
    /**
     * @brief Get actions for symbol
     */
    std::vector<DataQualityCorporateAction> get_actions(
        const std::string& symbol,
        const std::string& start_date = "",
        const std::string& end_date = "") const {
        
        auto it = actions_.find(symbol);
        if (it == actions_.end()) return {};
        
        std::vector<DataQualityCorporateAction> result;
        for (const auto& action : it->second) {
            if (!start_date.empty() && action.date < start_date) continue;
            if (!end_date.empty() && action.date > end_date) continue;
            result.push_back(action);
        }
        
        return result;
    }
    
    /**
     * @brief Calculate cumulative split factor up to date
     */
    double get_split_factor(const std::string& symbol, const std::string& date) const {
        double factor = 1.0;
        
        auto it = actions_.find(symbol);
        if (it == actions_.end()) return factor;
        
        for (const auto& action : it->second) {
            if (action.date <= date) break;  // Actions after our date
            
            if (action.type == CorporateActionType::Split ||
                action.type == CorporateActionType::ReverseSplit) {
                factor *= action.adjustment_factor();
            }
        }
        
        return factor;
    }
    
    /**
     * @brief Get dividends for period
     */
    double get_total_dividends(const std::string& symbol,
                               const std::string& start_date,
                               const std::string& end_date) const {
        double total = 0;
        
        auto it = actions_.find(symbol);
        if (it == actions_.end()) return total;
        
        for (const auto& action : it->second) {
            if (action.type != CorporateActionType::CashDividend) continue;
            if (action.date < start_date || action.date > end_date) continue;
            total += action.dividend_amount;
        }
        
        return total;
    }

private:
    std::map<std::string, std::vector<DataQualityCorporateAction>> actions_;
};

// ============================================================================
// Price Adjustment
// ============================================================================

/**
 * @brief Adjustment type
 */
enum class AdjustmentType {
    SplitOnly,            // Only split adjustments
    DividendOnly,         // Only dividend adjustments
    Full                  // Both splits and dividends
};

/**
 * @brief Price adjuster
 */
class PriceAdjuster {
public:
    explicit PriceAdjuster(CorporateActionEngine& engine)
        : engine_(engine) {}
    
    /**
     * @brief Adjust prices for corporate actions
     */
    std::vector<PriceBar> adjust(
        const std::string& symbol,
        const std::vector<PriceBar>& bars,
        AdjustmentType type = AdjustmentType::Full) {
        
        if (bars.empty()) return bars;
        
        std::vector<PriceBar> adjusted = bars;
        
        // Sort by date ascending
        std::sort(adjusted.begin(), adjusted.end(),
            [](const PriceBar& a, const PriceBar& b) {
                return a.date < b.date;
            });
        
        // Get actions
        auto actions = engine_.get_actions(symbol, bars.front().date, bars.back().date);
        
        // Apply adjustments from newest to oldest
        for (size_t i = 0; i < adjusted.size(); ++i) {
            double split_factor = 1.0;
            double div_adjustment = 0;
            
            // Calculate split factor for this date
            if (type == AdjustmentType::SplitOnly || type == AdjustmentType::Full) {
                split_factor = engine_.get_split_factor(symbol, adjusted[i].date);
            }
            
            // Calculate dividend adjustment
            if (type == AdjustmentType::DividendOnly || type == AdjustmentType::Full) {
                for (const auto& action : actions) {
                    if (action.type == CorporateActionType::CashDividend &&
                        action.date > adjusted[i].date) {
                        div_adjustment += action.dividend_amount;
                    }
                }
            }
            
            // Apply adjustments
            if (split_factor != 1.0) {
                adjusted[i].open *= split_factor;
                adjusted[i].high *= split_factor;
                adjusted[i].low *= split_factor;
                adjusted[i].close *= split_factor;
                adjusted[i].volume = static_cast<int64_t>(adjusted[i].volume / split_factor);
            }
            
            // Dividend adjustment (subtract from price to get what price would have been)
            if (div_adjustment > 0) {
                double adj_factor = 1.0 - (div_adjustment / adjusted[i].close);
                adjusted[i].open *= adj_factor;
                adjusted[i].high *= adj_factor;
                adjusted[i].low *= adj_factor;
                adjusted[i].close *= adj_factor;
            }
            
            adjusted[i].adjusted_close = adjusted[i].close;
        }
        
        return adjusted;
    }
    
    /**
     * @brief Calculate split-adjusted price
     */
    double adjust_price(const std::string& symbol, 
                        double price, 
                        const std::string& date) {
        double factor = engine_.get_split_factor(symbol, date);
        return price * factor;
    }
    
    /**
     * @brief Calculate split-adjusted quantity
     */
    double adjust_quantity(const std::string& symbol,
                           double quantity,
                           const std::string& date) {
        double factor = engine_.get_split_factor(symbol, date);
        return quantity / factor;
    }

private:
    CorporateActionEngine& engine_;
};

// ============================================================================
// Dividend-Adjusted Returns
// ============================================================================

/**
 * @brief Return calculation with dividends
 */
struct DividendAdjustedReturn {
    std::string date;
    double price_return{0};          // Price-only return
    double dividend_return{0};       // Return from dividends
    double total_return{0};          // Price + dividend return
    double dividend_yield{0};        // Dividend as % of price
};

/**
 * @brief Dividend-adjusted return calculator
 */
class DividendAdjustedCalculator {
public:
    explicit DividendAdjustedCalculator(CorporateActionEngine& engine)
        : engine_(engine) {}
    
    /**
     * @brief Calculate total returns with dividends
     */
    std::vector<DividendAdjustedReturn> calculate(
        const std::string& symbol,
        const std::vector<PriceBar>& bars) {
        
        std::vector<DividendAdjustedReturn> returns;
        if (bars.size() < 2) return returns;
        
        // Get dividends
        auto actions = engine_.get_actions(symbol, bars.front().date, bars.back().date);
        
        // Build dividend lookup by date
        std::map<std::string, double> dividend_by_date;
        for (const auto& action : actions) {
            if (action.type == CorporateActionType::CashDividend) {
                dividend_by_date[action.date] = action.dividend_amount;
            }
        }
        
        // Calculate returns
        for (size_t i = 1; i < bars.size(); ++i) {
            DividendAdjustedReturn ret;
            ret.date = bars[i].date;
            
            double prev_price = bars[i-1].close;
            double curr_price = bars[i].close;
            
            if (prev_price > 0) {
                // Price return
                ret.price_return = (curr_price - prev_price) / prev_price;
                
                // Check for dividend on this date
                auto it = dividend_by_date.find(bars[i].date);
                if (it != dividend_by_date.end()) {
                    ret.dividend_return = it->second / prev_price;
                    ret.dividend_yield = it->second / curr_price;
                }
                
                // Total return
                ret.total_return = ret.price_return + ret.dividend_return;
            }
            
            returns.push_back(ret);
        }
        
        return returns;
    }
    
    /**
     * @brief Calculate cumulative total return
     */
    double cumulative_total_return(
        const std::string& symbol,
        const std::vector<PriceBar>& bars) {
        
        auto returns = calculate(symbol, bars);
        
        double cumulative = 1.0;
        for (const auto& ret : returns) {
            cumulative *= (1.0 + ret.total_return);
        }
        
        return cumulative - 1.0;
    }

private:
    CorporateActionEngine& engine_;
};

// ============================================================================
// Data Source Failover
// ============================================================================

/**
 * @brief Data source status
 */
struct SourceStatus {
    std::string name;
    bool available{false};
    bool healthy{true};
    int consecutive_failures{0};
    int total_requests{0};
    int successful_requests{0};
    std::chrono::system_clock::time_point last_success;
    std::chrono::system_clock::time_point last_failure;
    std::string last_error;
    
    double success_rate() const {
        return total_requests > 0 ? 
            static_cast<double>(successful_requests) / total_requests : 0;
    }
};

/**
 * @brief Failover configuration
 */
struct FailoverConfig {
    int max_consecutive_failures{3};  // Failures before marking unhealthy
    int health_check_interval_sec{60}; // How often to check unhealthy sources
    int retry_delay_sec{5};            // Delay between retries
    bool auto_failover{true};          // Automatically switch sources
    std::vector<std::string> source_priority{"yahoo", "alpha_vantage"};
};

/**
 * @brief Data source failover manager
 */
class FailoverManager {
public:
    explicit FailoverManager(const FailoverConfig& config = {})
        : config_(config) {
        
        // Initialize sources
        for (const auto& name : config_.source_priority) {
            SourceStatus status;
            status.name = name;
            status.available = true;
            status.healthy = true;
            sources_[name] = status;
        }
    }
    
    /**
     * @brief Get next available source
     */
    std::string get_source() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& name : config_.source_priority) {
            auto& status = sources_[name];
            
            if (status.available && status.healthy) {
                return name;
            }
            
            // Check if unhealthy source should be retried
            if (!status.healthy) {
                auto since = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - status.last_failure).count();
                
                if (since > config_.health_check_interval_sec) {
                    status.healthy = true;  // Try again
                    return name;
                }
            }
        }
        
        // No healthy sources - return first available
        for (const auto& name : config_.source_priority) {
            if (sources_[name].available) {
                return name;
            }
        }
        
        return "";  // No sources available
    }
    
    /**
     * @brief Record successful request
     */
    void record_success(const std::string& source) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& status = sources_[source];
        status.total_requests++;
        status.successful_requests++;
        status.consecutive_failures = 0;
        status.healthy = true;
        status.last_success = std::chrono::system_clock::now();
    }
    
    /**
     * @brief Record failed request
     */
    void record_failure(const std::string& source, const std::string& error = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& status = sources_[source];
        status.total_requests++;
        status.consecutive_failures++;
        status.last_failure = std::chrono::system_clock::now();
        status.last_error = error;
        
        // Mark unhealthy if too many failures
        if (status.consecutive_failures >= config_.max_consecutive_failures) {
            status.healthy = false;
        }
    }
    
    /**
     * @brief Mark source unavailable
     */
    void set_unavailable(const std::string& source) {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_[source].available = false;
    }
    
    /**
     * @brief Mark source available
     */
    void set_available(const std::string& source) {
        std::lock_guard<std::mutex> lock(mutex_);
        sources_[source].available = true;
    }
    
    /**
     * @brief Get status of all sources
     */
    std::map<std::string, SourceStatus> get_all_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sources_;
    }
    
    /**
     * @brief Get status of specific source
     */
    SourceStatus get_status(const std::string& source) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sources_.find(source);
        if (it != sources_.end()) return it->second;
        return {};
    }
    
    /**
     * @brief Reset all source health
     */
    void reset_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, status] : sources_) {
            status.healthy = true;
            status.consecutive_failures = 0;
        }
    }
    
    const FailoverConfig& config() const { return config_; }

private:
    FailoverConfig config_;
    std::map<std::string, SourceStatus> sources_;
    mutable std::mutex mutex_;
};

// ============================================================================
// Gap Detection and Filling
// ============================================================================

/**
 * @brief Gap in price data
 */
struct DataGap {
    std::string symbol;
    std::string start_date;
    std::string end_date;
    int missing_days{0};
    double start_price{0};
    double end_price{0};
    bool is_weekend{false};
    bool is_holiday{false};
};

/**
 * @brief Gap fill method
 */
enum class GapFillMethod {
    None,              // Leave gaps as-is
    LastValue,         // Forward fill with last known value
    LinearInterp,      // Linear interpolation
    SplineInterp       // Spline interpolation (smoother)
};

/**
 * @brief Gap detector and filler
 */
class GapHandler {
public:
    /**
     * @brief Detect gaps in price series
     */
    std::vector<DataGap> detect_gaps(
        const std::string& symbol,
        const std::vector<PriceBar>& bars,
        int max_expected_gap = 3) {  // Max days between bars (weekends)
        
        std::vector<DataGap> gaps;
        if (bars.size() < 2) return gaps;
        
        for (size_t i = 1; i < bars.size(); ++i) {
            // Calculate days between bars (simplified)
            int days_diff = estimate_day_diff(bars[i-1].date, bars[i].date);
            
            if (days_diff > max_expected_gap) {
                DataGap gap;
                gap.symbol = symbol;
                gap.start_date = bars[i-1].date;
                gap.end_date = bars[i].date;
                gap.missing_days = days_diff - 1;
                gap.start_price = bars[i-1].close;
                gap.end_price = bars[i].close;
                
                gaps.push_back(gap);
            }
        }
        
        return gaps;
    }
    
    /**
     * @brief Fill gaps in price series
     */
    std::vector<PriceBar> fill_gaps(
        const std::vector<PriceBar>& bars,
        GapFillMethod method = GapFillMethod::LastValue,
        int max_fill_days = 5) {
        
        if (bars.size() < 2 || method == GapFillMethod::None) {
            return bars;
        }
        
        std::vector<PriceBar> filled;
        filled.push_back(bars[0]);
        
        for (size_t i = 1; i < bars.size(); ++i) {
            int days_diff = estimate_day_diff(bars[i-1].date, bars[i].date);
            
            if (days_diff > 1 && days_diff <= max_fill_days + 1) {
                // Fill the gap
                for (int d = 1; d < days_diff; ++d) {
                    PriceBar fill_bar;
                    fill_bar.date = interpolate_date(bars[i-1].date, d);
                    
                    switch (method) {
                        case GapFillMethod::LastValue:
                            fill_bar.open = bars[i-1].close;
                            fill_bar.high = bars[i-1].close;
                            fill_bar.low = bars[i-1].close;
                            fill_bar.close = bars[i-1].close;
                            fill_bar.adjusted_close = bars[i-1].adjusted_close;
                            fill_bar.volume = 0;
                            break;
                            
                        case GapFillMethod::LinearInterp:
                        case GapFillMethod::SplineInterp: {
                            double t = static_cast<double>(d) / days_diff;
                            fill_bar.close = bars[i-1].close + 
                                t * (bars[i].close - bars[i-1].close);
                            fill_bar.open = fill_bar.close;
                            fill_bar.high = fill_bar.close;
                            fill_bar.low = fill_bar.close;
                            fill_bar.adjusted_close = fill_bar.close;
                            fill_bar.volume = 0;
                            break;
                        }
                        
                        default:
                            break;
                    }
                    
                    filled.push_back(fill_bar);
                }
            }
            
            filled.push_back(bars[i]);
        }
        
        return filled;
    }

private:
    int estimate_day_diff(const std::string& date1, const std::string& date2) {
        // Simple estimation - assumes YYYY-MM-DD format
        // For production, use proper date parsing
        if (date1.size() < 10 || date2.size() < 10) return 1;
        
        int y1 = std::stoi(date1.substr(0, 4));
        int m1 = std::stoi(date1.substr(5, 2));
        int d1 = std::stoi(date1.substr(8, 2));
        
        int y2 = std::stoi(date2.substr(0, 4));
        int m2 = std::stoi(date2.substr(5, 2));
        int d2 = std::stoi(date2.substr(8, 2));
        
        // Simplified - doesn't handle month boundaries correctly
        if (y1 == y2 && m1 == m2) {
            return d2 - d1;
        }
        
        // Rough estimate for different months
        return (y2 - y1) * 365 + (m2 - m1) * 30 + (d2 - d1);
    }
    
    std::string interpolate_date(const std::string& start_date, int days_ahead) {
        // Simplified - for production use proper date library
        if (start_date.size() < 10) return start_date;
        
        int y = std::stoi(start_date.substr(0, 4));
        int m = std::stoi(start_date.substr(5, 2));
        int d = std::stoi(start_date.substr(8, 2));
        
        d += days_ahead;
        
        // Simple overflow handling
        while (d > 28) {
            d -= 28;
            m++;
            if (m > 12) {
                m = 1;
                y++;
            }
        }
        
        std::ostringstream oss;
        oss << y << "-" << std::setfill('0') << std::setw(2) << m 
            << "-" << std::setw(2) << d;
        return oss.str();
    }
};

// ============================================================================
// Data Quality Report
// ============================================================================

/**
 * @brief Data quality assessment
 */
struct DataQualityReport {
    std::string symbol;
    int total_bars{0};
    int valid_bars{0};
    int missing_bars{0};
    int spike_count{0};
    int gap_count{0};
    int corporate_actions{0};
    
    double completeness{0};           // % of expected data present
    double accuracy_score{0};         // Overall quality score
    
    std::string start_date;
    std::string end_date;
    int expected_bars{0};
    
    std::vector<DataGap> gaps;
    std::vector<DetectedSpike> spikes;
    
    std::string format() const {
        std::ostringstream oss;
        oss << "Data Quality Report for " << symbol << ":\n";
        oss << "  Period: " << start_date << " to " << end_date << "\n";
        oss << "  Total Bars: " << total_bars << " (expected " << expected_bars << ")\n";
        oss << "  Completeness: " << std::fixed << std::setprecision(1) 
            << (completeness * 100) << "%\n";
        oss << "  Spikes Detected: " << spike_count << "\n";
        oss << "  Gaps Found: " << gap_count << "\n";
        oss << "  Corporate Actions: " << corporate_actions << "\n";
        oss << "  Quality Score: " << std::setprecision(2) << accuracy_score << "/100\n";
        return oss.str();
    }
};

/**
 * @brief Generate data quality report
 */
inline DataQualityReport assess_quality(
    const std::string& symbol,
    const std::vector<PriceBar>& bars,
    const SpikeConfig& spike_config = {},
    CorporateActionEngine* action_engine = nullptr) {
    
    DataQualityReport report;
    report.symbol = symbol;
    report.total_bars = static_cast<int>(bars.size());
    
    if (bars.empty()) return report;
    
    report.start_date = bars.front().date;
    report.end_date = bars.back().date;
    
    // Spike detection
    SpikeDetector detector(spike_config);
    auto spike_result = detector.detect(symbol, bars, false);
    report.spike_count = spike_result.spikes_detected;
    report.spikes = spike_result.spikes;
    
    // Gap detection
    GapHandler gap_handler;
    report.gaps = gap_handler.detect_gaps(symbol, bars);
    report.gap_count = static_cast<int>(report.gaps.size());
    
    for (const auto& gap : report.gaps) {
        report.missing_bars += gap.missing_days;
    }
    
    // Corporate actions
    if (action_engine) {
        auto actions = action_engine->get_actions(symbol, report.start_date, report.end_date);
        report.corporate_actions = static_cast<int>(actions.size());
    }
    
    // Estimate expected bars (trading days)
    // Simplified: ~252 trading days per year
    int days_estimate = 0;
    if (!bars.empty()) {
        // Rough estimate based on date range
        int y1 = std::stoi(report.start_date.substr(0, 4));
        int y2 = std::stoi(report.end_date.substr(0, 4));
        days_estimate = (y2 - y1 + 1) * 252;
        if (days_estimate < report.total_bars) {
            days_estimate = report.total_bars;
        }
    }
    report.expected_bars = days_estimate > 0 ? days_estimate : report.total_bars;
    
    // Calculate completeness
    report.valid_bars = report.total_bars - report.spike_count;
    report.completeness = report.expected_bars > 0 ?
        static_cast<double>(report.total_bars) / report.expected_bars : 1.0;
    if (report.completeness > 1.0) report.completeness = 1.0;
    
    // Calculate quality score (0-100)
    double spike_penalty = report.spike_count * 2.0;
    double gap_penalty = report.gap_count * 5.0;
    double completeness_score = report.completeness * 100;
    
    report.accuracy_score = completeness_score - spike_penalty - gap_penalty;
    if (report.accuracy_score < 0) report.accuracy_score = 0;
    if (report.accuracy_score > 100) report.accuracy_score = 100;
    
    return report;
}

} // namespace genie::market

#endif // GENIE_MARKET_DATA_QUALITY_HPP
