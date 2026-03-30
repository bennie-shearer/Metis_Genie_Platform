/**
 * @file options_integration.hpp
 * @brief Options trading integration with Greeks, P&L tracking, and strategies
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 4: Options Integration
 * - Fetch real options chains from brokers
 * - Calculate Greeks from market prices
 * - Options P&L tracking
 * - Options expiration management
 * - Options strategy builder
 */
#pragma once
#ifndef GENIE_TRADING_OPTIONS_INTEGRATION_HPP
#define GENIE_TRADING_OPTIONS_INTEGRATION_HPP

#include "broker_abstraction.hpp"
#include "../core/date_utils.hpp"
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <functional>

namespace genie::trading {

// =============================================================================
// Options Data Structures
// =============================================================================

/**
 * @brief Option type
 */
enum class OptionType {
    Call,
    Put
};

inline std::string option_type_to_string(OptionType type) {
    return type == OptionType::Call ? "CALL" : "PUT";
}

/**
 * @brief Option style
 */
enum class OptionStyle {
    American,
    European
};

/**
 * @brief Option contract details
 */
struct OptionContract {
    std::string symbol;           // Option symbol (e.g., AAPL230120C00150000)
    std::string underlying;       // Underlying symbol (e.g., AAPL)
    OptionType type{OptionType::Call};
    OptionStyle style{OptionStyle::American};
    
    double strike{0};
    std::string expiration_date;  // YYYY-MM-DD
    int days_to_expiration{0};
    
    double multiplier{100};       // Contract multiplier
    
    // Market data
    double bid{0};
    double ask{0};
    double last{0};
    double mark{0};               // Mid-market price
    double change{0};
    double change_pct{0};
    int64_t volume{0};
    int64_t open_interest{0};
    
    // Greeks
    double delta{0};
    double gamma{0};
    double theta{0};
    double vega{0};
    double rho{0};
    double implied_volatility{0};
    
    // Underlying info
    double underlying_price{0};
    
    double intrinsic_value() const {
        if (type == OptionType::Call) {
            return std::max(0.0, underlying_price - strike);
        } else {
            return std::max(0.0, strike - underlying_price);
        }
    }
    
    double time_value() const {
        return std::max(0.0, mark - intrinsic_value());
    }
    
    bool is_itm() const {
        if (type == OptionType::Call) {
            return underlying_price > strike;
        } else {
            return underlying_price < strike;
        }
    }
    
    bool is_otm() const {
        return !is_itm() && !is_atm();
    }
    
    bool is_atm() const {
        return std::abs(underlying_price - strike) / underlying_price < 0.01;
    }
    
    std::string moneyness() const {
        if (is_atm()) return "ATM";
        if (is_itm()) return "ITM";
        return "OTM";
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << underlying << " " << expiration_date << " $" << strike 
            << " " << option_type_to_string(type) << " (" << moneyness() << ")\n"
            << "  Bid: $" << bid << " Ask: $" << ask << " Mark: $" << mark << "\n"
            << "  Delta: " << std::setprecision(4) << delta 
            << " Gamma: " << gamma 
            << " Theta: " << theta 
            << " Vega: " << vega << "\n"
            << "  IV: " << std::setprecision(2) << implied_volatility * 100 << "%"
            << " Volume: " << volume << " OI: " << open_interest;
        return oss.str();
    }
};

/**
 * @brief Options chain for an underlying
 */
struct OptionsChain {
    std::string underlying;
    double underlying_price{0};
    std::string as_of_date;
    
    std::vector<std::string> expirations;
    std::vector<double> strikes;
    
    // Organized by expiration -> strike -> call/put
    std::map<std::string, std::map<double, OptionContract>> calls;
    std::map<std::string, std::map<double, OptionContract>> puts;
    
    /**
     * @brief Get specific contract
     */
    const OptionContract* get(const std::string& expiration,
                              double strike,
                              OptionType type) const {
        const auto& chain = (type == OptionType::Call) ? calls : puts;
        auto exp_it = chain.find(expiration);
        if (exp_it == chain.end()) return nullptr;
        
        auto strike_it = exp_it->second.find(strike);
        if (strike_it == exp_it->second.end()) return nullptr;
        
        return &strike_it->second;
    }
    
    /**
     * @brief Get ATM strike for expiration
     */
    double get_atm_strike(const std::string& expiration) const {
        double min_diff = std::numeric_limits<double>::max();
        double atm = 0;
        
        auto it = calls.find(expiration);
        if (it == calls.end()) return 0;
        
        for (const auto& [strike, _] : it->second) {
            double diff = std::abs(strike - underlying_price);
            if (diff < min_diff) {
                min_diff = diff;
                atm = strike;
            }
        }
        return atm;
    }
    
    /**
     * @brief Get nearest expiration to target days
     */
    std::string get_nearest_expiration(int target_days) const {
        if (expirations.empty()) return "";
        
        int min_diff = std::numeric_limits<int>::max();
        std::string nearest;
        
        for (const auto& exp : expirations) {
            auto it = calls.find(exp);
            if (it == calls.end() || it->second.empty()) continue;
            
            int dte = it->second.begin()->second.days_to_expiration;
            int diff = std::abs(dte - target_days);
            if (diff < min_diff) {
                min_diff = diff;
                nearest = exp;
            }
        }
        return nearest;
    }
};

// =============================================================================
// Greeks Calculator
// =============================================================================

/**
 * @brief Black-Scholes Greeks Calculator
 */
class GreeksCalculator {
private:
    static constexpr double PI = 3.14159265358979323846;
    
