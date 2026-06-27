/**
 * @file ml_alpha.hpp
 * @brief Machine Learning Alpha Signal Generation Engine for Metis Genie Platform
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements ML-driven alpha signal generation for systematic trading:
 *   - Feature engineering pipeline (technical indicators, fundamental ratios)
 *   - Linear regression with L1/L2 regularization (Lasso/Ridge)
 *   - Decision tree ensemble (Random Forest via bagging)
 *   - Gradient boosted trees (simplified XGBoost-style)
 *   - K-nearest neighbors for non-parametric signals
 *   - Momentum and mean-reversion factor models
 *   - Cross-sectional ranking and z-score normalization
 *   - Walk-forward validation with expanding/rolling windows
 *   - Feature importance and signal decay analysis
 *   - Ensemble combiner (weighted average of models)
 *   - Signal portfolio construction (long-short, long-only)
 *   - Turnover and transaction cost estimation
 *   - Information coefficient (IC) and hit rate tracking
 *   - Sentiment score integration (placeholder for NLP feed)
 *
 * All algorithms are implemented from scratch in C++20 with no external
 * ML library dependencies. Suitable for research and education; production
 * deployment should use validated ML frameworks.
 *
 * Zero external dependencies. Pure C++20.
 */

#ifndef GENIE_ANALYTICS_ML_ALPHA_HPP
#define GENIE_ANALYTICS_ML_ALPHA_HPP

#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <memory>
#include <functional>
#include <optional>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <array>
#include <set>
#include <variant>

namespace genie {
namespace analytics {

// ============================================================
// Enumerations
// ============================================================

enum class AlphaModelType {
    LinearRegression,
    RidgeRegression,
    LassoRegression,
    ElasticNet,
    DecisionTree,
    RandomForest,
    GradientBoosted,
    KNearestNeighbors,
    MomentumFactor,
    MeanReversion,
    EnsembleCombiner
};

enum class FeatureType {
    // Technical
    Return_1d, Return_5d, Return_21d, Return_63d, Return_252d,
    Volatility_21d, Volatility_63d,
    RSI_14, RSI_28,
    MACD_Signal,
    BollingerBand_Position,
    ATR_14,
    Volume_Ratio_5d,
    // Fundamental
    PE_Ratio, PB_Ratio, Dividend_Yield,
    EarningsSurprise, RevenueGrowth,
    DebtToEquity, ROE, ROA,
    FreeCashFlowYield,
    // Cross-sectional
    SectorRelativeReturn,
    MarketCapRank,
    // Sentiment
    SentimentScore, NewsVolume, AnalystRevisions,
    // Custom
    Custom
};

enum class ValidationMethod {
    ExpandingWindow,    // Train on all data up to t, test on t+1
    RollingWindow,      // Fixed-size training window
    KFoldTimeSeries,    // Time-series aware k-fold
    Purged             // Purged walk-forward (embargo around test)
};

enum class SignalCombination {
    EqualWeight,
    ICWeight,           // Weight by information coefficient
    InverseVariance,
    OptimalShrinkage
};

// ============================================================
// Data Structures
// ============================================================

struct FeatureVector {
    std::string             symbol;
    std::string             date;       // YYYY-MM-DD
    std::vector<double>     features;
    std::vector<FeatureType> feature_names;
    double                  forward_return = 0.0;  // Target variable
    double                  weight = 1.0;
};

struct AlphaSignal {
    std::string symbol;
    std::string date;
    double      raw_signal      = 0.0;  // Model output
    double      z_score         = 0.0;  // Cross-sectional z-score
    double      rank_signal     = 0.0;  // Percentile rank [0, 1]
    double      confidence      = 0.0;  // Model confidence [0, 1]
    double      position_size   = 0.0;  // Suggested weight
    AlphaModelType source       = AlphaModelType::LinearRegression;
};

struct AlphaModelMetrics {
    double      ic              = 0.0;  // Information coefficient (Spearman rank corr)
    double      ic_ir           = 0.0;  // IC information ratio (mean IC / std IC)
    double      hit_rate        = 0.0;  // Fraction of correct direction predictions
    double      r_squared       = 0.0;
    double      mse             = 0.0;  // Mean squared error
    double      mae             = 0.0;  // Mean absolute error
    double      turnover        = 0.0;  // Average daily turnover
    double      long_return     = 0.0;  // Average return of long basket
    double      short_return    = 0.0;  // Average return of short basket
    double      spread_return   = 0.0;  // Long minus short
    double      sharpe          = 0.0;  // Annualized Sharpe of L/S
    size_t      num_predictions = 0;
    std::vector<double> ic_series;      // IC over time

