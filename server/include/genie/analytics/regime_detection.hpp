/**
 * @file regime_detection.hpp
 * @brief Market Regime Detection Engine
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Identifies market regimes for adaptive strategy management:
 * - Volatility regime classification (low/normal/high/crisis)
 * - Trend regime detection (bull/bear/sideways)
 * - Correlation regime shifts
 * - Hidden Markov Model-inspired state estimation
 * - Regime transition probability matrix
 * - Lookback window configurable analysis
 * - Multi-timeframe regime alignment
 *
 * Pure C++20. No external dependencies.
 * Cross-platform: Windows, Linux, macOS.
 */
#pragma once
#ifndef GENIE_ANALYTICS_REGIME_DETECTION_HPP
#define GENIE_ANALYTICS_REGIME_DETECTION_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <sstream>

namespace genie::analytics {

enum class VolatilityRegime { LOW, NORMAL, HIGH, CRISIS };
enum class TrendRegime { STRONG_BULL, BULL, SIDEWAYS, BEAR, STRONG_BEAR };
enum class CorrelationRegime { LOW_CORRELATION, NORMAL, HIGH_CORRELATION, CONTAGION };

inline std::string vol_regime_name(VolatilityRegime r) {
    switch (r) {
        case VolatilityRegime::LOW: return "low";
        case VolatilityRegime::NORMAL: return "normal";
        case VolatilityRegime::HIGH: return "high";
        case VolatilityRegime::CRISIS: return "crisis";
    }
    return "unknown";
}

inline std::string trend_regime_name(TrendRegime r) {
    switch (r) {
        case TrendRegime::STRONG_BULL: return "strong_bull";
        case TrendRegime::BULL: return "bull";
        case TrendRegime::SIDEWAYS: return "sideways";
        case TrendRegime::BEAR: return "bear";
        case TrendRegime::STRONG_BEAR: return "strong_bear";
    }
    return "unknown";
}

inline std::string corr_regime_name(CorrelationRegime r) {
    switch (r) {
        case CorrelationRegime::LOW_CORRELATION: return "low_correlation";
        case CorrelationRegime::NORMAL: return "normal";
        case CorrelationRegime::HIGH_CORRELATION: return "high_correlation";
        case CorrelationRegime::CONTAGION: return "contagion";
    }
    return "unknown";
}

struct RegimeState {
    VolatilityRegime volatility{VolatilityRegime::NORMAL};
    TrendRegime trend{TrendRegime::SIDEWAYS};
    CorrelationRegime correlation{CorrelationRegime::NORMAL};

    // Confidence levels (0-1)
    double vol_confidence{0.0};
    double trend_confidence{0.0};
    double corr_confidence{0.0};

    // Underlying metrics
    double realized_vol_annualized{0.0};
    double vol_percentile{0.0}; // vs historical
    double sma_50_return{0.0};
    double sma_200_return{0.0};
    double avg_correlation{0.0};
    double momentum_score{0.0};

    // Regime duration
    int vol_regime_days{0};
    int trend_regime_days{0};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"volatility\":\"" << vol_regime_name(volatility) << "\""
           << ",\"trend\":\"" << trend_regime_name(trend) << "\""
           << ",\"correlation\":\"" << corr_regime_name(correlation) << "\""
           << ",\"vol_confidence\":" << vol_confidence
           << ",\"trend_confidence\":" << trend_confidence
           << ",\"corr_confidence\":" << corr_confidence
           << ",\"realized_vol\":" << realized_vol_annualized
           << ",\"vol_percentile\":" << vol_percentile
           << ",\"momentum_score\":" << momentum_score
           << ",\"vol_regime_days\":" << vol_regime_days
           << ",\"trend_regime_days\":" << trend_regime_days
           << "}";
        return os.str();
    }
};

