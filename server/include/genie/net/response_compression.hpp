/**
 * @file response_compression.hpp
 * @brief gzip/deflate response compression for REST API responses
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Compresses HTTP responses when the client sends:
 *   Accept-Encoding: gzip, deflate
 *
 * Implementation uses zlib (part of the C runtime on all three platforms):
 *   Windows: zlib1.dll (available via vcpkg or bundled)
 *   Linux:   libz (available on all distributions)
 *   macOS:   libz (included in Xcode Command Line Tools)
 *
 * Since zlib may not always be available as a zero-dep build, this header
 * implements a pure C++20 DEFLATE compressor for small payloads (< 64KB)
 * and falls back gracefully when zlib is absent.
 *
 * The pure C++20 compressor uses LZ77 + Huffman coding (RFC 1951 store-only
 * mode for simplicity -- achieves ~0% compression but valid deflate format).
 * When zlib IS linked, the full deflate/gzip path is used.
 *
 * config.pson:
 *   "compression": {
 *       "enabled": true,
 *       "min_size_bytes": 1024,
 *       "level": 6,
 *       "prefer_gzip": true
 *   }
 */
#pragma once
#ifndef GENIE_NET_RESPONSE_COMPRESSION_HPP
#define GENIE_NET_RESPONSE_COMPRESSION_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <algorithm>

// Attempt to include zlib -- graceful degradation if absent
#ifdef GENIE_USE_ZLIB
  #include <zlib.h>
  #define GENIE_ZLIB_AVAILABLE 1
#else
  #define GENIE_ZLIB_AVAILABLE 0
#endif

namespace genie::net {

/** Compression encoding negotiated from Accept-Encoding header */
enum class CompressionEncoding { NONE, DEFLATE, GZIP };

/** Compression configuration (from config.pson compression.*) */
struct CompressionConfig {
    bool enabled{true};
    size_t min_size_bytes{1024};   // Don't compress small responses
    int    level{6};               // 1-9 (zlib), 0 = store-only (C++20 path)
    bool   prefer_gzip{true};      // gzip over deflate when both accepted
};

/**
 * @brief Negotiate compression encoding from Accept-Encoding header.
 */
[[nodiscard]] inline CompressionEncoding negotiate_encoding(
        const std::map<std::string, std::string>& headers,
        bool prefer_gzip = true) {
    auto it = headers.find("Accept-Encoding");
    if (it == headers.end()) return CompressionEncoding::NONE;
    const auto& ae = it->second;
    bool has_gzip    = ae.find("gzip")    != std::string::npos;
    bool has_deflate = ae.find("deflate") != std::string::npos;
    if (prefer_gzip && has_gzip)    return CompressionEncoding::GZIP;
    if (!prefer_gzip && has_deflate) return CompressionEncoding::DEFLATE;
    if (has_gzip)    return CompressionEncoding::GZIP;
    if (has_deflate) return CompressionEncoding::DEFLATE;
    return CompressionEncoding::NONE;
}

// ============================================================================
// Pure C++20 DEFLATE store-only (RFC 1951 non-compressed blocks)
// No external dependency. Valid decompressible deflate output.
// ============================================================================

[[nodiscard]] inline std::vector<uint8_t> deflate_store(
        const uint8_t* data, size_t len) {
    // Non-compressed deflate: BFINAL=1, BTYPE=00, LEN, ~LEN, data
    std::vector<uint8_t> out;
    out.reserve(len + 8);
    size_t offset = 0;
    while (offset < len || offset == 0) {
        size_t block = std::min<size_t>(65535, len - offset);
        bool is_last = (offset + block >= len);
        out.push_back(is_last ? 0x01 : 0x00); // BFINAL | BTYPE=00
        auto blen = static_cast<uint16_t>(block);
        out.push_back(blen & 0xFF);
        out.push_back((blen >> 8) & 0xFF);
        out.push_back(~blen & 0xFF);
        out.push_back((~blen >> 8) & 0xFF);
        out.insert(out.end(), data + offset, data + offset + block);
        offset += block;
        if (len == 0) break;
    }
    return out;
}

/** Adler-32 checksum (zlib) */
[[nodiscard]] inline uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1)      % 65521;
    }
    return (s2 << 16) | s1;
}

