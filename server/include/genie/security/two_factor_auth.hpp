/**
 * @file two_factor_auth.hpp
 * @brief Two-factor authentication and session management
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 * 
 * TIER 5: Security - Two-factor authentication and session timeout handling
 */

#ifndef GENIE_SECURITY_TWO_FACTOR_AUTH_HPP
#define GENIE_SECURITY_TWO_FACTOR_AUTH_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <chrono>
#include <mutex>
#include <memory>
#include <random>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

namespace genie {
namespace security {

/**
 * @brief TOTP (Time-based One-Time Password) generator
 */
class TOTP {
public:
    static constexpr int DEFAULT_DIGITS = 6;
    static constexpr int DEFAULT_PERIOD = 30;
    
    /**
     * @brief Generate TOTP code
     */
    static std::string generate(const std::string& secret, int digits = DEFAULT_DIGITS,
                                 int period = DEFAULT_PERIOD) {
        int64_t counter = std::time(nullptr) / period;
        return hotp(secret, counter, digits);
    }
    
    /**
     * @brief Verify TOTP code with time window
     */
    static bool verify(const std::string& secret, const std::string& code,
                       int digits = DEFAULT_DIGITS, int period = DEFAULT_PERIOD,
                       int window = 1) {
        int64_t current_counter = std::time(nullptr) / period;
        
        for (int i = -window; i <= window; ++i) {
            std::string expected = hotp(secret, current_counter + i, digits);
            if (constant_time_compare(expected, code)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Generate random secret (Base32 encoded)
     */
    static std::string generate_secret(int length = 16) {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 31);
        
        std::string secret;
        secret.reserve(length);
        for (int i = 0; i < length; ++i) {
            secret += alphabet[dist(gen)];
        }
        return secret;
    }
    
    /**
     * @brief Generate provisioning URI for authenticator apps
     */
    static std::string get_provisioning_uri(const std::string& secret,
                                             const std::string& account,
                                             const std::string& issuer = "MetisGenie") {
        std::ostringstream uri;
        uri << "otpauth://totp/" << url_encode(issuer) << ":" << url_encode(account);
        uri << "?secret=" << secret;
        uri << "&issuer=" << url_encode(issuer);
        uri << "&algorithm=SHA1";
        uri << "&digits=" << DEFAULT_DIGITS;
        uri << "&period=" << DEFAULT_PERIOD;
        return uri.str();
    }

private:
    /**
     * @brief HOTP (HMAC-based One-Time Password)
     */
    static std::string hotp(const std::string& secret, int64_t counter, int digits) {
        // Decode Base32 secret
        std::vector<uint8_t> key = base32_decode(secret);
        
        // Convert counter to big-endian bytes
        uint8_t counter_bytes[8];
        for (int i = 7; i >= 0; --i) {
            counter_bytes[i] = static_cast<uint8_t>(counter & 0xFF);
            counter >>= 8;
        }
        
        // HMAC-SHA1
        std::vector<uint8_t> hash = hmac_sha1(key, 
            std::vector<uint8_t>(counter_bytes, counter_bytes + 8));
        
        // Dynamic truncation
        int offset = hash[19] & 0x0F;
        int32_t code = ((hash[offset] & 0x7F) << 24) |
                       ((hash[offset + 1] & 0xFF) << 16) |
                       ((hash[offset + 2] & 0xFF) << 8) |
                       (hash[offset + 3] & 0xFF);
        
        // Reduce to digits
        int mod = 1;
        for (int i = 0; i < digits; ++i) mod *= 10;
        code %= mod;
        
        // Zero-pad
        std::ostringstream ss;
        ss << std::setw(digits) << std::setfill('0') << code;
        return ss.str();
    }
    
    /**
     * @brief Constant-time string comparison
     */
    static bool constant_time_compare(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        
        int result = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        }
        return result == 0;
    }
    
    /**
     * @brief Base32 decode
     */
    static std::vector<uint8_t> base32_decode(const std::string& input) {
        static const int8_t decode_map[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        };
        
        std::vector<uint8_t> output;
        int buffer = 0;
        int bits_left = 0;
        
        for (char c : input) {
            if (c == '=' || c == ' ') continue;
            int8_t val = decode_map[static_cast<unsigned char>(c)];
            if (val < 0) continue;
            
            buffer = (buffer << 5) | val;
            bits_left += 5;
            
            if (bits_left >= 8) {
                bits_left -= 8;
                output.push_back(static_cast<uint8_t>((buffer >> bits_left) & 0xFF));
            }
        }
        
        return output;
    }
    
    /**
     * @brief Simple HMAC-SHA1 implementation
     */
    static std::vector<uint8_t> hmac_sha1(const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& message) {
        const int block_size = 64;
        std::vector<uint8_t> k = key;
        
        // If key is longer than block size, hash it
        if (k.size() > block_size) {
            k = sha1(k);
        }
        
        // Pad key to block size
        k.resize(block_size, 0);
        
        // Create inner and outer pads
        std::vector<uint8_t> o_key_pad(block_size);
        std::vector<uint8_t> i_key_pad(block_size);
        for (int i = 0; i < block_size; ++i) {
            o_key_pad[i] = k[i] ^ 0x5C;
            i_key_pad[i] = k[i] ^ 0x36;
        }
        
        // Inner hash
        std::vector<uint8_t> inner_data = i_key_pad;
        inner_data.insert(inner_data.end(), message.begin(), message.end());
        std::vector<uint8_t> inner_hash = sha1(inner_data);
        
        // Outer hash
        std::vector<uint8_t> outer_data = o_key_pad;
        outer_data.insert(outer_data.end(), inner_hash.begin(), inner_hash.end());
        return sha1(outer_data);
    }
    
    /**
     * @brief Simple SHA1 implementation
     */
    static std::vector<uint8_t> sha1(const std::vector<uint8_t>& message) {
        uint32_t h0 = 0x67452301;
        uint32_t h1 = 0xEFCDAB89;
        uint32_t h2 = 0x98BADCFE;
        uint32_t h3 = 0x10325476;
        uint32_t h4 = 0xC3D2E1F0;
        
        // Pre-processing
        std::vector<uint8_t> msg = message;
        uint64_t original_len = msg.size() * 8;
        
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) {
            msg.push_back(0x00);
        }
        
        // Append length in big-endian
        for (int i = 7; i >= 0; --i) {
            msg.push_back(static_cast<uint8_t>((original_len >> (i * 8)) & 0xFF));
        }
        
        // Process each 512-bit chunk
        for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
            uint32_t w[80];
            
            for (int i = 0; i < 16; ++i) {
                w[i] = (static_cast<uint32_t>(msg[chunk + i*4]) << 24) |
                       (static_cast<uint32_t>(msg[chunk + i*4 + 1]) << 16) |
                       (static_cast<uint32_t>(msg[chunk + i*4 + 2]) << 8) |
                       (static_cast<uint32_t>(msg[chunk + i*4 + 3]));
            }
            
            for (int i = 16; i < 80; ++i) {
                uint32_t val = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
                w[i] = (val << 1) | (val >> 31);
            }
            
            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            
            for (int i = 0; i < 80; ++i) {
                uint32_t f, k;
                if (i < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                
                uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                e = d;
                d = c;
                c = (b << 30) | (b >> 2);
                b = a;
                a = temp;
            }
            
            h0 += a;
            h1 += b;
            h2 += c;
            h3 += d;
            h4 += e;
        }
        
        std::vector<uint8_t> hash;
        for (uint32_t h : {h0, h1, h2, h3, h4}) {
            hash.push_back(static_cast<uint8_t>((h >> 24) & 0xFF));
            hash.push_back(static_cast<uint8_t>((h >> 16) & 0xFF));
            hash.push_back(static_cast<uint8_t>((h >> 8) & 0xFF));
            hash.push_back(static_cast<uint8_t>(h & 0xFF));
        }
        return hash;
    }
    
