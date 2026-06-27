/**
 * @file backtesting.hpp
 * @brief Backtesting framework for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_BACKTESTING_HPP
#define GENIE_BACKTESTING_HPP

#include <string>
#include <vector>
#include <map>
#include "../core/thread_pool.hpp"
#include <functional>
#include <memory>
#include <chrono>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace genie {
namespace backtest {

using TimePoint = std::chrono::system_clock::time_point;

struct PriceBar {
    TimePoint timestamp;
    double open;
    double high;
    double low;
    double close;
    double volume;
};

struct Signal {
    enum class Type { Buy, Sell, Hold };
    Type type{Type::Hold};
    std::string security_id;
    double target_weight{0.0};
    double confidence{1.0};
};

struct BacktestTrade {
    TimePoint timestamp;
    std::string security_id;
    bool is_buy;
    double quantity;
    double price;
    double commission;
    double slippage;
};

struct EquityCurvePoint {
    TimePoint timestamp;
    double portfolio_value;
    double cash;
    double positions_value;
    double daily_return;
    double cumulative_return;
    double drawdown;
};

struct BacktestResult {
    // Returns
    double total_return{0};
    double annualized_return{0};
    double volatility{0};
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};
    
    // Drawdown
    double max_drawdown{0};
    int max_drawdown_duration{0};
    
    // Trading
    int total_trades{0};
    int winning_trades{0};
    int losing_trades{0};
    double win_rate{0};
    double profit_factor{0};
    double avg_win{0};
    double avg_loss{0};
    
    // Time
    int trading_days{0};
    TimePoint start_date;
    TimePoint end_date;
    
    // Equity curve
    std::vector<EquityCurvePoint> equity_curve;
    std::vector<BacktestTrade> trades;
    
    [[nodiscard]] std::string summary() const {
        std::ostringstream ss;
        ss << "=== BACKTEST RESULTS ===\n"
           << "Total Return: " << std::fixed << std::setprecision(2) << (total_return * 100) << "%\n"
           << "Annualized Return: " << (annualized_return * 100) << "%\n"
           << "Volatility: " << (volatility * 100) << "%\n"
           << "Sharpe Ratio: " << std::setprecision(3) << sharpe_ratio << "\n"
           << "Sortino Ratio: " << sortino_ratio << "\n"
           << "Max Drawdown: " << std::setprecision(2) << (max_drawdown * 100) << "%\n"
           << "Win Rate: " << (win_rate * 100) << "%\n"
           << "Total Trades: " << total_trades << "\n"
           << "Profit Factor: " << std::setprecision(2) << profit_factor << "\n";
        return ss.str();
    }
};

// Strategy interface
class Strategy {
public:
    virtual ~Strategy() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual void initialize() {}
    virtual Signal generate_signal(const std::string& security_id, 
                                   const std::vector<PriceBar>& history,
                                   const std::map<std::string, double>& current_positions) = 0;
    virtual void on_trade(const BacktestTrade& /*trade*/) {}
};

// Simple Moving Average Crossover Strategy
class SMACrossover : public Strategy {
    int fast_period_;
    int slow_period_;
    
    [[nodiscard]] double sma(const std::vector<PriceBar>& bars, int period) const {
        if (bars.size() < static_cast<size_t>(period)) return 0;
        double sum = 0;
        for (size_t i = bars.size() - period; i < bars.size(); ++i) {
            sum += bars[i].close;
        }
        return sum / period;
    }
    
public:
    SMACrossover(int fast = 10, int slow = 30) : fast_period_(fast), slow_period_(slow) {}
    
    [[nodiscard]] std::string name() const override { return "SMA Crossover (" + std::to_string(fast_period_) + "/" + std::to_string(slow_period_) + ")"; }
    
    Signal generate_signal(const std::string& security_id,
                          const std::vector<PriceBar>& history,
                          const std::map<std::string, double>& positions) override {
        Signal sig;
        sig.security_id = security_id;
        
        if (history.size() < static_cast<size_t>(slow_period_ + 1)) {
            sig.type = Signal::Type::Hold;
            return sig;
        }
        
        double fast_ma = sma(history, fast_period_);
        double slow_ma = sma(history, slow_period_);
        
        // Previous values
        std::vector<PriceBar> prev_history(history.begin(), history.end() - 1);
        double prev_fast = sma(prev_history, fast_period_);
        double prev_slow = sma(prev_history, slow_period_);
        
        bool has_position = positions.count(security_id) && positions.at(security_id) > 0;
        
        // Crossover detection
        if (prev_fast <= prev_slow && fast_ma > slow_ma) {
            sig.type = Signal::Type::Buy;
            sig.target_weight = 1.0;
        } else if (prev_fast >= prev_slow && fast_ma < slow_ma && has_position) {
            sig.type = Signal::Type::Sell;
            sig.target_weight = 0.0;
        } else {
            sig.type = Signal::Type::Hold;
        }
        
        return sig;
    }
};