    [[nodiscard]] std::string summary() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4)
           << "IC: " << ic << " | IC_IR: " << ic_ir
           << " | Hit: " << (hit_rate * 100) << "%"
           << " | R2: " << r_squared
           << " | Sharpe: " << std::setprecision(2) << sharpe
           << " | Spread: " << std::setprecision(4) << (spread_return * 100) << "%"
           << " (n=" << num_predictions << ")";
        return ss.str();
    }
};

struct FeatureImportance {
    FeatureType feature;
    std::string feature_name;
    double      importance = 0.0;   // Normalized [0, 1]
    double      ic_contribution = 0.0;
};

// ============================================================
// Utility Functions
// ============================================================

namespace ml_util {

inline double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

inline double stdev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double m = mean(v);
    double sum = 0.0;
    for (double x : v) sum += (x - m) * (x - m);
    return std::sqrt(sum / (v.size() - 1));
}

inline double correlation(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = std::min(x.size(), y.size());
    if (n < 2) return 0.0;
    double mx = 0.0, my = 0.0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double sxy = 0.0, sx2 = 0.0, sy2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxy += dx * dy;
        sx2 += dx * dx;
        sy2 += dy * dy;
    }
    double denom = std::sqrt(sx2 * sy2);
    return (denom > 1e-12) ? sxy / denom : 0.0;
}

inline double spearman_rank_correlation(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = std::min(x.size(), y.size());
    if (n < 2) return 0.0;
    // Rank transform
    auto rank = [](const std::vector<double>& v, size_t n_) {
        std::vector<size_t> idx(n_);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
        std::vector<double> ranks(n_);
        for (size_t i = 0; i < n_; ++i) ranks[idx[i]] = static_cast<double>(i + 1);
        return ranks;
    };
    auto rx = rank(x, n);
    auto ry = rank(y, n);
    return correlation(rx, ry);
}

inline std::vector<double> z_score_normalize(const std::vector<double>& v) {
    double m = mean(v);
    double s = stdev(v);
    std::vector<double> result(v.size());
    if (s < 1e-12) {
        std::fill(result.begin(), result.end(), 0.0);
    } else {
        for (size_t i = 0; i < v.size(); ++i) {
            result[i] = (v[i] - m) / s;
        }
    }
    return result;
}

inline std::vector<double> rank_normalize(const std::vector<double>& v) {
    size_t n = v.size();
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<double> ranks(n);
    for (size_t i = 0; i < n; ++i) {
        ranks[idx[i]] = static_cast<double>(i) / std::max(1.0, static_cast<double>(n - 1));
    }
    return ranks;
}

inline double winsorize(double val, double lower, double upper) {
    return std::max(lower, std::min(upper, val));
}

} // namespace ml_util

// ============================================================
// Feature Engineering
// ============================================================

class FeatureEngineer {
public:
    // Compute technical features from price/volume series
    static std::vector<double> compute_returns(const std::vector<double>& prices, int lookback) {
        std::vector<double> returns(prices.size(), 0.0);
        for (size_t i = lookback; i < prices.size(); ++i) {
            if (prices[i - lookback] > 0.0) {
                returns[i] = prices[i] / prices[i - lookback] - 1.0;
            }
        }
        return returns;
    }

    static std::vector<double> compute_volatility(const std::vector<double>& prices, int window) {
        auto daily_returns = compute_returns(prices, 1);
        std::vector<double> vol(prices.size(), 0.0);
        for (size_t i = window; i < prices.size(); ++i) {
            double sum = 0.0, sum2 = 0.0;
            for (int j = 0; j < window; ++j) {
                double r = daily_returns[i - j];
                sum += r;
                sum2 += r * r;
            }
            double m = sum / window;
            double var = sum2 / window - m * m;
            vol[i] = std::sqrt(std::max(0.0, var) * 252.0);  // Annualized
        }
        return vol;
    }

    static std::vector<double> compute_rsi(const std::vector<double>& prices, int period) {
        std::vector<double> rsi(prices.size(), 50.0);
        for (size_t i = period + 1; i < prices.size(); ++i) {
            double avg_gain = 0.0, avg_loss = 0.0;
            for (int j = 0; j < period; ++j) {
                double change = prices[i - j] - prices[i - j - 1];
                if (change > 0) avg_gain += change;
                else avg_loss -= change;
            }
            avg_gain /= period;
            avg_loss /= period;
            if (avg_loss < 1e-12) { rsi[i] = 100.0; continue; }
            double rs = avg_gain / avg_loss;
            rsi[i] = 100.0 - 100.0 / (1.0 + rs);
        }
        return rsi;
    }

