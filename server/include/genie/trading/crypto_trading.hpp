/**
 * @file crypto_trading.hpp
 * @brief Cryptocurrency trading module
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements cryptocurrency trading capabilities:
 * - Multi-exchange support (Coinbase, Kraken, Binance, Gemini)
 * - Wallet management with hot/cold separation
 * - DeFi protocol integration stubs
 * - Gas fee estimation and optimization
 * - Cross-chain bridge tracking
 * - Staking and yield tracking
 * - Token metadata and classification
 * - Regulatory compliance (Travel Rule, MiCA)
 *
 * Zero external dependencies. Cross-platform (Windows/Linux/macOS).
 */

#pragma once
#ifndef GENIE_TRADING_CRYPTO_TRADING_HPP
#define GENIE_TRADING_CRYPTO_TRADING_HPP

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <functional>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <atomic>
#include <memory>
#include <variant>

namespace genie {
namespace trading {
namespace crypto {

// ============================================================================
// Enumerations
// ============================================================================

enum class CryptoExchange {
    Coinbase,
    CoinbasePro,
    Kraken,
    Binance,
    BinanceUS,
    Gemini,
    FTX,
    Bitfinex,
    Huobi,
    KuCoin,
    Custom
};

enum class NetworkChain {
    Bitcoin,
    Ethereum,
    Solana,
    Polygon,
    Avalanche,
    Arbitrum,
    Optimism,
    BNBChain,
    Cardano,
    Polkadot,
    Cosmos,
    Custom
};

enum class TokenStandard {
    Native,       // BTC, ETH, SOL
    ERC20,        // Ethereum tokens
    ERC721,       // NFTs
    ERC1155,      // Multi-token
    BEP20,        // BNB Chain
    SPL,          // Solana
    CW20,         // Cosmos
    Custom
};

enum class WalletType {
    Hot,
    Cold,
    Hardware,
    Custodial,
    MultiSig,
    SmartContract
};

enum class OrderSide { Buy, Sell };
enum class CryptoOrderType { Market, Limit, StopLimit, TrailingStop, TWAP, Iceberg };
enum class CryptoOrderStatus { Pending, Open, PartialFill, Filled, Cancelled, Rejected, Expired };

enum class StakingStatus { Active, Pending, Unbonding, Completed, Slashed };

enum class DeFiProtocolType {
    DEX,          // Uniswap, SushiSwap
    Lending,      // Aave, Compound
    Yield,        // Yearn, Convex
    Bridge,       // Wormhole, Stargate
    Staking,      // Lido, Rocket Pool
    Derivatives,  // dYdX, GMX
    Insurance,    // Nexus Mutual
    Custom
};

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] inline std::string exchange_to_string(CryptoExchange ex) {
    switch (ex) {
        case CryptoExchange::Coinbase:    return "Coinbase";
        case CryptoExchange::CoinbasePro: return "Coinbase Pro";
        case CryptoExchange::Kraken:      return "Kraken";
        case CryptoExchange::Binance:     return "Binance";
        case CryptoExchange::BinanceUS:   return "Binance US";
        case CryptoExchange::Gemini:      return "Gemini";
        case CryptoExchange::FTX:         return "FTX";
        case CryptoExchange::Bitfinex:    return "Bitfinex";
        case CryptoExchange::Huobi:       return "Huobi";
        case CryptoExchange::KuCoin:      return "KuCoin";
        case CryptoExchange::Custom:      return "Custom";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string chain_to_string(NetworkChain chain) {
    switch (chain) {
        case NetworkChain::Bitcoin:   return "Bitcoin";
        case NetworkChain::Ethereum:  return "Ethereum";
        case NetworkChain::Solana:    return "Solana";
        case NetworkChain::Polygon:   return "Polygon";
        case NetworkChain::Avalanche: return "Avalanche";
        case NetworkChain::Arbitrum:  return "Arbitrum";
        case NetworkChain::Optimism:  return "Optimism";
        case NetworkChain::BNBChain:  return "BNB Chain";
        case NetworkChain::Cardano:   return "Cardano";
        case NetworkChain::Polkadot:  return "Polkadot";
        case NetworkChain::Cosmos:    return "Cosmos";
        case NetworkChain::Custom:    return "Custom";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string wallet_type_to_string(WalletType wt) {
    switch (wt) {
        case WalletType::Hot:           return "Hot";
        case WalletType::Cold:          return "Cold";
        case WalletType::Hardware:      return "Hardware";
        case WalletType::Custodial:     return "Custodial";
        case WalletType::MultiSig:      return "MultiSig";
        case WalletType::SmartContract: return "Smart Contract";
    }
    return "Unknown";
}

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Token metadata
 */
struct TokenInfo {
    std::string symbol;
    std::string name;
    std::string contract_address;
    NetworkChain chain{NetworkChain::Ethereum};
    TokenStandard standard{TokenStandard::Native};
    int decimals{18};
    double total_supply{0};
    double circulating_supply{0};
    double market_cap{0};
    std::string coingecko_id;
    std::string logo_url;
    bool is_stablecoin{false};
    bool is_wrapped{false};
    std::string underlying_symbol;    // For wrapped tokens
    std::vector<std::string> tags;    // "defi", "layer2", "governance", etc.
};

/**
 * @brief Crypto wallet
 */
struct CryptoWallet {
    std::string id;
    std::string name;
    std::string address;
    WalletType type{WalletType::Hot};
    NetworkChain chain{NetworkChain::Ethereum};
    std::map<std::string, double> balances;  // symbol -> amount
    double total_value_usd{0};
    bool is_connected{false};
    std::string last_sync;
    
    [[nodiscard]] double balance(const std::string& symbol) const {
        auto it = balances.find(symbol);
        return it != balances.end() ? it->second : 0.0;
    }
};

/**
 * @brief Gas fee estimate
 */
struct GasFeeEstimate {
    NetworkChain chain{NetworkChain::Ethereum};
    double slow_gwei{0};
    double standard_gwei{0};
    double fast_gwei{0};
    double instant_gwei{0};
    double base_fee_gwei{0};
    double priority_fee_gwei{0};
    int estimated_seconds_slow{0};
    int estimated_seconds_standard{0};
    int estimated_seconds_fast{0};
    double estimated_cost_usd_slow{0};
    double estimated_cost_usd_standard{0};
    double estimated_cost_usd_fast{0};
    std::string timestamp;
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << chain_to_string(chain) << " Gas Fees:\n";
        oss << "  Slow:     " << slow_gwei << " gwei ($" << estimated_cost_usd_slow << ")\n";
        oss << "  Standard: " << standard_gwei << " gwei ($" << estimated_cost_usd_standard << ")\n";
        oss << "  Fast:     " << fast_gwei << " gwei ($" << estimated_cost_usd_fast << ")\n";
        return oss.str();
    }
};

/**
 * @brief Crypto order
 */
struct CryptoOrder {
    std::string id;
    std::string client_order_id;
    CryptoExchange exchange{CryptoExchange::Coinbase};
    std::string base_symbol;       // e.g., "BTC"
    std::string quote_symbol;      // e.g., "USD"
    OrderSide side{OrderSide::Buy};
    CryptoOrderType type{CryptoOrderType::Market};
    CryptoOrderStatus status{CryptoOrderStatus::Pending};
    double quantity{0};
    double price{0};               // Limit price
    double stop_price{0};          // Stop trigger price
    double filled_quantity{0};
    double average_fill_price{0};
    double total_cost{0};
    double fee{0};
    std::string fee_currency;
    std::string created_at;
    std::string updated_at;
    
    [[nodiscard]] std::string pair() const {
        return base_symbol + "/" + quote_symbol;
    }
    
    [[nodiscard]] double fill_pct() const {
        return quantity > 0 ? (filled_quantity / quantity) * 100.0 : 0.0;
    }
    
    [[nodiscard]] double remaining() const {
        return quantity - filled_quantity;
    }
};

/**
 * @brief Staking position
 */
struct StakingPosition {
    std::string id;
    std::string validator_name;
    std::string validator_address;
    NetworkChain chain{NetworkChain::Ethereum};
    std::string token_symbol;
    double staked_amount{0};
    double rewards_earned{0};
    double annual_yield_pct{0};
    StakingStatus status{StakingStatus::Active};
    std::string staked_date;
    std::string unbond_date;         // When unbonding completes
    int unbond_period_days{0};
    double slashing_risk_pct{0};     // Estimated risk
    double commission_pct{0};        // Validator commission
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << token_symbol << " Staking: " << staked_amount;
        oss << " @ " << std::setprecision(2) << annual_yield_pct << "% APY";
        oss << " (Rewards: " << std::setprecision(4) << rewards_earned << ")";
        return oss.str();
    }
};

/**
 * @brief DeFi protocol position
 */
struct DeFiPosition {
    std::string id;
    std::string protocol_name;
    DeFiProtocolType type{DeFiProtocolType::DEX};
    NetworkChain chain{NetworkChain::Ethereum};
    std::string pool_name;
    std::map<std::string, double> deposited;    // symbol -> amount
    std::map<std::string, double> rewards;      // symbol -> amount
    double value_usd{0};
    double apy{0};
    double impermanent_loss_pct{0};
    double tvl{0};                              // Total value locked
    std::string contract_address;
    bool is_active{true};
};

/**
 * @brief Cross-chain bridge transfer
 */
struct BridgeTransfer {
    std::string id;
    std::string bridge_name;        // e.g., "Wormhole", "Stargate"
    NetworkChain source_chain{NetworkChain::Ethereum};
    NetworkChain dest_chain{NetworkChain::Polygon};
    std::string token_symbol;
    double amount{0};
    double fee{0};
    std::string source_tx_hash;
    std::string dest_tx_hash;
    std::string status;             // "pending", "confirmed", "completed", "failed"
    std::string initiated_at;
    std::string completed_at;
    int estimated_minutes{0};
};

/**
 * @brief Portfolio-level crypto analytics
 */
struct CryptoPortfolioAnalytics {
    double total_value_usd{0};
    double total_staked_usd{0};
    double total_defi_usd{0};
    double total_rewards_usd{0};
    double bitcoin_dominance_pct{0};
    double stablecoin_pct{0};
    int num_tokens{0};
    int num_chains{0};
    int num_wallets{0};
    int num_staking_positions{0};
    int num_defi_positions{0};
    
    std::map<std::string, double> allocation_by_chain;
    std::map<std::string, double> allocation_by_token;
    std::map<std::string, double> allocation_by_type;     // "native", "defi", "staked"
    
    [[nodiscard]] std::string format() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << "Crypto Portfolio Analytics:\n";
        oss << "  Total Value:    $" << total_value_usd << "\n";
        oss << "  Staked:         $" << total_staked_usd << "\n";
        oss << "  DeFi:           $" << total_defi_usd << "\n";
        oss << "  Rewards Earned: $" << total_rewards_usd << "\n";
        oss << "  Tokens: " << num_tokens << " across " << num_chains << " chains\n";
        oss << "  BTC Dominance:  " << bitcoin_dominance_pct << "%\n";
        oss << "  Stablecoin:     " << stablecoin_pct << "%\n";
        return oss.str();
    }
};

/**
 * @brief Travel Rule compliance data (FATF)
 */
struct TravelRuleData {
    std::string originator_name;
    std::string originator_account;
    std::string originator_address;
    std::string originator_country;
    std::string beneficiary_name;
    std::string beneficiary_account;
    std::string beneficiary_institution;
    std::string beneficiary_country;
    double amount{0};
    std::string currency;
    std::string transaction_ref;
    bool is_compliant{false};
    std::string vasp_id;           // Virtual Asset Service Provider ID
};

// ============================================================================
// Gas Fee Estimator
// ============================================================================

/**
 * @brief Gas fee estimation engine
 */
class GasFeeEstimator {
public:
    /**
     * @brief Get current gas fee estimate for a chain
     */
    [[nodiscard]] GasFeeEstimate estimate(NetworkChain chain) const {
        GasFeeEstimate est;
        est.chain = chain;
        
        // Default estimates (would be fetched from APIs in production)
        switch (chain) {
            case NetworkChain::Ethereum:
                est.slow_gwei = 15.0;
                est.standard_gwei = 25.0;
                est.fast_gwei = 40.0;
                est.instant_gwei = 60.0;
                est.base_fee_gwei = 12.0;
                est.estimated_seconds_slow = 300;
                est.estimated_seconds_standard = 60;
                est.estimated_seconds_fast = 15;
                break;
            case NetworkChain::Polygon:
                est.slow_gwei = 30.0;
                est.standard_gwei = 50.0;
                est.fast_gwei = 80.0;
                est.instant_gwei = 120.0;
                est.estimated_seconds_slow = 10;
                est.estimated_seconds_standard = 5;
                est.estimated_seconds_fast = 2;
                break;
            case NetworkChain::Arbitrum:
            case NetworkChain::Optimism:
                est.slow_gwei = 0.1;
                est.standard_gwei = 0.15;
                est.fast_gwei = 0.25;
                est.instant_gwei = 0.5;
                est.estimated_seconds_slow = 5;
                est.estimated_seconds_standard = 2;
                est.estimated_seconds_fast = 1;
                break;
            case NetworkChain::Solana:
                est.slow_gwei = 0.00001;
                est.standard_gwei = 0.00005;
                est.fast_gwei = 0.0001;
                est.estimated_seconds_slow = 2;
                est.estimated_seconds_standard = 1;
                est.estimated_seconds_fast = 1;
                break;
            default:
                est.slow_gwei = 5.0;
                est.standard_gwei = 10.0;
                est.fast_gwei = 20.0;
                break;
        }
        
        // Estimate USD costs (simplified: ETH at ~$3000)
        double eth_price = 3000.0;
        double gas_limit = 21000.0;  // Simple transfer
        est.estimated_cost_usd_slow = (est.slow_gwei * gas_limit * eth_price) / 1e9;
        est.estimated_cost_usd_standard = (est.standard_gwei * gas_limit * eth_price) / 1e9;
        est.estimated_cost_usd_fast = (est.fast_gwei * gas_limit * eth_price) / 1e9;
        
        return est;
    }
    
    /**
     * @brief Estimate gas for a specific operation type
     */
    [[nodiscard]] double estimate_gas_limit(const std::string& operation) const {
        if (operation == "transfer")     return 21000;
        if (operation == "erc20_transfer") return 65000;
        if (operation == "swap")         return 150000;
        if (operation == "add_liquidity") return 250000;
        if (operation == "stake")        return 100000;
        if (operation == "bridge")       return 200000;
        if (operation == "nft_mint")     return 120000;
        if (operation == "nft_transfer") return 80000;
        return 100000;  // Default
    }
};

// ============================================================================
// Wallet Manager
// ============================================================================

/**
 * @brief Cryptocurrency wallet management
 */
class WalletManager {
public:
    /**
     * @brief Add wallet
     */
    void add_wallet(const CryptoWallet& wallet) {
        std::lock_guard<std::mutex> lock(mutex_);
        wallets_[wallet.id] = wallet;
    }
    
    /**
     * @brief Get wallet by ID
     */
    [[nodiscard]] std::optional<CryptoWallet> get_wallet(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = wallets_.find(id);
        if (it == wallets_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief List all wallets
     */
    [[nodiscard]] std::vector<CryptoWallet> list_wallets() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoWallet> result;
        result.reserve(wallets_.size());
        for (const auto& [id, w] : wallets_) {
            result.push_back(w);
        }
        return result;
    }
    
    /**
     * @brief Get total portfolio value across all wallets
     */
    [[nodiscard]] double total_value_usd() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total = 0;
        for (const auto& [id, w] : wallets_) {
            total += w.total_value_usd;
        }
        return total;
    }
    
    /**
     * @brief Get aggregated balances across all wallets
     */
    [[nodiscard]] std::map<std::string, double> aggregated_balances() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, double> result;
        for (const auto& [id, w] : wallets_) {
            for (const auto& [symbol, amount] : w.balances) {
                result[symbol] += amount;
            }
        }
        return result;
    }
    
    /**
     * @brief Get wallets by chain
     */
    [[nodiscard]] std::vector<CryptoWallet> wallets_by_chain(NetworkChain chain) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoWallet> result;
        for (const auto& [id, w] : wallets_) {
            if (w.chain == chain) result.push_back(w);
        }
        return result;
    }
    