    /**
     * @brief Standard normal CDF
     */
    static double norm_cdf(double x) {
        return 0.5 * (1 + std::erf(x / std::sqrt(2.0)));
    }
    
    /**
     * @brief Standard normal PDF
     */
    static double norm_pdf(double x) {
        return std::exp(-0.5 * x * x) / std::sqrt(2 * PI);
    }
    
    /**
     * @brief Calculate d1 and d2 for Black-Scholes
     */
    static std::pair<double, double> calculate_d1_d2(double S, double K, double T,
                                                      double r, double sigma) {
        double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * T) / 
                    (sigma * std::sqrt(T));
        double d2 = d1 - sigma * std::sqrt(T);
        return {d1, d2};
    }

public:
    /**
     * @brief Calculate all Greeks for an option
     * 
     * @param S Current stock price
     * @param K Strike price
     * @param T Time to expiration (in years)
     * @param r Risk-free rate (annual)
     * @param sigma Volatility (annual)
     * @param type Call or Put
     * @return Greeks populated in OptionContract
     */
    static void calculate(OptionContract& option,
                         double S, double K, double T,
                         double r, double sigma) {
        if (T <= 0 || sigma <= 0) {
            // At or past expiration
            option.delta = (option.type == OptionType::Call) ? 
                          (S > K ? 1.0 : 0.0) : (S < K ? -1.0 : 0.0);
            option.gamma = 0;
            option.theta = 0;
            option.vega = 0;
            option.rho = 0;
            return;
        }
        
        auto [d1, d2] = calculate_d1_d2(S, K, T, r, sigma);
        double sqrt_T = std::sqrt(T);
        double exp_rt = std::exp(-r * T);
        
        // Delta
        if (option.type == OptionType::Call) {
            option.delta = norm_cdf(d1);
        } else {
            option.delta = norm_cdf(d1) - 1;
        }
        
        // Gamma (same for calls and puts)
        option.gamma = norm_pdf(d1) / (S * sigma * sqrt_T);
        
        // Theta (per day)
        double theta_common = -(S * norm_pdf(d1) * sigma) / (2 * sqrt_T);
        if (option.type == OptionType::Call) {
            option.theta = (theta_common - r * K * exp_rt * norm_cdf(d2)) / 365;
        } else {
            option.theta = (theta_common + r * K * exp_rt * norm_cdf(-d2)) / 365;
        }
        
        // Vega (per 1% change in IV)
        option.vega = S * sqrt_T * norm_pdf(d1) / 100;
        
        // Rho (per 1% change in rate)
        if (option.type == OptionType::Call) {
            option.rho = K * T * exp_rt * norm_cdf(d2) / 100;
        } else {
            option.rho = -K * T * exp_rt * norm_cdf(-d2) / 100;
        }
        
        option.implied_volatility = sigma;
        option.underlying_price = S;
    }
    
    /**
     * @brief Calculate implied volatility using Newton-Raphson
     */
    static double calculate_iv(double market_price, double S, double K, double T,
                              double r, OptionType type, double initial_guess = 0.3) {
        if (T <= 0) return 0;
        
        double sigma = initial_guess;
        const int max_iterations = 100;
        const double tolerance = 1e-8;
        
        for (int i = 0; i < max_iterations; ++i) {
            // Calculate option price with current sigma
            auto [d1, d2] = calculate_d1_d2(S, K, T, r, sigma);
            
            double price;
            if (type == OptionType::Call) {
                price = S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
            } else {
                price = K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
            }
            
            double diff = price - market_price;
            if (std::abs(diff) < tolerance) {
                return sigma;
            }
            
            // Vega for Newton-Raphson update
            double vega = S * std::sqrt(T) * norm_pdf(d1);
            if (vega < 1e-10) break;
            
            sigma = sigma - diff / vega;
            sigma = std::max(0.01, std::min(5.0, sigma));  // Bound sigma
        }
        
        return sigma;
    }
    
    /**
     * @brief Calculate theoretical option price
     */
    static double calculate_price(double S, double K, double T,
                                  double r, double sigma, OptionType type) {
        if (T <= 0) {
            // At expiration
            if (type == OptionType::Call) {
                return std::max(0.0, S - K);
            } else {
                return std::max(0.0, K - S);
            }
        }
        
        auto [d1, d2] = calculate_d1_d2(S, K, T, r, sigma);
        
        if (type == OptionType::Call) {
            return S * norm_cdf(d1) - K * std::exp(-r * T) * norm_cdf(d2);
        } else {
            return K * std::exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1);
        }
    }
};

// =============================================================================
// Options Position & P&L Tracking
// =============================================================================

/**
 * @brief Options position
 */
struct OptionsPosition {
    std::string position_id;
    OptionContract contract;
    
    int quantity{0};              // Positive = long, negative = short
    double avg_cost{0};           // Per contract
    double total_cost{0};
    
    std::string open_date;
    std::string close_date;
    bool is_closed{false};
    
    // Current values
    double current_price{0};
    double current_value{0};
    double unrealized_pnl{0};
    double unrealized_pnl_pct{0};
    
    // Realized
    double realized_pnl{0};
    
