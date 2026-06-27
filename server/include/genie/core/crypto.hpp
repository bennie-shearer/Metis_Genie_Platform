/**
 * @file crypto.hpp
 * @brief Cryptographic utilities - SHA-256 hashing with salt
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * Header-only SHA-256 implementation for password hashing.
 * No external dependencies.
 */

#ifndef GENIE_CORE_CRYPTO_HPP
#define GENIE_CORE_CRYPTO_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace genie::crypto {

// =========================================================================
// SHA-256 Implementation
// =========================================================================

namespace detail {

// SHA-256 constants
constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

} // namespace detail

/**
 * @brief Compute SHA-256 hash of input data
 * @param data Input bytes
 * @param len Length of input
 * @return 64-character hex string
 */
inline std::string sha256(const uint8_t* data, size_t len) {
    // Initial hash values
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Pre-processing: adding padding bits
    size_t ml = len * 8;  // message length in bits
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    
    std::vector<uint8_t> padded(padded_len, 0);
    std::memcpy(padded.data(), data, len);
    padded[len] = 0x80;  // append bit '1' to message
    
    // Append original length in bits as 64-bit big-endian
    for (int i = 0; i < 8; ++i) {
        padded[padded_len - 1 - i] = static_cast<uint8_t>(ml >> (i * 8));
    }

    // Process each 64-byte chunk
    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[64];
        
        // Break chunk into sixteen 32-bit big-endian words
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(padded[chunk + i*4]) << 24) |
                   (static_cast<uint32_t>(padded[chunk + i*4 + 1]) << 16) |
                   (static_cast<uint32_t>(padded[chunk + i*4 + 2]) << 8) |
                   (static_cast<uint32_t>(padded[chunk + i*4 + 3]));
        }
        
        // Extend to 64 words
        for (int i = 16; i < 64; ++i) {
            w[i] = detail::gamma1(w[i-2]) + w[i-7] + 
                   detail::gamma0(w[i-15]) + w[i-16];
        }

        // Initialize working variables
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        // Compression function
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + detail::sigma1(e) + detail::ch(e, f, g) + 
                         detail::K[i] + w[i];
            uint32_t t2 = detail::sigma0(a) + detail::maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        // Add to hash
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    // Produce final hash value (big-endian)
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        result << std::setw(8) << h[i];
    }
    return result.str();
}

/**
 * @brief Compute SHA-256 hash of a string
 */
inline std::string sha256(const std::string& input) {
    return sha256(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

// =========================================================================
// Salt Generation
// =========================================================================

/**
 * @brief Generate a random salt string
 * @param length Length of salt (default 32)
 * @return Random hex string
 */
inline std::string generate_salt(size_t length = 32) {
    static const char hex_chars[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, 15);
    
    std::string salt;
    salt.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        salt += hex_chars[dist(gen)];
    }
    return salt;
}

// =========================================================================
// Password Hashing
// =========================================================================

/**
 * @brief Hash a password with salt
 * @param password Plain text password
 * @param salt Salt string (generate with generate_salt())
 * @return Hashed password (64 hex chars)
 */
inline std::string hash_password(const std::string& password, const std::string& salt) {
    // Double hash: sha256(salt + sha256(password + salt))
    std::string inner = sha256(password + salt);
    return sha256(salt + inner);
}

/**
 * @brief Verify a password against stored hash
 * @param password Plain text password to verify
 * @param salt Salt used when hashing
 * @param hash Stored hash to compare against
 * @return true if password matches
 */
inline bool verify_password(const std::string& password, 
                           const std::string& salt, 
                           const std::string& hash) {
    // Constant-time comparison to prevent timing attacks
    std::string computed = hash_password(password, salt);
    if (computed.size() != hash.size()) return false;
    
    volatile int result = 0;
    for (size_t i = 0; i < computed.size(); ++i) {
        result |= computed[i] ^ hash[i];
    }
    return result == 0;
}

/**
 * @brief Create a password hash with new salt
 * @param password Plain text password
 * @return Pair of (salt, hash)
 */
inline std::pair<std::string, std::string> create_password_hash(const std::string& password) {
    std::string salt = generate_salt();
    std::string hash = hash_password(password, salt);
    return {salt, hash};
}

} // namespace genie::crypto

#endif // GENIE_CORE_CRYPTO_HPP
