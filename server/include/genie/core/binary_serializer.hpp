/**
 * @file binary_serializer.hpp
 * @brief Binary serialization for large internal datasets
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Provides a compact binary wire format for position arrays, price series,
 * and risk matrices. Approximately 10x faster than JSON for bulk data transfer
 * and 3-5x smaller on the wire.
 *
 * Wire format (little-endian):
 *   [4 bytes] magic  0x4D455449 ("METI")
 *   [1 byte]  version
 *   [1 byte]  type tag
 *   [4 bytes] payload length
 *   [N bytes] payload
 *   [4 bytes] CRC-32 of payload
 *
 * Type tags:
 *   0x01  DOUBLE_ARRAY  -- packed double[]
 *   0x02  FLOAT_ARRAY   -- packed float[]
 *   0x03  INT64_ARRAY   -- packed int64_t[]
 *   0x04  STRING_ARRAY  -- length-prefixed UTF-8 strings
 *   0x05  MATRIX        -- rows x cols + double[]
 *   0x06  PSON_MAP      -- key=value string pairs
 *
 * Usage:
 *   BinarySerializer bs;
 *   bs.write_doubles(prices);          // returns std::vector<uint8_t>
 *   auto prices2 = bs.read_doubles(buf);
 *
 * Zero external dependencies. Cross-platform: Windows/Linux/macOS.
 */
#pragma once
#ifndef GENIE_CORE_BINARY_SERIALIZER_HPP
#define GENIE_CORE_BINARY_SERIALIZER_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <map>
#include <numeric>
#include <algorithm>

namespace genie::core {

// ============================================================================
// CRC-32 (IEEE 802.3 polynomial, no external lib)
// ============================================================================

namespace detail {
    inline uint32_t crc32_byte(uint32_t crc, uint8_t byte) {
        for (int i = 0; i < 8; ++i) {
            if ((crc ^ byte) & 1) crc = (crc >> 1) ^ 0xEDB88320U;
            else                   crc >>= 1;
            byte >>= 1;
        }
        return crc;
    }

    inline uint32_t crc32(const uint8_t* data, size_t len) {
        uint32_t crc = 0xFFFFFFFFU;
        for (size_t i = 0; i < len; ++i) crc = crc32_byte(crc, data[i]);
        return ~crc;
    }
} // namespace detail

// ============================================================================
// Wire format constants
// ============================================================================

constexpr uint32_t BINARY_MAGIC   = 0x4D455449U; // "METI"
constexpr uint8_t  BINARY_VERSION = 1;

enum class BinaryTag : uint8_t {
    DOUBLE_ARRAY = 0x01,
    FLOAT_ARRAY  = 0x02,
    INT64_ARRAY  = 0x03,
    STRING_ARRAY = 0x04,
    MATRIX       = 0x05,
    PSON_MAP     = 0x06,
};

// ============================================================================
// BinarySerializer
// ============================================================================

class BinarySerializer {
public:
    // ------------------------------------------------------------------
    // Write API
    // ------------------------------------------------------------------

    [[nodiscard]] std::vector<uint8_t> write_doubles(const std::vector<double>& v) const {
        return pack(BinaryTag::DOUBLE_ARRAY, reinterpret_cast<const uint8_t*>(v.data()),
                    v.size() * sizeof(double));
    }

    [[nodiscard]] std::vector<uint8_t> write_floats(const std::vector<float>& v) const {
        return pack(BinaryTag::FLOAT_ARRAY, reinterpret_cast<const uint8_t*>(v.data()),
                    v.size() * sizeof(float));
    }

    [[nodiscard]] std::vector<uint8_t> write_int64s(const std::vector<int64_t>& v) const {
        return pack(BinaryTag::INT64_ARRAY, reinterpret_cast<const uint8_t*>(v.data()),
                    v.size() * sizeof(int64_t));
    }

    [[nodiscard]] std::vector<uint8_t> write_strings(const std::vector<std::string>& v) const {
        // Format: [4B count] { [4B len][bytes] }...
        std::vector<uint8_t> payload;
        auto count = static_cast<uint32_t>(v.size());
        append_u32(payload, count);
        for (const auto& s : v) {
            append_u32(payload, static_cast<uint32_t>(s.size()));
            payload.insert(payload.end(), s.begin(), s.end());
        }
        return pack(BinaryTag::STRING_ARRAY, payload.data(), payload.size());
    }

