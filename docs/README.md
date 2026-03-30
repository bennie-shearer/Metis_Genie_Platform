# Metis Genie Platform

**Enterprise Investment Management Platform Prototype**
**Version 5.3.2** | C++20 | Zero External Dependencies | MIT License

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

---

## Overview

Metis Genie Platform is a complete, single-executable Enterprise Investment
Management Platform built in C++20 with zero external dependencies. It provides
portfolio management, risk analytics, trading, compliance, tax optimization, and
reporting through 145+ REST API endpoints served to a 64-page browser client.

One binary. One PSON config file. Open in a browser.

---

## Features

### Financial Modules
- **Portfolio Management** -- positions, IBOR, rebalancing, tax lot optimization
- **Risk Analytics** -- VaR, Monte Carlo, stress testing, factor models, tail risk
- **Trading** -- order management, FIX engine, smart routing, execution analytics
- **Compliance** -- pre-trade compliance, ESG scoring, regulatory reporting
- **Performance** -- Brinson-Fachler attribution, benchmark tracking, peer comparison
- **Tax Optimization** -- wash sale detection, HIFO/FIFO/LIFO, tax lot optimizer
- **Reporting** -- PDF/CSV/XLSX generation, dashboards, scheduled reports
- **Market Data** -- Alpha Vantage, IEX Cloud, Polygon, Finnhub, Yahoo Finance, FRED

### Technical Features (v5.3.2)
- **Zero external dependencies** -- only SQLite3 bundled; zlib optional
- **PSON configuration** -- all 120+ parameters in `server/config.pson` with `//` comments
- **145+ REST endpoints** -- complete API for all financial domains
- **64-page browser UI** -- all pages wired to REST API
- **Server-Sent Events (SSE)** -- EventSource-compatible poll bridge, 3 live channels
- **Cross-platform file watcher** -- inotify / ReadDirectoryChangesW / kqueue + poll
- **Binary serialization** -- METI format (CRC-32, ~10x faster than JSON)
- **Prometheus /metrics** -- text v0.0.4, scrapeable by Prometheus/Grafana
- **Constexpr route table** -- RouteDescriptor with flags for compile-time metadata
- **gzip/deflate compression** -- pure C++20 RFC 1951/1952 + optional zlib
- **Response caching** -- per-endpoint TTL cache with X-Cache header
- **Request validation** -- Content-Type + JSON structure checks
- **Structured error responses** -- error codes + trace IDs
- **Graceful shutdown** -- in-flight request draining
- **Keep-alive HTTP/1.1** -- connection reuse, configurable timeout
- **WebSocket** -- live data push on port 8081
- **Circuit breaker** -- resilience against cascading failures
- **Audit logging** -- immutable trail in SQLite3

### GPU / Containers / Kubernetes / HTTP/2 / FIX / WASM
Future native C++20 implementations. Stub headers in place; configured via `config.pson`:

| Feature | Config Key | Status | Target |
|---|---|---|---|
| GPU: CUDA/Metal/DirectCompute/Vulkan | `compute.gpu_backend` | Stub | v6.x |
| Containers: cgroups/Job Objects | `containers.enabled` | Stub | v6.x |
| HTTP/2 multiplexing | `http2.enabled` | Stub + Alt-Svc active | v6.x |
| Kubernetes API client | `kubernetes.enabled` | Stub | v7.x |
| FIX 4.4/5.0 TCP session | `fix.enabled` | Parser+session done | v7.x |
| WebAssembly client | `wasm.enabled` | COOP/COEP active | v7.x |

---

## Quick Start

### Prerequisites
- C++20 compiler: GCC 13+, Clang 15+, or MSVC 2022+
- CMake 3.20+
- Windows: MinGW-w64 or MSVC; Linux/macOS: standard build tools

### Build

```bash
cd server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Windows (MinGW):
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Optional zlib (enables full deflate compression):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGENIE_USE_ZLIB=1
cmake --build build --parallel
```

### Configure

```bash
# config.pson is already in server/ with all defaults set and documented
# Edit to customize -- every parameter has an inline // comment
```

### Run

```bash
./build/metis-genie-platform --serve
# Server starts on http://localhost:8080
# Open client/index.html in your browser
```

Default credentials: `admin` / `demo`

---

## Project Structure

