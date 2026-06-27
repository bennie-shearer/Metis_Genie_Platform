# Architecture - Metis Genie Platform v5.5.11

## Server (C++20 / CLion)

```
metis-genie-platform-server/
  include/genie/           256 header-only modules
    core/        (61)      Config, logging, crypto, database, validation
    trading/     (30)      OMS, FIX, smart routing, algo execution
    market/      (28)      16 data providers, symbol master
    analytics/   (21)      ML alpha, NLQ, backtesting, timeseries
    portfolio/   (17)      Optimizer, rebalancing, tax lots, IBOR
    risk/        (16)      VaR, Monte Carlo, factor model, CVA/DVA
    net/         (14)      HTTP server, REST API, WebSocket
    reporting/   (12)      PDF, HTML, scheduled reports
    ops/         (10)      K8s, containers, backup, monitoring
    performance/ (10)      Attribution, benchmarks, peer comparison
    persistence/  (7)      Event store, archiver, reconciliation
    assets/       (7)      Private equity, ESG scoring
    compliance/   (7)      Regulatory, pre-trade, surveillance
    security/     (6)      Auth, sessions, API keys, RBAC
    ux/           (6)      Dashboards, NLQ, what-if, feature flags
    tax/          (3)      Lot optimizer, wash sales, Form 8949
  src/main.cpp             Server entry point
  tests/                   4 test executables, 330+ tests
  examples/                6 example programs
  docs/                    All .md and .txt documentation
  CMakeLists.txt           Build configuration
  CMakePresets.json        Build presets
  config.pson.template     All configurable parameters (JSON)
  VERSION.txt              Version source of truth
```

## Client (HTML5 / WebStorm)

```
web/
  *.html           (61)    All platform feature pages
  js/              (18)    JavaScript modules
  css/              (4)    Stylesheets
  img/                     Icons and assets
  config.pson              Client configuration
```

## Data Flow

```
Client (HTML5) -> REST API (130+ endpoints) -> Module Layer -> Data Layer
                      |                             |
                 Auth/Sessions              Market Data Providers
                      |                             |
                Rate Limiting                   SQLite3 DB
```

## Key Design Decisions

1. Zero external dependencies - all components built from C++20 stdlib
2. Header-only - no separate library compilation required
3. VERSION.txt drives CMake which drives compile definitions
4. REST API covers all features; every endpoint has client coverage
5. All parameters externalized to config.pson.template (JSON)
6. GPU, Kubernetes, and Containers are stubs for future C++20 implementation
7. Cross-platform: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

*Metis Genie Platform v5.5.11 -- Architecture*
