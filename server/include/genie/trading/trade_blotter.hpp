/**
 * @file trade_blotter.hpp
 * @brief Trade blotter and journal for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_TRADE_BLOTTER_HPP
#define GENIE_TRADE_BLOTTER_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <mutex>

namespace genie {
namespace trading {

using TimePoint = std::chrono::system_clock::time_point;

struct BlotterEntry {
    std::string trade_id;
    std::string order_id;
    std::string portfolio_id;
    std::string security_id;
    std::string security_name;
    TimePoint timestamp;
    bool is_buy;
    double quantity;
    double price;
    double value;
    double commission;
    double realized_pnl;
    std::string notes;
    
    std::string to_string() const {
        auto t = std::chrono::system_clock::to_time_t(timestamp);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
           << " | " << trade_id
           << " | " << std::setw(6) << security_id
           << " | " << (is_buy ? "BUY " : "SELL")
           << " | " << std::setw(10) << std::fixed << std::setprecision(0) << quantity
           << " | " << std::setw(10) << std::setprecision(2) << price
           << " | " << std::setw(12) << value
           << " | P&L: " << std::setw(10) << realized_pnl;
        return ss.str();
    }
};

struct DailyPnL {
    TimePoint date;
    double realized_pnl{0};
    double unrealized_pnl{0};
    double total_pnl{0};
    int trades{0};
    double volume{0};
    double commission{0};
};

struct SecurityPnL {
    std::string security_id;
    double realized_pnl{0};
    double unrealized_pnl{0};
    double total_pnl{0};
    int trades{0};
    int winning_trades{0};
    double volume{0};
    double avg_holding_period{0};  // days
};

class TradeBlotter {
    std::vector<BlotterEntry> entries_;
    std::map<std::string, double> position_costs_;  // Average cost basis
    std::map<std::string, double> positions_;
    mutable std::mutex mutex_;
    int next_id_{1};
    
    std::string generate_id() {
        std::ostringstream ss;
        ss << "TRD-" << std::setfill('0') << std::setw(6) << next_id_++;
        return ss.str();
    }
    
public:
    // Record a trade
    std::string record_trade(const std::string& order_id,
                             const std::string& portfolio_id,
                             const std::string& security_id,
                             const std::string& security_name,
                             bool is_buy,
                             double quantity,
                             double price,
                             double commission = 0,
                             const std::string& notes = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        BlotterEntry entry;
        entry.trade_id = generate_id();
        entry.order_id = order_id;
        entry.portfolio_id = portfolio_id;
        entry.security_id = security_id;
        entry.security_name = security_name;
        entry.timestamp = std::chrono::system_clock::now();
        entry.is_buy = is_buy;
        entry.quantity = quantity;
        entry.price = price;
        entry.value = quantity * price;
        entry.commission = commission;
        entry.notes = notes;
        
        // Calculate P&L for sells
        entry.realized_pnl = 0;
        if (!is_buy && positions_.count(security_id) && positions_[security_id] > 0) {
            double avg_cost = position_costs_[security_id] / positions_[security_id];
            entry.realized_pnl = (price - avg_cost) * quantity - commission;
        }
        
        // Update positions
        if (is_buy) {
            positions_[security_id] += quantity;
            position_costs_[security_id] += entry.value;
        } else {
            double sell_qty = std::min(quantity, positions_[security_id]);
            if (positions_[security_id] > 0) {
                double cost_per_share = position_costs_[security_id] / positions_[security_id];
                position_costs_[security_id] -= cost_per_share * sell_qty;
            }
            positions_[security_id] -= sell_qty;
        }
        
        entries_.push_back(entry);
        return entry.trade_id;
    }
    
    // Get all entries
    std::vector<BlotterEntry> get_entries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }
    
    // Get entries for a specific portfolio
    std::vector<BlotterEntry> get_portfolio_entries(const std::string& portfolio_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BlotterEntry> result;
        for (const auto& e : entries_) {
            if (e.portfolio_id == portfolio_id) result.push_back(e);
        }
        return result;
    }
    
    // Get entries for a specific security
    std::vector<BlotterEntry> get_security_entries(const std::string& security_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BlotterEntry> result;
        for (const auto& e : entries_) {
            if (e.security_id == security_id) result.push_back(e);
        }
        return result;
    }
    
    // Get entries within a date range
    std::vector<BlotterEntry> get_entries_range(TimePoint start, TimePoint end) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BlotterEntry> result;
        for (const auto& e : entries_) {
            if (e.timestamp >= start && e.timestamp <= end) result.push_back(e);
        }
        return result;
    }
    
    // Total realized P&L
    double total_realized_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::accumulate(entries_.begin(), entries_.end(), 0.0,
            [](double sum, const BlotterEntry& e) { return sum + e.realized_pnl; });
    }
    
    // Daily P&L breakdown
    std::vector<DailyPnL> daily_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, DailyPnL> daily;
        
        for (const auto& e : entries_) {
            auto t = std::chrono::system_clock::to_time_t(e.timestamp);
            std::ostringstream ss;
            ss << std::put_time(std::localtime(&t), "%Y-%m-%d");
            std::string date_str = ss.str();
            
            auto& d = daily[date_str];
            d.date = e.timestamp;
            d.realized_pnl += e.realized_pnl;
            d.trades++;
            d.volume += e.value;
            d.commission += e.commission;
        }
        
        std::vector<DailyPnL> result;
        for (auto& [date, pnl] : daily) {
            pnl.total_pnl = pnl.realized_pnl + pnl.unrealized_pnl;
            result.push_back(pnl);
        }
        
        std::sort(result.begin(), result.end(), 
            [](const DailyPnL& a, const DailyPnL& b) { return a.date < b.date; });
        
        return result;
    }
    
    // Security P&L breakdown
    std::map<std::string, SecurityPnL> security_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, SecurityPnL> result;
        
        for (const auto& e : entries_) {
            auto& s = result[e.security_id];
            s.security_id = e.security_id;
            s.realized_pnl += e.realized_pnl;
            s.trades++;
            s.volume += e.value;
            if (e.realized_pnl > 0) s.winning_trades++;
        }
        
        return result;
    }
    
    // Trade count
    size_t trade_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }
    
    // Total volume
    double total_volume() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::accumulate(entries_.begin(), entries_.end(), 0.0,
            [](double sum, const BlotterEntry& e) { return sum + e.value; });
    }
    
    // Total commission
    double total_commission() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::accumulate(entries_.begin(), entries_.end(), 0.0,
            [](double sum, const BlotterEntry& e) { return sum + e.commission; });
    }
    
    // Clear all entries
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        positions_.clear();
        position_costs_.clear();
        next_id_ = 1;
    }
    
    // Generate report
    std::string report() const {
        std::ostringstream ss;
        ss << "=== TRADE BLOTTER REPORT ===\n\n";
        ss << "Total Trades: " << trade_count() << "\n";
        ss << "Total Volume: $" << std::fixed << std::setprecision(2) << total_volume() << "\n";
        ss << "Total Commission: $" << total_commission() << "\n";
        ss << "Realized P&L: $" << total_realized_pnl() << "\n\n";
        
        ss << "--- Recent Trades ---\n";
        auto entries = get_entries();
        size_t start = entries.size() > 10 ? entries.size() - 10 : 0;
        for (size_t i = start; i < entries.size(); ++i) {
            ss << entries[i].to_string() << "\n";
        }
        
        ss << "\n--- P&L by Security ---\n";
        auto sec_pnl = security_pnl();
        for (const auto& [id, pnl] : sec_pnl) {
            ss << id << ": $" << std::setprecision(2) << pnl.realized_pnl 
               << " (" << pnl.trades << " trades, " 
               << std::setprecision(0) << (pnl.trades > 0 ? 100.0 * pnl.winning_trades / pnl.trades : 0)
               << "% win rate)\n";
        }
        
        return ss.str();
    }
    
    // Export to CSV
    std::string to_csv() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;
        ss << "TradeID,OrderID,Portfolio,Security,Name,Timestamp,Side,Quantity,Price,Value,Commission,RealizedPnL,Notes\n";
        
        for (const auto& e : entries_) {
            auto t = std::chrono::system_clock::to_time_t(e.timestamp);
            ss << e.trade_id << ","
               << e.order_id << ","
               << e.portfolio_id << ","
               << e.security_id << ","
               << "\"" << e.security_name << "\","
               << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << ","
               << (e.is_buy ? "BUY" : "SELL") << ","
               << std::fixed << std::setprecision(4) << e.quantity << ","
               << std::setprecision(4) << e.price << ","
               << std::setprecision(2) << e.value << ","
               << e.commission << ","
               << e.realized_pnl << ","
               << "\"" << e.notes << "\"\n";
        }
        
        return ss.str();
    }
};

} // namespace trading
} // namespace genie
#endif // GENIE_TRADE_BLOTTER_HPP