    // Risk metrics
    double position_delta{0};
    double position_gamma{0};
    double position_theta{0};
    double position_vega{0};
    
    void update_greeks() {
        double multiplier = contract.multiplier * quantity;
        position_delta = contract.delta * multiplier;
        position_gamma = contract.gamma * multiplier;
        position_theta = contract.theta * multiplier;
        position_vega = contract.vega * multiplier;
    }
    
    void update_pnl(double mark_price) {
        current_price = mark_price;
        current_value = mark_price * contract.multiplier * std::abs(quantity);
        
        if (quantity > 0) {
            unrealized_pnl = (mark_price - avg_cost) * contract.multiplier * quantity;
        } else {
            unrealized_pnl = (avg_cost - mark_price) * contract.multiplier * std::abs(quantity);
        }
        
        if (total_cost != 0) {
            unrealized_pnl_pct = unrealized_pnl / std::abs(total_cost) * 100;
        }
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << (quantity > 0 ? "LONG" : "SHORT") << " " << std::abs(quantity) 
            << "x " << contract.underlying << " " << contract.expiration_date
            << " $" << contract.strike << " " << option_type_to_string(contract.type) << "\n"
            << "  Cost: $" << avg_cost << " Current: $" << current_price << "\n"
            << "  P/L: $" << unrealized_pnl << " (" << unrealized_pnl_pct << "%)\n"
            << "  Delta: " << position_delta << " Theta: $" << position_theta;
        return oss.str();
    }
};

/**
 * @brief Options portfolio tracker
 */
class OptionsPortfolio {
private:
    std::vector<OptionsPosition> positions_;
    mutable std::mutex mutex_;
    int position_counter_{0};
    
    // Portfolio Greeks
    double total_delta_{0};
    double total_gamma_{0};
    double total_theta_{0};
    double total_vega_{0};

public:
    /**
     * @brief Open a new position
     */
    std::string open_position(const OptionContract& contract,
                              int quantity,
                              double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        OptionsPosition pos;
        pos.position_id = "OPT-" + std::to_string(++position_counter_);
        pos.contract = contract;
        pos.quantity = quantity;
        pos.avg_cost = price;
        pos.total_cost = price * contract.multiplier * std::abs(quantity);
        
        // Get current date
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d");
        pos.open_date = oss.str();
        
        pos.update_greeks();
        pos.update_pnl(price);
        
        positions_.push_back(pos);
        update_portfolio_greeks();
        
        return pos.position_id;
    }
    
    /**
     * @brief Close a position
     */
    double close_position(const std::string& position_id, double close_price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& pos : positions_) {
            if (pos.position_id == position_id && !pos.is_closed) {
                pos.is_closed = true;
                
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::ostringstream oss;
                oss << std::put_time(std::localtime(&time), "%Y-%m-%d");
                pos.close_date = oss.str();
                
                if (pos.quantity > 0) {
                    pos.realized_pnl = (close_price - pos.avg_cost) * 
                                       pos.contract.multiplier * pos.quantity;
                } else {
                    pos.realized_pnl = (pos.avg_cost - close_price) * 
                                       pos.contract.multiplier * std::abs(pos.quantity);
                }
                
                update_portfolio_greeks();
                return pos.realized_pnl;
            }
        }
        return 0;
    }
    
    /**
     * @brief Update all position Greeks and P&L
     */
    void update_all(const std::map<std::string, double>& prices) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& pos : positions_) {
            if (pos.is_closed) continue;
            
            auto it = prices.find(pos.contract.symbol);
            if (it != prices.end()) {
                pos.update_pnl(it->second);
            }
        }
        
        update_portfolio_greeks();
    }
    
    /**
     * @brief Get positions expiring soon
     */
    std::vector<OptionsPosition> get_expiring(int days_threshold = 7) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<OptionsPosition> expiring;
        
        for (const auto& pos : positions_) {
            if (!pos.is_closed && pos.contract.days_to_expiration <= days_threshold) {
                expiring.push_back(pos);
            }
        }
        
        return expiring;
    }
    
    /**
     * @brief Get all open positions
     */
    std::vector<OptionsPosition> get_open_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<OptionsPosition> open;
        
        for (const auto& pos : positions_) {
            if (!pos.is_closed) {
                open.push_back(pos);
            }
        }
        
        return open;
    }
    
    /**
     * @brief Get position by ID
     */
    OptionsPosition get_position(const std::string& position_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& pos : positions_) {
            if (pos.position_id == position_id) {
                return pos;
            }
        }
        return {};
    }
    
    /**
     * @brief Get portfolio Greeks
     */
    struct PortfolioGreeks {
        double delta{0};
        double gamma{0};
        double theta{0};
        double vega{0};
        double total_value{0};
        double total_pnl{0};
    };
    
    PortfolioGreeks get_portfolio_greeks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PortfolioGreeks greeks;
        greeks.delta = total_delta_;
        greeks.gamma = total_gamma_;
        greeks.theta = total_theta_;
        greeks.vega = total_vega_;
        
        for (const auto& pos : positions_) {
            if (!pos.is_closed) {
                greeks.total_value += pos.current_value;
                greeks.total_pnl += pos.unrealized_pnl;
            }
        }
        
        return greeks;
    }