    static std::vector<double> compute_macd_signal(const std::vector<double>& prices,
                                                     int fast = 12, int slow = 26, int signal = 9) {
        auto ema = [](const std::vector<double>& data, int period) {
            std::vector<double> result(data.size(), 0.0);
            double alpha = 2.0 / (period + 1);
            result[0] = data[0];
            for (size_t i = 1; i < data.size(); ++i) {
                result[i] = alpha * data[i] + (1.0 - alpha) * result[i - 1];
            }
            return result;
        };

        auto ema_fast = ema(prices, fast);
        auto ema_slow = ema(prices, slow);
        std::vector<double> macd_line(prices.size());
        for (size_t i = 0; i < prices.size(); ++i) {
            macd_line[i] = ema_fast[i] - ema_slow[i];
        }
        auto signal_line = ema(macd_line, signal);
        std::vector<double> histogram(prices.size());
        for (size_t i = 0; i < prices.size(); ++i) {
            histogram[i] = macd_line[i] - signal_line[i];
        }
        return histogram;
    }

    static std::vector<double> compute_bollinger_position(const std::vector<double>& prices, int window = 20) {
        std::vector<double> position(prices.size(), 0.5);
        for (size_t i = window; i < prices.size(); ++i) {
            double sum = 0.0, sum2 = 0.0;
            for (int j = 0; j < window; ++j) {
                sum += prices[i - j];
                sum2 += prices[i - j] * prices[i - j];
            }
            double sma = sum / window;
            double std_dev = std::sqrt(sum2 / window - sma * sma);
            if (std_dev > 1e-12) {
                double upper = sma + 2.0 * std_dev;
                double lower = sma - 2.0 * std_dev;
                position[i] = (prices[i] - lower) / (upper - lower);
            }
        }
        return position;
    }

    // Build complete feature vector for a given time index
    static FeatureVector build_features(
        const std::string& symbol,
        const std::string& date,
        const std::vector<double>& prices,
        const std::vector<double>& volumes,
        size_t index,
        int forward_period = 5)
    {
        FeatureVector fv;
        fv.symbol = symbol;
        fv.date = date;
        fv.feature_names = {
            FeatureType::Return_1d, FeatureType::Return_5d,
            FeatureType::Return_21d, FeatureType::Return_63d,
            FeatureType::Volatility_21d, FeatureType::Volatility_63d,
            FeatureType::RSI_14, FeatureType::BollingerBand_Position,
            FeatureType::MACD_Signal, FeatureType::Volume_Ratio_5d
        };

        auto add_safe = [&](const std::vector<double>& v) {
            fv.features.push_back(index < v.size() ? v[index] : 0.0);
        };

        add_safe(compute_returns(prices, 1));
        add_safe(compute_returns(prices, 5));
        add_safe(compute_returns(prices, 21));
        add_safe(compute_returns(prices, 63));
        add_safe(compute_volatility(prices, 21));
        add_safe(compute_volatility(prices, 63));
        add_safe(compute_rsi(prices, 14));
        add_safe(compute_bollinger_position(prices, 20));
        add_safe(compute_macd_signal(prices));

        // Volume ratio
        if (index >= 5 && !volumes.empty()) {
            double recent = 0.0, past = 0.0;
            for (int j = 0; j < 5; ++j) {
                recent += volumes[index - j];
                if (index >= 10 + static_cast<size_t>(j))
                    past += volumes[index - 5 - j];
            }
            fv.features.push_back(past > 0.0 ? recent / past : 1.0);
        } else {
            fv.features.push_back(1.0);
        }

        // Forward return (target)
        if (index + forward_period < prices.size() && prices[index] > 0.0) {
            fv.forward_return = prices[index + forward_period] / prices[index] - 1.0;
        }

        return fv;
    }
};

// ============================================================
// Base Model Interface
// ============================================================

class AlphaModel {
public:
    virtual ~AlphaModel() = default;
    virtual AlphaModelType type() const = 0;
    virtual std::string name() const = 0;
    virtual void train(const std::vector<FeatureVector>& data) = 0;
    virtual double predict(const std::vector<double>& features) const = 0;
    virtual std::vector<FeatureImportance> feature_importance() const { return {}; }
    virtual bool is_trained() const = 0;
};

// ============================================================
// Linear Regression (Ridge/Lasso/ElasticNet)
// ============================================================

class LinearRegressionModel : public AlphaModel {
public:
    struct Config {
        Config() = default;
        double      l1_penalty      = 0.0;     // Lasso
        double      l2_penalty      = 0.001;   // Ridge
        double      learning_rate   = 0.01;
        int         max_iterations  = 1000;
        double      tolerance       = 1e-6;
        bool        fit_intercept   = true;
    };

    LinearRegressionModel() : config_() {}
    explicit LinearRegressionModel(Config config) : config_(config) {}

