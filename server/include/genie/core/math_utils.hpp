/**
 * @file math_utils.hpp
 * @brief Mathematical utilities for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CORE_MATH_UTILS_HPP
#define GENIE_CORE_MATH_UTILS_HPP
#include "types.hpp"
namespace genie::math {
inline constexpr double PI = 3.14159265358979323846;
inline constexpr double SQRT_2PI = 2.50662827463100050242;
inline constexpr double EPSILON = 1e-10;
using Matrix = std::vector<std::vector<double>>;

[[nodiscard]] inline double sum(const std::vector<double>& d) { return std::accumulate(d.begin(), d.end(), 0.0); }
[[nodiscard]] inline double mean(const std::vector<double>& d) { return d.empty() ? 0.0 : sum(d) / static_cast<double>(d.size()); }
[[nodiscard]] inline double variance(const std::vector<double>& d, bool sample = true) {
    if (d.size() < 2) return 0.0;
    double m = mean(d), s = 0.0;
    for (double x : d) { double diff = x - m; s += diff * diff; }
    return s / static_cast<double>(sample ? (d.size() - 1) : d.size());
}
[[nodiscard]] inline double stddev(const std::vector<double>& d, bool sample = true) { return std::sqrt(variance(d, sample)); }
[[nodiscard]] inline double covariance(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2) return 0.0;
    double mx = mean(x), my = mean(y), s = 0.0;
    for (size_t i = 0; i < x.size(); ++i) s += (x[i] - mx) * (y[i] - my);
    return s / static_cast<double>(x.size() - 1);
}
[[nodiscard]] inline double correlation(const std::vector<double>& x, const std::vector<double>& y) {
    double sx = stddev(x), sy = stddev(y);
    return (sx < EPSILON || sy < EPSILON) ? 0.0 : covariance(x, y) / (sx * sy);
}
[[nodiscard]] inline double beta(const std::vector<double>& a, const std::vector<double>& m) {
    double v = variance(m); return v < EPSILON ? 1.0 : covariance(a, m) / v;
}
[[nodiscard]] inline double percentile(std::vector<double> d, double pct) {
    if (d.empty()) return 0.0;
    std::sort(d.begin(), d.end());
    double idx = (pct / 100.0) * static_cast<double>(d.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx)), hi = static_cast<size_t>(std::ceil(idx));
    return (lo == hi || hi >= d.size()) ? d[lo] : d[lo] * (1.0 - (idx - static_cast<double>(lo))) + d[hi] * (idx - static_cast<double>(lo));
}
[[nodiscard]] inline double compound_return(const std::vector<double>& r) {
    double res = 1.0; for (double x : r) res *= (1.0 + x); return res - 1.0;
}
[[nodiscard]] inline double annualized_return(double total, double years) {
    return years <= 0 ? 0.0 : std::pow(1.0 + total, 1.0 / years) - 1.0;
}
[[nodiscard]] inline double annualized_volatility(double vol, Frequency f = Frequency::Daily) {
    return vol * std::sqrt(static_cast<double>(frequency_to_periods_per_year(f)));
}
[[nodiscard]] inline double sharpe_ratio(double ret, double rf, double vol) { return vol < EPSILON ? 0.0 : (ret - rf) / vol; }
[[nodiscard]] inline double max_drawdown(const std::vector<double>& r) {
    if (r.empty()) return 0.0;
    double peak = 1.0, max_dd = 0.0, val = 1.0;
    for (double x : r) { val *= (1.0 + x); peak = std::max(peak, val); max_dd = std::max(max_dd, (peak - val) / peak); }
    return max_dd;
}
[[nodiscard]] inline double downside_deviation(const std::vector<double>& r, double mar = 0.0) {
    if (r.empty()) return 0.0;
    double s = 0.0; for (double x : r) if (x < mar) s += (x - mar) * (x - mar);
    return std::sqrt(s / static_cast<double>(r.size()));
}
[[nodiscard]] inline double normal_pdf(double x, double m = 0.0, double s = 1.0) {
    double z = (x - m) / s; return std::exp(-0.5 * z * z) / (s * SQRT_2PI);
}
[[nodiscard]] inline double normal_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }
[[nodiscard]] inline double normal_inverse_cdf(double p) {
    if (p <= 0.0) return -1e10;
    if (p >= 1.0) return 1e10;
    if (p > 0.5) return -normal_inverse_cdf(1.0 - p);
    double t = std::sqrt(-2.0 * std::log(p));
    return -(t - (2.515517 + 0.802853*t + 0.010328*t*t) / (1.0 + 1.432788*t + 0.189269*t*t + 0.001308*t*t*t));
}
[[nodiscard]] inline double bs_d1(double S, double K, double r, double sig, double T) {
    return (T < EPSILON || sig < EPSILON) ? 0.0 : (std::log(S/K) + (r + 0.5*sig*sig)*T) / (sig*std::sqrt(T));
}
[[nodiscard]] inline double bs_d2(double S, double K, double r, double sig, double T) { return bs_d1(S,K,r,sig,T) - sig*std::sqrt(T); }
[[nodiscard]] inline double black_scholes_call(double S, double K, double r, double sig, double T) {
    if (T < EPSILON) return std::max(0.0, S - K);
    return S * normal_cdf(bs_d1(S,K,r,sig,T)) - K * std::exp(-r*T) * normal_cdf(bs_d2(S,K,r,sig,T));
}
[[nodiscard]] inline double black_scholes_put(double S, double K, double r, double sig, double T) {
    if (T < EPSILON) return std::max(0.0, K - S);
    return K * std::exp(-r*T) * normal_cdf(-bs_d2(S,K,r,sig,T)) - S * normal_cdf(-bs_d1(S,K,r,sig,T));
}
[[nodiscard]] inline double bs_delta_call(double S, double K, double r, double sig, double T) { return normal_cdf(bs_d1(S,K,r,sig,T)); }
[[nodiscard]] inline double bs_delta_put(double S, double K, double r, double sig, double T) { return bs_delta_call(S,K,r,sig,T) - 1.0; }
[[nodiscard]] inline double bs_gamma(double S, double K, double r, double sig, double T) {
    return (T < EPSILON || sig < EPSILON || S < EPSILON) ? 0.0 : normal_pdf(bs_d1(S,K,r,sig,T)) / (S * sig * std::sqrt(T));
}
[[nodiscard]] inline double bs_vega(double S, double K, double r, double sig, double T) {
    return T < EPSILON ? 0.0 : S * std::sqrt(T) * normal_pdf(bs_d1(S,K,r,sig,T)) / 100.0;
}
[[nodiscard]] inline double bond_price(double face, double coupon, double yield, int periods, int freq = 2) {
    double c = face * coupon / freq, y = yield / freq, pv = 0.0;
    for (int t = 1; t <= periods * freq; ++t) pv += c / std::pow(1.0 + y, t);
    return pv + face / std::pow(1.0 + y, periods * freq);
}
[[nodiscard]] inline double modified_duration(double face, double coupon, double yield, int periods, int freq = 2) {
    double y = yield / freq, c = face * coupon / freq, price = bond_price(face, coupon, yield, periods, freq), mac = 0.0;
    for (int t = 1; t <= periods * freq; ++t) { double cf = c; if (t == periods * freq) cf += face; mac += t * cf / std::pow(1.0 + y, t); }
    return (mac / price / freq) / (1.0 + y);
}
[[nodiscard]] inline double dv01(double face, double coupon, double yield, int periods, int freq = 2) {
    return (bond_price(face, coupon, yield - 0.0001, periods, freq) - bond_price(face, coupon, yield + 0.0001, periods, freq)) / 2.0;
}
[[nodiscard]] inline Matrix covariance_matrix(const std::vector<std::vector<double>>& data) {
    size_t n = data.size(); Matrix res(n, std::vector<double>(n));
    for (size_t i = 0; i < n; ++i) for (size_t j = i; j < n; ++j) { double c = covariance(data[i], data[j]); res[i][j] = res[j][i] = c; }
    return res;
}
[[nodiscard]] inline Matrix correlation_matrix(const std::vector<std::vector<double>>& data) {
    size_t n = data.size(); Matrix res(n, std::vector<double>(n));
    for (size_t i = 0; i < n; ++i) for (size_t j = i; j < n; ++j) { double c = correlation(data[i], data[j]); res[i][j] = res[j][i] = c; }
    return res;
}
[[nodiscard]] inline Matrix cholesky_decomposition(const Matrix& m) {
    size_t n = m.size(); Matrix L(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j <= i; ++j) {
        double s = 0.0; for (size_t k = 0; k < j; ++k) s += L[i][k] * L[j][k];
        L[i][j] = (i == j) ? std::sqrt(std::max(0.0, m[i][i] - s)) : (L[j][j] > EPSILON ? (m[i][j] - s) / L[j][j] : 0.0);
    }
    return L;
}
} // namespace genie::math
#endif
