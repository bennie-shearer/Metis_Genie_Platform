/**
 * @file interactive_charts.hpp
 * @brief Interactive price charts backend support
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: User Experience - Interactive price charts
 */

#ifndef GENIE_UX_INTERACTIVE_CHARTS_HPP
#define GENIE_UX_INTERACTIVE_CHARTS_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace genie {
namespace ux {

/**
 * @brief OHLCV candle data
 */
struct Candle {
    int64_t timestamp{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    int64_t volume{0};
    double vwap{0.0};
    int trades{0};
};

/**
 * @brief Chart timeframe
 */
enum class Timeframe {
    M1,    // 1 minute
    M5,    // 5 minutes
    M15,   // 15 minutes
    M30,   // 30 minutes
    H1,    // 1 hour
    H4,    // 4 hours
    D1,    // 1 day
    W1,    // 1 week
    MN1    // 1 month
};

/**
 * @brief Technical indicator value
 */
struct IndicatorValue {
    int64_t timestamp{0};
    std::map<std::string, double> values;
};

/**
 * @brief Drawing object on chart
 */
struct ChartDrawing {
    std::string id;
    std::string type;          // line, ray, channel, fib, rect, text
    int64_t start_time{0};
    double start_price{0.0};
    int64_t end_time{0};
    double end_price{0.0};
    std::string color{"#FFFFFF"};
    int line_width{1};
    std::string line_style{"solid"};  // solid, dashed, dotted
    std::map<std::string, std::string> properties;
};

/**
 * @brief Price level (support/resistance)
 */
struct PriceLevel {
    double price{0.0};
    std::string type;          // support, resistance, pivot
    double strength{0.0};      // 0-1, based on touches
    int touch_count{0};
    int64_t first_touch{0};
    int64_t last_touch{0};
};

/**
 * @brief Chart annotation
 */
struct ChartAnnotation {
    std::string id;
    int64_t timestamp{0};
    double price{0.0};
    std::string text;
    std::string type;          // trade, dividend, split, earnings, news
    std::string color{"#FFFFFF"};
};

/**
 * @brief Technical indicators calculator
 */
class TechnicalIndicators {
public:
    /**
     * @brief Simple Moving Average
     */
    static std::vector<IndicatorValue> sma(const std::vector<Candle>& candles,
                                            int period) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(period)) return result;
        
        double sum = 0.0;
        for (int i = 0; i < period; ++i) {
            sum += candles[i].close;
        }
        
        for (size_t i = period - 1; i < candles.size(); ++i) {
            if (i >= static_cast<size_t>(period)) {
                sum = sum - candles[i - period].close + candles[i].close;
            }
            
            IndicatorValue iv;
            iv.timestamp = candles[i].timestamp;
            iv.values["sma"] = sum / period;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief Exponential Moving Average
     */
    static std::vector<IndicatorValue> ema(const std::vector<Candle>& candles,
                                            int period) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(period)) return result;
        
        double multiplier = 2.0 / (period + 1);
        
        // Start with SMA
        double sum = 0.0;
        for (int i = 0; i < period; ++i) {
            sum += candles[i].close;
        }
        double ema_val = sum / period;
        
        IndicatorValue first;
        first.timestamp = candles[period - 1].timestamp;
        first.values["ema"] = ema_val;
        result.push_back(first);
        
        // Continue with EMA
        for (size_t i = period; i < candles.size(); ++i) {
            ema_val = (candles[i].close - ema_val) * multiplier + ema_val;
            
            IndicatorValue iv;
            iv.timestamp = candles[i].timestamp;
            iv.values["ema"] = ema_val;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief Bollinger Bands
     */
    static std::vector<IndicatorValue> bollinger_bands(const std::vector<Candle>& candles,
                                                        int period = 20,
                                                        double std_dev = 2.0) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(period)) return result;
        
        for (size_t i = period - 1; i < candles.size(); ++i) {
            // Calculate SMA
            double sum = 0.0;
            for (size_t j = i - period + 1; j <= i; ++j) {
                sum += candles[j].close;
            }
            double sma = sum / period;
            
            // Calculate standard deviation
            double sq_sum = 0.0;
            for (size_t j = i - period + 1; j <= i; ++j) {
                double diff = candles[j].close - sma;
                sq_sum += diff * diff;
            }
            double std = std::sqrt(sq_sum / period);
            
            IndicatorValue iv;
            iv.timestamp = candles[i].timestamp;
            iv.values["middle"] = sma;
            iv.values["upper"] = sma + std_dev * std;
            iv.values["lower"] = sma - std_dev * std;
            iv.values["bandwidth"] = (iv.values["upper"] - iv.values["lower"]) / sma;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief RSI - Relative Strength Index
     */
    static std::vector<IndicatorValue> rsi(const std::vector<Candle>& candles,
                                            int period = 14) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(period + 1)) return result;
        
