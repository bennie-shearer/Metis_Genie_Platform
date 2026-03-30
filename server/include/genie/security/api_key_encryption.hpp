/**
 * @file api_key_encryption.hpp
 * @brief API key encryption with AES-256
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Security - Implement API key encryption (AES-256)
 */

#ifndef GENIE_SECURITY_API_KEY_ENCRYPTION_HPP
#define GENIE_SECURITY_API_KEY_ENCRYPTION_HPP

#include <string>
#include <vector>
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <cstdint>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>

namespace genie {
namespace security {

/**
 * @brief AES-256 constants
 */
constexpr int AES_BLOCK_SIZE = 16;
constexpr int AES_KEY_SIZE = 32;    // 256 bits
constexpr int AES_ROUNDS = 14;

/**
 * @brief AES S-box
 */
constexpr uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/**
 * @brief Inverse S-box
 */
constexpr uint8_t INV_SBOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/**
 * @brief Round constants
 */
constexpr uint8_t RCON[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/**
 * @brief AES-256 encryption engine
 */
class AES256 {
public:
    using Block = std::array<uint8_t, AES_BLOCK_SIZE>;
    using Key = std::array<uint8_t, AES_KEY_SIZE>;
    using IV = std::array<uint8_t, AES_BLOCK_SIZE>;
    
    /**
     * @brief Initialize with key
     */
    explicit AES256(const Key& key) {
        key_expansion(key);
    }
    
    /**
     * @brief Encrypt single block (ECB mode)
     */
    Block encrypt_block(const Block& plaintext) const {
        Block state = plaintext;
        
        // Initial round key addition
        add_round_key(state, 0);
        
        // Main rounds
        for (int round = 1; round < AES_ROUNDS; ++round) {
            sub_bytes(state);
            shift_rows(state);
            mix_columns(state);
            add_round_key(state, round);
        }
        
        // Final round (no mix columns)
        sub_bytes(state);
        shift_rows(state);
        add_round_key(state, AES_ROUNDS);
        
        return state;
    }
    
    /**
     * @brief Decrypt single block (ECB mode)
     */
    Block decrypt_block(const Block& ciphertext) const {
        Block state = ciphertext;
        
        // Initial round key addition
        add_round_key(state, AES_ROUNDS);
        
        // Main rounds
        for (int round = AES_ROUNDS - 1; round > 0; --round) {
            inv_shift_rows(state);
            inv_sub_bytes(state);
            add_round_key(state, round);
            inv_mix_columns(state);
        }
        
        // Final round
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, 0);
        
        return state;
    }
    
    /**
     * @brief Encrypt data in CBC mode
     */
    std::vector<uint8_t> encrypt_cbc(const std::vector<uint8_t>& plaintext,
                                      const IV& iv) const {
        std::vector<uint8_t> padded = pkcs7_pad(plaintext);
        std::vector<uint8_t> ciphertext;
        ciphertext.reserve(padded.size());
        
        Block prev_block = iv;
        
        for (size_t i = 0; i < padded.size(); i += AES_BLOCK_SIZE) {
            Block block;
            std::copy(padded.begin() + i, padded.begin() + i + AES_BLOCK_SIZE, block.begin());
            
            // XOR with previous block
            for (int j = 0; j < AES_BLOCK_SIZE; ++j) {
                block[j] ^= prev_block[j];
            }
            
            Block encrypted = encrypt_block(block);
            ciphertext.insert(ciphertext.end(), encrypted.begin(), encrypted.end());
            prev_block = encrypted;
        }
        
        return ciphertext;
    }
    
    /**
     * @brief Decrypt data in CBC mode
     */
    std::vector<uint8_t> decrypt_cbc(const std::vector<uint8_t>& ciphertext,
                                      const IV& iv) const {
        if (ciphertext.size() % AES_BLOCK_SIZE != 0) {
            throw std::invalid_argument("Invalid ciphertext length");
        }
        
        std::vector<uint8_t> plaintext;
        plaintext.reserve(ciphertext.size());
        
        Block prev_block = iv;
        
        for (size_t i = 0; i < ciphertext.size(); i += AES_BLOCK_SIZE) {
            Block block;
            std::copy(ciphertext.begin() + i, ciphertext.begin() + i + AES_BLOCK_SIZE, block.begin());
            
            Block decrypted = decrypt_block(block);
            
            // XOR with previous ciphertext block
            for (int j = 0; j < AES_BLOCK_SIZE; ++j) {
                decrypted[j] ^= prev_block[j];
            }
            
            plaintext.insert(plaintext.end(), decrypted.begin(), decrypted.end());
            prev_block = block;
        }
        
        return pkcs7_unpad(plaintext);
    }
    
    /**
     * @brief Generate random IV
     */
    static IV generate_iv() {
        IV iv;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        
        for (auto& b : iv) {
            b = static_cast<uint8_t>(dist(gen));
        }
        
        return iv;
    }
    
    /**
     * @brief Derive key from password using PBKDF2-like function
     */
    static Key derive_key(const std::string& password, const std::string& salt,
                          int iterations = 10000) {
        Key key{};
        std::vector<uint8_t> data;
        data.reserve(password.size() + salt.size() + 4);
        
        for (char c : password) data.push_back(static_cast<uint8_t>(c));
        for (char c : salt) data.push_back(static_cast<uint8_t>(c));
        
        // Simple key derivation (in production, use proper PBKDF2)
        for (int i = 0; i < iterations; ++i) {
            uint64_t hash = 0xcbf29ce484222325ULL;
            for (uint8_t b : data) {
                hash ^= b;
                hash *= 0x100000001b3ULL;
            }
            
            for (int j = 0; j < 8 && j + (i % 4) * 8 < AES_KEY_SIZE; ++j) {
                key[(i % 4) * 8 + j] ^= static_cast<uint8_t>((hash >> (j * 8)) & 0xFF);
            }
            
            data.push_back(static_cast<uint8_t>(hash & 0xFF));
        }
        
        return key;
    }

private:
    std::array<std::array<uint8_t, 4>, 60> round_keys_;
    
    void key_expansion(const Key& key) {
        // Copy initial key
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 4; ++j) {
                round_keys_[i][j] = key[i * 4 + j];
            }
        }
        