private:
    void update_portfolio_greeks() {
        total_delta_ = 0;
        total_gamma_ = 0;
        total_theta_ = 0;
        total_vega_ = 0;
        
        for (const auto& pos : positions_) {
            if (!pos.is_closed) {
                total_delta_ += pos.position_delta;
                total_gamma_ += pos.position_gamma;
                total_theta_ += pos.position_theta;
                total_vega_ += pos.position_vega;
            }
        }
    }
};

// =============================================================================
// Options Strategy Builder
// =============================================================================

/**
 * @brief Strategy leg
 */
struct StrategyLeg {
    OptionContract contract;
    int quantity{0};              // Positive = buy, negative = sell
    double price{0};
    
    double leg_cost() const {
        return price * contract.multiplier * quantity;
    }
};

/**
 * @brief Options strategy definition
 */
struct OptionsStrategy {
    std::string name;
    std::string underlying;
    std::vector<StrategyLeg> legs;
    
    // Net Greeks
    double net_delta{0};
    double net_gamma{0};
    double net_theta{0};
    double net_vega{0};
    
    // Cost/Risk
    double net_debit{0};          // Positive = pay, negative = receive
    double max_profit{0};
    double max_loss{0};
    double breakeven_lower{0};
    double breakeven_upper{0};
    
    // Probability estimates
    double prob_profit{0};
    double prob_max_profit{0};
    
    void calculate_net_greeks() {
        net_delta = 0;
        net_gamma = 0;
        net_theta = 0;
        net_vega = 0;
        net_debit = 0;
        
        for (const auto& leg : legs) {
            double multiplier = leg.contract.multiplier * leg.quantity;
            net_delta += leg.contract.delta * multiplier;
            net_gamma += leg.contract.gamma * multiplier;
            net_theta += leg.contract.theta * multiplier;
            net_vega += leg.contract.vega * multiplier;
            net_debit += leg.leg_cost();
        }
    }
    
    std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Strategy: " << name << " on " << underlying << "\n";
        
        for (size_t i = 0; i < legs.size(); ++i) {
            const auto& leg = legs[i];
            oss << "  Leg " << (i+1) << ": " 
                << (leg.quantity > 0 ? "BUY" : "SELL") << " " << std::abs(leg.quantity)
                << "x " << leg.contract.expiration_date
                << " $" << leg.contract.strike << " " 
                << option_type_to_string(leg.contract.type)
                << " @ $" << leg.price << "\n";
        }
        
        oss << "Net Debit/Credit: $" << net_debit 
            << " (" << (net_debit > 0 ? "Debit" : "Credit") << ")\n"
            << "Max Profit: $" << max_profit << " | Max Loss: $" << max_loss << "\n"
            << "Breakeven: $" << breakeven_lower;
        if (breakeven_upper != breakeven_lower) {
            oss << " - $" << breakeven_upper;
        }
        oss << "\nNet Greeks: Delta=" << net_delta << " Theta=$" << net_theta;
        
        return oss.str();
    }
};

/**
 * @brief Strategy type enumeration
 */
enum class StrategyType {
    LongCall,
    LongPut,
    CoveredCall,
    ProtectivePut,
    BullCallSpread,
    BearPutSpread,
    Straddle,
    Strangle,
    IronCondor,
    Butterfly,
    Calendar,
    Diagonal
};

inline std::string strategy_type_to_string(StrategyType type) {
    switch (type) {
        case StrategyType::LongCall: return "Long Call";
        case StrategyType::LongPut: return "Long Put";
        case StrategyType::CoveredCall: return "Covered Call";
        case StrategyType::ProtectivePut: return "Protective Put";
        case StrategyType::BullCallSpread: return "Bull Call Spread";
        case StrategyType::BearPutSpread: return "Bear Put Spread";
        case StrategyType::Straddle: return "Straddle";
        case StrategyType::Strangle: return "Strangle";
        case StrategyType::IronCondor: return "Iron Condor";
        case StrategyType::Butterfly: return "Butterfly";
        case StrategyType::Calendar: return "Calendar Spread";
        case StrategyType::Diagonal: return "Diagonal Spread";
        default: return "Custom";
    }
}

/**
 * @brief Options Strategy Builder
 */
class StrategyBuilder {
private:
    OptionsChain chain_;
    double risk_free_rate_{0.05};

public:
    explicit StrategyBuilder(const OptionsChain& chain, double risk_free_rate = 0.05)
        : chain_(chain), risk_free_rate_(risk_free_rate) {}
    
    /**
     * @brief Build a standard strategy
     */
    OptionsStrategy build(StrategyType type,
                          const std::string& expiration,
                          double strike1,
                          double strike2 = 0,
                          double strike3 = 0,
                          double strike4 = 0,
                          int quantity = 1) {
        switch (type) {
            case StrategyType::LongCall:
                return build_long_call(expiration, strike1, quantity);
            case StrategyType::LongPut:
                return build_long_put(expiration, strike1, quantity);
            case StrategyType::BullCallSpread:
                return build_bull_call_spread(expiration, strike1, strike2, quantity);
            case StrategyType::BearPutSpread:
                return build_bear_put_spread(expiration, strike1, strike2, quantity);
            case StrategyType::Straddle:
                return build_straddle(expiration, strike1, quantity);
            case StrategyType::Strangle:
                return build_strangle(expiration, strike1, strike2, quantity);
            case StrategyType::IronCondor:
                return build_iron_condor(expiration, strike1, strike2, strike3, strike4, quantity);
            case StrategyType::Butterfly:
                return build_butterfly(expiration, strike1, strike2, strike3, quantity);
            default:
                return {};
        }
    }
    