        std::vector<double> gains, losses;
        for (size_t i = 1; i < candles.size(); ++i) {
            double change = candles[i].close - candles[i-1].close;
            gains.push_back(change > 0 ? change : 0);
            losses.push_back(change < 0 ? -change : 0);
        }
        
        // Initial averages
        double avg_gain = 0.0, avg_loss = 0.0;
        for (int i = 0; i < period; ++i) {
            avg_gain += gains[i];
            avg_loss += losses[i];
        }
        avg_gain /= period;
        avg_loss /= period;
        
        // First RSI value
        double rs = avg_loss > 0 ? avg_gain / avg_loss : 100.0;
        double rsi_val = 100.0 - (100.0 / (1.0 + rs));
        
        IndicatorValue first;
        first.timestamp = candles[period].timestamp;
        first.values["rsi"] = rsi_val;
        result.push_back(first);
        
        // Subsequent values using Wilder's smoothing
        for (size_t i = period; i < gains.size(); ++i) {
            avg_gain = (avg_gain * (period - 1) + gains[i]) / period;
            avg_loss = (avg_loss * (period - 1) + losses[i]) / period;
            
            rs = avg_loss > 0 ? avg_gain / avg_loss : 100.0;
            rsi_val = 100.0 - (100.0 / (1.0 + rs));
            
            IndicatorValue iv;
            iv.timestamp = candles[i + 1].timestamp;
            iv.values["rsi"] = rsi_val;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief MACD - Moving Average Convergence Divergence
     */
    static std::vector<IndicatorValue> macd(const std::vector<Candle>& candles,
                                             int fast_period = 12,
                                             int slow_period = 26,
                                             int signal_period = 9) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(slow_period + signal_period)) return result;
        
        // Calculate EMAs
        auto fast_ema = ema(candles, fast_period);
        auto slow_ema = ema(candles, slow_period);
        
        // Calculate MACD line
        std::vector<double> macd_line;
        size_t start_idx = slow_period - fast_period;
        
        for (size_t i = 0; i < slow_ema.size(); ++i) {
            double macd_val = fast_ema[start_idx + i].values["ema"] - 
                              slow_ema[i].values["ema"];
            macd_line.push_back(macd_val);
        }
        
        // Calculate signal line (EMA of MACD)
        double multiplier = 2.0 / (signal_period + 1);
        double signal = 0.0;
        for (int i = 0; i < signal_period; ++i) {
            signal += macd_line[i];
        }
        signal /= signal_period;
        
        // First complete value
        IndicatorValue first;
        first.timestamp = slow_ema[signal_period - 1].timestamp;
        first.values["macd"] = macd_line[signal_period - 1];
        first.values["signal"] = signal;
        first.values["histogram"] = macd_line[signal_period - 1] - signal;
        result.push_back(first);
        
