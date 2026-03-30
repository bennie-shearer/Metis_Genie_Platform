# Metis Genie Platform v5.5.8

**Enterprise Investment Management Platform**  
C++20 · Zero External Dependencies · MIT License · Windows / Linux / macOS

---

## Overview

Metis Genie Platform is a complete, self-contained enterprise investment management
system built entirely in C++20. It compiles to a single executable and serves a
64-page browser client through 144 REST API endpoints — covering portfolio management,
risk analytics, trading, compliance, tax optimization, reporting, and operations.

No Docker. No external runtime. No package manager. One binary.

---

## Quick Start

### Prerequisites

| Platform | Toolchain |
|---|---|
| Windows | MinGW-w64 GCC 13 (primary), MSVC 2022 |
| Linux | GCC 13 or Clang 16 |
| macOS | Apple Clang 15 |

CMake 3.16 or later required on all platforms.

### Build and Run

```bash
# Linux / macOS
cd server
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/metis-genie-platform --serve
```

```bat
:: Windows / MinGW
cd server
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
build\bin\metis-genie-platform.exe --serve
```

Server starts at **http://localhost:8080**.

Default credentials:

| Username | Password | Role |
|---|---|---|
| admin | demo | Administrator |
| trader | trade | Trader |
| user | user | Read-only |

### IDEs

- **CLion** — open the `server/` directory as a CMake project. Build with **Ctrl+F9**.
  The `sync_web_assets` CMake target copies `client/` → `bin/web/` on every build.
- **WebStorm** — open the `client/` directory. No build step required.

---

## Project Layout

```
Metis_Genie_Platform/
├── README.md               ← You are here
├── LICENSE                 ← MIT License
├── .gitignore
│
├── server/                 ← CLion project (C++20 server)
│   ├── CMakeLists.txt      ← CMake root (reads version from VERSION.txt)
│   ├── CMakePresets.json
│   ├── VERSION.txt         ← 5.5.8 (single source of truth for CMake)
│   ├── config.pson         ← 240 server parameters (PSON format)
│   ├── include/genie/      ← 266 header files, 17 module directories
│   ├── src/main.cpp        ← Single translation unit
│   ├── tests/              ← Custom C++20 test framework (no GTest)
│   ├── examples/           ← Per-module usage examples
│   └── third_party/        ← Bundled SQLite3 (only external code)
│
├── client/                 ← WebStorm project (browser UI)
│   ├── index.html          ← Main dashboard
│   ├── config.pson         ← Client config (server URL, theme, logging)
│   ├── *.html              ← 64 pages covering all financial domains
│   ├── css/                ← 4 stylesheets
│   ├── js/                 ← 18 JavaScript modules
│   └── img/                ← SVG logo and icon
│
└── docs/                   ← All documentation
    ├── BACKGROUND.md       ← History, ecosystem, design philosophy
    ├── CHANGELOG.md        ← Full version history
    ├── API.md              ← REST API reference (144 endpoints)
    ├── BUILD.md            ← Detailed build instructions
    ├── ARCHITECTURE.md     ← System architecture
    ├── TODO.md             ← Roadmap
    └── VERSION.txt         ← 5.5.8
```

> **Why is `CMakeLists.txt` inside `server/` and not at the repo root?**  
> `server/` is the CLion project root. Opening it directly gives CMake the correct
> working directory so that `include/`, `src/`, `tests/`, `third_party/`, and
> `config.pson` are all at the expected relative paths. The repo root holds two
> separate IDE projects (`server/` and `client/`) plus shared resources.

---

## What It Does

144 REST endpoints across all investment management domains:

| Domain | Coverage |
|---|---|
| Portfolio, Positions, IBOR, Rebalancing | Portfolio accounting, cash management, lot tracking |
| Risk: VaR, Monte Carlo, Stress Testing | Full risk analytics with factor models |
| Trading, Orders, FIX Engine, TCA | Order lifecycle, routing, execution analytics |
| Compliance, ESG, Surveillance | Pre-trade checks, regulatory reporting, ESG scoring |
| Analytics, Backtesting, ML Alpha | Performance attribution, strategy testing |
| Tax Optimization, Wash Sales | HIFO/FIFO/LIFO, wash sale detection |
| Reporting, Dashboards, Scheduling | PDF/CSV/XLSX generation, scheduled reports |
| Market Data | Yahoo Finance, IEX, Polygon.io, FRED integrations |
| Operations, Health, Backup | Monitoring, backup automation, deployment |
| SSE, Prometheus, Config, Compute | Streaming, metrics, GPU/K8s/Container stubs |

All 64 client pages have active REST API connections.

Full API reference: [docs/API.md](docs/API.md)

---

## Architecture

```
Browser (client/ — HTML/JS/CSS, 64 pages)
        │  HTTP/1.1 fetch() → /api/v1/*
        ▼
HttpServer  (net/http_server.hpp)
  Multi-threaded · Static file serving · Keep-alive
        │
RestApi  (net/rest_api.hpp)
  Auth · Rate limiting · Response cache · Input validation
  Graceful shutdown · Structured errors · 144 routes
        │
Domain Modules  (include/genie/*/*.hpp)
  portfolio/ · risk/ · trading/ · compliance/ · reporting/ · ...
        │
SQLite3  (third_party/sqlite3/)
  Users DB · Audit DB · Main DB · Prices DB
```

All platform logic lives in `.hpp` files under `server/include/genie/` across
17 module directories. `src/main.cpp` is the single translation unit.

---

## Configuration

All 240 parameters live in `server/config.pson` with inline comments.
PSON is JSON extended with `//` comments and trailing commas.

```jsonc
{
    "server": {
        "port": 8080,
        "host": "0.0.0.0",
        "workers": 4,
    },
    "auth": {
        "required": true,
        "session_lifetime_minutes": 480,
        "max_login_attempts": 5,
        "lockout_minutes": 15,
    },
}
```

No hardcoded values in source. Every tunable behavior is in `config.pson`.

---

## Design Principles

**Zero external dependencies** — No Boost, no OpenSSL, no libcurl, no gRPC, no
Python, no JVM. Every capability uses the C++20 standard library. The sole
exception is bundled SQLite3 (single-file amalgamation, no installation).

**Header-only architecture** — All logic in `.hpp` files. One translation unit
compiles the entire server. Every algorithm is visible and auditable in source.

**PSON configuration** — Every tunable parameter is externalized in `config.pson`.
No recompile to reconfigure.

**Single executable** — One binary. No daemon, no Docker, no container runtime,
no Kubernetes required to run.

**C++20 throughout** — `std::jthread`, concepts, `std::span`, ranges, constexpr,
designated initializers — used throughout, not bolted on.

---

## Security

- Bearer token sessions with configurable lifetime and lockout
- Immutable audit log in a separate SQLite3 database
- Input validation on all POST/PUT requests
- HTTP security headers on every response (CSP, X-Frame-Options, Referrer-Policy)
- Structured error responses with trace IDs (no internal detail in responses)
- Per-session rate limiting with `X-RateLimit-Remaining` response header

---

## Testing

Custom C++20 test framework — no GTest, no Catch2, no external test runner.

```bash
./test_genie          # Core modules, PSON config, utility functions
./test_integration    # HTTP server lifecycle, API route dispatch
./test_tier3          # Analytics, trading, compliance
```

330+ test cases. All tests run against in-memory state and local HTTP server
instances — no external services, no Docker required.

---

## Acknowledgments

| | |
|---|---|
| JetBrains CLion | https://www.jetbrains.com/clion/ |
| JetBrains WebStorm | https://www.jetbrains.com/webstorm/ |
| Anthropic Claude | https://www.anthropic.com/ |

---

## License

MIT License — Copyright (c) 2026 Bennie Shearer (Retired)

See [LICENSE](LICENSE) for full text.

See [docs/BACKGROUND.md](docs/BACKGROUND.md) for history, ecosystem context,
and detailed design philosophy.