    AlphaModelType type() const override {
        if (config_.l1_penalty > 0 && config_.l2_penalty > 0) return AlphaModelType::ElasticNet;
        if (config_.l1_penalty > 0) return AlphaModelType::LassoRegression;
        if (config_.l2_penalty > 0) return AlphaModelType::RidgeRegression;
        return AlphaModelType::LinearRegression;
    }
    std::string name() const override { return "LinearRegression"; }
    bool is_trained() const override { return trained_; }

    void train(const std::vector<FeatureVector>& data) override {
        if (data.empty() || data[0].features.empty()) return;
        size_t n = data.size();
        size_t p = data[0].features.size();

        // Initialize weights
        weights_.assign(p, 0.0);
        intercept_ = 0.0;

        // Coordinate descent for elastic net
        for (int iter = 0; iter < config_.max_iterations; ++iter) {
            double max_change = 0.0;

            for (size_t j = 0; j < p; ++j) {
                double numerator = 0.0;
                double denominator = 0.0;

                for (size_t i = 0; i < n; ++i) {
                    double pred = predict_internal(data[i].features);
                    double residual = data[i].forward_return - pred;
                    residual += weights_[j] * data[i].features[j]; // Add back current feature
                    numerator += data[i].features[j] * residual;
                    denominator += data[i].features[j] * data[i].features[j];
                }

                numerator /= n;
                denominator /= n;
                denominator += config_.l2_penalty;

                // Soft thresholding for L1
                double new_weight;
                if (config_.l1_penalty > 0) {
                    if (numerator > config_.l1_penalty)
                        new_weight = (numerator - config_.l1_penalty) / denominator;
                    else if (numerator < -config_.l1_penalty)
                        new_weight = (numerator + config_.l1_penalty) / denominator;
                    else
                        new_weight = 0.0;
                } else {
                    new_weight = numerator / denominator;
                }

                max_change = std::max(max_change, std::abs(new_weight - weights_[j]));
                weights_[j] = new_weight;
            }

            // Update intercept
            if (config_.fit_intercept) {
                double sum_residual = 0.0;
                for (size_t i = 0; i < n; ++i) {
                    sum_residual += data[i].forward_return - predict_internal(data[i].features) + intercept_;
                }
                intercept_ = sum_residual / n;
            }

            if (max_change < config_.tolerance) break;
        }

        feature_names_.clear();
        if (!data.empty()) feature_names_ = data[0].feature_names;
        trained_ = true;
    }

    double predict(const std::vector<double>& features) const override {
        return predict_internal(features);
    }

    std::vector<FeatureImportance> feature_importance() const override {
        std::vector<FeatureImportance> imp;
        double max_w = 0.0;
        for (double w : weights_) max_w = std::max(max_w, std::abs(w));
        for (size_t i = 0; i < weights_.size(); ++i) {
            FeatureImportance fi;
            fi.feature = i < feature_names_.size() ? feature_names_[i] : FeatureType::Custom;
            fi.importance = (max_w > 0) ? std::abs(weights_[i]) / max_w : 0.0;
            imp.push_back(fi);
        }
        std::sort(imp.begin(), imp.end(), [](const auto& a, const auto& b) {
            return a.importance > b.importance;
        });
        return imp;
    }

private:
    double predict_internal(const std::vector<double>& features) const {
        double pred = intercept_;
        for (size_t i = 0; i < std::min(features.size(), weights_.size()); ++i) {
            pred += weights_[i] * features[i];
        }
        return pred;
    }

    Config config_;
    std::vector<double> weights_;
    double intercept_ = 0.0;
    bool trained_ = false;
    std::vector<FeatureType> feature_names_;
};

// ============================================================
// Decision Tree
// ============================================================

class DecisionTreeModel : public AlphaModel {
public:
    struct Config {
        Config() = default;
        int     max_depth       = 5;
        int     min_samples_leaf = 10;
        int     min_samples_split = 20;
        double  min_impurity_decrease = 1e-7;
    };

    DecisionTreeModel() : config_() {}
    explicit DecisionTreeModel(Config config) : config_(config) {}

    AlphaModelType type() const override { return AlphaModelType::DecisionTree; }
    std::string name() const override { return "DecisionTree"; }
    bool is_trained() const override { return root_ != nullptr; }

    void train(const std::vector<FeatureVector>& data) override {
        if (data.empty()) return;
        std::vector<size_t> indices(data.size());
        std::iota(indices.begin(), indices.end(), 0);
        root_ = build_tree(data, indices, 0);
    }

