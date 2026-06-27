/**
 * @file http2_server.hpp
 * @brief HTTP/2 with multiplexing -- future C++20 native implementation
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Planned implementation: HTTP/2 over TLS (h2) and cleartext (h2c).
 * Uses the existing HttpServer socket infrastructure extended with:
 *   - HPACK header compression (RFC 7541)
 *   - Stream multiplexing (RFC 9113 Section 5)
 *   - Flow control (WINDOW_UPDATE frames)
 *   - Server push (PUSH_PROMISE frames) for SSE replacement
 *   - Priority and dependency trees (RFC 7540 Section 5.3)
 *
 * No external library: implements the HTTP/2 frame parser directly.
 * TLS via OpenSSL stubs in stub/openssl/ (same pattern as existing code).
 *
 * Platform targets (C++20, no Docker):
 *   Windows: Schannel for TLS, existing Winsock2 for sockets
 *   Linux:   BoringSSL stub or OpenSSL if available, POSIX sockets
 *   macOS:   SecureTransport / Network.framework, BSD sockets
 *
 * Status: stub -- returns HTTP/1.1 upgrade rejection (426 Upgrade Required)
 *         with Upgrade: h2c header indicating H2 capability.
 *
 * config.pson:
 *   "http2": { "enabled": false, "h2c_upgrade": true, "push_enabled": false }
 */
#pragma once
#ifndef GENIE_NET_HTTP2_SERVER_HPP
#define GENIE_NET_HTTP2_SERVER_HPP

#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace genie::net::http2 {

/** HTTP/2 frame types (RFC 9113 Section 6) */
enum class FrameType : uint8_t {
    DATA          = 0x00,
    HEADERS       = 0x01,
    PRIORITY      = 0x02,
    RST_STREAM    = 0x03,
    SETTINGS      = 0x04,
    PUSH_PROMISE  = 0x05,
    PING          = 0x06,
    GOAWAY        = 0x07,
    WINDOW_UPDATE = 0x08,
    CONTINUATION  = 0x09,
};

/** HTTP/2 error codes (RFC 9113 Section 7) */
#ifdef NO_ERROR
#  undef NO_ERROR  // undefine Windows <winerror.h> macro that clashes with enum
#endif
enum class ErrorCode : uint32_t {
    NO_ERROR            = 0x00,
    PROTOCOL_ERROR      = 0x01,
    INTERNAL_ERROR      = 0x02,
    FLOW_CONTROL_ERROR  = 0x03,
    SETTINGS_TIMEOUT    = 0x04,
    STREAM_CLOSED       = 0x05,
    FRAME_SIZE_ERROR    = 0x06,
    REFUSED_STREAM      = 0x07,
    CANCEL              = 0x08,
    COMPRESSION_ERROR   = 0x09,
    CONNECT_ERROR       = 0x0A,
    ENHANCE_YOUR_CALM   = 0x0B,
    INADEQUATE_SECURITY = 0x0C,
    HTTP_1_1_REQUIRED   = 0x0D,
};

/** HTTP/2 frame header (9 bytes) */
struct FrameHeader {
    uint32_t length : 24;   // payload length
    FrameType type;
    uint8_t  flags;
    uint32_t stream_id : 31;
    bool     reserved  : 1;
};

/** Configuration (from config.pson http2.*) */
struct Http2Config {
    bool enabled{false};         // Set true when implementation is complete
    bool h2c_upgrade{true};      // Advertise h2c via Upgrade header
    bool push_enabled{false};    // Server push (PUSH_PROMISE)
    int  initial_window_size{65535};
    int  max_concurrent_streams{100};
    int  max_frame_size{16384};
    int  max_header_list_size{8192};
};

/**
 * @brief HTTP/2 capability advertisement.
 *
 * Current behaviour: adds "Upgrade: h2c" and "Connection: Upgrade" headers
 * to HTTP/1.1 responses, advertising H2 availability.
 * When enabled=true and full implementation is present, performs the
 * connection preface handshake and frame multiplexing.
 */
class Http2Adapter {
public:
    void configure(const Http2Config& cfg) { cfg_ = cfg; }
    [[nodiscard]] bool is_enabled() const { return cfg_.enabled; }

    /** Add H2 advertisement headers to an HTTP/1.1 response */
    void add_alt_svc_header(std::map<std::string, std::string>& headers) const {
        if (cfg_.h2c_upgrade) {
            headers["Alt-Svc"] = "h2c=\":0\""; // same host, same port, h2c
        }
    }

    /** Check if Upgrade: h2c was requested */
    [[nodiscard]] static bool client_requested_upgrade(
            const std::map<std::string, std::string>& req_headers) {
        auto it = req_headers.find("Upgrade");
        if (it == req_headers.end()) return false;
        return it->second.find("h2c") != std::string::npos;
    }

    /** Status for /api/v1/compute/http2 endpoint */
    [[nodiscard]] std::string status_json() const {
        return std::string("{")
            + "\"enabled\":" + (cfg_.enabled ? "true" : "false")
            + ",\"status\":\"" + (cfg_.enabled ? "active" : "stub -- planned v6.x") + "\""
            + ",\"h2c_upgrade\":" + (cfg_.h2c_upgrade ? "true" : "false")
            + ",\"push_enabled\":" + (cfg_.push_enabled ? "true" : "false")
            + ",\"max_concurrent_streams\":" + std::to_string(cfg_.max_concurrent_streams)
            + ",\"initial_window_size\":" + std::to_string(cfg_.initial_window_size)
            + ",\"implementation\":\"planned\""
            + ",\"platforms\":{\"windows\":\"Schannel\",\"linux\":\"OpenSSL\",\"macos\":\"SecureTransport\"}"
            + "}";
    }

private:
    Http2Config cfg_;
};

} // namespace genie::net::http2

#endif // GENIE_NET_HTTP2_SERVER_HPP