struct TransitionMatrix {
    // 4x4 for volatility regimes
    double vol_transitions[4][4] = {};
    // 5x5 for trend regimes
    double trend_transitions[5][5] = {};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"volatility_transitions\":[";
        for (int i = 0; i < 4; ++i) {
            if (i > 0) os << ",";
            os << "[";
            for (int j = 0; j < 4; ++j) {
                if (j > 0) os << ",";
                os << vol_transitions[i][j];
            }
            os << "]";
        }
        os << "],\"trend_transitions\":[";
        for (int i = 0; i < 5; ++i) {
            if (i > 0) os << ",";
            os << "[";
            for (int j = 0; j < 5; ++j) {
                if (j > 0) os << ",";
                os << trend_transitions[i][j];
            }
            os << "]";
        }
        os << "]}";
        return os.str();
    }
};

struct RegimeHistory {
    std::vector<RegimeState> states;
    TransitionMatrix transitions;
    int total_observations{0};

    std::string to_json() const {
        std::ostringstream os;
        os << "{\"observations\":" << total_observations
           << ",\"current\":" << (states.empty() ? "{}" : states.back().to_json())
           << ",\"transitions\":" << transitions.to_json()
           << "}";
        return os.str();
    }
};

// ---------------------------------------------------------------
// Regime Detection Engine
// ---------------------------------------------------------------
class RegimeDetectionEngine {
public:
    // Thresholds (annualized volatility)
    void set_vol_thresholds(double low_high = 0.10, double high_crisis = 0.25, double crisis = 0.40) {
        vol_low_high_ = low_high;
        vol_high_crisis_ = high_crisis;
        vol_crisis_ = crisis;
    }

    // Detect current regime from return series
    RegimeState detect(const std::vector<double>& daily_returns,
                       int lookback = 60) const {
        RegimeState state;
        if (daily_returns.size() < 20) return state;

        size_t n = daily_returns.size();
        size_t window = std::min(static_cast<size_t>(lookback), n);
        auto start = daily_returns.end() - static_cast<ptrdiff_t>(window);

        // --- Volatility regime ---
        double sum = 0.0, sum_sq = 0.0;
        for (auto it = start; it != daily_returns.end(); ++it) {
            sum += *it;
            sum_sq += (*it) * (*it);
        }
        double mean = sum / static_cast<double>(window);
        double variance = (sum_sq / static_cast<double>(window)) - mean * mean;
        double daily_vol = std::sqrt(std::max(0.0, variance));
        state.realized_vol_annualized = daily_vol * std::sqrt(252.0);

        if (state.realized_vol_annualized < vol_low_high_) {
            state.volatility = VolatilityRegime::LOW;
            state.vol_confidence = 1.0 - (state.realized_vol_annualized / vol_low_high_);
        } else if (state.realized_vol_annualized < vol_high_crisis_) {
            state.volatility = VolatilityRegime::NORMAL;
            double range = vol_high_crisis_ - vol_low_high_;
            double pos = state.realized_vol_annualized - vol_low_high_;
            state.vol_confidence = 1.0 - std::abs(pos / range - 0.5) * 2.0;
        } else if (state.realized_vol_annualized < vol_crisis_) {
            state.volatility = VolatilityRegime::HIGH;
            state.vol_confidence = (state.realized_vol_annualized - vol_high_crisis_) /
                                    (vol_crisis_ - vol_high_crisis_);
        } else {
            state.volatility = VolatilityRegime::CRISIS;
            state.vol_confidence = std::min(1.0, state.realized_vol_annualized / vol_crisis_ - 1.0 + 0.5);
        }

        // Historical percentile
        state.vol_percentile = compute_vol_percentile(daily_returns, state.realized_vol_annualized);

        // --- Trend regime ---
        // Use cumulative return over different windows
        double cum_return_short = cumulative_return(daily_returns, 20);
        double cum_return_long = cumulative_return(daily_returns, std::min(static_cast<int>(n), 120));
        state.sma_50_return = cumulative_return(daily_returns, std::min(static_cast<int>(n), 50));
        state.sma_200_return = cumulative_return(daily_returns, std::min(static_cast<int>(n), 200));

        // Momentum score: blend of short and long term
        state.momentum_score = cum_return_short * 0.6 + cum_return_long * 0.4;

        double annualized_mom = state.momentum_score * (252.0 / static_cast<double>(lookback));
        if (annualized_mom > 0.20) {
            state.trend = TrendRegime::STRONG_BULL;
            state.trend_confidence = std::min(1.0, annualized_mom / 0.40);
        } else if (annualized_mom > 0.05) {
            state.trend = TrendRegime::BULL;
            state.trend_confidence = (annualized_mom - 0.05) / 0.15;
        } else if (annualized_mom > -0.05) {
            state.trend = TrendRegime::SIDEWAYS;
            state.trend_confidence = 1.0 - std::abs(annualized_mom) / 0.05;
        } else if (annualized_mom > -0.20) {
            state.trend = TrendRegime::BEAR;
            state.trend_confidence = (-annualized_mom - 0.05) / 0.15;
        } else {
            state.trend = TrendRegime::STRONG_BEAR;
            state.trend_confidence = std::min(1.0, -annualized_mom / 0.40);
        }

        return state;
    }