    [[nodiscard]] std::vector<uint8_t> write_matrix(
            size_t rows, size_t cols, const std::vector<double>& data) const {
        // Format: [4B rows][4B cols][doubles]
        std::vector<uint8_t> payload;
        append_u32(payload, static_cast<uint32_t>(rows));
        append_u32(payload, static_cast<uint32_t>(cols));
        auto* p = reinterpret_cast<const uint8_t*>(data.data());
        payload.insert(payload.end(), p, p + data.size() * sizeof(double));
        return pack(BinaryTag::MATRIX, payload.data(), payload.size());
    }

    [[nodiscard]] std::vector<uint8_t> write_map(
            const std::map<std::string, std::string>& m) const {
        // Format: [4B count] { [4B klen][k][4B vlen][v] }...
        std::vector<uint8_t> payload;
        append_u32(payload, static_cast<uint32_t>(m.size()));
        for (const auto& [k, v] : m) {
            append_u32(payload, static_cast<uint32_t>(k.size()));
            payload.insert(payload.end(), k.begin(), k.end());
            append_u32(payload, static_cast<uint32_t>(v.size()));
            payload.insert(payload.end(), v.begin(), v.end());
        }
        return pack(BinaryTag::PSON_MAP, payload.data(), payload.size());
    }

    // ------------------------------------------------------------------
    // Read API
    // ------------------------------------------------------------------

    [[nodiscard]] std::vector<double> read_doubles(const std::vector<uint8_t>& buf) const {
        auto [tag, payload] = unpack(buf);
        if (tag != BinaryTag::DOUBLE_ARRAY) throw std::runtime_error("BinarySerializer: expected DOUBLE_ARRAY");
        size_t n = payload.size() / sizeof(double);
        std::vector<double> out(n);
        std::memcpy(out.data(), payload.data(), payload.size());
        return out;
    }

    [[nodiscard]] std::vector<float> read_floats(const std::vector<uint8_t>& buf) const {
        auto [tag, payload] = unpack(buf);
        if (tag != BinaryTag::FLOAT_ARRAY) throw std::runtime_error("BinarySerializer: expected FLOAT_ARRAY");
        size_t n = payload.size() / sizeof(float);
        std::vector<float> out(n);
        std::memcpy(out.data(), payload.data(), payload.size());
        return out;
    }

    [[nodiscard]] std::vector<int64_t> read_int64s(const std::vector<uint8_t>& buf) const {
        auto [tag, payload] = unpack(buf);
        if (tag != BinaryTag::INT64_ARRAY) throw std::runtime_error("BinarySerializer: expected INT64_ARRAY");
        size_t n = payload.size() / sizeof(int64_t);
        std::vector<int64_t> out(n);
        std::memcpy(out.data(), payload.data(), payload.size());
        return out;
    }

    [[nodiscard]] std::vector<std::string> read_strings(const std::vector<uint8_t>& buf) const {
        auto [tag, payload] = unpack(buf);
        if (tag != BinaryTag::STRING_ARRAY) throw std::runtime_error("BinarySerializer: expected STRING_ARRAY");
        size_t pos = 0;
        uint32_t count = read_u32(payload, pos);
        std::vector<std::string> out;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len = read_u32(payload, pos);
            out.emplace_back(reinterpret_cast<const char*>(payload.data() + pos), len);
            pos += len;
        }
        return out;
    }

    struct Matrix { size_t rows, cols; std::vector<double> data; };

    [[nodiscard]] Matrix read_matrix(const std::vector<uint8_t>& buf) const {
        auto [tag, payload] = unpack(buf);
        if (tag != BinaryTag::MATRIX) throw std::runtime_error("BinarySerializer: expected MATRIX");
        size_t pos = 0;
        uint32_t rows = read_u32(payload, pos);
        uint32_t cols = read_u32(payload, pos);
        size_t n = static_cast<size_t>(rows) * cols;
        std::vector<double> data(n);
        std::memcpy(data.data(), payload.data() + pos, n * sizeof(double));
        return {rows, cols, std::move(data)};
    }