    static std::string url_encode(const std::string& s) {
        std::ostringstream encoded;
        encoded << std::hex << std::uppercase;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded << c;
            } else {
                encoded << '%' << std::setw(2) << std::setfill('0') 
                        << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return encoded.str();
    }
};

/**
 * @brief Session data
 */
struct Session {
    std::string session_id;
    std::string user_id;
    std::string ip_address;
    std::string user_agent;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_activity;
    std::chrono::system_clock::time_point expires_at;
    bool is_2fa_verified{false};
    std::map<std::string, std::string> data;
    
    bool is_expired() const {
        return std::chrono::system_clock::now() > expires_at;
    }
    
    bool is_idle_timeout(std::chrono::minutes timeout) const {
        return std::chrono::system_clock::now() - last_activity > timeout;
    }
};

/**
 * @brief 2FA user data
 */
struct TwoFactorData {
    std::string user_id;
    std::string secret;
    bool enabled{false};
    std::vector<std::string> backup_codes;
    std::chrono::system_clock::time_point enabled_at;
    std::set<std::string> used_backup_codes;
};

/**
 * @brief Session manager with 2FA support
 */
class SessionManager {
public:
    struct Config {
        std::chrono::minutes session_timeout{60};      // Absolute timeout
        std::chrono::minutes idle_timeout{15};         // Idle timeout
        bool require_2fa{false};
        int max_sessions_per_user{5};
        bool enforce_single_session{false};
    };
    