        // Continue
        for (size_t i = signal_period; i < macd_line.size(); ++i) {
            signal = (macd_line[i] - signal) * multiplier + signal;
            
            IndicatorValue iv;
            iv.timestamp = slow_ema[i].timestamp;
            iv.values["macd"] = macd_line[i];
            iv.values["signal"] = signal;
            iv.values["histogram"] = macd_line[i] - signal;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief ATR - Average True Range
     */
    static std::vector<IndicatorValue> atr(const std::vector<Candle>& candles,
                                            int period = 14) {
        std::vector<IndicatorValue> result;
        if (candles.size() < static_cast<size_t>(period + 1)) return result;
        
        std::vector<double> tr;
        for (size_t i = 1; i < candles.size(); ++i) {
            double hl = candles[i].high - candles[i].low;
            double hc = std::abs(candles[i].high - candles[i-1].close);
            double lc = std::abs(candles[i].low - candles[i-1].close);
            tr.push_back(std::max({hl, hc, lc}));
        }
        
        // Initial ATR
        double atr_val = 0.0;
        for (int i = 0; i < period; ++i) {
            atr_val += tr[i];
        }
        atr_val /= period;
        
        IndicatorValue first;
        first.timestamp = candles[period].timestamp;
        first.values["atr"] = atr_val;
        result.push_back(first);
        
        // Wilder's smoothing
        for (size_t i = period; i < tr.size(); ++i) {
            atr_val = (atr_val * (period - 1) + tr[i]) / period;
            
            IndicatorValue iv;
            iv.timestamp = candles[i + 1].timestamp;
            iv.values["atr"] = atr_val;
            result.push_back(iv);
        }
        
        return result;
    }
    
    /**
     * @brief Volume Profile
     */
    static std::map<double, int64_t> volume_profile(const std::vector<Candle>& candles,
                                                     int num_levels = 20) {
        std::map<double, int64_t> profile;
        if (candles.empty()) return profile;
        
        // Find price range
        double min_price = candles[0].low;
        double max_price = candles[0].high;
        for (const auto& c : candles) {
            min_price = std::min(min_price, c.low);
            max_price = std::max(max_price, c.high);
        }
        
        double step = (max_price - min_price) / num_levels;
        if (step <= 0) return profile;
        
        // Initialize levels
        for (int i = 0; i <= num_levels; ++i) {
            profile[min_price + i * step] = 0;
        }
        
        // Distribute volume
        for (const auto& c : candles) {
            double typical_price = (c.high + c.low + c.close) / 3.0;
            double level = min_price + std::floor((typical_price - min_price) / step) * step;
            profile[level] += c.volume;
        }
        
        return profile;
    }
    
    /**
     * @brief Find support/resistance levels
     */
    static std::vector<PriceLevel> find_levels(const std::vector<Candle>& candles,
                                                double tolerance_pct = 0.5) {
        std::vector<PriceLevel> levels;
        if (candles.size() < 20) return levels;
        
        // Find local highs and lows
        std::vector<std::pair<int64_t, double>> pivots;
        
        for (size_t i = 2; i < candles.size() - 2; ++i) {
            // Local high
            if (candles[i].high > candles[i-1].high &&
                candles[i].high > candles[i-2].high &&
                candles[i].high > candles[i+1].high &&
                candles[i].high > candles[i+2].high) {
                pivots.emplace_back(candles[i].timestamp, candles[i].high);
            }
            // Local low
            if (candles[i].low < candles[i-1].low &&
                candles[i].low < candles[i-2].low &&
                candles[i].low < candles[i+1].low &&
                candles[i].low < candles[i+2].low) {
                pivots.emplace_back(candles[i].timestamp, candles[i].low);
            }
        }
        
        // Cluster nearby pivots
        double tolerance = candles.back().close * tolerance_pct / 100.0;
        std::vector<bool> used(pivots.size(), false);
        
        for (size_t i = 0; i < pivots.size(); ++i) {
            if (used[i]) continue;
            
            PriceLevel level;
            level.price = pivots[i].second;
            level.touch_count = 1;
            level.first_touch = pivots[i].first;
            level.last_touch = pivots[i].first;
            
            for (size_t j = i + 1; j < pivots.size(); ++j) {
                if (used[j]) continue;
                if (std::abs(pivots[j].second - level.price) <= tolerance) {
                    level.price = (level.price * level.touch_count + pivots[j].second) / 
                                  (level.touch_count + 1);
                    level.touch_count++;
                    level.last_touch = std::max(level.last_touch, pivots[j].first);
                    used[j] = true;
                }
            }
            
            if (level.touch_count >= 2) {
                level.strength = std::min(1.0, level.touch_count / 5.0);
                level.type = level.price > candles.back().close ? "resistance" : "support";
                levels.push_back(level);
            }
            used[i] = true;
        }
        
        // Sort by strength
        std::sort(levels.begin(), levels.end(),
                  [](const PriceLevel& a, const PriceLevel& b) {
                      return a.strength > b.strength;
                  });
        
        return levels;
    }
};

/**
 * @brief Chart data provider
 */
class ChartDataProvider {
public:
    struct ChartConfig {
        std::string symbol;
        Timeframe timeframe{Timeframe::D1};
        int64_t start_time{0};
        int64_t end_time{0};
        std::vector<std::string> indicators;
        bool show_volume{true};
        bool show_drawings{true};
    };
    
    /**
     * @brief Get chart data for rendering
     */
    static std::string get_chart_json(const std::vector<Candle>& candles,
                                       const ChartConfig& config) {
        std::ostringstream json;
        json << std::fixed << std::setprecision(4);
        
        json << "{";
        json << "\"symbol\":\"" << config.symbol << "\",";
        json << "\"timeframe\":\"" << timeframe_to_string(config.timeframe) << "\",";
        
        // Candles
        json << "\"candles\":[";
        for (size_t i = 0; i < candles.size(); ++i) {
            if (i > 0) json << ",";
            json << "{";
            json << "\"t\":" << candles[i].timestamp << ",";
            json << "\"o\":" << candles[i].open << ",";
            json << "\"h\":" << candles[i].high << ",";
            json << "\"l\":" << candles[i].low << ",";
            json << "\"c\":" << candles[i].close << ",";
            json << "\"v\":" << candles[i].volume;
            json << "}";
        }
        json << "],";
        
        // Indicators
        json << "\"indicators\":{";
        bool first_ind = true;
        for (const auto& ind : config.indicators) {
            if (!first_ind) json << ",";
            first_ind = false;
            
            json << "\"" << ind << "\":[";
            
            std::vector<IndicatorValue> values;
            if (ind == "sma20") values = TechnicalIndicators::sma(candles, 20);
            else if (ind == "sma50") values = TechnicalIndicators::sma(candles, 50);
            else if (ind == "sma200") values = TechnicalIndicators::sma(candles, 200);
            else if (ind == "ema12") values = TechnicalIndicators::ema(candles, 12);
            else if (ind == "ema26") values = TechnicalIndicators::ema(candles, 26);
            else if (ind == "bb") values = TechnicalIndicators::bollinger_bands(candles);
            else if (ind == "rsi") values = TechnicalIndicators::rsi(candles);
            else if (ind == "macd") values = TechnicalIndicators::macd(candles);
            else if (ind == "atr") values = TechnicalIndicators::atr(candles);
            
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) json << ",";
                json << "{\"t\":" << values[i].timestamp;
                for (const auto& [key, val] : values[i].values) {
                    json << ",\"" << key << "\":" << val;
                }
                json << "}";
            }
            json << "]";
        }
        json << "},";
        