    /**
     * @brief Get wallets by type
     */
    [[nodiscard]] std::vector<CryptoWallet> wallets_by_type(WalletType type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoWallet> result;
        for (const auto& [id, w] : wallets_) {
            if (w.type == type) result.push_back(w);
        }
        return result;
    }
    
    /**
     * @brief Remove wallet
     */
    bool remove_wallet(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return wallets_.erase(id) > 0;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, CryptoWallet> wallets_;
};

// ============================================================================
// Staking Manager
// ============================================================================

/**
 * @brief Staking position management
 */
class StakingManager {
public:
    /**
     * @brief Add staking position
     */
    void add_position(const StakingPosition& pos) {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_[pos.id] = pos;
    }
    
    /**
     * @brief Get staking position
     */
    [[nodiscard]] std::optional<StakingPosition> get_position(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = positions_.find(id);
        if (it == positions_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief List all staking positions
     */
    [[nodiscard]] std::vector<StakingPosition> list_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StakingPosition> result;
        result.reserve(positions_.size());
        for (const auto& [id, p] : positions_) {
            result.push_back(p);
        }
        return result;
    }
    
    /**
     * @brief Total staked value
     */
    [[nodiscard]] double total_staked(const std::string& token = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total = 0;
        for (const auto& [id, p] : positions_) {
            if (token.empty() || p.token_symbol == token) {
                total += p.staked_amount;
            }
        }
        return total;
    }
    
    /**
     * @brief Total rewards earned
     */
    [[nodiscard]] double total_rewards(const std::string& token = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total = 0;
        for (const auto& [id, p] : positions_) {
            if (token.empty() || p.token_symbol == token) {
                total += p.rewards_earned;
            }
        }
        return total;
    }
    
    /**
     * @brief Weighted average APY
     */
    [[nodiscard]] double weighted_avg_apy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total_staked = 0;
        double weighted_apy = 0;
        for (const auto& [id, p] : positions_) {
            total_staked += p.staked_amount;
            weighted_apy += p.staked_amount * p.annual_yield_pct;
        }
        return total_staked > 0 ? weighted_apy / total_staked : 0.0;
    }
    