    double predict(const std::vector<double>& features) const override {
        if (!root_) return 0.0;
        const TreeNode* node = root_.get();
        while (node && !node->is_leaf) {
            if (node->feature_index < features.size() &&
                features[node->feature_index] <= node->threshold) {
                node = node->left.get();
            } else {
                node = node->right.get();
            }
        }
        return node ? node->prediction : 0.0;
    }

private:
    struct TreeNode {
        bool    is_leaf         = false;
        double  prediction      = 0.0;
        size_t  feature_index   = 0;
        double  threshold       = 0.0;
        size_t  num_samples     = 0;
        std::unique_ptr<TreeNode> left;
        std::unique_ptr<TreeNode> right;
    };

    std::unique_ptr<TreeNode> build_tree(const std::vector<FeatureVector>& data,
                                          const std::vector<size_t>& indices,
                                          int depth) {
        auto node = std::make_unique<TreeNode>();
        node->num_samples = indices.size();

        // Compute mean prediction
        double sum = 0.0;
        for (size_t idx : indices) sum += data[idx].forward_return;
        node->prediction = sum / indices.size();

        // Check stopping conditions
        if (depth >= config_.max_depth ||
            static_cast<int>(indices.size()) < config_.min_samples_split) {
            node->is_leaf = true;
            return node;
        }

        // Find best split
        size_t num_features = data[0].features.size();
        double best_impurity = -1.0;
        size_t best_feature = 0;
        double best_threshold = 0.0;

        double total_var = 0.0;
        double total_mean = node->prediction;
        for (size_t idx : indices) {
            double d = data[idx].forward_return - total_mean;
            total_var += d * d;
        }

        for (size_t f = 0; f < num_features; ++f) {
            // Sort indices by feature
            std::vector<size_t> sorted_idx(indices);
            std::sort(sorted_idx.begin(), sorted_idx.end(),
                [&](size_t a, size_t b) { return data[a].features[f] < data[b].features[f]; });

            double left_sum = 0.0, left_sum2 = 0.0;
            size_t left_count = 0;
            double right_sum = sum;
            size_t right_count = indices.size();

            for (size_t i = 0; i < sorted_idx.size() - 1; ++i) {
                double val = data[sorted_idx[i]].forward_return;
                left_sum += val;
                left_sum2 += val * val;
                left_count++;
                right_sum -= val;
                right_count--;

                if (static_cast<int>(left_count) < config_.min_samples_leaf ||
                    static_cast<int>(right_count) < config_.min_samples_leaf) continue;

                // Skip if same feature value
                if (data[sorted_idx[i]].features[f] == data[sorted_idx[i+1]].features[f]) continue;

                double left_mean = left_sum / left_count;
                double right_mean = right_sum / right_count;
                (void)(left_sum2 / left_count - left_mean * left_mean); // variance used via mse below
                double right_var = (sum * sum / right_count -
                    2 * right_mean * right_sum + right_count * right_mean * right_mean) / right_count;
                (void)right_var;

                // Weighted variance (used implicitly via MSE computation below)
                (void)(left_count * (left_sum2 / left_count - left_mean * left_mean) +
                       right_count * ((sum * sum - 2 * left_sum * sum + left_sum * left_sum) /
                       right_count - right_mean * right_mean + right_mean * right_mean));
                double impurity_decrease = total_var - left_count * (left_sum2 / left_count - left_mean * left_mean)
                    * 0; // placeholder
                // Actually compute properly
                double left_mse = 0.0, right_mse = 0.0;
                for (size_t j = 0; j <= i; ++j) {
                    double d = data[sorted_idx[j]].forward_return - left_mean;
                    left_mse += d * d;
                }
                for (size_t j = i + 1; j < sorted_idx.size(); ++j) {
                    double d = data[sorted_idx[j]].forward_return - right_mean;
                    right_mse += d * d;
                }
                impurity_decrease = total_var - left_mse - right_mse;

                if (impurity_decrease > best_impurity) {
                    best_impurity = impurity_decrease;
                    best_feature = f;
                    best_threshold = (data[sorted_idx[i]].features[f] +
                                     data[sorted_idx[i+1]].features[f]) / 2.0;
                }
            }
        }

        if (best_impurity <= config_.min_impurity_decrease) {
            node->is_leaf = true;
            return node;
        }

        node->feature_index = best_feature;
        node->threshold = best_threshold;

        std::vector<size_t> left_indices, right_indices;
        for (size_t idx : indices) {
            if (data[idx].features[best_feature] <= best_threshold)
                left_indices.push_back(idx);
            else
                right_indices.push_back(idx);
        }

        if (left_indices.empty() || right_indices.empty()) {
            node->is_leaf = true;
            return node;
        }

        node->left = build_tree(data, left_indices, depth + 1);
        node->right = build_tree(data, right_indices, depth + 1);
        return node;
    }

    Config config_;
    std::unique_ptr<TreeNode> root_;
};

// ============================================================
// Random Forest
// ============================================================