        // Support/resistance levels
        auto levels = TechnicalIndicators::find_levels(candles);
        json << "\"levels\":[";
        for (size_t i = 0; i < std::min(levels.size(), size_t(10)); ++i) {
            if (i > 0) json << ",";
            json << "{";
            json << "\"price\":" << levels[i].price << ",";
            json << "\"type\":\"" << levels[i].type << "\",";
            json << "\"strength\":" << levels[i].strength << ",";
            json << "\"touches\":" << levels[i].touch_count;
            json << "}";
        }
        json << "]";
        
        json << "}";
        
        return json.str();
    }
    
    /**
     * @brief Convert timeframe to string
     */
    static std::string timeframe_to_string(Timeframe tf) {
        switch (tf) {
            case Timeframe::M1: return "1m";
            case Timeframe::M5: return "5m";
            case Timeframe::M15: return "15m";
            case Timeframe::M30: return "30m";
            case Timeframe::H1: return "1h";
            case Timeframe::H4: return "4h";
            case Timeframe::D1: return "1d";
            case Timeframe::W1: return "1w";
            case Timeframe::MN1: return "1M";
            default: return "1d";
        }
    }
    
    /**
     * @brief Parse timeframe from string
     */
    static Timeframe parse_timeframe(const std::string& s) {
        if (s == "1m" || s == "M1") return Timeframe::M1;
        if (s == "5m" || s == "M5") return Timeframe::M5;
        if (s == "15m" || s == "M15") return Timeframe::M15;
        if (s == "30m" || s == "M30") return Timeframe::M30;
        if (s == "1h" || s == "H1") return Timeframe::H1;
        if (s == "4h" || s == "H4") return Timeframe::H4;
        if (s == "1d" || s == "D1") return Timeframe::D1;
        if (s == "1w" || s == "W1") return Timeframe::W1;
        if (s == "1M" || s == "MN1") return Timeframe::MN1;
        return Timeframe::D1;
    }
    
    /**
     * @brief Aggregate candles to higher timeframe
     */
    static std::vector<Candle> aggregate(const std::vector<Candle>& candles,
                                          Timeframe from, Timeframe to) {
        std::vector<Candle> result;
        if (candles.empty() || from >= to) return candles;
        
        int ratio = get_aggregation_ratio(from, to);
        if (ratio <= 1) return candles;
        
        for (size_t i = 0; i < candles.size(); i += ratio) {
            Candle agg;
            agg.timestamp = candles[i].timestamp;
            agg.open = candles[i].open;
            agg.high = candles[i].high;
            agg.low = candles[i].low;
            agg.volume = 0;
            
            size_t end = std::min(i + ratio, candles.size());
            for (size_t j = i; j < end; ++j) {
                agg.high = std::max(agg.high, candles[j].high);
                agg.low = std::min(agg.low, candles[j].low);
                agg.close = candles[j].close;
                agg.volume += candles[j].volume;
            }
            
            result.push_back(agg);
        }
        
        return result;
    }

private:
    static int get_aggregation_ratio(Timeframe from, Timeframe to) {
        int from_mins = timeframe_minutes(from);
        int to_mins = timeframe_minutes(to);
        return to_mins / from_mins;
    }
    
    static int timeframe_minutes(Timeframe tf) {
        switch (tf) {
            case Timeframe::M1: return 1;
            case Timeframe::M5: return 5;
            case Timeframe::M15: return 15;
            case Timeframe::M30: return 30;
            case Timeframe::H1: return 60;
            case Timeframe::H4: return 240;
            case Timeframe::D1: return 1440;
            case Timeframe::W1: return 10080;
            case Timeframe::MN1: return 43200;
            default: return 1440;
        }
    }
};

} // namespace ux
} // namespace genie

#endif // GENIE_UX_INTERACTIVE_CHARTS_HPP
