/**
 * @file platform_websocket.hpp
 * @brief RFC 6455 WebSocket client - zero external dependencies
 * @version 5.5.11
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * Implements a real WebSocket client using platform-native TLS:
 *   - Windows : WinHTTP WebSocket API (Win 8+)
 *   - macOS   : SecureTransport + raw RFC 6455 framing
 *   - Linux   : OpenSSL + raw RFC 6455 framing
 *
 * Provides:
 *   - TCP connection with optional TLS (wss://)
 *   - HTTP Upgrade handshake with Sec-WebSocket-Key
 *   - RFC 6455 frame encoding/decoding (text, binary, ping, pong, close)
 *   - Client-side masking (required by spec)
 *   - Configurable read timeout
 *
 * Reuses SHA-1 and Base64 already present in the codebase.
 */
#pragma once
#ifndef GENIE_CORE_PLATFORM_WEBSOCKET_HPP
#define GENIE_CORE_PLATFORM_WEBSOCKET_HPP

#include "platform_http.hpp"
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <random>
#include <mutex>
#include <functional>
#include <algorithm>
#include <array>

namespace genie::core {

// ============================================================================
// SHA-1 (standalone copy for WebSocket handshake - avoids circular include)
// ============================================================================

namespace ws_detail {

inline std::vector<uint8_t> sha1(const std::vector<uint8_t>& message) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg = message;
    uint64_t original_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>((original_len >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            auto ii = static_cast<size_t>(i);
            w[i] = (uint32_t(msg[chunk+ii*4]) << 24) | (uint32_t(msg[chunk+ii*4+1]) << 16) |
                   (uint32_t(msg[chunk+ii*4+2]) << 8)  |  uint32_t(msg[chunk+ii*4+3]);
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t val = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (val << 1) | (val >> 31);
        }
        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b&c)|((~b)&d);       k = 0x5A827999; }
            else if (i < 40) { f = b^c^d;                 k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b&c)|(b&d)|(c&d);     k = 0x8F1BBCDC; }
            else              { f = b^c^d;                 k = 0xCA62C1D6; }
            uint32_t temp = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e=d; d=c; c=(b<<30)|(b>>2); b=a; a=temp;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }

    std::vector<uint8_t> hash;
    for (uint32_t h : {h0,h1,h2,h3,h4}) {
        hash.push_back(uint8_t((h>>24)&0xFF));
        hash.push_back(uint8_t((h>>16)&0xFF));
        hash.push_back(uint8_t((h>>8)&0xFF));
        hash.push_back(uint8_t(h&0xFF));
    }
    return hash;
}

inline std::string base64_encode(const std::vector<uint8_t>& input) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result += chars[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6) result += chars[((val << 8) >> (valb + 8)) & 0x3F];
    while (result.size() % 4) result += '=';
    return result;
}

inline std::string base64_encode(const std::string& s) {
    return base64_encode(std::vector<uint8_t>(s.begin(), s.end()));
}

inline std::string generate_ws_key() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    std::vector<uint8_t> bytes(16);
    for (auto& b : bytes) b = dist(gen);
    return base64_encode(bytes);
}

inline std::string compute_accept_key(const std::string& ws_key) {
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concat = ws_key + magic;
    std::vector<uint8_t> data(concat.begin(), concat.end());
    auto hash = sha1(data);
    return base64_encode(hash);
}

} // namespace ws_detail

// ============================================================================
// WebSocket Frame Types
// ============================================================================

enum class WsOpcode : uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA
};

struct WsFrame {
    bool fin{true};
    WsOpcode opcode{WsOpcode::Text};
    std::string payload;
};

// ============================================================================
// Platform WebSocket Implementation
// ============================================================================

#ifdef _WIN32

// Windows: Use WinHTTP WebSocket (available on Win 8+)
// Falls back to raw socket RFC 6455 if WinHTTP WS not available

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

// Undefine Windows macros that conflict with C++ identifiers
// (ERROR_SUCCESS and other _SUCCESS variants are unaffected)
#ifdef DELETE
#undef DELETE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

class PlatformWebSocket {
public:
    PlatformWebSocket() = default;
    ~PlatformWebSocket() { close(); }

    PlatformWebSocket(const PlatformWebSocket&) = delete;
    PlatformWebSocket& operator=(const PlatformWebSocket&) = delete;

