/**
 * @file random.hpp
 * @brief Random number generation for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CORE_RANDOM_HPP
#define GENIE_CORE_RANDOM_HPP
#include "types.hpp"
#include "math_utils.hpp"

namespace genie {
class RandomGenerator {
    std::mt19937_64 engine_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
    std::normal_distribution<double> normal_{0.0, 1.0};
    mutable std::mutex mutex_;
public:
    RandomGenerator() : engine_(std::random_device{}()) {}
    explicit RandomGenerator(uint64_t seed) : engine_(seed) {}
    void seed(uint64_t s) { std::lock_guard<std::mutex> lk(mutex_); engine_.seed(s); }
    [[nodiscard]] double uniform(double min = 0.0, double max = 1.0) { std::lock_guard<std::mutex> lk(mutex_); return min + (max - min) * uniform_(engine_); }
    [[nodiscard]] double normal(double m = 0.0, double s = 1.0) { std::lock_guard<std::mutex> lk(mutex_); return m + s * normal_(engine_); }
    [[nodiscard]] double lognormal(double m = 0.0, double s = 1.0) { return std::exp(normal(m, s)); }
    [[nodiscard]] double exponential(double lambda = 1.0) { return -std::log(1.0 - uniform()) / lambda; }
    [[nodiscard]] int poisson(double lambda) { double L = std::exp(-lambda); int k = 0; double p = 1.0; do { ++k; p *= uniform(); } while (p > L); return k - 1; }
    [[nodiscard]] std::vector<double> normal_vector(size_t n, double m = 0.0, double s = 1.0) {
        std::vector<double> r(n); for (auto& x : r) x = normal(m, s); return r;
    }
    [[nodiscard]] std::vector<double> correlated_normals(const math::Matrix& chol) {
        size_t n = chol.size(); std::vector<double> z = normal_vector(n), r(n, 0.0);
        for (size_t i = 0; i < n; ++i) for (size_t j = 0; j <= i; ++j) r[i] += chol[i][j] * z[j];
        return r;
    }
    [[nodiscard]] double gbm_step(double S, double drift, double vol, double dt) {
        return S * std::exp((drift - 0.5 * vol * vol) * dt + vol * std::sqrt(dt) * normal());
    }
    [[nodiscard]] std::vector<double> gbm_path(double S0, double drift, double vol, double T, int steps) {
        std::vector<double> path(steps + 1); path[0] = S0; double dt = T / steps;
        for (int i = 1; i <= steps; ++i) path[i] = gbm_step(path[i-1], drift, vol, dt);
        return path;
    }
};

inline RandomGenerator& rng() { static RandomGenerator inst; return inst; }
} // namespace genie
#endif
