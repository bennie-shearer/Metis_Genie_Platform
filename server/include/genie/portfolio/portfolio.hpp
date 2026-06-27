/**
 * @file portfolio.hpp
 * @brief Portfolio management for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_PORTFOLIO_PORTFOLIO_HPP
#define GENIE_PORTFOLIO_PORTFOLIO_HPP
#include "position.hpp"
#include "../market/market_data.hpp"
#include "../core/thread_pool.hpp"

namespace genie::portfolio {
struct PortfolioConfig {
    PortfolioId id; std::string name, description; Currency base_currency{"USD"};
    BenchmarkId benchmark_id; TimePoint inception_date; bool allow_short_selling{false}; double max_leverage{1.0};
    std::string owner_id;  // User who owns this portfolio (multi-user support)
};

struct PortfolioSnapshot { TimePoint timestamp; Money nav, gross_exposure, net_exposure, cash_balance; size_t position_count{0}; double leverage{1.0}; };

class Portfolio {
    PortfolioConfig config_;
    mutable std::shared_mutex mutex_;
    std::map<SecurityId, Position> positions_;
    CashPosition cash_;
    market::MarketDataService* market_data_{nullptr};
    std::vector<PortfolioSnapshot> history_;
    Money nav_, gross_exposure_, net_exposure_;
    HoldingsSummary holdings_;
public:
    Portfolio() = default;
    explicit Portfolio(PortfolioConfig cfg) : config_(std::move(cfg)) { nav_ = gross_exposure_ = net_exposure_ = Money(0.0, config_.base_currency); }
    [[nodiscard]] const PortfolioConfig& config() const { return config_; }
    [[nodiscard]] const PortfolioId& id() const { return config_.id; }
    [[nodiscard]] const std::string& name() const { return config_.name; }
    [[nodiscard]] const Currency& base_currency() const { return config_.base_currency; }
    void set_market_data(market::MarketDataService* md) { market_data_ = md; }
    
    void deposit_cash(const Money& m) { std::unique_lock lk(mutex_); cash_.deposit(m); recalc(); }
    void withdraw_cash(const Money& m) { std::unique_lock lk(mutex_); cash_.withdraw(m); recalc(); }
    [[nodiscard]] Money cash_balance() const { std::shared_lock lk(mutex_); return cash_.balance(config_.base_currency); }
    [[nodiscard]] const CashPosition& cash() const { return cash_; }
    
    void open_position(const SecurityId& id, Quantity qty, Price price) {
        std::unique_lock lk(mutex_);
        auto& pos = positions_[id]; pos = Position(id);
        if (market_data_) pos.set_security(market_data_->get_security(id));
        if (qty > 0) pos.add_shares(qty, price, config_.base_currency);
        else pos.short_sell(-qty, price, config_.base_currency);
        cash_.withdraw(Money(std::abs(qty) * price, config_.base_currency)); recalc();
    }
    Money close_position(const SecurityId& id, Price price) {
        std::unique_lock lk(mutex_);
        auto it = positions_.find(id);
        if (it == positions_.end()) return Money(0.0, config_.base_currency);
        Quantity qty = std::abs(it->second.quantity());
        Money realized = positions_[id].remove_shares(qty, price, config_.base_currency);
        cash_.deposit(Money(qty * price, config_.base_currency));
        if (positions_[id].is_flat()) positions_.erase(id);
        recalc();
        return realized;
    }
    [[nodiscard]] std::optional<Position> get_position(const SecurityId& id) const {
        std::shared_lock lk(mutex_); auto it = positions_.find(id);
        return it != positions_.end() ? std::optional(it->second) : std::nullopt;
    }
    [[nodiscard]] std::vector<Position> get_all_positions() const {
        std::shared_lock lk(mutex_); std::vector<Position> r; r.reserve(positions_.size());
        for (const auto& [id, pos] : positions_) r.push_back(pos);
        return r;
    }
    [[nodiscard]] size_t position_count() const { std::shared_lock lk(mutex_); return positions_.size(); }
    [[nodiscard]] bool has_position(const SecurityId& id) const { std::shared_lock lk(mutex_); return positions_.count(id) > 0; }
    [[nodiscard]] const std::map<SecurityId, Position>& positions() const { return positions_; }
    
    void update_market_values() {
        if (!market_data_) return;
        std::unique_lock lk(mutex_);
        for (auto& [id, pos] : positions_) { Price p = market_data_->get_price(id); if (p > 0) pos.update_market_value(p); }
        recalc();
    }
    [[nodiscard]] Money nav() const { std::shared_lock lk(mutex_); return nav_; }
    [[nodiscard]] Money gross_exposure() const { std::shared_lock lk(mutex_); return gross_exposure_; }
    [[nodiscard]] double leverage() const { std::shared_lock lk(mutex_); return nav_.amount > 0 ? gross_exposure_.amount / nav_.amount : 0.0; }
    [[nodiscard]] HoldingsSummary get_holdings_summary() const { std::shared_lock lk(mutex_); return holdings_; }
    [[nodiscard]] std::map<SecurityId, double> get_weights() const {
        std::shared_lock lk(mutex_); std::map<SecurityId, double> w;
        if (nav_.amount <= 0) return w;
        for (const auto& [id, pos] : positions_) w[id] = pos.market_value().amount / nav_.amount;
        return w;
    }
    [[nodiscard]] std::map<AssetClass, double> get_asset_class_weights() const { std::shared_lock lk(mutex_); return holdings_.weight_by_asset_class; }
    [[nodiscard]] std::map<std::string, double> get_sector_weights() const { std::shared_lock lk(mutex_); return holdings_.weight_by_sector; }
private:
    void recalc() {
        Money lv(0, config_.base_currency), sv(0, config_.base_currency), cb(0, config_.base_currency), up(0, config_.base_currency);
        holdings_ = HoldingsSummary(); holdings_.position_count = positions_.size();
        for (auto& [id, pos] : positions_) {
            Money mv = pos.market_value();
            if (pos.is_long()) { lv = lv + mv; holdings_.long_positions++; }
            else if (pos.is_short()) { sv = sv + mv; holdings_.short_positions++; }
            cb = cb + pos.cost_basis(); up = up + pos.unrealized_pnl();
            if (pos.security()) {
                AssetClass ac = pos.security()->asset_class();
                holdings_.by_asset_class[ac] = holdings_.by_asset_class[ac] + mv;
                std::string sec = pos.security()->reference().sector;
                if (!sec.empty()) holdings_.by_sector[sec] = holdings_.by_sector[sec] + mv;
            }
        }
        holdings_.total_long_value = lv; holdings_.total_short_value = sv;
        holdings_.total_cost_basis = cb; holdings_.total_unrealized_pnl = up;
        Money cash_bal = cash_.balance(config_.base_currency);
        gross_exposure_ = lv + Money(std::abs(sv.amount), config_.base_currency);
        net_exposure_ = lv - Money(std::abs(sv.amount), config_.base_currency);
        holdings_.net_market_value = net_exposure_; nav_ = net_exposure_ + cash_bal;
        if (nav_.amount > 0) {
            for (const auto& [ac, mv] : holdings_.by_asset_class) holdings_.weight_by_asset_class[ac] = mv.amount / nav_.amount;
            for (const auto& [sec, mv] : holdings_.by_sector) holdings_.weight_by_sector[sec] = mv.amount / nav_.amount;
            for (auto& [id, pos] : positions_) pos.set_weight(pos.market_value().amount / nav_.amount);
        }
    }
};

class PortfolioManager {
    std::map<PortfolioId, std::shared_ptr<Portfolio>> portfolios_;
    market::MarketDataService* market_data_{nullptr};
    mutable std::shared_mutex mutex_;
public:
    void set_market_data(market::MarketDataService* md) { market_data_ = md; }
    std::shared_ptr<Portfolio> create_portfolio(PortfolioConfig cfg) {
        std::unique_lock lk(mutex_);
        auto p = std::make_shared<Portfolio>(std::move(cfg)); p->set_market_data(market_data_);
        portfolios_[p->id()] = p; return p;
    }
    [[nodiscard]] std::shared_ptr<Portfolio> get_portfolio(const PortfolioId& id) const {
        std::shared_lock lk(mutex_); auto it = portfolios_.find(id); return it != portfolios_.end() ? it->second : nullptr;
    }
    [[nodiscard]] size_t portfolio_count() const { std::shared_lock lk(mutex_); return portfolios_.size(); }
    [[nodiscard]] Money total_aum() const {
        std::shared_lock lk(mutex_); Money total(0.0, "USD");
        for (const auto& [id, p] : portfolios_) total = total + p->nav();
        return total;
    }

    /**
     * Revalue all portfolios in parallel.
     * Each portfolio's update_market_values() runs on a separate thread.
     * Safe because each Portfolio has its own mutex.
     */
    void revalue_all_parallel() {
        std::shared_lock lk(mutex_);
        std::vector<std::shared_ptr<Portfolio>> ptrs;
        ptrs.reserve(portfolios_.size());
        for (const auto& [id, p] : portfolios_) ptrs.push_back(p);
        lk.unlock();

        if (ptrs.size() <= 1) {
            for (auto& p : ptrs) p->update_market_values();
            return;
        }

        thread_pool().parallel_for(0, ptrs.size(), [&ptrs](size_t i) {
            ptrs[i]->update_market_values();
        });
    }

    /** Revalue all portfolios sequentially (original behavior). */
    void revalue_all() {
        std::shared_lock lk(mutex_);
        for (const auto& [id, p] : portfolios_) p->update_market_values();
    }
};
} // namespace genie::portfolio
#endif