    explicit SessionManager(const Config& config) : config_(config) {}
    
    /**
     * @brief Create new session
     */
    Session create_session(const std::string& user_id,
                           const std::string& ip_address = "",
                           const std::string& user_agent = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Enforce session limits
        auto& user_sessions = user_session_map_[user_id];
        if (config_.enforce_single_session) {
            // Invalidate all existing sessions
            for (const auto& sid : user_sessions) {
                sessions_.erase(sid);
            }
            user_sessions.clear();
        } else if (static_cast<int>(user_sessions.size()) >= config_.max_sessions_per_user) {
            // Remove oldest session
            if (!user_sessions.empty()) {
                auto oldest_it = sessions_.find(*user_sessions.begin());
                if (oldest_it != sessions_.end()) {
                    sessions_.erase(oldest_it);
                }
                user_sessions.erase(user_sessions.begin());
            }
        }
        
        Session session;
        session.session_id = generate_session_id();
        session.user_id = user_id;
        session.ip_address = ip_address;
        session.user_agent = user_agent;
        session.created_at = std::chrono::system_clock::now();
        session.last_activity = session.created_at;
        session.expires_at = session.created_at + config_.session_timeout;
        session.is_2fa_verified = !is_2fa_enabled(user_id);
        
        sessions_[session.session_id] = session;
        user_sessions.insert(session.session_id);
        
        return session;
    }
    
    /**
     * @brief Validate session
     */
    std::optional<Session> validate_session(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return std::nullopt;
        }
        
        auto& session = it->second;
        
        // Check expiry
        if (session.is_expired()) {
            invalidate_session_internal(session_id);
            return std::nullopt;
        }
        
        // Check idle timeout
        if (session.is_idle_timeout(config_.idle_timeout)) {
            invalidate_session_internal(session_id);
            return std::nullopt;
        }
        
        // Update activity
        session.last_activity = std::chrono::system_clock::now();
        
        return session;
    }
    
    /**
     * @brief Invalidate session
     */
    bool invalidate_session(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return invalidate_session_internal(session_id);
    }
    
    /**
     * @brief Invalidate all sessions for user
     */
    void invalidate_user_sessions(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = user_session_map_.find(user_id);
        if (it != user_session_map_.end()) {
            for (const auto& session_id : it->second) {
                sessions_.erase(session_id);
            }
            it->second.clear();
        }
    }
    
    /**
     * @brief Setup 2FA for user
     */
    std::pair<std::string, std::string> setup_2fa(const std::string& user_id,
                                                   const std::string& account_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        TwoFactorData data;
        data.user_id = user_id;
        data.secret = TOTP::generate_secret();
        data.enabled = false;
        
        // Generate backup codes
        for (int i = 0; i < 10; ++i) {
            data.backup_codes.push_back(generate_backup_code());
        }
        
        two_factor_data_[user_id] = data;
        
        std::string uri = TOTP::get_provisioning_uri(data.secret, account_name);
        return {data.secret, uri};
    }
    
    /**
     * @brief Verify and enable 2FA
     */
    bool enable_2fa(const std::string& user_id, const std::string& code) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = two_factor_data_.find(user_id);
        if (it == two_factor_data_.end()) {
            return false;
        }
        