    // Detect regime with multiple asset returns (for correlation regime)
    RegimeState detect_multi_asset(const std::vector<std::vector<double>>& asset_returns,
                                    int lookback = 60) const {
        if (asset_returns.empty()) return {};

        // Use first asset for vol/trend
        RegimeState state = detect(asset_returns[0], lookback);

        // Correlation regime from cross-asset correlations
        if (asset_returns.size() >= 2) {
            double avg_corr = compute_avg_correlation(asset_returns, lookback);
            state.avg_correlation = avg_corr;

            if (avg_corr < 0.2) {
                state.correlation = CorrelationRegime::LOW_CORRELATION;
                state.corr_confidence = 1.0 - avg_corr / 0.2;
            } else if (avg_corr < 0.5) {
                state.correlation = CorrelationRegime::NORMAL;
                state.corr_confidence = 1.0 - std::abs(avg_corr - 0.35) / 0.15;
            } else if (avg_corr < 0.8) {
                state.correlation = CorrelationRegime::HIGH_CORRELATION;
                state.corr_confidence = (avg_corr - 0.5) / 0.3;
            } else {
                state.correlation = CorrelationRegime::CONTAGION;
                state.corr_confidence = std::min(1.0, (avg_corr - 0.8) / 0.2 + 0.5);
            }
        }

        return state;
    }

    // Build regime history from full return series
    RegimeHistory build_history(const std::vector<double>& daily_returns,
                                 int window = 60, int step = 5) const {
        RegimeHistory history;
        if (static_cast<int>(daily_returns.size()) < window) return history;

        VolatilityRegime prev_vol = VolatilityRegime::NORMAL;
        TrendRegime prev_trend = TrendRegime::SIDEWAYS;
        int vol_days = 0, trend_days = 0;

        for (int i = window; i <= static_cast<int>(daily_returns.size()); i += step) {
            std::vector<double> sub(daily_returns.begin(), daily_returns.begin() + i);
            auto state = detect(sub, window);

            if (state.volatility == prev_vol) {
                vol_days += step;
            } else {
                // Record transition
                int from = static_cast<int>(prev_vol);
                int to = static_cast<int>(state.volatility);
                history.transitions.vol_transitions[from][to] += 1.0;
                prev_vol = state.volatility;
                vol_days = step;
            }

            if (state.trend == prev_trend) {
                trend_days += step;
            } else {
                int from = static_cast<int>(prev_trend);
                int to = static_cast<int>(state.trend);
                history.transitions.trend_transitions[from][to] += 1.0;
                prev_trend = state.trend;
                trend_days = step;
            }

            state.vol_regime_days = vol_days;
            state.trend_regime_days = trend_days;
            history.states.push_back(state);
            history.total_observations++;
        }

        // Normalize transition matrices to probabilities
        for (int i = 0; i < 4; ++i) {
            double row_sum = 0.0;
            for (int j = 0; j < 4; ++j) row_sum += history.transitions.vol_transitions[i][j];
            if (row_sum > 0.0) {
                for (int j = 0; j < 4; ++j) history.transitions.vol_transitions[i][j] /= row_sum;
            }
        }
        for (int i = 0; i < 5; ++i) {
            double row_sum = 0.0;
            for (int j = 0; j < 5; ++j) row_sum += history.transitions.trend_transitions[i][j];
            if (row_sum > 0.0) {
                for (int j = 0; j < 5; ++j) history.transitions.trend_transitions[i][j] /= row_sum;
            }
        }

        return history;
    }

private:
    double vol_low_high_{0.10};
    double vol_high_crisis_{0.25};
    double vol_crisis_{0.40};

