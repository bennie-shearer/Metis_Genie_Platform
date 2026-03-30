/**
 * @file benchmark.hpp
 * @brief Benchmark comparison engine for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_BENCHMARK_HPP
#define GENIE_BENCHMARK_HPP

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>

namespace genie {
namespace benchmark {

struct BenchmarkData {
    std::string id;
    std::string name;
    std::vector<double> returns;
    double annualized_return{0};
    double volatility{0};
};

struct RelativeMetrics {
    double alpha{0};
    double beta{0};
    double tracking_error{0};
    double information_ratio{0};
    double active_return{0};
    double r_squared{0};
    double up_capture{0};
    double down_capture{0};
    double batting_average{0};  // % of periods outperforming
    std::vector<double> rolling_excess_returns;
};

class BenchmarkEngine {
    std::map<std::string, BenchmarkData> benchmarks_;
    double risk_free_rate_{0.02};
    
public:
    void set_risk_free_rate(double rate) { risk_free_rate_ = rate; }
    
    void add_benchmark(const std::string& id, const std::string& name, 
                       const std::vector<double>& returns) {
        BenchmarkData b;
        b.id = id;
        b.name = name;
        b.returns = returns;
        
        if (!returns.empty()) {
            // Calculate annualized return
            double product = 1.0;
            for (double r : returns) product *= (1.0 + r);
            b.annualized_return = std::pow(product, 252.0 / returns.size()) - 1.0;
            
            // Calculate volatility
            double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double var = 0;
            for (double r : returns) var += (r - mean) * (r - mean);
            b.volatility = std::sqrt(var / (returns.size() - 1)) * std::sqrt(252.0);
        }
        
        benchmarks_[id] = b;
    }
    
    // Add common benchmarks
    void add_sp500(const std::vector<double>& returns) {
        add_benchmark("SPX", "S&P 500", returns);
    }
    
    void add_nasdaq(const std::vector<double>& returns) {
        add_benchmark("NDX", "NASDAQ 100", returns);
    }
    
    void add_russell2000(const std::vector<double>& returns) {
        add_benchmark("RUT", "Russell 2000", returns);
    }
    
    void add_agg_bond(const std::vector<double>& returns) {
        add_benchmark("AGG", "Bloomberg Aggregate Bond", returns);
    }
    
    [[nodiscard]] bool has_benchmark(const std::string& id) const {
        return benchmarks_.count(id) > 0;
    }
    
    const BenchmarkData& get_benchmark(const std::string& id) const {
        return benchmarks_.at(id);
    }
    
    RelativeMetrics compare(const std::vector<double>& portfolio_returns,
                            const std::string& benchmark_id) const {
        RelativeMetrics m;
        
        if (!benchmarks_.count(benchmark_id)) return m;
        const auto& bench = benchmarks_.at(benchmark_id);
        
        size_t n = std::min(portfolio_returns.size(), bench.returns.size());
        if (n < 2) return m;
        
        // Calculate means
        double port_mean = 0, bench_mean = 0;
        for (size_t i = 0; i < n; ++i) {
            port_mean += portfolio_returns[i];
            bench_mean += bench.returns[i];
        }
        port_mean /= n;
        bench_mean /= n;
        
        // Calculate beta and alpha
        double covar = 0, bench_var = 0;
        for (size_t i = 0; i < n; ++i) {
            covar += (portfolio_returns[i] - port_mean) * (bench.returns[i] - bench_mean);
            bench_var += (bench.returns[i] - bench_mean) * (bench.returns[i] - bench_mean);
        }
        covar /= (n - 1);
        bench_var /= (n - 1);
        
        m.beta = (bench_var > 0.0001) ? covar / bench_var : 1.0;
        
        // Annualized returns for alpha calculation
        double port_annual = port_mean * 252.0;
        double bench_annual = bench_mean * 252.0;
        m.alpha = port_annual - (risk_free_rate_ + m.beta * (bench_annual - risk_free_rate_));
        
        // Tracking error
        double te_var = 0;
        for (size_t i = 0; i < n; ++i) {
            double excess = portfolio_returns[i] - bench.returns[i];
            te_var += excess * excess;
            m.rolling_excess_returns.push_back(excess);
        }
        m.tracking_error = std::sqrt(te_var / (n - 1)) * std::sqrt(252.0);
        
        // Information ratio
        m.active_return = port_annual - bench_annual;
        m.information_ratio = (m.tracking_error > 0.0001) ? m.active_return / m.tracking_error : 0;
        
        // R-squared
        double ss_res = 0, ss_tot = 0;
        for (size_t i = 0; i < n; ++i) {
            double predicted = port_mean + m.beta * (bench.returns[i] - bench_mean);
            ss_res += (portfolio_returns[i] - predicted) * (portfolio_returns[i] - predicted);
            ss_tot += (portfolio_returns[i] - port_mean) * (portfolio_returns[i] - port_mean);
        }
        m.r_squared = (ss_tot > 0.0001) ? 1.0 - (ss_res / ss_tot) : 0;
        
        // Up/Down capture
        double up_port = 0, up_bench = 0, down_port = 0, down_bench = 0;
        int up_count = 0, down_count = 0;
        int outperform_count = 0;
        
        for (size_t i = 0; i < n; ++i) {
            if (bench.returns[i] > 0) {
                up_port += portfolio_returns[i];
                up_bench += bench.returns[i];
                ++up_count;
            } else if (bench.returns[i] < 0) {
                down_port += portfolio_returns[i];
                down_bench += bench.returns[i];
                ++down_count;
            }
            if (portfolio_returns[i] > bench.returns[i]) ++outperform_count;
        }
        
        m.up_capture = (up_count > 0 && up_bench != 0) ? (up_port / up_count) / (up_bench / up_count) * 100 : 100;
        m.down_capture = (down_count > 0 && down_bench != 0) ? (down_port / down_count) / (down_bench / down_count) * 100 : 100;
        m.batting_average = static_cast<double>(outperform_count) / n * 100;
        
        return m;
    }
    
    // Generate comparison report
    [[nodiscard]] std::string report(const std::vector<double>& portfolio_returns,
                       const std::string& benchmark_id) const {
        auto m = compare(portfolio_returns, benchmark_id);
        const auto& bench = benchmarks_.at(benchmark_id);
        
        std::ostringstream ss;
        ss << "=== BENCHMARK COMPARISON REPORT ===\n";
        ss << "Benchmark: " << bench.name << " (" << bench.id << ")\n\n";
        ss << std::fixed << std::setprecision(2);
        ss << "Alpha (annualized):    " << (m.alpha * 100) << "%\n";
        ss << "Beta:                  " << std::setprecision(3) << m.beta << "\n";
        ss << "R-Squared:             " << std::setprecision(2) << (m.r_squared * 100) << "%\n";
        ss << "Tracking Error:        " << (m.tracking_error * 100) << "%\n";
        ss << "Information Ratio:     " << std::setprecision(3) << m.information_ratio << "\n";
        ss << "Active Return:         " << std::setprecision(2) << (m.active_return * 100) << "%\n";
        ss << "Up Capture Ratio:      " << m.up_capture << "%\n";
        ss << "Down Capture Ratio:    " << m.down_capture << "%\n";
        ss << "Batting Average:       " << m.batting_average << "%\n";
        
        return ss.str();
    }
    
    std::vector<std::string> list_benchmarks() const {
        std::vector<std::string> result;
        for (const auto& [id, b] : benchmarks_) {
            result.push_back(id + " - " + b.name);
        }
        return result;
    }
};

} // namespace benchmark
} // namespace genie
#endif // GENIE_BENCHMARK_HPP