/** CRC-32 for gzip (IEEE 802.3) */
[[nodiscard]] inline uint32_t crc32_gzip(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
    }
    return ~crc;
}

/**
 * @brief Compress a string using gzip (RFC 1952).
 *
 * When GENIE_ZLIB_AVAILABLE=1 uses zlib deflate; otherwise uses
 * the pure C++20 store-only deflate path (valid but uncompressed).
 */
[[nodiscard]] inline std::vector<uint8_t> compress_gzip(
        const std::string& input, [[maybe_unused]] int level = 6) {
    const auto* data = reinterpret_cast<const uint8_t*>(input.data());
    size_t len = input.size();

#if GENIE_ZLIB_AVAILABLE
    z_stream zs{};
    deflateInit2(&zs, level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    zs.next_in  = const_cast<Bytef*>(data);
    zs.avail_in = static_cast<uInt>(len);
    std::vector<uint8_t> out;
    out.resize(deflateBound(&zs, static_cast<uLong>(len)));
    zs.next_out  = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
#else
    // Pure C++20 gzip wrapper around store-only deflate
    // gzip header (10 bytes, minimal)
    std::vector<uint8_t> out;
    out.reserve(len + 22);
    out.insert(out.end(), {
        0x1F, 0x8B,  // magic
        0x08,        // CM = DEFLATE
        0x00,        // FLG = none
        0,0,0,0,     // MTIME = 0
        0x00,        // XFL = 0
        0xFF         // OS = unknown
    });
    auto body = deflate_store(data, len);
    out.insert(out.end(), body.begin(), body.end());
    uint32_t crc = crc32_gzip(data, len);
    out.push_back(crc & 0xFF); out.push_back((crc>>8)&0xFF);
    out.push_back((crc>>16)&0xFF); out.push_back((crc>>24)&0xFF);
    uint32_t isize = static_cast<uint32_t>(len);
    out.push_back(isize & 0xFF); out.push_back((isize>>8)&0xFF);
    out.push_back((isize>>16)&0xFF); out.push_back((isize>>24)&0xFF);
    return out;
#endif
}

/**
 * @brief Apply compression to an HTTP response body if appropriate.
 *
 * Modifies the body in-place and sets Content-Encoding + Content-Length.
 * Returns the encoding applied (NONE if not compressed).
 */
inline CompressionEncoding maybe_compress(
        std::string& body,
        std::map<std::string, std::string>& headers,
        const std::map<std::string, std::string>& req_headers,
        const CompressionConfig& cfg) {
    if (!cfg.enabled || body.size() < cfg.min_size_bytes)
        return CompressionEncoding::NONE;

    auto enc = negotiate_encoding(req_headers, cfg.prefer_gzip);
    if (enc == CompressionEncoding::NONE) return CompressionEncoding::NONE;

    // Only compress text-like content
    auto ct_it = headers.find("Content-Type");
    if (ct_it != headers.end()) {
        const auto& ct = ct_it->second;
        bool compressible = ct.find("json") != std::string::npos
                         || ct.find("text") != std::string::npos
                         || ct.find("javascript") != std::string::npos
                         || ct.find("xml") != std::string::npos;
        if (!compressible) return CompressionEncoding::NONE;
    }

    auto compressed = compress_gzip(body, cfg.level);
    body = std::string(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    headers["Content-Encoding"] = (enc == CompressionEncoding::GZIP) ? "gzip" : "deflate";
    headers["Content-Length"]   = std::to_string(body.size());
    headers["Vary"]             = "Accept-Encoding";
    return enc;
}

} // namespace genie::net

#endif // GENIE_NET_RESPONSE_COMPRESSION_HPP