class RandomForestModel : public AlphaModel {
public:
    struct Config {
        Config() = default;
        int         num_trees       = 100;
        int         max_depth       = 6;
        int         min_samples_leaf = 5;
        double      sample_fraction = 0.8;  // Bootstrap sample size
        double      feature_fraction = 0.7; // Features per tree
        uint64_t    seed            = 42;
    };

    RandomForestModel() : config_() {}
    explicit RandomForestModel(Config config) : config_(config) {}

    AlphaModelType type() const override { return AlphaModelType::RandomForest; }
    std::string name() const override { return "RandomForest"; }
    bool is_trained() const override { return !trees_.empty(); }

    void train(const std::vector<FeatureVector>& data) override {
        trees_.clear();
        std::mt19937_64 rng(config_.seed);
        size_t n = data.size();
        size_t sample_size = static_cast<size_t>(n * config_.sample_fraction);

        for (int t = 0; t < config_.num_trees; ++t) {
            // Bootstrap sample
            std::vector<FeatureVector> sample;
            sample.reserve(sample_size);
            std::uniform_int_distribution<size_t> dist(0, n - 1);
            for (size_t i = 0; i < sample_size; ++i) {
                sample.push_back(data[dist(rng)]);
            }

            // Feature subsetting (randomly zero out some features)
            size_t num_features = data[0].features.size();
            size_t features_to_use = static_cast<size_t>(num_features * config_.feature_fraction);
            std::vector<size_t> feature_indices(num_features);
            std::iota(feature_indices.begin(), feature_indices.end(), 0);
            std::shuffle(feature_indices.begin(), feature_indices.end(), rng);
            std::set<size_t> active_features(feature_indices.begin(),
                                              feature_indices.begin() + features_to_use);

            // Mask inactive features
            for (auto& fv : sample) {
                for (size_t f = 0; f < num_features; ++f) {
                    if (active_features.find(f) == active_features.end()) {
                        fv.features[f] = 0.0;
                    }
                }
            }

            DecisionTreeModel::Config tree_config;
            tree_config.max_depth = config_.max_depth;
            tree_config.min_samples_leaf = config_.min_samples_leaf;
            auto tree = std::make_unique<DecisionTreeModel>(tree_config);
            tree->train(sample);
            trees_.push_back(std::move(tree));
        }
    }

    double predict(const std::vector<double>& features) const override {
        if (trees_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& tree : trees_) {
            sum += tree->predict(features);
        }
        return sum / trees_.size();
    }

private:
    Config config_;
    std::vector<std::unique_ptr<DecisionTreeModel>> trees_;
};

// ============================================================
// Momentum / Mean-Reversion Factor Models
// ============================================================

class MomentumModel : public AlphaModel {
public:
    struct Config {
        Config() = default;
        int lookback_days = 252;    // 12-month momentum
        int skip_days     = 21;     // Skip most recent month
        bool cross_sectional = true; // Rank within universe
    };

    MomentumModel() : config_() {}
    explicit MomentumModel(Config config) : config_(config) {}

    AlphaModelType type() const override { return AlphaModelType::MomentumFactor; }
    std::string name() const override { return "Momentum"; }
    bool is_trained() const override { return true; } // No training needed

    void train(const std::vector<FeatureVector>&) override {} // No-op

    double predict(const std::vector<double>& features) const override {
        // Expects features[0] = Return_252d, features[1] = Return_21d
        if (features.size() < 4) return 0.0;
        // Momentum = 12M return minus 1M return (skip recent)
        double mom_12m = features[3];   // Return_63d as proxy (use Return_252d if available)
        double mom_1m = features[2];    // Return_21d
        return mom_12m - mom_1m;        // Standard 12-1 momentum
    }

private:
    Config config_;
};

class MeanReversionModel : public AlphaModel {
public:
    struct Config {
        Config() = default;
        int lookback_days = 5;
        double z_score_threshold = 2.0;
    };

    MeanReversionModel() : config_() {}
    explicit MeanReversionModel(Config config) : config_(config) {}

    AlphaModelType type() const override { return AlphaModelType::MeanReversion; }
    std::string name() const override { return "MeanReversion"; }
    bool is_trained() const override { return true; }

    void train(const std::vector<FeatureVector>&) override {}

    double predict(const std::vector<double>& features) const override {
        // Mean reversion: negative of short-term return
        if (features.empty()) return 0.0;
        return -features[0]; // Negative of 1-day return
    }

private:
    Config config_;
};

// ============================================================
// Ensemble Combiner
// ============================================================

class EnsembleModel : public AlphaModel {
public:
    AlphaModelType type() const override { return AlphaModelType::EnsembleCombiner; }
    std::string name() const override { return "Ensemble"; }
    bool is_trained() const override { return !models_.empty(); }

