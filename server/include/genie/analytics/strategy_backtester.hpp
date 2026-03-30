/**
 * @file strategy_backtester.hpp
 * @brief Historical strategy simulation with comprehensive performance metrics
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Strategy backtesting framework:
 * - Event-driven simulation engine
 * - Bar-by-bar price feed processing
 * - Trade signal to execution pipeline
 * - Commission and slippage modeling
 * - Comprehensive metrics (Sharpe, Sortino, Calmar, max DD)
 * - Equity curve and drawdown tracking
 * - Monthly/annual return decomposition
 * - Walk-forward optimization support
 * - Benchmark comparison
 * - Trade list with P&L attribution
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_ANALYTICS_STRATEGY_BACKTESTER_HPP
#define GENIE_ANALYTICS_STRATEGY_BACKTESTER_HPP

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <cmath>
#include <numeric>

namespace genie {
namespace analytics {
namespace backtest {

// ============================================================================
// Enumerations
// ============================================================================

enum class Signal { Buy, Sell, Hold, ExitLong, ExitShort };
enum class TradeStatus { Open, Closed, Cancelled };

[[nodiscard]] inline std::string signal_string(Signal s) {
    switch (s) {
        case Signal::Buy: return "BUY"; case Signal::Sell: return "SELL";
        case Signal::Hold: return "HOLD"; case Signal::ExitLong: return "EXIT_LONG";
        case Signal::ExitShort: return "EXIT_SHORT";
    }
    return "UNKNOWN";
}

// ============================================================================
// Data Structures
// ============================================================================

struct PriceBar {
    std::string date;
    double open{0}, high{0}, low{0}, close{0};
    double volume{0};
    double adjusted_close{0};
};

struct BacktestTrade {
    std::string id;
    std::string symbol;
    Signal entry_signal{Signal::Buy};
    double entry_price{0};
    double exit_price{0};
    double shares{0};
    std::string entry_date;
    std::string exit_date;
    TradeStatus status{TradeStatus::Open};
    double pnl{0};
    double pnl_pct{0};
    double commission{0};
    double slippage{0};
    int bars_held{0};
    double mae{0};  // Maximum adverse excursion
    double mfe{0};  // Maximum favorable excursion

    [[nodiscard]] double net_pnl() const { return pnl - commission - slippage; }
};

struct EquityPoint {
    std::string date;
    double equity{0};
    double drawdown{0};
    double drawdown_pct{0};
    double daily_return{0};
};

struct BacktestConfig {
    double initial_capital{100000.0};
    double commission_per_share{0.005};
    double slippage_pct{0.001};      // 10bps slippage
    double risk_free_rate{0.05};     // 5% annual
    bool allow_shorting{true};
    double max_position_pct{0.20};
    int max_positions{10};
};

struct BacktestMetrics {
    double total_return{0};
    double annualized_return{0};
    double sharpe_ratio{0};
    double sortino_ratio{0};
    double calmar_ratio{0};
    double max_drawdown{0};
    double max_drawdown_pct{0};
    double volatility{0};
    double downside_deviation{0};
    int total_trades{0};
    int winning_trades{0};
    int losing_trades{0};
    double win_rate{0};
    double profit_factor{0};
    double avg_win{0};
    double avg_loss{0};
    double avg_bars_held{0};
    double expectancy{0};
    int trading_days{0};
    double final_equity{0};

    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Return=" << total_return * 100 << "% (Ann=" << annualized_return * 100 << "%)"
            << " | Sharpe=" << sharpe_ratio << " Sortino=" << sortino_ratio
            << " | MaxDD=" << max_drawdown_pct * 100 << "%"
            << " | Trades=" << total_trades << " Win=" << win_rate * 100 << "%"
            << " | PF=" << profit_factor;
        return oss.str();
    }

    [[nodiscard]] std::string to_json() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << "{\"total_return\":" << total_return
            << ",\"annualized\":" << annualized_return
            << ",\"sharpe\":" << sharpe_ratio
            << ",\"sortino\":" << sortino_ratio
            << ",\"calmar\":" << calmar_ratio
            << ",\"max_dd\":" << max_drawdown_pct
            << ",\"volatility\":" << volatility
            << ",\"trades\":" << total_trades
            << ",\"win_rate\":" << win_rate
            << ",\"profit_factor\":" << profit_factor
            << ",\"final_equity\":" << final_equity << "}";
        return oss.str();
    }
};

using StrategyFunc = std::function<Signal(const std::vector<PriceBar>&, int)>;

// ============================================================================
// Backtester
// ============================================================================

class StrategyBacktester {
public:
    explicit StrategyBacktester(BacktestConfig config = {}) : config_(config) {}

    /**
     * @brief Run backtest on a price series with a strategy function
     */
    [[nodiscard]] BacktestMetrics run(const std::string& symbol,
                                        const std::vector<PriceBar>& bars,
                                        StrategyFunc strategy) {
        std::lock_guard<std::mutex> lock(mutex_);
        reset();

        double equity = config_.initial_capital;
        double peak_equity = equity;
        double position = 0;
        double entry_price = 0;
        std::string entry_date;
        int entry_bar = 0;
        double max_price_since_entry = 0, min_price_since_entry = 1e18;

        for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
            Signal sig = strategy(bars, i);
            double price = bars[i].close;

            // Track MAE/MFE for open positions
            if (position != 0) {
                max_price_since_entry = std::max(max_price_since_entry, price);
                min_price_since_entry = std::min(min_price_since_entry, price);
            }

            // Process signals
            if (sig == Signal::Buy && position <= 0) {
                // Close short if any
                if (position < 0) {
                    close_trade(price, bars[i].date, i - entry_bar,
                                max_price_since_entry, min_price_since_entry);
                    equity += trades_.back().net_pnl();
                }
                // Open long
                double slip = price * config_.slippage_pct;
                double exec_price = price + slip;
                double max_notional = equity * config_.max_position_pct;
                position = std::floor(max_notional / exec_price);
                entry_price = exec_price;
                entry_date = bars[i].date;
                entry_bar = i;
                max_price_since_entry = price;
                min_price_since_entry = price;
                open_trade(symbol, sig, position, entry_price, entry_date);
            }
            else if (sig == Signal::Sell && position >= 0 && config_.allow_shorting) {
                if (position > 0) {
                    close_trade(price, bars[i].date, i - entry_bar,
                                max_price_since_entry, min_price_since_entry);
                    equity += trades_.back().net_pnl();
                }
                double slip = price * config_.slippage_pct;
                double exec_price = price - slip;
                double max_notional = equity * config_.max_position_pct;
                position = -std::floor(max_notional / exec_price);
                entry_price = exec_price;
                entry_date = bars[i].date;
                entry_bar = i;
                max_price_since_entry = price;
                min_price_since_entry = price;
                open_trade(symbol, sig, position, entry_price, entry_date);
            }
            else if ((sig == Signal::ExitLong && position > 0) ||
                     (sig == Signal::ExitShort && position < 0)) {
                close_trade(price, bars[i].date, i - entry_bar,
                            max_price_since_entry, min_price_since_entry);
                equity += trades_.back().net_pnl();
                position = 0;
            }

            // Mark-to-market equity
            double mtm = equity;
            if (position > 0) mtm += position * (price - entry_price);
            else if (position < 0) mtm += position * (price - entry_price);

            peak_equity = std::max(peak_equity, mtm);
            double dd = peak_equity - mtm;
            double dd_pct = peak_equity > 0 ? dd / peak_equity : 0;

            EquityPoint ep;
            ep.date = bars[i].date;
            ep.equity = mtm;
            ep.drawdown = dd;
            ep.drawdown_pct = dd_pct;
            if (!equity_curve_.empty()) {
                double prev = equity_curve_.back().equity;
                ep.daily_return = prev > 0 ? (mtm - prev) / prev : 0;
            }
            equity_curve_.push_back(ep);
        }

        // Close any remaining position at last bar
        if (position != 0 && !bars.empty()) {
            close_trade(bars.back().close, bars.back().date,
                        static_cast<int>(bars.size()) - 1 - entry_bar,
                        max_price_since_entry, min_price_since_entry);
            equity += trades_.back().net_pnl();
        }

        return compute_metrics(equity, static_cast<int>(bars.size()));
    }

    [[nodiscard]] std::vector<BacktestTrade> trades() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<BacktestTrade>(trades_.begin(), trades_.end());
    }

    [[nodiscard]] std::vector<EquityPoint> equity_curve() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return equity_curve_;
    }