    /**
     * @brief Long Call
     */
    OptionsStrategy build_long_call(const std::string& expiration,
                                    double strike,
                                    int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Long Call";
        strategy.underlying = chain_.underlying;
        
        auto* contract = chain_.get(expiration, strike, OptionType::Call);
        if (!contract) return strategy;
        
        StrategyLeg leg;
        leg.contract = *contract;
        leg.quantity = quantity;
        leg.price = contract->ask;  // Pay the ask
        strategy.legs.push_back(leg);
        
        strategy.calculate_net_greeks();
        
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = std::numeric_limits<double>::infinity();
        strategy.breakeven_lower = strike + leg.price;
        strategy.breakeven_upper = strategy.breakeven_lower;
        
        return strategy;
    }
    
    /**
     * @brief Long Put
     */
    OptionsStrategy build_long_put(const std::string& expiration,
                                   double strike,
                                   int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Long Put";
        strategy.underlying = chain_.underlying;
        
        auto* contract = chain_.get(expiration, strike, OptionType::Put);
        if (!contract) return strategy;
        
        StrategyLeg leg;
        leg.contract = *contract;
        leg.quantity = quantity;
        leg.price = contract->ask;
        strategy.legs.push_back(leg);
        
        strategy.calculate_net_greeks();
        
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = (strike - leg.price) * contract->multiplier * quantity;
        strategy.breakeven_lower = strike - leg.price;
        strategy.breakeven_upper = strategy.breakeven_lower;
        
        return strategy;
    }
    
    /**
     * @brief Bull Call Spread (vertical)
     */
    OptionsStrategy build_bull_call_spread(const std::string& expiration,
                                           double lower_strike,
                                           double upper_strike,
                                           int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Bull Call Spread";
        strategy.underlying = chain_.underlying;
        
        auto* long_call = chain_.get(expiration, lower_strike, OptionType::Call);
        auto* short_call = chain_.get(expiration, upper_strike, OptionType::Call);
        if (!long_call || !short_call) return strategy;
        
        // Buy lower strike call
        StrategyLeg leg1;
        leg1.contract = *long_call;
        leg1.quantity = quantity;
        leg1.price = long_call->ask;
        strategy.legs.push_back(leg1);
        
        // Sell higher strike call
        StrategyLeg leg2;
        leg2.contract = *short_call;
        leg2.quantity = -quantity;
        leg2.price = short_call->bid;
        strategy.legs.push_back(leg2);
        
        strategy.calculate_net_greeks();
        
        double width = upper_strike - lower_strike;
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = (width * long_call->multiplier * quantity) - strategy.net_debit;
        strategy.breakeven_lower = lower_strike + (strategy.net_debit / (long_call->multiplier * quantity));
        strategy.breakeven_upper = strategy.breakeven_lower;
        
        return strategy;
    }
    
    /**
     * @brief Bear Put Spread (vertical)
     */
    OptionsStrategy build_bear_put_spread(const std::string& expiration,
                                          double upper_strike,
                                          double lower_strike,
                                          int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Bear Put Spread";
        strategy.underlying = chain_.underlying;
        
        auto* long_put = chain_.get(expiration, upper_strike, OptionType::Put);
        auto* short_put = chain_.get(expiration, lower_strike, OptionType::Put);
        if (!long_put || !short_put) return strategy;
        
        // Buy higher strike put
        StrategyLeg leg1;
        leg1.contract = *long_put;
        leg1.quantity = quantity;
        leg1.price = long_put->ask;
        strategy.legs.push_back(leg1);
        
        // Sell lower strike put
        StrategyLeg leg2;
        leg2.contract = *short_put;
        leg2.quantity = -quantity;
        leg2.price = short_put->bid;
        strategy.legs.push_back(leg2);
        
        strategy.calculate_net_greeks();
        
        double width = upper_strike - lower_strike;
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = (width * long_put->multiplier * quantity) - strategy.net_debit;
        strategy.breakeven_lower = upper_strike - (strategy.net_debit / (long_put->multiplier * quantity));
        strategy.breakeven_upper = strategy.breakeven_lower;
        
        return strategy;
    }
    
    /**
     * @brief Long Straddle
     */
    OptionsStrategy build_straddle(const std::string& expiration,
                                   double strike,
                                   int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Long Straddle";
        strategy.underlying = chain_.underlying;
        
        auto* call = chain_.get(expiration, strike, OptionType::Call);
        auto* put = chain_.get(expiration, strike, OptionType::Put);
        if (!call || !put) return strategy;
        
        // Buy call
        StrategyLeg leg1;
        leg1.contract = *call;
        leg1.quantity = quantity;
        leg1.price = call->ask;
        strategy.legs.push_back(leg1);
        
        // Buy put
        StrategyLeg leg2;
        leg2.contract = *put;
        leg2.quantity = quantity;
        leg2.price = put->ask;
        strategy.legs.push_back(leg2);
        
        strategy.calculate_net_greeks();
        
        double total_premium = leg1.price + leg2.price;
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = std::numeric_limits<double>::infinity();
        strategy.breakeven_lower = strike - total_premium;
        strategy.breakeven_upper = strike + total_premium;
        
        return strategy;
    }
    