```
Metis_Genie_Platform-5.3.2/
|
+-- server/          <- CLion project (C++20 server only)
|   +-- include/     <- 266 .hpp files, 17 module directories
|   |   +-- genie/
|   |       +-- core/          <- PSON config, file watcher, binary serializer, ...
|   |       +-- net/           <- HTTP server, REST API, SSE, Prometheus, ...
|   |       +-- portfolio/     <- Portfolio management
|   |       +-- risk/          <- Risk analytics
|   |       +-- trading/       <- Trading, FIX engine v2
|   |       +-- ops/           <- Kubernetes client, containers
|   |       +-- ...            <- 11 more domain directories
|   +-- src/
|   |   +-- main.cpp           <- Single translation unit
|   +-- tests/                 <- Custom C++20 test framework (no GTest)
|   +-- examples/              <- Usage examples
|   +-- third_party/sqlite3/   <- Bundled SQLite3 amalgamation
|   +-- config.pson            <- ALL parameters (PSON: JSON + // comments)
|   +-- CMakeLists.txt
|
+-- client/          <- WebStorm project (browser UI only)
|   +-- *.html       <- 64 HTML pages (all wired to REST API)
|   +-- css/         <- 4 stylesheets
|   +-- js/          <- 18 JavaScript modules
|   +-- img/         <- SVG logo and icon
|   +-- config.pson  <- Client configuration (server URL, theme, logging)
|
+-- docs/            <- All documentation
    +-- README.md    <- This file
    +-- BACKGROUND.md
    +-- CHANGELOG.md
    +-- API.md
    +-- BUILD.md
    +-- ARCHITECTURE.md
    +-- TODO.md
    +-- VERSION.txt  <- 5.3.2
```

---

## Configuration (config.pson)

All parameters are in `server/config.pson` using PSON format (JSON + `//` comments
and trailing commas). Open the file -- every parameter is documented inline.

Key sections:

| Section | Parameters |
|---|---|
| `server` | port, host, threads, timeouts |
| `auth` | session timeout, lockout policy |
| `api` | rate limits, caching, validation, graceful shutdown |
| `cache_ttl` | per-endpoint TTL in seconds |
| `sse` | enabled, keep-alive, max clients |
| `prometheus` | enabled, path, namespace |
| `file_watcher` | enabled, poll fallback ms |
| `compression` | enabled, min size, level |
| `http2` | enabled (stub), Alt-Svc advertisement |
| `fix` | enabled, version, host, port |
| `kubernetes` | enabled (stub), api_url, namespace |
| `wasm` | enabled (stub), COOP/COEP headers |
| `compute` | GPU backend selection |
| `api_keys` | market data, brokers, notifications |

---

## REST API

Base URL: `http://localhost:8080/api/v1/`

Authentication: `Authorization: Bearer <token>` (from `POST /api/v1/auth/login`)

Notable endpoints:

```bash
# Public (no auth)
curl http://localhost:8080/api/v1/health
curl http://localhost:8080/metrics                    # Prometheus text format

# Auth
curl -X POST http://localhost:8080/api/v1/auth/login \
     -H "Content-Type: application/json" \
     -d '{"username":"admin","password":"demo"}'

# SSE poll (EventSource bridge)
curl "http://localhost:8080/api/v1/stream/poll?channel=market&last_id=0" \
     -H "Authorization: Bearer <token>"

# Prometheus metrics (authenticated)
curl http://localhost:8080/api/v1/metrics/prometheus \
     -H "Authorization: Bearer <token>"

# Compute status
curl http://localhost:8080/api/v1/compute/sse        -H "Authorization: Bearer <token>"
curl http://localhost:8080/api/v1/compute/http2      -H "Authorization: Bearer <token>"
curl http://localhost:8080/api/v1/compute/kubernetes -H "Authorization: Bearer <token>"
curl http://localhost:8080/api/v1/compute/fix        -H "Authorization: Bearer <token>"
curl http://localhost:8080/api/v1/compute/wasm       -H "Authorization: Bearer <token>"
curl http://localhost:8080/api/v1/compute/compression -H "Authorization: Bearer <token>"
```

Full API documentation: [docs/API.md](API.md)

---

## License

MIT License -- see [LICENSE](../LICENSE)

Copyright (c) 2026 Bennie Shearer (Retired)

DISCLAIMER: This software provides Enterprise Investment Management Platform Prototype
capabilities. Users are responsible for proper configuration, security testing,
and compliance with applicable regulations and licensing requirements.

---

## Author

Bennie Shearer (Retired)

## Acknowledgments

Thanks to all mentors through the years. Special thanks to:

| Organization | Website |
|---|---|
| CLion by JetBrains s.r.o. | https://www.jetbrains.com/clion/ |
| WebStorm by JetBrains s.r.o. | https://www.jetbrains.com/webstorm/ |
| Claude by Anthropic PBC | https://www.anthropic.com/ |