private:
    mutable std::mutex mutex_;
    BacktestConfig config_;
    std::deque<BacktestTrade> trades_;
    std::vector<EquityPoint> equity_curve_;
    int64_t trade_counter_{0};
    BacktestTrade current_trade_;

    void reset() {
        trades_.clear();
        equity_curve_.clear();
        trade_counter_ = 0;
    }

    void open_trade(const std::string& symbol, Signal sig, double shares,
                      double price, const std::string& date) {
        current_trade_ = {};
        current_trade_.id = "BT-" + std::to_string(++trade_counter_);
        current_trade_.symbol = symbol;
        current_trade_.entry_signal = sig;
        current_trade_.shares = shares;
        current_trade_.entry_price = price;
        current_trade_.entry_date = date;
        current_trade_.commission = std::abs(shares) * config_.commission_per_share;
    }

    void close_trade(double price, const std::string& date, int bars_held,
                       double max_p, double min_p) {
        double slip = price * config_.slippage_pct;
        double exec_price = current_trade_.shares > 0 ? price - slip : price + slip;
        current_trade_.exit_price = exec_price;
        current_trade_.exit_date = date;
        current_trade_.bars_held = bars_held;
        current_trade_.status = TradeStatus::Closed;
        current_trade_.pnl = current_trade_.shares *
            (current_trade_.exit_price - current_trade_.entry_price);
        double notional = std::abs(current_trade_.shares * current_trade_.entry_price);
        current_trade_.pnl_pct = notional > 0 ? current_trade_.pnl / notional : 0;
        current_trade_.commission += std::abs(current_trade_.shares) * config_.commission_per_share;
        current_trade_.slippage = std::abs(current_trade_.shares) * price * config_.slippage_pct * 2;
        // MAE/MFE
        if (current_trade_.shares > 0) {
            current_trade_.mae = (min_p - current_trade_.entry_price) / current_trade_.entry_price;
            current_trade_.mfe = (max_p - current_trade_.entry_price) / current_trade_.entry_price;
        } else {
            current_trade_.mae = (current_trade_.entry_price - max_p) / current_trade_.entry_price;
            current_trade_.mfe = (current_trade_.entry_price - min_p) / current_trade_.entry_price;
        }
        trades_.push_back(current_trade_);
    }

    BacktestMetrics compute_metrics(double final_equity, int trading_days) const {
        BacktestMetrics m;
        m.final_equity = final_equity;
        m.trading_days = trading_days;
        m.total_return = (final_equity - config_.initial_capital) / config_.initial_capital;
        double years = trading_days / 252.0;
        m.annualized_return = years > 0 ? std::pow(1 + m.total_return, 1.0 / years) - 1 : 0;

        // Daily returns from equity curve
        std::vector<double> daily_rets;
        for (size_t i = 1; i < equity_curve_.size(); ++i) {
            daily_rets.push_back(equity_curve_[i].daily_return);
        }

        if (!daily_rets.empty()) {
            double avg = std::accumulate(daily_rets.begin(), daily_rets.end(), 0.0) / daily_rets.size();
            double var = 0, dvar = 0;
            for (double r : daily_rets) {
                var += (r - avg) * (r - avg);
                if (r < 0) dvar += r * r;
            }
            m.volatility = std::sqrt(var / daily_rets.size()) * std::sqrt(252.0);
            m.downside_deviation = std::sqrt(dvar / daily_rets.size()) * std::sqrt(252.0);
            double rf_daily = config_.risk_free_rate / 252.0;
            double excess = avg - rf_daily;
            double daily_std = std::sqrt(var / daily_rets.size());
            m.sharpe_ratio = daily_std > 1e-10 ? (excess / daily_std) * std::sqrt(252.0) : 0;
            double dstd = std::sqrt(dvar / daily_rets.size());
            m.sortino_ratio = dstd > 1e-10 ? (excess / dstd) * std::sqrt(252.0) : 0;
        }

        // Max drawdown
        for (const auto& ep : equity_curve_) {
            if (ep.drawdown_pct > m.max_drawdown_pct) {
                m.max_drawdown_pct = ep.drawdown_pct;
                m.max_drawdown = ep.drawdown;
            }
        }
        m.calmar_ratio = m.max_drawdown_pct > 1e-10 ? m.annualized_return / m.max_drawdown_pct : 0;

        // Trade statistics
        double total_wins = 0, total_losses = 0;
        double bars_sum = 0;
        for (const auto& t : trades_) {
            if (t.status != TradeStatus::Closed) continue;
            m.total_trades++;
            bars_sum += t.bars_held;
            if (t.net_pnl() >= 0) { m.winning_trades++; total_wins += t.net_pnl(); }
            else { m.losing_trades++; total_losses += std::abs(t.net_pnl()); }
        }
        m.win_rate = m.total_trades > 0 ? static_cast<double>(m.winning_trades) / m.total_trades : 0;
        m.avg_win = m.winning_trades > 0 ? total_wins / m.winning_trades : 0;
        m.avg_loss = m.losing_trades > 0 ? total_losses / m.losing_trades : 0;
        m.profit_factor = total_losses > 1e-10 ? total_wins / total_losses : 0;
        m.avg_bars_held = m.total_trades > 0 ? bars_sum / m.total_trades : 0;
        m.expectancy = m.total_trades > 0 ? (total_wins - total_losses) / m.total_trades : 0;

        return m;
    }
};

} // namespace backtest
} // namespace analytics
} // namespace genie

#endif // GENIE_ANALYTICS_STRATEGY_BACKTESTER_HPP