    /**
     * @brief Long Strangle
     */
    OptionsStrategy build_strangle(const std::string& expiration,
                                   double put_strike,
                                   double call_strike,
                                   int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Long Strangle";
        strategy.underlying = chain_.underlying;
        
        auto* call = chain_.get(expiration, call_strike, OptionType::Call);
        auto* put = chain_.get(expiration, put_strike, OptionType::Put);
        if (!call || !put) return strategy;
        
        // Buy OTM call
        StrategyLeg leg1;
        leg1.contract = *call;
        leg1.quantity = quantity;
        leg1.price = call->ask;
        strategy.legs.push_back(leg1);
        
        // Buy OTM put
        StrategyLeg leg2;
        leg2.contract = *put;
        leg2.quantity = quantity;
        leg2.price = put->ask;
        strategy.legs.push_back(leg2);
        
        strategy.calculate_net_greeks();
        
        double total_premium = leg1.price + leg2.price;
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = std::numeric_limits<double>::infinity();
        strategy.breakeven_lower = put_strike - total_premium;
        strategy.breakeven_upper = call_strike + total_premium;
        
        return strategy;
    }
    
    /**
     * @brief Iron Condor
     */
    OptionsStrategy build_iron_condor(const std::string& expiration,
                                      double put_long_strike,
                                      double put_short_strike,
                                      double call_short_strike,
                                      double call_long_strike,
                                      int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Iron Condor";
        strategy.underlying = chain_.underlying;
        
        auto* put_long = chain_.get(expiration, put_long_strike, OptionType::Put);
        auto* put_short = chain_.get(expiration, put_short_strike, OptionType::Put);
        auto* call_short = chain_.get(expiration, call_short_strike, OptionType::Call);
        auto* call_long = chain_.get(expiration, call_long_strike, OptionType::Call);
        
        if (!put_long || !put_short || !call_short || !call_long) return strategy;
        
        // Put spread: buy lower, sell higher
        StrategyLeg leg1;
        leg1.contract = *put_long;
        leg1.quantity = quantity;
        leg1.price = put_long->ask;
        strategy.legs.push_back(leg1);
        
        StrategyLeg leg2;
        leg2.contract = *put_short;
        leg2.quantity = -quantity;
        leg2.price = put_short->bid;
        strategy.legs.push_back(leg2);
        
        // Call spread: sell lower, buy higher
        StrategyLeg leg3;
        leg3.contract = *call_short;
        leg3.quantity = -quantity;
        leg3.price = call_short->bid;
        strategy.legs.push_back(leg3);
        
        StrategyLeg leg4;
        leg4.contract = *call_long;
        leg4.quantity = quantity;
        leg4.price = call_long->ask;
        strategy.legs.push_back(leg4);
        
        strategy.calculate_net_greeks();
        
        // Net credit received
        double credit = -strategy.net_debit;
        double put_width = put_short_strike - put_long_strike;
        double call_width = call_long_strike - call_short_strike;
        double max_width = std::max(put_width, call_width);
        
        strategy.max_profit = credit;
        strategy.max_loss = (max_width * put_long->multiplier * quantity) - credit;
        strategy.breakeven_lower = put_short_strike - (credit / (put_long->multiplier * quantity));
        strategy.breakeven_upper = call_short_strike + (credit / (put_long->multiplier * quantity));
        
        return strategy;
    }
    
    /**
     * @brief Butterfly Spread
     */
    OptionsStrategy build_butterfly(const std::string& expiration,
                                    double lower_strike,
                                    double middle_strike,
                                    double upper_strike,
                                    int quantity = 1) {
        OptionsStrategy strategy;
        strategy.name = "Long Call Butterfly";
        strategy.underlying = chain_.underlying;
        
        auto* lower = chain_.get(expiration, lower_strike, OptionType::Call);
        auto* middle = chain_.get(expiration, middle_strike, OptionType::Call);
        auto* upper = chain_.get(expiration, upper_strike, OptionType::Call);
        
        if (!lower || !middle || !upper) return strategy;
        
        // Buy 1 lower
        StrategyLeg leg1;
        leg1.contract = *lower;
        leg1.quantity = quantity;
        leg1.price = lower->ask;
        strategy.legs.push_back(leg1);
        
        // Sell 2 middle
        StrategyLeg leg2;
        leg2.contract = *middle;
        leg2.quantity = -2 * quantity;
        leg2.price = middle->bid;
        strategy.legs.push_back(leg2);
        
        // Buy 1 upper
        StrategyLeg leg3;
        leg3.contract = *upper;
        leg3.quantity = quantity;
        leg3.price = upper->ask;
        strategy.legs.push_back(leg3);
        
        strategy.calculate_net_greeks();
        
        double width = middle_strike - lower_strike;
        strategy.max_loss = strategy.net_debit;
        strategy.max_profit = (width * lower->multiplier * quantity) - strategy.net_debit;
        strategy.breakeven_lower = lower_strike + (strategy.net_debit / (lower->multiplier * quantity));
        strategy.breakeven_upper = upper_strike - (strategy.net_debit / (lower->multiplier * quantity));
        
        return strategy;
    }
    
