/**
 * @file wasm_client.hpp
 * @brief WebAssembly client compilation -- server-side support stub
 * @version 5.3.1
 * @copyright (c) 2026 Bennie Shearer (Retired). MIT License.
 *
 * WebAssembly client compilation enables the client/js/ code to be
 * compiled to WASM for zero-install offline deployment.
 *
 * Server-side support: serve *.wasm with correct MIME type and COOP/COEP
 * headers required by SharedArrayBuffer (for multi-threaded WASM).
 *
 * Client-side: Emscripten compilation of select analytics modules to WASM
 * for in-browser computation (VaR, scenario analysis) without server round-trips.
 *
 * Status: stub -- MIME type and headers implemented; Emscripten build planned.
 *
 * config.pson:
 *   "wasm": { "enabled": false, "serve_wasm": true, "coop_coep_headers": true }
 */
#pragma once
#ifndef GENIE_NET_WASM_CLIENT_HPP
#define GENIE_NET_WASM_CLIENT_HPP

#include <string>
#include <map>

namespace genie::net::wasm {

struct WasmConfig {
    bool enabled{false};
    bool serve_wasm{true};
    bool coop_coep_headers{true};
};

/** Add COOP/COEP headers needed for SharedArrayBuffer / multi-threaded WASM */
inline void add_isolation_headers(std::map<std::string, std::string>& headers,
                                   const WasmConfig& cfg) {
    if (!cfg.coop_coep_headers) return;
    headers["Cross-Origin-Opener-Policy"]   = "same-origin";
    headers["Cross-Origin-Embedder-Policy"] = "require-corp";
}

/** Status JSON */
inline std::string status_json(const WasmConfig& cfg) {
    return std::string("{")
        + "\"enabled\":"          + (cfg.enabled ? "true" : "false")
        + ",\"serve_wasm\":"      + (cfg.serve_wasm ? "true" : "false")
        + ",\"coop_coep\":"       + (cfg.coop_coep_headers ? "true" : "false")
        + ",\"status\":\""        + (cfg.enabled ? "active" : "stub -- planned v7.x") + "\""
        + ",\"implementation\":\"MIME type + COOP/COEP headers implemented\""
        + ",\"planned\":\"Emscripten WASM build of analytics modules -- v7.x\""
        + "}";
}

} // namespace genie::net::wasm
#endif // GENIE_NET_WASM_CLIENT_HPP
