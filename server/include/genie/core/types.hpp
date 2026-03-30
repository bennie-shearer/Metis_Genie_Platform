/**
 * @file types.hpp
 * @brief Core types for Metis Genie Platform Emulator
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 */
#pragma once
#ifndef GENIE_CORE_TYPES_HPP
#define GENIE_CORE_TYPES_HPP
#include "version.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <variant>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <any>
#include <deque>
#include <unordered_map>
#include <numeric>
#include <future>
#ifdef GENIE_PLATFORM_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

namespace genie {
using Price = double;
using Quantity = double;
using Weight = double;
using Rate = double;
using Currency = std::string;
using SecurityId = std::string;
using PortfolioId = std::string;
using AccountId = std::string;
using OrderId = std::string;
using TradeId = std::string;
using BenchmarkId = std::string;
using TimePoint = std::chrono::system_clock::time_point;

struct Money {
    double amount{0.0};
    Currency currency{"USD"};
    Money() = default;
    Money(double a, Currency c = "USD") : amount(a), currency(std::move(c)) {}
    Money operator+(const Money& o) const { return Money(amount + o.amount, currency); }
    Money operator-(const Money& o) const { return Money(amount - o.amount, currency); }
    Money operator*(double f) const { return Money(amount * f, currency); }
    bool operator<(const Money& o) const { return amount < o.amount; }
    [[nodiscard]] Money abs() const { return Money(std::abs(amount), currency); }
    [[nodiscard]] std::string to_string() const {
        std::ostringstream s; s << std::fixed << std::setprecision(2) << amount << " " << currency; return s.str();
    }
};

template<typename T> class Result {
    std::variant<T, std::string> data_; bool success_;
public:
    Result(T v) : data_(std::move(v)), success_(true) {}
    static Result ok(T v) { return Result(std::move(v)); }
    static Result error(std::string m) { Result r; r.data_ = std::move(m); r.success_ = false; return r; }
    [[nodiscard]] bool is_ok() const { return success_; }
    [[nodiscard]] T& unwrap() { return std::get<T>(data_); }
    [[nodiscard]] const T& unwrap() const { return std::get<T>(data_); }
private:
    Result() : success_(false) {}
};

class UuidGenerator {
public:
    static std::string generate() {
        static std::random_device rd; static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        uint64_t a = dis(gen), b = dis(gen);
        a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x4000ULL;
        b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        std::ostringstream s; s << std::hex << std::setfill('0')
            << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << "-"
            << std::setw(4) << ((a >> 16) & 0xFFFF) << "-"
            << std::setw(4) << (a & 0xFFFF) << "-"
            << std::setw(4) << ((b >> 48) & 0xFFFF) << "-"
            << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
        return s.str();
    }
};

enum class AssetClass { Equity, FixedIncome, Derivative, Commodity, FX, RealEstate, Alternative, Cash, Unknown };
enum class SecurityType { CommonStock, PreferredStock, ETF, MutualFund, GovernmentBond, CorporateBond, MunicipalBond, Option, Future, Swap, CDS, Index, Cash, Unknown };
enum class OrderSide { Buy, Sell, SellShort, BuyToCover };
enum class OrderType { Market, Limit, Stop, StopLimit, MOC, LOC };
enum class OrderStatus { Pending, New, PartiallyFilled, Filled, Cancelled, Rejected, Expired };
enum class TimeInForce { Day, GTC, IOC, FOK, GTD };
enum class CreditRating { AAA, AA, A, BBB, BB, B, CCC, CC, C, D, NR };
enum class VaRMethod { Parametric, Historical, MonteCarlo };
enum class OptimizationObjective { MaxSharpe, MinVariance, MaxReturn, RiskParity, MinCVaR };
enum class Frequency { Daily, Weekly, Monthly, Quarterly, Annually };
enum class DayCountConvention { Actual360, Actual365, Thirty360, ActualActual };
enum class ConstraintType { Concentration, Sector, AssetClass, Rating, Duration, Liquidity, ESG, Regulatory, Custom };
enum class ComplianceStatus { Compliant, Warning, Breach, Exempt };
enum class EventType { PriceUpdate, OrderSubmitted, OrderFilled, OrderCancelled, TradeExecuted, PositionOpened, PositionClosed, ComplianceViolation, RiskLimitBreached, SystemStartup, SystemShutdown, Custom };

[[nodiscard]] inline std::string asset_class_to_string(AssetClass ac) {
    switch (ac) {
        case AssetClass::Equity: return "Equity"; case AssetClass::FixedIncome: return "Fixed Income";
        case AssetClass::Derivative: return "Derivative"; case AssetClass::Cash: return "Cash"; default: return "Unknown";
    }
}
[[nodiscard]] inline int frequency_to_periods_per_year(Frequency f) {
    switch (f) { case Frequency::Daily: return 252; case Frequency::Weekly: return 52; case Frequency::Monthly: return 12; case Frequency::Quarterly: return 4; case Frequency::Annually: return 1; default: return 252; }
}
} // namespace genie
#endif