    /**
     * @brief Calculate P&L at expiration for a range of prices
     */
    std::vector<std::pair<double, double>> calculate_pnl_curve(
        const OptionsStrategy& strategy,
        double price_min,
        double price_max,
        int points = 100) {
        
        std::vector<std::pair<double, double>> curve;
        double step = (price_max - price_min) / (points - 1);
        
        for (int i = 0; i < points; ++i) {
            double price = price_min + i * step;
            double pnl = 0;
            
            for (const auto& leg : strategy.legs) {
                double intrinsic;
                if (leg.contract.type == OptionType::Call) {
                    intrinsic = std::max(0.0, price - leg.contract.strike);
                } else {
                    intrinsic = std::max(0.0, leg.contract.strike - price);
                }
                
                // P/L = (intrinsic - premium paid) * multiplier * quantity
                double leg_pnl = (intrinsic - leg.price) * leg.contract.multiplier * leg.quantity;
                pnl += leg_pnl;
            }
            
            curve.emplace_back(price, pnl);
        }
        
        return curve;
    }
};

// =============================================================================
// Options Expiration Manager
// =============================================================================

/**
 * @brief Expiration action
 */
enum class ExpirationAction {
    Exercise,       // Exercise the option
    Close,          // Close before expiration
    Expire,         // Let expire worthless
    Roll            // Roll to later expiration
};

/**
 * @brief Expiration event
 */
struct ExpirationEvent {
    std::string position_id;
    OptionsPosition position;
    std::string expiration_date;
    int days_to_expiration{0};
    
    ExpirationAction recommended_action{ExpirationAction::Expire};
    std::string recommendation_reason;
    
    // Roll suggestion
    std::string roll_to_expiration;
    double roll_credit{0};
};

/**
 * @brief Options Expiration Manager
 */
class ExpirationManager {
private:
    OptionsPortfolio& portfolio_;
    double threshold_itm_pct_{0.01};  // Consider ITM if > 1% in the money

public:
    explicit ExpirationManager(OptionsPortfolio& portfolio)
        : portfolio_(portfolio) {}
    
    /**
     * @brief Check for expiring positions
     */
    std::vector<ExpirationEvent> check_expirations(int days_threshold = 7) {
        std::vector<ExpirationEvent> events;
        
        auto expiring = portfolio_.get_expiring(days_threshold);
        
        for (const auto& pos : expiring) {
            ExpirationEvent event;
            event.position_id = pos.position_id;
            event.position = pos;
            event.expiration_date = pos.contract.expiration_date;
            event.days_to_expiration = pos.contract.days_to_expiration;
            
            // Determine recommended action
            double moneyness = pos.contract.underlying_price / pos.contract.strike;
            if (pos.contract.type == OptionType::Put) {
                moneyness = 1.0 / moneyness;
            }
            
            if (pos.quantity > 0) {
                // Long position
                if (pos.contract.is_itm()) {
                    if (pos.contract.time_value() < 0.05) {
                        event.recommended_action = ExpirationAction::Exercise;
                        event.recommendation_reason = "Deep ITM with minimal time value";
                    } else {
                        event.recommended_action = ExpirationAction::Close;
                        event.recommendation_reason = "ITM - capture remaining time value";
                    }
                } else if (pos.contract.mark > 0.10) {
                    event.recommended_action = ExpirationAction::Close;
                    event.recommendation_reason = "OTM but still has value";
                } else {
                    event.recommended_action = ExpirationAction::Expire;
                    event.recommendation_reason = "OTM with minimal value";
                }
            } else {
                // Short position
                if (pos.contract.is_itm()) {
                    event.recommended_action = ExpirationAction::Roll;
                    event.recommendation_reason = "ITM short - risk of assignment";
                } else if (pos.contract.mark < 0.05) {
                    event.recommended_action = ExpirationAction::Expire;
                    event.recommendation_reason = "OTM short - let expire worthless";
                } else {
                    event.recommended_action = ExpirationAction::Close;
                    event.recommendation_reason = "Short position - close to remove risk";
                }
            }
            
            events.push_back(event);
        }
        
        return events;
    }
    
    /**
     * @brief Get roll suggestions
     */
    struct RollSuggestion {
        std::string from_expiration;
        std::string to_expiration;
        double from_strike;
        double to_strike;
        double net_credit{0};
        double delta_change{0};
        std::string rationale;
    };
    
    RollSuggestion suggest_roll(const OptionsPosition& position,
                                const OptionsChain& chain,
                                int target_dte = 30) {
        RollSuggestion suggestion;
        suggestion.from_expiration = position.contract.expiration_date;
        suggestion.from_strike = position.contract.strike;
        
        // Find nearest expiration to target DTE
        suggestion.to_expiration = chain.get_nearest_expiration(target_dte);
        if (suggestion.to_expiration.empty()) {
            return suggestion;
        }
        
        // Same strike or adjust based on delta
        suggestion.to_strike = position.contract.strike;
        
        // Get new contract
        auto* new_contract = chain.get(suggestion.to_expiration,
                                        suggestion.to_strike,
                                        position.contract.type);
        if (!new_contract) return suggestion;
        
        // Calculate credit/debit
        if (position.quantity < 0) {
            // Short position: buy to close, sell to open
            suggestion.net_credit = position.contract.ask - new_contract->bid;
        } else {
            // Long position: sell to close, buy to open
            suggestion.net_credit = position.contract.bid - new_contract->ask;
        }
        
        suggestion.net_credit *= position.contract.multiplier * std::abs(position.quantity);
        
        suggestion.delta_change = new_contract->delta - position.contract.delta;
        suggestion.rationale = suggestion.net_credit > 0 ? 
            "Roll for credit" : "Roll for debit to extend duration";
        
        return suggestion;
    }
};

