/**
 * @file equity.hpp
 * @brief Equity securities for Metis Genie Platform Emulator
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_ASSETS_EQUITY_HPP
#define GENIE_ASSETS_EQUITY_HPP
#include "../market/security.hpp"

namespace genie::assets {
using namespace genie::market;

struct EquityData { double beta{1.0}, pe_ratio{0}, eps{0}, dividend_yield{0}, market_cap{0}; int shares_outstanding{0}; };

class CommonStock : public Security {
    EquityData data_;
public:
    CommonStock() : Security("", SecurityType::CommonStock, AssetClass::Equity) {}
    CommonStock(SecurityId id, const std::string& name, const std::string& ticker, const std::string& exchange = "NYSE")
        : Security(std::move(id), SecurityType::CommonStock, AssetClass::Equity) {
        reference_.name = name; reference_.ticker = ticker; reference_.exchange = exchange;
    }
    [[nodiscard]] EquityData& equity_data() { return data_; }
    [[nodiscard]] const EquityData& equity_data() const { return data_; }
};

class ETF : public Security {
    EquityData data_; double expense_ratio_{0};
public:
    ETF() : Security("", SecurityType::ETF, AssetClass::Equity) {}
    ETF(SecurityId id, const std::string& name, const std::string& ticker) : Security(std::move(id), SecurityType::ETF, AssetClass::Equity) {
        reference_.name = name; reference_.ticker = ticker;
    }
    [[nodiscard]] EquityData& equity_data() { return data_; }
    [[nodiscard]] double expense_ratio() const { return expense_ratio_; }
    void set_expense_ratio(double r) { expense_ratio_ = r; }
};

inline std::shared_ptr<CommonStock> create_common_stock(const SecurityId& id, const std::string& name, const std::string& ticker, const std::string& exchange = "NYSE") {
    return std::make_shared<CommonStock>(id, name, ticker, exchange);
}
inline std::shared_ptr<ETF> create_etf(const SecurityId& id, const std::string& name, const std::string& ticker) {
    return std::make_shared<ETF>(id, name, ticker);
}
} // namespace genie::assets
#endif