        if (TOTP::verify(it->second.secret, code)) {
            it->second.enabled = true;
            it->second.enabled_at = std::chrono::system_clock::now();
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Disable 2FA
     */
    bool disable_2fa(const std::string& user_id, const std::string& code) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = two_factor_data_.find(user_id);
        if (it == two_factor_data_.end() || !it->second.enabled) {
            return false;
        }
        
        if (TOTP::verify(it->second.secret, code)) {
            two_factor_data_.erase(it);
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Verify 2FA code for session
     */
    bool verify_2fa(const std::string& session_id, const std::string& code) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto session_it = sessions_.find(session_id);
        if (session_it == sessions_.end()) {
            return false;
        }
        
        auto& session = session_it->second;
        auto data_it = two_factor_data_.find(session.user_id);
        
        if (data_it == two_factor_data_.end() || !data_it->second.enabled) {
            session.is_2fa_verified = true;
            return true;
        }
        
        // Check TOTP
        if (TOTP::verify(data_it->second.secret, code)) {
            session.is_2fa_verified = true;
            return true;
        }
        
        // Check backup codes
        auto& backup_codes = data_it->second.backup_codes;
        auto& used_codes = data_it->second.used_backup_codes;
        
        auto code_it = std::find(backup_codes.begin(), backup_codes.end(), code);
        if (code_it != backup_codes.end() && used_codes.find(code) == used_codes.end()) {
            used_codes.insert(code);
            session.is_2fa_verified = true;
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Check if 2FA is enabled for user
     */
    bool is_2fa_enabled(const std::string& user_id) const {
        auto it = two_factor_data_.find(user_id);
        return it != two_factor_data_.end() && it->second.enabled;
    }
    
    /**
     * @brief Get remaining backup codes
     */
    std::vector<std::string> get_backup_codes(const std::string& user_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = two_factor_data_.find(user_id);
        if (it == two_factor_data_.end()) {
            return {};
        }
        
        std::vector<std::string> remaining;
        for (const auto& code : it->second.backup_codes) {
            if (it->second.used_backup_codes.find(code) == it->second.used_backup_codes.end()) {
                remaining.push_back(code);
            }
        }
        return remaining;
    }
    
    /**
     * @brief Regenerate backup codes
     */
    std::vector<std::string> regenerate_backup_codes(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = two_factor_data_.find(user_id);
        if (it == two_factor_data_.end()) {
            return {};
        }
        
        it->second.backup_codes.clear();
        it->second.used_backup_codes.clear();
        
        for (int i = 0; i < 10; ++i) {
            it->second.backup_codes.push_back(generate_backup_code());
        }
        
        return it->second.backup_codes;
    }
    
    /**
     * @brief Cleanup expired sessions
     */
    int cleanup_expired() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::string> to_remove;
        for (const auto& [id, session] : sessions_) {
            if (session.is_expired() || session.is_idle_timeout(config_.idle_timeout)) {
                to_remove.push_back(id);
            }
        }
        
        for (const auto& id : to_remove) {
            invalidate_session_internal(id);
        }
        
        return static_cast<int>(to_remove.size());
    }
    
    /**
     * @brief Get active session count
     */
    size_t get_session_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

private:
    Config config_;
    std::map<std::string, Session> sessions_;
    std::map<std::string, std::set<std::string>> user_session_map_;
    std::map<std::string, TwoFactorData> two_factor_data_;
    mutable std::mutex mutex_;
    
    bool invalidate_session_internal(const std::string& session_id) {
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return false;
        }
        
        // Remove from user map
        auto user_it = user_session_map_.find(it->second.user_id);
        if (user_it != user_session_map_.end()) {
            user_it->second.erase(session_id);
        }
        
        sessions_.erase(it);
        return true;
    }
    
    static std::string generate_session_id() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(16) << dist(gen);
        ss << std::setw(16) << dist(gen);
        return ss.str();
    }
    
    static std::string generate_backup_code() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 35);
        
        static const char* chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string code;
        for (int i = 0; i < 8; ++i) {
            if (i == 4) code += '-';
            code += chars[dist(gen)];
        }
        return code;
    }
};

} // namespace security
} // namespace genie

#endif // GENIE_SECURITY_TWO_FACTOR_AUTH_HPP