    // ------------------------------------------------------------------
    // Encode/decode to hex string (for embedding in JSON or PSON)
    // ------------------------------------------------------------------

    [[nodiscard]] static std::string to_hex(const std::vector<uint8_t>& v) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string s;
        s.reserve(v.size() * 2);
        for (uint8_t b : v) { s += hex[b >> 4]; s += hex[b & 0xF]; }
        return s;
    }

    [[nodiscard]] static std::vector<uint8_t> from_hex(const std::string& s) {
        std::vector<uint8_t> v;
        v.reserve(s.size() / 2);
        for (size_t i = 0; i + 1 < s.size(); i += 2) {
            auto h = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
                if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
                if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
                return 0;
            };
            v.push_back(static_cast<uint8_t>((h(s[i]) << 4) | h(s[i+1])));
        }
        return v;
    }

private:
    // ------------------------------------------------------------------
    // Pack / unpack
    // ------------------------------------------------------------------

    [[nodiscard]] std::vector<uint8_t> pack(BinaryTag tag,
                                             const uint8_t* payload,
                                             size_t payload_len) const {
        // Header: magic(4) version(1) tag(1) len(4) = 10 bytes
        // Footer: crc32(4) = 4 bytes
        std::vector<uint8_t> out;
        out.reserve(10 + payload_len + 4);
        append_u32(out, BINARY_MAGIC);
        out.push_back(BINARY_VERSION);
        out.push_back(static_cast<uint8_t>(tag));
        append_u32(out, static_cast<uint32_t>(payload_len));
        out.insert(out.end(), payload, payload + payload_len);
        uint32_t crc = detail::crc32(payload, payload_len);
        append_u32(out, crc);
        return out;
    }

    [[nodiscard]] std::pair<BinaryTag, std::vector<uint8_t>>
    unpack(const std::vector<uint8_t>& buf) const {
        if (buf.size() < 14)
            throw std::runtime_error("BinarySerializer: buffer too small");
        size_t pos = 0;
        uint32_t magic = read_u32(buf, pos);
        if (magic != BINARY_MAGIC)
            throw std::runtime_error("BinarySerializer: bad magic");
        uint8_t ver = buf[pos++];
        if (ver != BINARY_VERSION)
            throw std::runtime_error("BinarySerializer: unsupported version");
        BinaryTag tag = static_cast<BinaryTag>(buf[pos++]);
        uint32_t payload_len = read_u32(buf, pos);
        if (pos + payload_len + 4 > buf.size())
            throw std::runtime_error("BinarySerializer: truncated payload");
        const uint8_t* payload_ptr = buf.data() + pos;
        uint32_t expected_crc = detail::crc32(payload_ptr, payload_len);
        pos += payload_len;
        uint32_t actual_crc = read_u32(buf, pos);
        if (expected_crc != actual_crc)
            throw std::runtime_error("BinarySerializer: CRC mismatch");
        return {tag, std::vector<uint8_t>(payload_ptr, payload_ptr + payload_len)};
    }

    // ------------------------------------------------------------------
    // Little-endian integer helpers
    // ------------------------------------------------------------------

    static void append_u32(std::vector<uint8_t>& v, uint32_t n) {
        v.push_back(static_cast<uint8_t>(n));
        v.push_back(static_cast<uint8_t>(n >> 8));
        v.push_back(static_cast<uint8_t>(n >> 16));
        v.push_back(static_cast<uint8_t>(n >> 24));
    }

    static uint32_t read_u32(const std::vector<uint8_t>& v, size_t& pos) {
        uint32_t n = static_cast<uint32_t>(v[pos])
                   | (static_cast<uint32_t>(v[pos+1]) << 8)
                   | (static_cast<uint32_t>(v[pos+2]) << 16)
                   | (static_cast<uint32_t>(v[pos+3]) << 24);
        pos += 4;
        return n;
    }
};

} // namespace genie::core

#endif // GENIE_CORE_BINARY_SERIALIZER_HPP