// =============================================================================
// Options Chain Fetcher Interface
// =============================================================================

/**
 * @brief Interface for fetching options chains from brokers
 */
class IOptionsDataProvider {
public:
    virtual ~IOptionsDataProvider() = default;
    
    virtual OptionsChain get_chain(const std::string& symbol) = 0;
    virtual OptionsChain get_chain(const std::string& symbol,
                                   const std::string& expiration) = 0;
    virtual std::vector<std::string> get_expirations(const std::string& symbol) = 0;
    virtual std::vector<double> get_strikes(const std::string& symbol,
                                            const std::string& expiration) = 0;
};

/**
 * @brief Mock options data provider for testing
 */
class MockOptionsProvider : public IOptionsDataProvider {
private:
    double risk_free_rate_{0.05};
    
    OptionContract create_contract(const std::string& underlying,
                                   double underlying_price,
                                   double strike,
                                   const std::string& expiration,
                                   int dte,
                                   OptionType type,
                                   double iv) {
        OptionContract contract;
        contract.underlying = underlying;
        contract.underlying_price = underlying_price;
        contract.strike = strike;
        contract.expiration_date = expiration;
        contract.days_to_expiration = dte;
        contract.type = type;
        contract.multiplier = 100;
        
        // Calculate Greeks
        double T = dte / 365.0;
        GreeksCalculator::calculate(contract, underlying_price, strike, T, 
                                    risk_free_rate_, iv);
        
        // Calculate theoretical price
        double theo = GreeksCalculator::calculate_price(underlying_price, strike, T,
                                                        risk_free_rate_, iv, type);
        
        // Add spread
        contract.mark = theo;
        contract.bid = theo * 0.98;
        contract.ask = theo * 1.02;
        contract.last = theo;
        
        // Generate symbol
        std::ostringstream sym;
        sym << underlying << expiration.substr(2, 2) << expiration.substr(5, 2) 
            << expiration.substr(8, 2) << (type == OptionType::Call ? "C" : "P")
            << std::fixed << std::setprecision(0) << std::setw(8) << std::setfill('0')
            << strike * 1000;
        contract.symbol = sym.str();
        
        // Mock volume/OI
        double moneyness = std::abs(strike - underlying_price) / underlying_price;
        contract.volume = static_cast<int64_t>(1000 * std::exp(-moneyness * 10));
        contract.open_interest = contract.volume * 10;
        
        return contract;
    }

public:
    OptionsChain get_chain(const std::string& symbol) override {
        OptionsChain chain;
        chain.underlying = symbol;
        chain.underlying_price = 150.0;  // Mock price
        
        // Generate expirations (weeklys + monthlys)
        std::vector<std::pair<std::string, int>> expirations = {
            {"2026-02-07", 3},
            {"2026-02-14", 10},
            {"2026-02-21", 17},
            {"2026-02-28", 24},
            {"2026-03-21", 45},
            {"2026-04-18", 73},
            {"2026-06-19", 135},
            {"2026-09-18", 226}
        };
        
        // Generate strikes around ATM
        std::vector<double> strikes;
        for (double s = 120; s <= 180; s += 5) {
            strikes.push_back(s);
        }
        chain.strikes = strikes;
        
        // Base IV (ATM)
        double base_iv = 0.25;
        
        for (const auto& [exp, dte] : expirations) {
            chain.expirations.push_back(exp);
            
            for (double strike : strikes) {
                // IV skew: higher for OTM puts, lower for OTM calls
                double moneyness = (strike - chain.underlying_price) / chain.underlying_price;
                double iv_call = base_iv - moneyness * 0.1;
                double iv_put = base_iv + moneyness * 0.15;
                iv_call = std::clamp(iv_call, 0.1, 1.0);
                iv_put = std::clamp(iv_put, 0.1, 1.0);
                
                auto call = create_contract(symbol, chain.underlying_price, strike,
                                           exp, dte, OptionType::Call, iv_call);
                auto put = create_contract(symbol, chain.underlying_price, strike,
                                          exp, dte, OptionType::Put, iv_put);
                
                chain.calls[exp][strike] = call;
                chain.puts[exp][strike] = put;
            }
        }
        
        return chain;
    }
    
    OptionsChain get_chain(const std::string& symbol,
                           const std::string& expiration) override {
        auto full_chain = get_chain(symbol);
        
        // Filter to single expiration
        OptionsChain filtered;
        filtered.underlying = full_chain.underlying;
        filtered.underlying_price = full_chain.underlying_price;
        filtered.expirations = {expiration};
        filtered.strikes = full_chain.strikes;
        
        if (full_chain.calls.count(expiration)) {
            filtered.calls[expiration] = full_chain.calls[expiration];
        }
        if (full_chain.puts.count(expiration)) {
            filtered.puts[expiration] = full_chain.puts[expiration];
        }
        
        return filtered;
    }
    
    std::vector<std::string> get_expirations(const std::string& symbol) override {
        auto chain = get_chain(symbol);
        return chain.expirations;
    }
    
    std::vector<double> get_strikes(const std::string& symbol,
                                    const std::string& expiration) override {
        auto chain = get_chain(symbol, expiration);
        return chain.strikes;
    }
};

} // namespace genie::trading

#endif // GENIE_TRADING_OPTIONS_INTEGRATION_HPP