        // Generate remaining round keys
        for (int i = 8; i < 60; ++i) {
            std::array<uint8_t, 4> temp = round_keys_[i - 1];
            
            if (i % 8 == 0) {
                // Rotate
                uint8_t t = temp[0];
                temp[0] = temp[1];
                temp[1] = temp[2];
                temp[2] = temp[3];
                temp[3] = t;
                
                // SubBytes
                for (auto& b : temp) {
                    b = SBOX[b];
                }
                
                temp[0] ^= RCON[i / 8];
            } else if (i % 8 == 4) {
                for (auto& b : temp) {
                    b = SBOX[b];
                }
            }
            
            for (int j = 0; j < 4; ++j) {
                round_keys_[i][j] = round_keys_[i - 8][j] ^ temp[j];
            }
        }
    }
    
    void add_round_key(Block& state, int round) const {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                state[j * 4 + i] ^= round_keys_[round * 4 + i][j];
            }
        }
    }
    
    static void sub_bytes(Block& state) {
        for (auto& b : state) {
            b = SBOX[b];
        }
    }
    
    static void inv_sub_bytes(Block& state) {
        for (auto& b : state) {
            b = INV_SBOX[b];
        }
    }
    
    static void shift_rows(Block& state) {
        // Row 1: shift left by 1
        uint8_t t = state[1];
        state[1] = state[5];
        state[5] = state[9];
        state[9] = state[13];
        state[13] = t;
        
        // Row 2: shift left by 2
        std::swap(state[2], state[10]);
        std::swap(state[6], state[14]);
        
        // Row 3: shift left by 3
        t = state[15];
        state[15] = state[11];
        state[11] = state[7];
        state[7] = state[3];
        state[3] = t;
    }
    
    static void inv_shift_rows(Block& state) {
        // Row 1: shift right by 1
        uint8_t t = state[13];
        state[13] = state[9];
        state[9] = state[5];
        state[5] = state[1];
        state[1] = t;
        
        // Row 2: shift right by 2
        std::swap(state[2], state[10]);
        std::swap(state[6], state[14]);
        
        // Row 3: shift right by 3
        t = state[3];
        state[3] = state[7];
        state[7] = state[11];
        state[11] = state[15];
        state[15] = t;
    }
    
    static uint8_t gmul(uint8_t a, uint8_t b) {
        uint8_t p = 0;
        for (int i = 0; i < 8; ++i) {
            if (b & 1) p ^= a;
            bool hi = a & 0x80;
            a <<= 1;
            if (hi) a ^= 0x1b;
            b >>= 1;
        }
        return p;
    }
    
    static void mix_columns(Block& state) {
        for (int c = 0; c < 4; ++c) {
            int i = c * 4;
            uint8_t a0 = state[i], a1 = state[i+1], a2 = state[i+2], a3 = state[i+3];
            
            state[i]   = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
            state[i+1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
            state[i+2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
            state[i+3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
        }
    }
    
    static void inv_mix_columns(Block& state) {
        for (int c = 0; c < 4; ++c) {
            int i = c * 4;
            uint8_t a0 = state[i], a1 = state[i+1], a2 = state[i+2], a3 = state[i+3];
            
            state[i]   = gmul(a0, 0x0e) ^ gmul(a1, 0x0b) ^ gmul(a2, 0x0d) ^ gmul(a3, 0x09);
            state[i+1] = gmul(a0, 0x09) ^ gmul(a1, 0x0e) ^ gmul(a2, 0x0b) ^ gmul(a3, 0x0d);
            state[i+2] = gmul(a0, 0x0d) ^ gmul(a1, 0x09) ^ gmul(a2, 0x0e) ^ gmul(a3, 0x0b);
            state[i+3] = gmul(a0, 0x0b) ^ gmul(a1, 0x0d) ^ gmul(a2, 0x09) ^ gmul(a3, 0x0e);
        }
    }
    
    static std::vector<uint8_t> pkcs7_pad(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> padded = data;
        uint8_t pad_len = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
        for (int i = 0; i < pad_len; ++i) {
            padded.push_back(pad_len);
        }
        return padded;
    }
    
    static std::vector<uint8_t> pkcs7_unpad(const std::vector<uint8_t>& data) {
        if (data.empty()) return data;
        
        uint8_t pad_len = data.back();
        if (pad_len > AES_BLOCK_SIZE || pad_len == 0) {
            throw std::invalid_argument("Invalid padding");
        }
        
        // Verify padding
        for (size_t i = data.size() - pad_len; i < data.size(); ++i) {
            if (data[i] != pad_len) {
                throw std::invalid_argument("Invalid padding");
            }
        }
        
        return std::vector<uint8_t>(data.begin(), data.end() - pad_len);
    }
};

/**
 * @brief API Key vault for secure storage
 */
class APIKeyVault {
public:
    struct EncryptedKey {
        std::string key_id;
        std::string provider;           // e.g., "alpaca", "polygon"
        std::vector<uint8_t> encrypted_key;
        std::vector<uint8_t> encrypted_secret;
        AES256::IV key_iv;
        AES256::IV secret_iv;
        int64_t created_at{0};
        int64_t last_used{0};
        bool active{true};
    };
    
    /**
     * @brief Initialize vault with master key
     */
    explicit APIKeyVault(const std::string& master_password,
                         const std::string& salt = "MetisGenieVault") {
        master_key_ = AES256::derive_key(master_password, salt);
        cipher_ = std::make_unique<AES256>(master_key_);
    }
    
    /**
     * @brief Store API key
     */
    std::string store_key(const std::string& provider,
                          const std::string& api_key,
                          const std::string& api_secret = "") {
        std::string key_id = generate_key_id();
        
        EncryptedKey ek;
        ek.key_id = key_id;
        ek.provider = provider;
        ek.created_at = std::chrono::system_clock::now().time_since_epoch().count();
        
        // Encrypt API key
        ek.key_iv = AES256::generate_iv();
        std::vector<uint8_t> key_data(api_key.begin(), api_key.end());
        ek.encrypted_key = cipher_->encrypt_cbc(key_data, ek.key_iv);
        
        // Encrypt API secret if provided
        if (!api_secret.empty()) {
            ek.secret_iv = AES256::generate_iv();
            std::vector<uint8_t> secret_data(api_secret.begin(), api_secret.end());
            ek.encrypted_secret = cipher_->encrypt_cbc(secret_data, ek.secret_iv);
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        keys_[key_id] = ek;
        
        return key_id;
    }
    
    /**
     * @brief Retrieve API key
     */
    std::pair<std::string, std::string> get_key(const std::string& key_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = keys_.find(key_id);
        if (it == keys_.end() || !it->second.active) {
            throw std::runtime_error("Key not found or inactive");
        }
        
        // Update last used
        it->second.last_used = std::chrono::system_clock::now().time_since_epoch().count();
        
        // Decrypt key
        auto key_data = cipher_->decrypt_cbc(it->second.encrypted_key, it->second.key_iv);
        std::string api_key(key_data.begin(), key_data.end());
        
        // Decrypt secret if present
        std::string api_secret;
        if (!it->second.encrypted_secret.empty()) {
            auto secret_data = cipher_->decrypt_cbc(it->second.encrypted_secret, it->second.secret_iv);
            api_secret = std::string(secret_data.begin(), secret_data.end());
        }
        
        return {api_key, api_secret};
    }
    
    /**
     * @brief Revoke API key
     */
    bool revoke_key(const std::string& key_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keys_.find(key_id);
        if (it != keys_.end()) {
            it->second.active = false;
            return true;
        }
        return false;
    }
    
    /**
     * @brief List stored keys (metadata only)
     */
    std::vector<std::pair<std::string, std::string>> list_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::string, std::string>> result;
        for (const auto& [id, ek] : keys_) {
            if (ek.active) {
                result.emplace_back(id, ek.provider);
            }
        }
        return result;
    }
    
    /**
     * @brief Rotate master key
     */
    void rotate_master_key(const std::string& new_password,
                           const std::string& salt = "MetisGenieVault") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Decrypt all keys with old key
        std::map<std::string, std::pair<std::string, std::string>> decrypted;
        for (const auto& [id, ek] : keys_) {
            if (!ek.active) continue;
            
            auto key_data = cipher_->decrypt_cbc(ek.encrypted_key, ek.key_iv);
            std::string api_key(key_data.begin(), key_data.end());
            
            std::string api_secret;
            if (!ek.encrypted_secret.empty()) {
                auto secret_data = cipher_->decrypt_cbc(ek.encrypted_secret, ek.secret_iv);
                api_secret = std::string(secret_data.begin(), secret_data.end());
            }
            
            decrypted[id] = {api_key, api_secret};
        }
        
        // Create new cipher
        master_key_ = AES256::derive_key(new_password, salt);
        cipher_ = std::make_unique<AES256>(master_key_);
        
        // Re-encrypt all keys
        for (auto& [id, ek] : keys_) {
            if (!ek.active) continue;
            
            auto& [api_key, api_secret] = decrypted[id];
            
            ek.key_iv = AES256::generate_iv();
            std::vector<uint8_t> key_data(api_key.begin(), api_key.end());
            ek.encrypted_key = cipher_->encrypt_cbc(key_data, ek.key_iv);
            
            if (!api_secret.empty()) {
                ek.secret_iv = AES256::generate_iv();
                std::vector<uint8_t> secret_data(api_secret.begin(), api_secret.end());
                ek.encrypted_secret = cipher_->encrypt_cbc(secret_data, ek.secret_iv);
            }
        }
    }

private:
    AES256::Key master_key_;
    std::unique_ptr<AES256> cipher_;
    std::map<std::string, EncryptedKey> keys_;
    mutable std::mutex mutex_;
    
    static std::string generate_key_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 15);
        
        std::ostringstream ss;
        ss << std::hex;
        for (int i = 0; i < 16; ++i) {
            ss << dist(gen);
        }
        return ss.str();
    }
};

} // namespace security
} // namespace genie

#endif // GENIE_SECURITY_API_KEY_ENCRYPTION_HPP