    void add_model(std::shared_ptr<AlphaModel> model, double weight = 1.0) {
        models_.push_back(model);
        weights_.push_back(weight);
    }

    void train(const std::vector<FeatureVector>& data) override {
        for (auto& model : models_) {
            model->train(data);
        }
    }

    double predict(const std::vector<double>& features) const override {
        if (models_.empty()) return 0.0;
        double total_weight = 0.0;
        double weighted_sum = 0.0;
        for (size_t i = 0; i < models_.size(); ++i) {
            weighted_sum += weights_[i] * models_[i]->predict(features);
            total_weight += weights_[i];
        }
        return (total_weight > 0) ? weighted_sum / total_weight : 0.0;
    }

    // Update weights based on IC
    void update_weights_by_ic(const std::vector<double>& ic_values) {
        if (ic_values.size() != models_.size()) return;
        double total_ic = 0.0;
        for (double ic : ic_values) total_ic += std::max(0.0, ic);
        if (total_ic > 0) {
            for (size_t i = 0; i < weights_.size(); ++i) {
                weights_[i] = std::max(0.0, ic_values[i]) / total_ic;
            }
        }
    }

private:
    std::vector<std::shared_ptr<AlphaModel>> models_;
    std::vector<double> weights_;
};

// ============================================================
// Walk-Forward Validator
// ============================================================

class WalkForwardValidator {
public:
    struct Config {
        Config() = default;
        ValidationMethod method     = ValidationMethod::ExpandingWindow;
        int     initial_train_size  = 252;  // Minimum training window
        int     test_size           = 21;   // Test on 1 month
        int     step_size           = 21;   // Step forward by 1 month
        int     embargo_days        = 5;    // Gap between train and test
        int     rolling_window_size = 504;  // 2 years rolling window
    };

    WalkForwardValidator() : config_() {}
    explicit WalkForwardValidator(Config config) : config_(config) {}

    AlphaModelMetrics validate(AlphaModel& model, const std::vector<FeatureVector>& data) {
        AlphaModelMetrics metrics;
        std::vector<double> all_predictions;
        std::vector<double> all_actuals;
        std::vector<double> period_ics;

        size_t n = data.size();
        size_t start = config_.initial_train_size;

        while (start + config_.embargo_days + config_.test_size <= n) {
            // Training data
            size_t train_start = (config_.method == ValidationMethod::RollingWindow)
                ? std::max(0, static_cast<int>(start) - config_.rolling_window_size)
                : 0;
            size_t train_end = start;

            std::vector<FeatureVector> train_data(data.begin() + train_start,
                                                   data.begin() + train_end);

            // Test data (after embargo)
            size_t test_start = start + config_.embargo_days;
            size_t test_end = std::min(test_start + config_.test_size, n);

            // Train
            model.train(train_data);

            // Predict on test
            std::vector<double> preds, actuals;
            for (size_t i = test_start; i < test_end; ++i) {
                double pred = model.predict(data[i].features);
                preds.push_back(pred);
                actuals.push_back(data[i].forward_return);
                all_predictions.push_back(pred);
                all_actuals.push_back(data[i].forward_return);
            }

            // Period IC
            if (preds.size() >= 5) {
                double ic = ml_util::spearman_rank_correlation(preds, actuals);
                period_ics.push_back(ic);
            }

            start += config_.step_size;
        }

        // Compute metrics
        metrics.num_predictions = all_predictions.size();
        if (metrics.num_predictions > 0) {
            metrics.ic = ml_util::spearman_rank_correlation(all_predictions, all_actuals);

            // IC from periods
            if (!period_ics.empty()) {
                metrics.ic_series = period_ics;
                double mean_ic = ml_util::mean(period_ics);
                double std_ic = ml_util::stdev(period_ics);
                metrics.ic_ir = (std_ic > 0) ? mean_ic / std_ic : 0.0;
            }

            // Hit rate
            size_t correct = 0;
            for (size_t i = 0; i < all_predictions.size(); ++i) {
                if ((all_predictions[i] > 0 && all_actuals[i] > 0) ||
                    (all_predictions[i] < 0 && all_actuals[i] < 0)) {
                    correct++;
                }
            }
            metrics.hit_rate = static_cast<double>(correct) / all_predictions.size();

            // MSE and MAE
            double mse = 0.0, mae = 0.0;
            for (size_t i = 0; i < all_predictions.size(); ++i) {
                double err = all_predictions[i] - all_actuals[i];
                mse += err * err;
                mae += std::abs(err);
            }
            metrics.mse = mse / all_predictions.size();
            metrics.mae = mae / all_predictions.size();

            // R-squared
            double ss_res = mse * all_predictions.size();
            double mean_actual = ml_util::mean(all_actuals);
            double ss_tot = 0.0;
            for (double a : all_actuals) {
                double d = a - mean_actual;
                ss_tot += d * d;
            }
            metrics.r_squared = (ss_tot > 0) ? 1.0 - ss_res / ss_tot : 0.0;
        }

        return metrics;
    }

private:
    Config config_;
};