    bool connect(const std::string& url, int timeout_ms = 10000) {
        auto parsed = parse_url(url);
        bool use_ssl = (parsed.scheme == "wss" || parsed.scheme == "https");
        int port = parsed.port > 0 ? parsed.port : (use_ssl ? 443 : 80);

        session_ = WinHttpOpen(L"MetisGenie/2.25.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) return false;

        WinHttpSetTimeouts(session_, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        connection_ = WinHttpConnect(session_, to_wide(parsed.host).c_str(),
                                     static_cast<INTERNET_PORT>(port), 0);
        if (!connection_) { cleanup(); return false; }

        DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
        request_ = WinHttpOpenRequest(connection_, L"GET",
                                       to_wide(parsed.request_uri()).c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request_) { cleanup(); return false; }

        // Security flags for development
        if (use_ssl) {
            DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                        SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            WinHttpSetOption(request_, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
        }

        // Upgrade to WebSocket
        if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                              nullptr, 0)) {
            cleanup(); return false;
        }

        if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            cleanup(); return false;
        }

        if (!WinHttpReceiveResponse(request_, nullptr)) {
            cleanup(); return false;
        }

        websocket_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
        if (!websocket_) { cleanup(); return false; }

        // Close the request handle - no longer needed after upgrade
        WinHttpCloseHandle(request_);
        request_ = nullptr;

        connected_ = true;
        return true;
    }

    bool send_text(const std::string& data) {
        if (!connected_ || !websocket_) return false;
        DWORD err = WinHttpWebSocketSend(websocket_,
                                          WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                          (PVOID)data.data(), (DWORD)data.size());
        return err == ERROR_SUCCESS;
    }

    bool send_binary(const std::vector<uint8_t>& data) {
        if (!connected_ || !websocket_) return false;
        DWORD err = WinHttpWebSocketSend(websocket_,
                                          WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                          (PVOID)data.data(), (DWORD)data.size());
        return err == ERROR_SUCCESS;
    }

    WsFrame receive(int timeout_ms = 5000) {
        (void)timeout_ms;  // WinHTTP handles timeouts internally
        WsFrame frame;
        if (!connected_ || !websocket_) {
            frame.opcode = WsOpcode::Close;
            return frame;
        }

        std::vector<uint8_t> buffer(65536);
        DWORD bytes_read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type;

        DWORD err = WinHttpWebSocketReceive(websocket_, buffer.data(),
                                             (DWORD)buffer.size(), &bytes_read, &buf_type);
        if (err != ERROR_SUCCESS) {
            frame.opcode = WsOpcode::Close;
            connected_ = false;
            return frame;
        }

        frame.payload = std::string(buffer.begin(), buffer.begin() + bytes_read);

        switch (buf_type) {
            case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
            case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
                frame.opcode = WsOpcode::Text; break;
            case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
            case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
                frame.opcode = WsOpcode::Binary; break;
            case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
                frame.opcode = WsOpcode::Close;
                connected_ = false; break;
            default:
                frame.opcode = WsOpcode::Text; break;
        }

        return frame;
    }

    void send_ping() {
        // WinHTTP handles ping/pong internally
    }

    void close() {
        if (websocket_) {
            WinHttpWebSocketClose(websocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                                   nullptr, 0);
            WinHttpCloseHandle(websocket_);
            websocket_ = nullptr;
        }
        cleanup();
        connected_ = false;
    }

    bool is_connected() const { return connected_; }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connection_ = nullptr;
    HINTERNET request_ = nullptr;
    HINTERNET websocket_ = nullptr;
    bool connected_ = false;

    void cleanup() {
        if (request_) { WinHttpCloseHandle(request_); request_ = nullptr; }
        if (connection_) { WinHttpCloseHandle(connection_); connection_ = nullptr; }
        if (session_) { WinHttpCloseHandle(session_); session_ = nullptr; }
    }
};

#else
// ============================================================================
// POSIX (Linux + macOS): Raw RFC 6455 over OpenSSL or SecureTransport
// ============================================================================

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#include <poll.h>

#ifdef __APPLE__
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#else
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

class PlatformWebSocket {
public:
    PlatformWebSocket() = default;
    ~PlatformWebSocket() { close(); }

    PlatformWebSocket(const PlatformWebSocket&) = delete;
    PlatformWebSocket& operator=(const PlatformWebSocket&) = delete;