    /**
     * @brief Positions by chain
     */
    [[nodiscard]] std::vector<StakingPosition> positions_by_chain(NetworkChain chain) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<StakingPosition> result;
        for (const auto& [id, p] : positions_) {
            if (p.chain == chain) result.push_back(p);
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, StakingPosition> positions_;
};

// ============================================================================
// Token Registry
// ============================================================================

/**
 * @brief Token metadata registry
 */
class TokenRegistry {
public:
    TokenRegistry() {
        // Pre-load major tokens
        register_defaults();
    }
    
    /**
     * @brief Register token
     */
    void register_token(const TokenInfo& token) {
        tokens_[token.symbol] = token;
    }
    
    /**
     * @brief Get token info
     */
    [[nodiscard]] std::optional<TokenInfo> get_token(const std::string& symbol) const {
        auto it = tokens_.find(symbol);
        if (it == tokens_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief Get all registered tokens
     */
    [[nodiscard]] std::vector<TokenInfo> all_tokens() const {
        std::vector<TokenInfo> result;
        result.reserve(tokens_.size());
        for (const auto& [sym, info] : tokens_) {
            result.push_back(info);
        }
        return result;
    }
    
    /**
     * @brief Search tokens by tag
     */
    [[nodiscard]] std::vector<TokenInfo> tokens_by_tag(const std::string& tag) const {
        std::vector<TokenInfo> result;
        for (const auto& [sym, info] : tokens_) {
            for (const auto& t : info.tags) {
                if (t == tag) { result.push_back(info); break; }
            }
        }
        return result;
    }
    
    /**
     * @brief Get stablecoins
     */
    [[nodiscard]] std::vector<TokenInfo> stablecoins() const {
        std::vector<TokenInfo> result;
        for (const auto& [sym, info] : tokens_) {
            if (info.is_stablecoin) result.push_back(info);
        }
        return result;
    }
    
    /**
     * @brief Tokens by chain
     */
    [[nodiscard]] std::vector<TokenInfo> tokens_by_chain(NetworkChain chain) const {
        std::vector<TokenInfo> result;
        for (const auto& [sym, info] : tokens_) {
            if (info.chain == chain) result.push_back(info);
        }
        return result;
    }

private:
    std::map<std::string, TokenInfo> tokens_;
    
    void register_defaults() {
        tokens_["BTC"] = {"BTC", "Bitcoin", "", NetworkChain::Bitcoin, 
            TokenStandard::Native, 8, 21000000, 19500000, 0, "bitcoin", "", 
            false, false, "", {"layer1", "store-of-value"}};
        tokens_["ETH"] = {"ETH", "Ethereum", "", NetworkChain::Ethereum, 
            TokenStandard::Native, 18, 0, 120000000, 0, "ethereum", "", 
            false, false, "", {"layer1", "smart-contracts", "defi"}};
        tokens_["SOL"] = {"SOL", "Solana", "", NetworkChain::Solana, 
            TokenStandard::Native, 9, 0, 430000000, 0, "solana", "", 
            false, false, "", {"layer1", "smart-contracts"}};
        tokens_["USDC"] = {"USDC", "USD Coin", "0xa0b8...fE6B", NetworkChain::Ethereum, 
            TokenStandard::ERC20, 6, 0, 30000000000, 0, "usd-coin", "", 
            true, false, "", {"stablecoin", "defi"}};
        tokens_["USDT"] = {"USDT", "Tether", "0xdAC1...1eC7", NetworkChain::Ethereum, 
            TokenStandard::ERC20, 6, 0, 83000000000, 0, "tether", "", 
            true, false, "", {"stablecoin"}};
        tokens_["MATIC"] = {"MATIC", "Polygon", "0x7D1A...4c27", NetworkChain::Polygon, 
            TokenStandard::Native, 18, 10000000000, 9300000000, 0, "matic-network", "", 
            false, false, "", {"layer2", "scaling"}};
    }
};

// ============================================================================
// Crypto Order Manager
// ============================================================================

/**
 * @brief Cryptocurrency order management
 */
class CryptoOrderManager {
public:
    /**
     * @brief Submit crypto order
     */
    CryptoOrder submit_order(CryptoOrder order) {
        std::lock_guard<std::mutex> lock(mutex_);
        order.id = generate_order_id();
        order.status = CryptoOrderStatus::Pending;
        
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        order.created_at = oss.str();
        order.updated_at = order.created_at;
        
        orders_[order.id] = order;
        return order;
    }
    
    /**
     * @brief Cancel order
     */
    bool cancel_order(const std::string& order_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return false;
        if (it->second.status == CryptoOrderStatus::Filled ||
            it->second.status == CryptoOrderStatus::Cancelled) return false;
        it->second.status = CryptoOrderStatus::Cancelled;
        return true;
    }
    
    /**
     * @brief Get order
     */
    [[nodiscard]] std::optional<CryptoOrder> get_order(const std::string& order_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief List open orders
     */
    [[nodiscard]] std::vector<CryptoOrder> open_orders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoOrder> result;
        for (const auto& [id, o] : orders_) {
            if (o.status == CryptoOrderStatus::Open || 
                o.status == CryptoOrderStatus::Pending ||
                o.status == CryptoOrderStatus::PartialFill) {
                result.push_back(o);
            }
        }
        return result;
    }
    
    /**
     * @brief List all orders
     */
    [[nodiscard]] std::vector<CryptoOrder> all_orders() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoOrder> result;
        result.reserve(orders_.size());
        for (const auto& [id, o] : orders_) {
            result.push_back(o);
        }
        return result;
    }
    
    /**
     * @brief Orders by exchange
     */
    [[nodiscard]] std::vector<CryptoOrder> orders_by_exchange(CryptoExchange exchange) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CryptoOrder> result;
        for (const auto& [id, o] : orders_) {
            if (o.exchange == exchange) result.push_back(o);
        }
        return result;
    }
    