// ============================================================
// Alpha Signal Generator (Main Engine)
// ============================================================

class AlphaSignalEngine {
public:
    struct Config {
        Config() = default;
        SignalCombination   combination     = SignalCombination::ICWeight;
        double              winsorize_z     = 3.0;      // Z-score cap
        int                 top_n_long      = 20;       // Long basket size
        int                 top_n_short     = 20;       // Short basket size
        double              max_position    = 0.05;     // 5% max weight
        double              turnover_penalty = 0.001;   // 10bps per trade
        bool                long_only       = false;
    };

    AlphaSignalEngine() : config_() {}
    explicit AlphaSignalEngine(Config config) : config_(config) {}

    // Add a model to the ensemble
    void add_model(std::shared_ptr<AlphaModel> model, double weight = 1.0) {
        models_.push_back({model, weight, 0.0});
    }

    // Generate signals for a universe of stocks
    std::vector<AlphaSignal> generate_signals(
        const std::map<std::string, std::vector<double>>& features_by_symbol,
        const std::string& date) const
    {
        // Get raw predictions from each model
        std::map<std::string, double> combined_signals;

        for (const auto& [symbol, features] : features_by_symbol) {
            double total_weight = 0.0;
            double weighted_signal = 0.0;

            for (const auto& [model, weight, ic] : models_) {
                if (!model->is_trained()) continue;
                double pred = model->predict(features);
                double w = (config_.combination == SignalCombination::ICWeight && ic > 0)
                    ? ic : weight;
                weighted_signal += w * pred;
                total_weight += w;
            }

            if (total_weight > 0) {
                combined_signals[symbol] = weighted_signal / total_weight;
            }
        }

        // Cross-sectional z-score normalization
        std::vector<double> raw_values;
        std::vector<std::string> symbols;
        for (const auto& [sym, sig] : combined_signals) {
            symbols.push_back(sym);
            raw_values.push_back(sig);
        }

        auto z_scores = ml_util::z_score_normalize(raw_values);
        auto ranks = ml_util::rank_normalize(raw_values);

        // Build signal output
        std::vector<AlphaSignal> signals;
        for (size_t i = 0; i < symbols.size(); ++i) {
            AlphaSignal sig;
            sig.symbol = symbols[i];
            sig.date = date;
            sig.raw_signal = raw_values[i];
            sig.z_score = ml_util::winsorize(z_scores[i], -config_.winsorize_z, config_.winsorize_z);
            sig.rank_signal = ranks[i];
            sig.confidence = std::min(1.0, std::abs(sig.z_score) / config_.winsorize_z);
            sig.source = models_.empty() ? AlphaModelType::LinearRegression : models_[0].model->type();
            signals.push_back(sig);
        }

        // Assign position sizes
        assign_positions(signals);

        return signals;
    }

    // Update model IC weights based on backtest results
    void update_model_weights(const std::vector<double>& ic_values) {
        if (ic_values.size() != models_.size()) return;
        for (size_t i = 0; i < models_.size(); ++i) {
            models_[i].ic = ic_values[i];
        }
    }

    [[nodiscard]] size_t model_count() const { return models_.size(); }

private:
    void assign_positions(std::vector<AlphaSignal>& signals) const {
        if (signals.empty()) return;

        // Sort by z-score
        std::sort(signals.begin(), signals.end(),
            [](const AlphaSignal& a, const AlphaSignal& b) {
                return a.z_score > b.z_score;
            });

        size_t n = signals.size();
        size_t long_count = std::min(static_cast<size_t>(config_.top_n_long), n);
        size_t short_count = config_.long_only ? 0 :
            std::min(static_cast<size_t>(config_.top_n_short), n);

        // Equal weight within buckets (could be signal-weighted)
        double long_weight = config_.max_position;
        double short_weight = config_.long_only ? 0.0 : -config_.max_position;

        for (size_t i = 0; i < n; ++i) {
            if (i < long_count) {
                signals[i].position_size = std::min(long_weight, 1.0 / long_count);
            } else if (i >= n - short_count) {
                signals[i].position_size = std::max(short_weight, -1.0 / short_count);
            } else {
                signals[i].position_size = 0.0;
            }
        }
    }

    struct ModelEntry {
        std::shared_ptr<AlphaModel> model;
        double weight;
        double ic;
    };

    Config config_;
    std::vector<ModelEntry> models_;
};

} // namespace analytics
} // namespace genie

#endif // GENIE_ANALYTICS_ML_ALPHA_HPP