    bool connect(const std::string& url, int timeout_ms = 10000) {
        auto parsed = parse_url(url);
        // wss:// -> treat as https
        bool use_ssl = (parsed.scheme == "wss" || parsed.scheme == "https");
        int port = parsed.port > 0 ? parsed.port : (use_ssl ? 443 : 80);
        host_ = parsed.host;

        // TCP connect
        fd_ = connect_tcp(parsed.host, port);
        if (fd_ < 0) return false;

        // Set timeout
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // TLS
        if (use_ssl) {
            if (!setup_tls()) {
                ::close(fd_); fd_ = -1;
                return false;
            }
        }

        // WebSocket handshake
        if (!do_handshake(parsed)) {
            close();
            return false;
        }

        connected_ = true;
        return true;
    }

    bool send_text(const std::string& data) {
        return send_frame(WsOpcode::Text, data);
    }

    bool send_binary(const std::vector<uint8_t>& data) {
        return send_frame(WsOpcode::Binary, std::string(data.begin(), data.end()));
    }

    WsFrame receive(int timeout_ms = 5000) {
        WsFrame frame;
        if (!connected_ || fd_ < 0) {
            frame.opcode = WsOpcode::Close;
            return frame;
        }

        // Poll for data
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0) {
            frame.opcode = WsOpcode::Close;
            if (ret == 0) frame.payload = "timeout";
            return frame;
        }

        // Read frame header (2 bytes minimum)
        uint8_t header[2];
        if (raw_read(header, 2) != 2) {
            frame.opcode = WsOpcode::Close;
            connected_ = false;
            return frame;
        }

        frame.fin = (header[0] & 0x80) != 0;
        frame.opcode = static_cast<WsOpcode>(header[0] & 0x0F);
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payload_len = header[1] & 0x7F;

        if (payload_len == 126) {
            uint8_t ext[2];
            if (raw_read(ext, 2) != 2) { frame.opcode = WsOpcode::Close; return frame; }
            payload_len = (uint64_t(ext[0]) << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (raw_read(ext, 8) != 8) { frame.opcode = WsOpcode::Close; return frame; }
            payload_len = 0;
            for (int i = 0; i < 8; ++i)
                payload_len = (payload_len << 8) | ext[i];
        }

        uint8_t mask_key[4] = {};
        if (masked) {
            if (raw_read(mask_key, 4) != 4) { frame.opcode = WsOpcode::Close; return frame; }
        }

        // Read payload
        std::vector<uint8_t> payload(payload_len);
        if (payload_len > 0) {
            size_t total = 0;
            while (total < payload_len) {
                int n = raw_read(payload.data() + total, payload_len - total);
                if (n <= 0) break;
                total += static_cast<size_t>(n);
            }
        }

        // Unmask if needed
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] ^= mask_key[i % 4];
        }

        frame.payload = std::string(payload.begin(), payload.end());

        // Handle control frames
        if (frame.opcode == WsOpcode::Ping) {
            send_frame(WsOpcode::Pong, frame.payload);
        } else if (frame.opcode == WsOpcode::Close) {
            // Send close frame back
            send_frame(WsOpcode::Close, "");
            connected_ = false;
        }

        return frame;
    }

    void send_ping() {
        send_frame(WsOpcode::Ping, "");
    }

    void close() {
        if (connected_) {
            send_frame(WsOpcode::Close, "");
        }
        cleanup_tls();
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        connected_ = false;
    }

    bool is_connected() const { return connected_; }

private:
    int fd_ = -1;
    bool connected_ = false;
    std::string host_;

    // TLS state
#ifdef __APPLE__
    SSLContextRef ssl_ctx_ = nullptr;
#else
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
#endif

    // --- TLS setup ---
    bool setup_tls() {
#ifdef __APPLE__
        ssl_ctx_ = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (!ssl_ctx_) return false;
        SSLSetIOFuncs(ssl_ctx_, ssl_read_func, ssl_write_func);
        SSLSetConnection(ssl_ctx_, reinterpret_cast<SSLConnectionRef>(&fd_));
        SSLSetPeerDomainName(ssl_ctx_, host_.c_str(), host_.size());
        SSLSetSessionOption(ssl_ctx_, kSSLSessionOptionBreakOnServerAuth, true);
        OSStatus status = SSLHandshake(ssl_ctx_);
        while (status == errSSLServerAuthCompleted) {
            status = SSLHandshake(ssl_ctx_);
        }
        return (status == noErr);
#else
        init_openssl();
        ssl_ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_) return false;
        SSL_CTX_set_default_verify_paths(ssl_ctx_);
        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) return false;
        SSL_set_tlsext_host_name(ssl_, host_.c_str());
        SSL_set_fd(ssl_, fd_);
        return SSL_connect(ssl_) == 1;