// Momentum Strategy
class MomentumStrategy : public Strategy {
    int lookback_;
    double threshold_;
    
public:
    MomentumStrategy(int lookback = 20, double threshold = 0.02) 
        : lookback_(lookback), threshold_(threshold) {}
    
    [[nodiscard]] std::string name() const override { return "Momentum (" + std::to_string(lookback_) + "d)"; }
    
    Signal generate_signal(const std::string& security_id,
                          const std::vector<PriceBar>& history,
                          const std::map<std::string, double>& positions) override {
        Signal sig;
        sig.security_id = security_id;
        
        if (history.size() < static_cast<size_t>(lookback_)) {
            sig.type = Signal::Type::Hold;
            return sig;
        }
        
        double current = history.back().close;
        double past = history[history.size() - lookback_].close;
        double momentum = (current - past) / past;
        
        bool has_position = positions.count(security_id) && positions.at(security_id) > 0;
        
        if (momentum > threshold_ && !has_position) {
            sig.type = Signal::Type::Buy;
            sig.target_weight = 1.0;
            sig.confidence = std::min(1.0, momentum / threshold_);
        } else if (momentum < -threshold_ && has_position) {
            sig.type = Signal::Type::Sell;
            sig.target_weight = 0.0;
        } else {
            sig.type = Signal::Type::Hold;
        }
        
        return sig;
    }
};

// Backtesting Engine
class BacktestEngine {
    double initial_capital_;
    double slippage_bps_{5.0};
    double commission_per_share_{0.01};
    double risk_free_rate_{0.02};
    
public:
    explicit BacktestEngine(double initial_capital = 100000.0) : initial_capital_(initial_capital) {}
    
    void set_slippage(double bps) { slippage_bps_ = bps; }
    void set_commission(double per_share) { commission_per_share_ = per_share; }
    void set_risk_free_rate(double rate) { risk_free_rate_ = rate; }
    