    /**
     * @brief Total order count
     */
    [[nodiscard]] size_t order_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return orders_.size();
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, CryptoOrder> orders_;
    int next_id_{1};
    
    std::string generate_order_id() {
        return "CRYPTO-" + std::to_string(next_id_++);
    }
};

// ============================================================================
// DeFi Manager
// ============================================================================

/**
 * @brief DeFi position management
 */
class DeFiManager {
public:
    /**
     * @brief Add DeFi position
     */
    void add_position(const DeFiPosition& pos) {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_[pos.id] = pos;
    }
    
    /**
     * @brief Get position
     */
    [[nodiscard]] std::optional<DeFiPosition> get_position(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = positions_.find(id);
        if (it == positions_.end()) return std::nullopt;
        return it->second;
    }
    
    /**
     * @brief List all positions
     */
    [[nodiscard]] std::vector<DeFiPosition> list_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DeFiPosition> result;
        result.reserve(positions_.size());
        for (const auto& [id, p] : positions_) {
            result.push_back(p);
        }
        return result;
    }
    
    /**
     * @brief Total DeFi value
     */
    [[nodiscard]] double total_value_usd() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total = 0;
        for (const auto& [id, p] : positions_) {
            total += p.value_usd;
        }
        return total;
    }
    
    /**
     * @brief Positions by protocol type
     */
    [[nodiscard]] std::vector<DeFiPosition> positions_by_type(DeFiProtocolType type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DeFiPosition> result;
        for (const auto& [id, p] : positions_) {
            if (p.type == type) result.push_back(p);
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, DeFiPosition> positions_;
};

// ============================================================================
// Travel Rule Compliance
// ============================================================================

/**
 * @brief Travel Rule compliance checker (FATF/MiCA)
 */
class TravelRuleCompliance {
public:
    /**
     * @brief Check if transfer requires Travel Rule data
     */
    [[nodiscard]] bool requires_travel_rule(double amount_usd, 
                                             const std::string& jurisdiction = "US") const {
        // US: $3,000 threshold (FinCEN)
        // EU: €1,000 threshold (MiCA/TFR)
        // FATF: $1,000 recommended
        if (jurisdiction == "US") return amount_usd >= 3000.0;
        if (jurisdiction == "EU") return amount_usd >= 1000.0;
        return amount_usd >= 1000.0;  // FATF default
    }
    
    /**
     * @brief Validate Travel Rule data completeness
     */
    [[nodiscard]] std::vector<std::string> validate(const TravelRuleData& data) const {
        std::vector<std::string> errors;
        
        if (data.originator_name.empty())
            errors.push_back("Originator name required");
        if (data.originator_account.empty())
            errors.push_back("Originator account/address required");
        if (data.beneficiary_name.empty())
            errors.push_back("Beneficiary name required");
        if (data.beneficiary_account.empty())
            errors.push_back("Beneficiary account/address required");
        if (data.amount <= 0)
            errors.push_back("Valid amount required");
        if (data.currency.empty())
            errors.push_back("Currency required");
            
        return errors;
    }
};

// ============================================================================
// Crypto Portfolio Analyzer
// ============================================================================

/**
 * @brief Analyze crypto portfolio across wallets, staking, and DeFi
 */
class CryptoPortfolioAnalyzer {
public:
    CryptoPortfolioAnalyzer(const WalletManager& wallets,
                            const StakingManager& staking,
                            const DeFiManager& defi)
        : wallets_(wallets), staking_(staking), defi_(defi) {}
    
    /**
     * @brief Generate comprehensive portfolio analytics
     */
    [[nodiscard]] CryptoPortfolioAnalytics analyze() const {
        CryptoPortfolioAnalytics analytics;
        
        // Wallet analysis
        auto all_wallets = wallets_.list_wallets();
        analytics.num_wallets = static_cast<int>(all_wallets.size());
        
        std::map<std::string, double> chain_values;
        std::map<std::string, double> token_totals;
        
        for (const auto& w : all_wallets) {
            analytics.total_value_usd += w.total_value_usd;
            chain_values[chain_to_string(w.chain)] += w.total_value_usd;
            for (const auto& [sym, amt] : w.balances) {
                token_totals[sym] += amt;
            }
        }
        
        // Staking analysis
        auto staking_positions = staking_.list_positions();
        analytics.num_staking_positions = static_cast<int>(staking_positions.size());
        analytics.total_staked_usd = staking_.total_staked();
        analytics.total_rewards_usd = staking_.total_rewards();
        
        // DeFi analysis
        auto defi_positions = defi_.list_positions();
        analytics.num_defi_positions = static_cast<int>(defi_positions.size());
        analytics.total_defi_usd = defi_.total_value_usd();
        
        analytics.num_tokens = static_cast<int>(token_totals.size());
        analytics.num_chains = static_cast<int>(chain_values.size());
        
        // Allocation by chain
        double total = analytics.total_value_usd;
        if (total > 0) {
            for (const auto& [chain, value] : chain_values) {
                analytics.allocation_by_chain[chain] = (value / total) * 100.0;
            }
        }
        
        return analytics;
    }

private:
    const WalletManager& wallets_;
    const StakingManager& staking_;
    const DeFiManager& defi_;
};

} // namespace crypto
} // namespace trading
} // namespace genie

#endif // GENIE_TRADING_CRYPTO_TRADING_HPP