    double cumulative_return(const std::vector<double>& returns, int days) const {
        if (returns.empty()) return 0.0;
        int n = std::min(days, static_cast<int>(returns.size()));
        double cum = 0.0;
        for (int i = static_cast<int>(returns.size()) - n; i < static_cast<int>(returns.size()); ++i) {
            cum += returns[static_cast<size_t>(i)];
        }
        return cum;
    }

    double compute_vol_percentile(const std::vector<double>& returns, double current_vol) const {
        if (returns.size() < 60) return 50.0;
        std::vector<double> rolling_vols;
        int window = 20;
        for (size_t i = static_cast<size_t>(window); i <= returns.size(); i += 5) {
            double s = 0.0, ss = 0.0;
            for (size_t j = i - static_cast<size_t>(window); j < i; ++j) {
                s += returns[j];
                ss += returns[j] * returns[j];
            }
            double m = s / static_cast<double>(window);
            double v = (ss / static_cast<double>(window)) - m * m;
            rolling_vols.push_back(std::sqrt(std::max(0.0, v)) * std::sqrt(252.0));
        }
        if (rolling_vols.empty()) return 50.0;
        std::sort(rolling_vols.begin(), rolling_vols.end());
        auto it = std::lower_bound(rolling_vols.begin(), rolling_vols.end(), current_vol);
        size_t rank = static_cast<size_t>(std::distance(rolling_vols.begin(), it));
        return (static_cast<double>(rank) / static_cast<double>(rolling_vols.size())) * 100.0;
    }

    double compute_avg_correlation(const std::vector<std::vector<double>>& assets, int lookback) const {
        size_t n_assets = assets.size();
        if (n_assets < 2) return 0.0;
        double total_corr = 0.0;
        int pairs = 0;
        for (size_t i = 0; i < n_assets; ++i) {
            for (size_t j = i + 1; j < n_assets; ++j) {
                double corr = compute_correlation(assets[i], assets[j], lookback);
                total_corr += std::abs(corr);
                pairs++;
            }
        }
        return (pairs > 0) ? total_corr / static_cast<double>(pairs) : 0.0;
    }

    double compute_correlation(const std::vector<double>& a, const std::vector<double>& b, int lookback) const {
        size_t n = std::min({a.size(), b.size(), static_cast<size_t>(lookback)});
        if (n < 5) return 0.0;
        size_t start_a = a.size() - n;
        size_t start_b = b.size() - n;
        double sa = 0, sb = 0, sab = 0, sa2 = 0, sb2 = 0;
        for (size_t i = 0; i < n; ++i) {
            double va = a[start_a + i], vb = b[start_b + i];
            sa += va; sb += vb; sab += va * vb;
            sa2 += va * va; sb2 += vb * vb;
        }
        double nd = static_cast<double>(n);
        double num = nd * sab - sa * sb;
        double den = std::sqrt((nd * sa2 - sa * sa) * (nd * sb2 - sb * sb));
        return (den > 1e-15) ? num / den : 0.0;
    }
};

} // namespace genie::analytics

#endif // GENIE_ANALYTICS_REGIME_DETECTION_HPP