    [[nodiscard]] BacktestResult run(Strategy& strategy,
                       const std::map<std::string, std::vector<PriceBar>>& market_data) {
        BacktestResult result;
        
        if (market_data.empty()) return result;
        
        // Find date range
        size_t min_bars = SIZE_MAX;
        for (const auto& [id, bars] : market_data) {
            min_bars = std::min(min_bars, bars.size());
        }
        if (min_bars == 0) return result;
        
        strategy.initialize();
        
        double cash = initial_capital_;
        std::map<std::string, double> positions;
        std::map<std::string, double> position_costs;
        std::vector<double> daily_returns;
        double peak_value = initial_capital_;
        double max_dd = 0;
        
        // Simulate day by day
        for (size_t day = 30; day < min_bars; ++day) {
            // Get current prices
            std::map<std::string, double> prices;
            for (const auto& [id, bars] : market_data) {
                prices[id] = bars[day].close;
            }
            
            // Calculate portfolio value
            double positions_value = 0;
            for (const auto& [id, qty] : positions) {
                if (prices.count(id)) {
                    positions_value += qty * prices[id];
                }
            }
            double portfolio_value = cash + positions_value;
            
            // Generate signals and execute
            for (const auto& [security_id, bars] : market_data) {
                std::vector<PriceBar> history(bars.begin(), bars.begin() + day + 1);
                Signal signal = strategy.generate_signal(security_id, history, positions);
                
                if (signal.type == Signal::Type::Buy && cash > 0) {
                    double price = prices[security_id];
                    double slippage = price * slippage_bps_ / 10000.0;
                    double exec_price = price + slippage;
                    
                    double target_value = portfolio_value * signal.target_weight * signal.confidence;
                    double qty = std::floor((std::min(cash, target_value) - 100) / exec_price);
                    
                    if (qty > 0) {
                        double cost = qty * exec_price;
                        double comm = qty * commission_per_share_;
                        cash -= (cost + comm);
                        positions[security_id] += qty;
                        position_costs[security_id] += cost;
                        
                        BacktestTrade trade{bars[day].timestamp, security_id, true, qty, exec_price, comm, slippage * qty};
                        result.trades.push_back(trade);
                        strategy.on_trade(trade);
                    }
                } else if (signal.type == Signal::Type::Sell && positions.count(security_id) && positions[security_id] > 0) {
                    double price = prices[security_id];
                    double slippage = price * slippage_bps_ / 10000.0;
                    double exec_price = price - slippage;
                    
                    double qty = positions[security_id];
                    double proceeds = qty * exec_price;
                    double comm = qty * commission_per_share_;
                    
                    cash += (proceeds - comm);
                    positions[security_id] = 0;
                    
                    BacktestTrade trade{bars[day].timestamp, security_id, false, qty, exec_price, comm, slippage * qty};
                    result.trades.push_back(trade);
                    strategy.on_trade(trade);
                }
            }
            
            // Update portfolio value after trades
            positions_value = 0;
            for (const auto& [id, qty] : positions) {
                if (prices.count(id)) {
                    positions_value += qty * prices[id];
                }
            }
            double new_value = cash + positions_value;
            
            // Calculate return
            double daily_ret = (day > 30) ? (new_value / portfolio_value - 1.0) : 0.0;
            daily_returns.push_back(daily_ret);
            
            // Update drawdown
            peak_value = std::max(peak_value, new_value);
            double dd = (peak_value - new_value) / peak_value;
            max_dd = std::max(max_dd, dd);
            
            // Record equity curve
            EquityCurvePoint ecp;
            ecp.timestamp = market_data.begin()->second[day].timestamp;
            ecp.portfolio_value = new_value;
            ecp.cash = cash;
            ecp.positions_value = positions_value;
            ecp.daily_return = daily_ret;
            ecp.cumulative_return = (new_value / initial_capital_) - 1.0;
            ecp.drawdown = dd;
            result.equity_curve.push_back(ecp);
        }
        
        // Calculate final metrics
        double final_value = cash;
        for (const auto& [id, qty] : positions) {
            final_value += qty * market_data.at(id).back().close;
        }
        
        result.total_return = (final_value / initial_capital_) - 1.0;
        result.trading_days = static_cast<int>(daily_returns.size());
        result.annualized_return = std::pow(1.0 + result.total_return, 252.0 / result.trading_days) - 1.0;
        
        // Volatility
        double mean_ret = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
        double var = 0;
        for (double r : daily_returns) var += (r - mean_ret) * (r - mean_ret);
        var /= daily_returns.size();
        result.volatility = std::sqrt(var) * std::sqrt(252.0);
        
        // Sharpe
        result.sharpe_ratio = (result.annualized_return - risk_free_rate_) / result.volatility;
        
        // Sortino (downside deviation)
        double downside_var = 0;
        int downside_count = 0;
        for (double r : daily_returns) {
            if (r < 0) { downside_var += r * r; ++downside_count; }
        }
        double downside_dev = (downside_count > 0) ? std::sqrt(downside_var / downside_count) * std::sqrt(252.0) : 0.0001;
        result.sortino_ratio = (result.annualized_return - risk_free_rate_) / downside_dev;
        
        // Drawdown
        result.max_drawdown = max_dd;
        result.calmar_ratio = (max_dd > 0.0001) ? result.annualized_return / max_dd : 0;
        
        // Trade analysis
        result.total_trades = static_cast<int>(result.trades.size());
        double gross_profit = 0, gross_loss = 0;
        for (size_t i = 0; i < result.trades.size(); i += 2) {
            if (i + 1 < result.trades.size()) {
                double pnl = (result.trades[i+1].price - result.trades[i].price) * result.trades[i].quantity;
                pnl -= result.trades[i].commission + result.trades[i+1].commission;
                if (pnl > 0) { ++result.winning_trades; gross_profit += pnl; }
                else { ++result.losing_trades; gross_loss += std::abs(pnl); }
            }
        }
        
        result.win_rate = (result.winning_trades + result.losing_trades > 0) 
            ? static_cast<double>(result.winning_trades) / (result.winning_trades + result.losing_trades) : 0;
        result.profit_factor = (gross_loss > 0) ? gross_profit / gross_loss : 0;
        result.avg_win = (result.winning_trades > 0) ? gross_profit / result.winning_trades : 0;
        result.avg_loss = (result.losing_trades > 0) ? gross_loss / result.losing_trades : 0;
        
        return result;
    }

    /**
     * Run multiple strategies in parallel against the same market data.
     * Returns one BacktestResult per strategy, in order.
     */
    std::vector<BacktestResult> parallel_run(
            std::vector<std::shared_ptr<Strategy>> strategies,
            const std::map<std::string, std::vector<PriceBar>>& market_data) {
        std::vector<BacktestResult> results(strategies.size());

        if (strategies.size() <= 1) {
            for (size_t i = 0; i < strategies.size(); ++i)
                results[i] = run(*strategies[i], market_data);
            return results;
        }

        // Each strategy gets its own BacktestEngine copy (independent state)
        thread_pool().parallel_for(0, strategies.size(),
            [&](size_t i) {
                BacktestEngine engine(initial_capital_);
                engine.set_slippage(slippage_bps_);
                engine.set_commission(commission_per_share_);
                engine.set_risk_free_rate(risk_free_rate_);
                results[i] = engine.run(*strategies[i], market_data);
            });

        return results;
    }
};

} // namespace backtest
} // namespace genie
#endif // GENIE_BACKTESTING_HPP