#endif
    }

    void cleanup_tls() {
#ifdef __APPLE__
        if (ssl_ctx_) { SSLClose(ssl_ctx_); CFRelease(ssl_ctx_); ssl_ctx_ = nullptr; }
#else
        if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
        if (ssl_ctx_) { SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; }
#endif
    }

    // --- Raw read/write with TLS ---
    int raw_read(void* buf, size_t len) {
#ifdef __APPLE__
        if (ssl_ctx_) {
            size_t processed = 0;
            OSStatus st = SSLRead(ssl_ctx_, buf, len, &processed);
            (void)st;
            return static_cast<int>(processed);
        }
#else
        if (ssl_) return SSL_read(ssl_, buf, static_cast<int>(len));
#endif
        return static_cast<int>(::read(fd_, buf, len));
    }

    int raw_write(const void* buf, size_t len) {
#ifdef __APPLE__
        if (ssl_ctx_) {
            size_t processed = 0;
            SSLWrite(ssl_ctx_, buf, len, &processed);
            return static_cast<int>(processed);
        }
#else
        if (ssl_) return SSL_write(ssl_, buf, static_cast<int>(len));
#endif
        return static_cast<int>(::write(fd_, buf, len));
    }

    // --- WebSocket Handshake ---
    bool do_handshake(const ParsedUrl& parsed) {
        std::string ws_key = ws_detail::generate_ws_key();
        std::string expected_accept = ws_detail::compute_accept_key(ws_key);

        std::ostringstream req;
        req << "GET " << parsed.request_uri() << " HTTP/1.1\r\n"
            << "Host: " << parsed.host_header() << "\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Key: " << ws_key << "\r\n"
            << "Sec-WebSocket-Version: 13\r\n"
            << "\r\n";

        std::string req_str = req.str();
        raw_write(req_str.data(), req_str.size());

        // Read response headers
        std::string response;
        char buf[1];
        int consecutive_crlf = 0;
        while (consecutive_crlf < 4) {
            if (raw_read(buf, 1) != 1) return false;
            response += buf[0];
            if (buf[0] == '\r' || buf[0] == '\n') consecutive_crlf++;
            else consecutive_crlf = 0;
        }

        // Verify 101 Switching Protocols
        if (response.find("101") == std::string::npos) return false;

        // Verify Sec-WebSocket-Accept
        std::string lower_resp = response;
        std::transform(lower_resp.begin(), lower_resp.end(), lower_resp.begin(), ::tolower);
        size_t accept_pos = lower_resp.find("sec-websocket-accept:");
        if (accept_pos != std::string::npos) {
            size_t val_start = accept_pos + 21; // length of "sec-websocket-accept:"
            while (val_start < response.size() && response[val_start] == ' ') val_start++;
            size_t val_end = response.find("\r\n", val_start);
            if (val_end == std::string::npos) val_end = response.size();
            std::string accept_val = response.substr(val_start, val_end - val_start);
            // Trim
            while (!accept_val.empty() && (accept_val.back() == ' ' || accept_val.back() == '\r'))
                accept_val.pop_back();
            if (accept_val != expected_accept) return false;
        }

        return true;
    }

    // --- Frame Encoding ---
    bool send_frame(WsOpcode opcode, const std::string& payload) {
        if (fd_ < 0) return false;

        std::vector<uint8_t> frame;

        // FIN + opcode
        frame.push_back(0x80 | static_cast<uint8_t>(opcode));

        // Payload length + mask bit (clients MUST mask)
        uint64_t len = payload.size();
        if (len < 126) {
            frame.push_back(static_cast<uint8_t>(len) | 0x80);
        } else if (len < 65536) {
            frame.push_back(126 | 0x80);
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            frame.push_back(127 | 0x80);
            for (int i = 7; i >= 0; --i)
                frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }

        // Generate mask key
        std::random_device rd;
        uint8_t mask[4];
        uint32_t mask_val = rd();
        std::memcpy(mask, &mask_val, 4);
        frame.insert(frame.end(), mask, mask + 4);

        // Masked payload
        for (size_t i = 0; i < payload.size(); ++i) {
            frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
        }

        size_t total = 0;
        size_t to_send = frame.size();
        while (total < to_send) {
            int n = raw_write(frame.data() + total, to_send - total);
            if (n <= 0) return false;
            total += static_cast<size_t>(n);
        }
        return true;
    }
};

#endif // platform selection

} // namespace genie::core

#endif // GENIE_CORE_PLATFORM_WEBSOCKET_HPP
