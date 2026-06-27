# Metis Genie Platform — Background and Design Philosophy

**Version:** 5.5.11
**Author:** Bennie Shearer (Retired)
**Copyright:** (c) 2026 Bennie Shearer (Retired). MIT License.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [History and Purpose of Enterprise Investment Management Libraries](#2-history-and-purpose-of-enterprise-investment-management-libraries)
   - 2.1 [The Mainframe Era (1970s-1990s)](#21-the-mainframe-era-1970s1990s)
   - 2.2 [Client-Server Revolution (1990s-2000s)](#22-client-server-revolution-1990s2000s)
   - 2.3 [The Open Source Movement (2000s-2010s)](#23-the-open-source-movement-2000s2010s)
   - 2.4 [Cloud-Native and Modern Platforms (2010s-Present)](#24-cloud-native-and-modern-platforms-2010spresent)
   - 2.5 [Purpose of Investment Management Libraries](#25-purpose-of-investment-management-libraries)
3. [The Broader Ecosystem of Investment Management Platform Libraries](#3-the-broader-ecosystem-of-investment-management-platform-libraries)
   - 3.1 [Commercial Platforms](#31-commercial-platforms)
   - 3.2 [Open Source Libraries and Frameworks](#32-open-source-libraries-and-frameworks)
   - 3.3 [Quantitative and Analytics Libraries](#33-quantitative-and-analytics-libraries)
   - 3.4 [Market Data and Connectivity Libraries](#34-market-data-and-connectivity-libraries)
   - 3.5 [Where Metis Genie Platform Fits](#35-where-metis-genie-platform-fits)
   - 3.6 [Ecosystem Comparison Matrix](#36-ecosystem-comparison-matrix)
4. [Design Philosophy of Investment Management Platform Libraries](#4-design-philosophy-of-investment-management-platform-libraries)
   - 4.1 [Core Principles](#41-core-principles)
   - 4.2 [Zero External Dependencies](#42-zero-external-dependencies)
   - 4.3 [Header-Only Architecture](#43-header-only-architecture)
   - 4.4 [C++20 as the Foundation](#44-c20-as-the-foundation)
   - 4.5 [Cross-Platform by Design](#45-cross-platform-by-design)
   - 4.6 [PSON Configuration -- All Parameters Externalized](#46-pson-configuration----all-parameters-externalized)
   - 4.7 [Security-First Development](#47-security-first-development)
   - 4.8 [Extensibility and Future-Proofing](#48-extensibility-and-future-proofing)
5. [Metis Genie Platform Architecture](#5-metis-genie-platform-architecture)
   - 5.1 [Module Organization](#51-module-organization)
   - 5.2 [REST API Design](#52-rest-api-design)
   - 5.3 [Client-Server Architecture](#53-client-server-architecture)
   - 5.4 [Data Flow and Processing](#54-data-flow-and-processing)
   - 5.5 [Directory Structure](#55-directory-structure)
6. [GPU, Kubernetes, and Container Strategy](#6-gpu-kubernetes-and-container-strategy)
   - 6.1 [GPU Compute Strategy](#61-gpu-compute-strategy)
   - 6.2 [Container Strategy](#62-container-strategy)
   - 6.3 [Kubernetes Strategy](#63-kubernetes-strategy)
   - 6.4 [Implementation Roadmap](#64-implementation-roadmap)
7. [Quality Standards and Testing](#7-quality-standards-and-testing)
   - 7.1 [Code Quality Standards](#71-code-quality-standards)
   - 7.2 [Testing Strategy](#72-testing-strategy)
   - 7.3 [Documentation Standards](#73-documentation-standards)
8. [Project Evolution](#8-project-evolution)
   - 8.1 [Version History Summary](#81-version-history-summary)
   - 8.2 [Lessons Learned](#82-lessons-learned)
   - 8.3 [Future Direction](#83-future-direction)

---

## 1. Introduction

Metis Genie Platform is a comprehensive Enterprise Investment Management Platform
Prototype built entirely in C++20 with zero external dependencies beyond bundled
SQLite3. It compiles to a single executable providing portfolio management, risk
analytics, trading, compliance, tax optimization, and reporting through 130+ REST
API endpoints served directly to a 64-page browser client.

The name draws from Metis -- the Greek Titaness of wisdom, craft, and deep
thought -- and Genie -- a powerful entity that delivers results on demand.
Together they describe software that brings deep financial intelligence to the
surface with no ceremony and no intermediary layers.

This document covers the history that motivated the project, the ecosystem of
technologies it relates to, the design decisions that shaped it, and the roadmap
for where it goes next.

---

## 2. History and Purpose of Enterprise Investment Management Libraries

### 2.1 The Mainframe Era (1970s-1990s)

Enterprise investment management computing began on IBM mainframes running OS/360,
MVS, and eventually z/OS. Portfolio accounting systems such as SunGard InvestOne
and State Street HiPortfolio ran as batch COBOL and PL/I programs against VSAM
flat files and IMS hierarchical databases.

The dominant paradigm was nightly batch: positions were calculated overnight, risk
reports printed in the morning, and trading desks operated largely on paper and
telephone. Real-time portfolio analytics simply did not exist -- the hardware could
not support it, and the regulatory environment did not demand it.

Libraries in this era were proprietary, undocumented, and tightly coupled to
specific hardware vendors. Code was a competitive secret, not a shareable resource.

### 2.2 Client-Server Revolution (1990s-2000s)

The arrival of Unix workstations, Windows NT, and relational databases (Sybase,
Oracle, SQL Server) fundamentally changed investment management software. Systems
moved from mainframe batch to client-server real-time architectures.

Bloomberg launched its Terminal and API (BLPAPI), setting the gold standard for
financial data access. Charles River Development, SimCorp Dimension, and Advent
Software built Order Management Systems (OMS) and Portfolio Management Systems
(PMS) that ran on these new platforms.

C++ emerged as the language of choice for performance-critical financial systems.
The STL standardization in 1998 made C++ a viable foundation for reusable library
code. Risk engines, pricing models, and analytics modules began to be factored out
as shared libraries.

Key libraries from this era include QuantLib (2000) -- the first major open source
C++ quantitative finance library -- and the ACE framework for high-performance
C++ server networking. Boost provided foundational utilities that eventually fed
into the C++ standard library.

### 2.3 The Open Source Movement (2000s-2010s)

The 2000s brought the democratization of financial computing through open source.
Python emerged as a research language with NumPy, SciPy, and Pandas making
quantitative analysis accessible beyond C++ experts.

Open source quantitative finance libraries proliferated: zipline (Quantopian),
PyAlgoTrade, LEAN (QuantConnect), and VectorBT. The FIX Protocol became the
industry standard for order routing, and open source FIX engines (QuickFIX)
emerged. REST APIs began replacing proprietary data feeds for market data access.

Regulatory requirements (MiFID I/II, Dodd-Frank, Basel III) drove investment in
compliance infrastructure, spawning a new category of risk and compliance libraries.

### 2.4 Cloud-Native and Modern Platforms (2010s-Present)

The 2010s brought cloud computing, containerization, and microservices to financial
services. AWS, Azure, and GCP replaced on-premise data centers. Docker and
Kubernetes became standard deployment infrastructure.

Python and machine learning converged in finance: scikit-learn, TensorFlow, and
PyTorch enabled alpha signal generation and risk modeling at scale.

Despite these trends, C++ retained its dominant position in low-latency trading
systems, real-time risk calculation engines, market data feed handlers, and core
pricing libraries.

The shift to cloud also brought challenges: dependency sprawl, security exposure
from third-party libraries, and the operational complexity of distributed systems.

### 2.5 Purpose of Investment Management Libraries

Enterprise investment management libraries exist to solve recurring, mathematically
precise, and operationally critical problems:

**Portfolio accounting** -- tracking positions, cash flows, corporate actions, and
accruals across multiple custodians, currencies, and accounting bases.

**Risk analytics** -- computing Value at Risk, Expected Shortfall, factor exposures,
stress tests, and scenario analyses with sufficient speed for real-time decisions.

**Trading and execution** -- managing the lifecycle of orders from generation
through routing, execution, allocation, and settlement, with compliance checks
at each stage.

**Performance attribution** -- decomposing portfolio returns into contributions
from allocation, selection, interaction, and currency effects relative to a benchmark.

**Compliance and reporting** -- enforcing investment policy constraints, generating
regulatory reports, maintaining audit trails, and supporting operational due diligence.

Every investment management firm must solve all of these problems. Libraries that
provide correct, tested, and documented implementations create enormous leverage.

---

## 3. The Broader Ecosystem of Investment Management Platform Libraries

### 3.1 Commercial Platforms

| Platform | Vendor | Focus |
|---|---|---|
| Aladdin | BlackRock | Risk, portfolio management, compliance |
| SimCorp Dimension | SimCorp | End-to-end investment management |
| Charles River IMS | SS&C | OMS, PMS, compliance |
| Enfusion | Enfusion | Cloud-native PMS/OMS/Risk |
| Advent APX | SS&C | Portfolio accounting |
| Bloomberg PORT | Bloomberg | Pricing, analytics, reporting |
| FactSet | FactSet | Data, analytics, risk |
| Murex | Murex | Multi-asset trading and risk |

Commercial platforms are comprehensive but expensive, opaque, vendor-locked, and
poorly suited to research or rapid prototyping.

### 3.2 Open Source Libraries and Frameworks

| Library | Language | Focus |
|---|---|---|
| QuantLib | C++ | Derivatives pricing, yield curves |
| LEAN | C# | Backtesting, live trading |
| zipline | Python | Event-driven backtesting |
| PyPortfolioOpt | Python | Portfolio optimization |
| Riskfolio-Lib | Python | Portfolio optimization |
| vectorbt | Python | Vectorized backtesting |
| QuickFIX | C++/Python | FIX protocol engine |

Open source libraries solve specific problems well but rarely provide end-to-end
coverage. Integrating multiple libraries requires significant engineering effort
and creates dependency management challenges.

### 3.3 Quantitative and Analytics Libraries

| Library | Language | Focus |
|---|---|---|
| NumPy/SciPy | Python | Numerical computing |
| Pandas | Python | Time series and tabular data |
| statsmodels | Python | Statistical models |
| scikit-learn | Python | Machine learning |
| Eigen | C++ | Linear algebra |
| Armadillo | C++ | Linear algebra |
| oneTBB | C++ | Parallel computing |

These libraries are essential for research but typically require Python runtime
environments and scientific package ecosystems that add operational complexity.

### 3.4 Market Data and Connectivity Libraries

| Provider | API Type | Data |
|---|---|---|
| Bloomberg BLPAPI | C++/Python | Real-time, historical, reference |
| Refinitiv Eikon | Python | Real-time, news, analytics |
| Alpha Vantage | REST | Historical, fundamentals |
| IEX Cloud | REST | Real-time, historical |
| Polygon.io | REST/WebSocket | Real-time, historical |
| Finnhub | REST/WebSocket | Real-time, news |
| Yahoo Finance | REST (public) | Historical, fundamentals |
| FRED | REST | Macroeconomic data |

### 3.5 Where Metis Genie Platform Fits

Metis Genie Platform occupies a unique position in this ecosystem:

**Not a research library.** Unlike QuantLib or PyPortfolioOpt, it is not a
collection of mathematical primitives. It is a complete, running system with a
web-based user interface.

**Not a commercial platform.** Unlike Aladdin or SimCorp, it is open source,
has no licensing fees, and is designed to be understood and modified by its users.

**Not a cloud-dependent service.** Unlike Enfusion or cloud-native platforms, it
runs as a single executable with no external service dependencies, no Docker
daemon, no Kubernetes cluster.

**The gap it fills:** A complete, buildable, runnable reference implementation of
an enterprise investment management system in C++20. It demonstrates how all the
components -- portfolio accounting, risk analytics, trading, compliance, reporting,
and operations -- fit together in a coherent architecture, without the obscurity of
commercial platforms or the incompleteness of single-purpose libraries.

### 3.6 Ecosystem Comparison Matrix

| Capability | Metis Genie | QuantLib | LEAN | Commercial |
|---|---|---|---|---|
| Portfolio management | Full | None | Partial | Full |
| Risk analytics | Full | Partial | Partial | Full |
| Trading / OMS | Full | None | Full | Full |
| Compliance | Full | None | Partial | Full |
| Tax optimization | Full | None | None | Partial |
| Web UI (64 pages) | Yes | No | No | Yes |
| REST API (144 routes) | Yes | No | Partial | Yes |
| Zero dependencies | Yes | No | No | N/A |
| C++20 native | Yes | Partial | No | N/A |
| Single executable | Yes | Yes | No | N/A |
| Open source (MIT) | Yes | Yes (QuantLib) | Yes | No |
| No Docker required | Yes | Yes | No | N/A |

---

## 4. Design Philosophy of Investment Management Platform Libraries

### 4.1 Core Principles

Metis Genie Platform is built on five non-negotiable principles:

1. **Zero external dependencies** -- everything compiles from the repository
2. **PSON configuration** -- every tunable parameter lives in config.pson
3. **Cross-platform** -- Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Clang)
4. **Single executable** -- one binary, no daemon dependencies
5. **American English** -- consistent spelling and terminology throughout

These are not preferences. They are constraints enforced at every revision and
reflected in every design decision.

### 4.2 Zero External Dependencies

The financial technology industry has a dependency problem. Systems built on large
dependency graphs are vulnerable to supply chain attacks, license changes, breaking
API changes, and build system complexity.

Metis Genie Platform takes the opposite approach. Every capability is implemented
in C++20 using only the standard library. The single exception is SQLite3, which is
bundled as a single-file amalgamation in server/third_party/sqlite3/.

This means no Boost, no OpenSSL, no libcurl, no gRPC, no Protobuf, no Python
runtime, no JVM, no Docker, no Kubernetes cluster, no package manager. The build
is: cmake .. && cmake --build . -- nothing else.

### 4.3 Header-Only Architecture

All platform logic lives in .hpp files under server/include/genie/. This offers
several advantages for an investment management platform:

**Transparency** -- every algorithm is visible in source form. Users can read,
audit, and understand the exact implementation of their risk calculations.

**Flexibility** -- header-only code can be included selectively. A team that needs
only the risk analytics modules can include just those headers.

**Simplicity** -- the build system compiles exactly one translation unit
(src/main.cpp) that includes everything needed.

### 4.4 C++20 as the Foundation

C++20 was chosen deliberately and its features are used throughout:

| Feature | Use in Metis Genie Platform |
|---|---|
| std::span | Zero-copy buffer views in data processing |
| Concepts | Constrained template interfaces |
| std::jthread | Automatic thread lifecycle management |
| Designated initializers | Readable struct initialization |
| constexpr improvements | Compile-time route and config validation |
| Ranges | Algorithm composition in analytics |
| std::atomic enhancements | Lock-free statistics counters |
| Three-way comparison | Ordered comparisons on financial types |

C++20 eliminates the need for many external libraries. std::filesystem replaces
Boost.Filesystem. std::chrono provides full date-time arithmetic.

### 4.5 Cross-Platform by Design

Every file compiles on three operating systems using four compilers without
modification:

| OS | Compiler | Notes |
|---|---|---|
| Windows 11/10 | GCC 13 via MinGW-w64 | Primary development environment |
| Windows 11/10 | MSVC 2022 | Full support |
| Linux (Ubuntu 22.04+) | GCC 13 | Supported |
| Linux | Clang 16 | Supported |
| macOS 13+ | Apple Clang 15 | Supported |

Platform differences are isolated in http_server.hpp (socket APIs),
version.hpp (platform macros), and main.cpp (signal handling).
All other code is platform-neutral C++20.

### 4.6 PSON Configuration -- All Parameters Externalized

PSON (Permissive Structured Object Notation) is JSON extended with:

1. Single-line comments beginning with //
2. Trailing commas before } or ] for easy parameter editing

PSON files are parsed by the Config class in core/config.hpp, which strips
comments and trailing commas before parsing. Every tunable parameter lives in
config.pson -- no hardcoded values in source code:

- Server networking (port, host, timeouts, thread count)
- Authentication (session lifetime, lockout policy)
- Response caching (enabled, per-endpoint TTLs, max entries) -- v5.5.8
- Request validation (enabled, max body size) -- v5.5.8
- Graceful shutdown (drain timeout) -- v5.5.8
- Risk parameters (VaR confidence, Monte Carlo simulations)
- Portfolio settings (base currency, tax lot method, benchmark)
- GPU compute backend selection (for future implementation)
- Container and Kubernetes configuration (for future implementation)
- All external API keys (market data providers, brokers, notifications)

The configuration file is self-documenting: every parameter has an inline
// comment explaining its purpose, valid values, and where to obtain any
required API keys.

### 4.7 Security-First Development

Financial systems handle sensitive data and execute real-money transactions.
Security is built in at every layer:

**Authentication:** Bearer token sessions with configurable expiry. Login rate
limiting with configurable lockout (default: 5 attempts, 15 minutes). Persistent
user store with bcrypt-style password hashing.

**Audit logging:** Immutable audit trail for all authenticated actions stored in
a separate SQLite3 database with structured fields for regulatory reporting.

**Input validation (v5.5.8):** All POST/PUT requests are validated for Content-Type
and JSON structure before reaching route handlers. Oversized bodies are rejected
with structured error responses.

**Structured errors (v5.5.8):** All error responses include a unique trace ID
(ErrorHandler::build()), enabling correlation of client-reported errors with server
log entries without exposing internal implementation details.

**HTTP security headers:** Every response includes X-Content-Type-Options,
X-Frame-Options, Referrer-Policy, Permissions-Policy, and a restrictive
Content-Security-Policy.

**Rate limiting:** Configurable request rate limits per authenticated session with
X-RateLimit-Remaining response headers.

### 4.8 Extensibility and Future-Proofing

The platform is designed to be extended without modifying existing code:

**Route registration** -- new endpoints are added by calling api.get(), api.post(),
etc. before configure_defaults(). Routes are matched in registration order.

**Middleware** -- cross-cutting concerns (logging, authentication, metrics) are
added via api.use(). The middleware chain runs before route dispatch.

**Module directories** -- new financial domain modules go in their own directory
under server/include/genie/. Each module is a standalone .hpp file.

**GPU/K8s/Container** -- stub headers are present for all three platform extension
areas. Native implementations slot in without architectural changes.

---

## 5. Metis Genie Platform Architecture

### 5.1 Module Organization

The server modules are organized into 17 directories by financial domain:

| Directory | Content |
|---|---|
| core/ | PSON config, logging, crypto, validation, thread pool, HTTP client |
| net/ | HTTP server, REST API, WebSocket, response cache, error handler |
| portfolio/ | Positions, tax lots, rebalancing, IBOR, cash management |
| risk/ | VaR, Monte Carlo, stress testing, factor models, tail risk |
| analytics/ | Performance attribution, backtesting, ML alpha, regime detection |
| trading/ | Order management, FIX engine, execution analytics, settlement |
| market/ | Market data feeds, pricing, corporate actions, symbol master |
| compliance/ | Pre-trade compliance, ESG scoring, regulatory reporting |
| reporting/ | PDF/CSV/XLSX generation, dashboards, scheduled reports |
| assets/ | Equities, fixed income, derivatives, FX, private assets |
| persistence/ | Time-series store, event store, data archival |
| security/ | Session management, API key management, 2FA |
| tax/ | Tax lot optimization, wash sale detection, HIFO/FIFO/LIFO |
| performance/ | Benchmark tracking, performance engine, job processing |
| ops/ | Health monitoring, backup, deployment automation |
| ux/ | Dashboard, interactive charts, what-if scenarios |

Total: 266 header files across 17 module directories (+10 v5.5.8 feature headers).

### 5.2 REST API Design

The REST API follows standard HTTP conventions:

- All endpoints are under /api/v1/
- Authentication via Authorization: Bearer <token> header
- Responses are JSON with Content-Type: application/json; charset=utf-8
- Errors include error, code, trace_id, timestamp, and path fields (v5.5.8)
- Rate limiting enforced with X-RateLimit-Remaining response header
- Cache hits indicated by X-Cache: HIT response header (v5.5.8)
- All responses include X-Request-Id for end-to-end tracing

Total: 144 REST endpoints covering all financial domains.
Client coverage: All 64 HTML pages have active REST API connections.

### 5.3 Client-Server Architecture

```
Browser (client/ -- WebStorm)
  HTTP/1.1 with keep-alive + fetch() to /api/v1/*
  WebSocket port 8081 for live data push
         |
HttpServer (net/http_server.hpp)
  Multi-threaded (configurable workers)
  Static file serving from client/ directory
  Request queuing with backpressure
         |
RestApi (net/rest_api.hpp)
  Graceful shutdown guard -- 503 if draining (v5.5.8)
  Request validation -- Content-Type + JSON check (v5.5.8)
  Response cache -- GET cache with per-endpoint TTL (v5.5.8)
  Rate limiter -- per-token request counting
  Bearer token authentication -- session validation
  Route dispatch -- 144 routes
         |
Domain Modules (include/genie/*/*.hpp)
  Portfolio, Risk, Trading, Compliance, Reporting, ...
         |
SQLite3 (third_party/sqlite3/)
  Users DB, Audit DB, Main DB, Prices DB
```

### 5.4 Data Flow and Processing

A typical API request flows as follows:

1. HttpServer accepts TCP connection and reads HTTP/1.1 request
2. HttpServer passes method, path, headers, body to RestApi::handle()
3. RestApi checks graceful shutdown state -- returns 503 if draining
4. Request validator checks Content-Type and JSON body structure
5. Response cache is checked for GET requests -- returns cached body on HIT
6. Middleware chain runs
7. Rate limiter checks per-token request count
8. Route table is searched for matching method + path pattern
9. Route handler executes domain logic
10. Successful GET responses are stored in cache
11. Mutating requests (POST/PUT/DELETE) invalidate related cache entries
12. Structured error response generated on any exception
13. HttpServer serializes HTTP response and sends to client

### 5.5 Directory Structure

```
Metis_Genie_Platform-5.5.8/
|
+-- server/                    <- CLion project (C++20 server only)
|   +-- include/
|   |   +-- genie/             <- 266 header files, 17 module dirs
|   |       +-- core/          <- PSON config, logging, crypto, ...
|   |       +-- net/           <- HTTP server, REST API, cache, ...
|   |       +-- portfolio/     <- Portfolio management
|   |       +-- risk/          <- Risk analytics
|   |       +-- trading/       <- Trading and OMS
|   |       +-- ...            <- 12 more domain directories
|   +-- src/
|   |   +-- main.cpp           <- Single translation unit
|   +-- tests/                 <- Custom C++20 test framework (no GTest)
|   +-- examples/              <- Usage examples
|   +-- third_party/sqlite3/   <- Bundled SQLite3 amalgamation
|   +-- stub/openssl/          <- OpenSSL stubs (not required)
|   +-- CMakeLists.txt
|   +-- CMakePresets.json
|   +-- config.pson            <- ALL server parameters (PSON format)
|
+-- client/                    <- WebStorm project (browser UI only)
|   +-- *.html                 <- 64 HTML pages (all wired to REST API)
|   +-- css/                   <- 4 stylesheets
|   +-- js/                    <- 18 JavaScript modules
|   +-- img/                   <- SVG logo and icon
|
+-- docs/                      <- All .md and .txt files
|   +-- README.md
|   +-- BACKGROUND.md          <- This file
|   +-- CHANGELOG.md
|   +-- API.md
|   +-- ARCHITECTURE.md
|   +-- BUILD.md
|   +-- TODO.md
|   +-- VERSION.txt
|
+-- LICENSE                    <- MIT License
```

---

## 6. GPU, Kubernetes, and Container Strategy

### 6.1 GPU Compute Strategy

Certain investment management computations benefit significantly from GPU
acceleration: Monte Carlo VaR simulation, correlation matrix computation, stress
test scenario evaluation, and machine learning alpha model inference.

**Current state:** The compute.gpu_enabled parameter in config.pson controls GPU
usage. When false (the default), all computations use the CPU thread pool. The
gpu_backend parameter documents the intended target backend.

**Planned native C++20 implementation -- no Docker required:**

| Platform | API | Status |
|---|---|---|
| Windows | DirectCompute via D3D11 Compute Shaders | Planned (v6.x) |
| Windows / Linux | CUDA via NVAPI / libcuda | Planned (v6.x) |
| macOS | Metal Performance Shaders | Planned (v6.x) |
| Cross-platform | Vulkan Compute (any Vulkan 1.1 GPU) | Planned (v6.x) |
| Fallback | OpenCL (Intel/AMD/NVIDIA) | Planned (v6.x) |

All GPU implementations use native OS APIs directly -- no Docker, no NVIDIA
Container Runtime, no external SDK installation beyond vendor drivers.

The abstraction layer (server/include/genie/core/compute_device.hpp) defines the
interface. Implementations slot in without changing calling code.

Config.pson parameters:

    "compute": {
        "gpu_enabled": false,
        "gpu_backend": "CPU",     // CPU|CUDA|METAL|DIRECTCOMPUTE|VULKAN|OPENCL
        "gpu_fallback_to_cpu": true,
        "gpu_device_index": 0,
    }

### 6.2 Container Strategy

Container isolation enables running multiple isolated instances of the platform
on a single host for multi-tenant deployments.

**Planned native C++20 implementation -- no Docker daemon required:**

| Platform | Technology | Mechanism |
|---|---|---|
| Linux | cgroups v2 + namespaces | clone() / unshare() syscalls |
| Windows | Job Objects + Process Isolation | CreateJobObject() / AssignProcessToJobObject() |
| macOS | sandbox-exec + launchd | sandbox_init() profiles |

The stub header (server/include/genie/ops/container_orchestration.hpp) defines the
interface. The C++20 implementation uses OS-native APIs available on any standard
installation -- no Docker daemon, no containerd, no container registry required.

Config.pson parameters:

    "containers": {
        "enabled": false,
        "runtime": "NATIVE",
        "max_containers": 10,
        "container_memory_limit_mb": 512,
    }

### 6.3 Kubernetes Strategy

The Kubernetes API is HTTP/JSON. The existing HttpClient in
server/include/genie/core/http_client.hpp can speak to it directly with a service
account token. No kubectl binary, no Kubernetes SDK, no external library required.

**Planned operations:**

- GET /apis/apps/v1/namespaces/{ns}/deployments/{name} -- deployment status
- PATCH /apis/apps/v1/namespaces/{ns}/deployments/{name} -- scale replicas
- GET /api/v1/namespaces/{ns}/pods -- list pods
- GET /api/v1/namespaces/{ns}/pods/{name}/log -- stream pod logs

Config.pson parameters:

    "kubernetes": {
        "enabled": false,
        "api_url": "",
        "namespace": "default",
        "service_account_token": "",
        "deployment_name": "metis-genie-platform",
        "replica_count": 1,
    }

### 6.4 Implementation Roadmap

| Feature | Config Key | Status | Target |
|---|---|---|---|
| CPU thread pool | (always on) | Implemented | v5.x |
| Response cache (ApiCache) | api.cache_enabled | Implemented | v5.5.8 |
| Request validation | api.validation_enabled | Implemented | v5.5.8 |
| Graceful shutdown drain | api.graceful_shutdown_enabled | Implemented | v5.5.8 |
| GPU: DirectCompute (Windows) | compute.gpu_backend: DIRECTCOMPUTE | Stub | v6.x |
| GPU: CUDA (Linux/Windows) | compute.gpu_backend: CUDA | Stub | v6.x |
| GPU: Metal (macOS) | compute.gpu_backend: METAL | Stub | v6.x |
| GPU: Vulkan (cross-platform) | compute.gpu_backend: VULKAN | Stub | v6.x |
| Containers: Linux cgroups v2 | containers.enabled: true | Stub | v6.x |
| Containers: Windows Job Objects | containers.enabled: true | Stub | v6.x |
| Containers: macOS sandbox-exec | containers.enabled: true | Stub | v6.x |
| Kubernetes API client | kubernetes.enabled: true | Stub | v7.x |

---

## 7. Quality Standards and Testing

### 7.1 Code Quality Standards

Every file meets these standards:

- C++20 -- no C++17 fallbacks, no deprecated features
- Zero warnings -- -Wall -Wextra -Wpedantic clean on GCC, Clang, and MSVC
- [[nodiscard]] -- applied to all functions where ignoring the return value
  would likely indicate a programming error
- const-correctness -- const applied to all members and parameters not modified
- RAII -- all resources (sockets, file handles, locks) managed by RAII types
- No raw new/delete -- heap allocation through std::make_unique, std::make_shared
- American English -- consistent spelling throughout all identifiers, comments,
  and documentation

### 7.2 Testing Strategy

Metis Genie Platform uses a custom C++20 test framework. No GTest, no Catch2,
no Boost.Test, no external test runner.

| Test File | Coverage |
|---|---|
| test_genie.cpp | Core modules, PSON config loading, utility functions |
| test_integration.cpp | HTTP server lifecycle, API route dispatch |
| test_rest_endpoints.cpp | All 144 REST endpoints, auth, rate limiting, caching |
| test_tier3.cpp | Advanced analytics, trading, compliance |

Total: 330+ test cases.

The framework uses static_assert for compile-time checks and a simple
assert_equal / assert_true runtime framework writing TAP-compatible output
without external dependencies.

No Docker, no external services: unit tests run against in-memory state;
integration tests start a local HttpServer on a random port, make real HTTP
requests, and tear down cleanly.

### 7.3 Documentation Standards

All documentation lives in docs/ and meets these standards:

- Markdown -- compatible with GitHub rendering
- Version synchronized -- every document carries a version number matching
  VERSION.txt and the version.hpp constants
- Table of Contents -- all documents over 200 lines have a linked TOC
- American English -- consistent spelling and terminology

---

## 8. Project Evolution

### 8.1 Version History Summary

| Version | Key Additions |
|---|---|
| 2.18-2.24 | Five tiers: Trading, Analytics, Data, Advanced Trading, Polish |
| 3.0 | Client-server parity, GPU/K8s/Container architecture |
| 3.4 | Circuit breaker, rate limiting, telemetry, input sanitization |
| 4.4 | Crypto trading, message bus, DB migrations, OpenAPI, ADR |
| 4.5 | Result<T,E>, JSON logging, time-series store, pre-trade compliance |
| 4.6 | Config hot-reload, Prometheus metrics, FX engine, TCA, reports |
| 4.7 | Wash sale engine, tail risk, Brinson-Fachler, compliance calendar |
| 4.8 | Feature flags, ETL pipeline, alert manager, API versioning |
| 4.9 | Audit trail, benchmark manager, instrument master, session manager |
| 4.10 | Rebalance optimizer, correlation matrix, liquidity scorer |
| 4.11 | Position sizer, strategy backtester, trade journal, currency hedger |
| 4.13 | Performance attribution, trade allocator, cash manager |
| 5.0 | Full client-server parity -- 64 HTML pages, all endpoints wired |
| 5.1 | 16 new endpoints, config.json.template (100+ parameters) |
| 5.2 | PSON config, response caching, validation, structured errors, graceful shutdown, server/client split |
| 5.3-5.5 | Logging unification, CSS grid fixes, server logs modal, KPI dashboard improvements, PSON comment parser fix |

### 8.2 Lessons Learned

**On zero dependencies:** The discipline of zero external dependencies forces
creative solutions that end up being simpler and more understandable than their
third-party equivalents. Writing a PSON parser took 200 lines of C++20 and is
completely transparent. Using a JSON library would have added a dependency with
its own build requirements, API surface, and failure modes.

**On header-only architecture:** The single-translation-unit build was initially
a convenience. It became an architectural asset: the entire platform compiles in
one pass with consistent optimization and guaranteed ODR compliance.

**On PSON over JSON:** Configuration files with comments are dramatically easier
to maintain than pure JSON. The investment in building a PSON parser pays
dividends every time a developer opens config.pson and immediately understands
what each parameter does.

**On directory separation:** Separating server/ (CLion, C++20) from client/
(WebStorm, HTML/JS) made the project significantly more approachable. Each
directory is a coherent project in its own IDE.

**On structured errors:** Bare {"error": "message"} responses are unhelpful
in production. Structured errors with trace IDs enable correlating a user-
reported error with server logs in seconds.

### 8.3 Future Direction

**Implemented in v5.x:**
- Server-Sent Events (SSE) — `net/sse_server.hpp`, SseChannel, EventSource-compatible
  poll bridge at `/api/v1/stream/poll`. True persistent-connection streaming is a
  planned refinement (requires HttpServer streaming callback extension).
- Cross-platform file watcher — `core/file_watcher.hpp` with inotify (Linux),
  ReadDirectoryChangesW (Windows), kqueue (macOS) and poll fallback. Wired into
  `main.cpp`; config hot-reload triggers on `config.pson` change.
- Binary serialization — `core/binary_serializer.hpp`, METI format with CRC-32,
  little-endian fixed fields, ~10x throughput vs JSON for large datasets.
- Prometheus `/metrics` endpoint — `net/prometheus_endpoint.hpp`, PrometheusRegistry
  singleton, text exposition format v0.0.4 at `/metrics` and `/api/v1/metrics/prometheus`.
- Compile-time route table — `net/route_table.hpp`, constexpr RouteDescriptor array
  with RouteFlags bitmask. Included in `rest_api.hpp`.

**Stub infrastructure (v6.x) — headers present, activation pending:**
- GPU compute — `core/compute_device.hpp`; platform dispatch stubs for
  DirectCompute, CUDA, Metal, Vulkan, OpenCL. Controlled by `compute.gpu_backend`
  in config.pson.
- Native container isolation — `ops/container_orchestration.hpp`; stubs for
  Linux cgroups v2, Windows Job Objects, macOS sandbox-exec.
- HTTP/2 — `net/http2_server.hpp`; Http2Adapter with Alt-Svc advertisement active,
  frame types defined. HPACK and stream multiplexing are v6.x targets.
- Kubernetes API client — `ops/kubernetes_client.hpp`; uses HttpClient to speak
  the Kubernetes HTTP/JSON API with service account token auth.

**Long-term (v7.x) — substantial headers, full activation pending:**
- Full FIX 4.4/5.0 engine — `trading/fix_engine.hpp` (1,235 lines): session
  state machine, message parser, FIX tag dictionary. REST endpoints wired.
  Full certification-grade implementation is a v7.x target.
- Multi-node distributed architecture — `core/message_bus.hpp` (772 lines):
  publish/subscribe, topic routing, dead-letter queue. Not yet wired to
  inter-process transport for multi-node deployment.
- WebAssembly client compilation — `net/wasm_client.hpp`; COOP/COEP headers
  active in HTTP responses. Emscripten compilation of the client is a v7.x target.

The guiding constraint for all future development remains unchanged:
C++20, zero external dependencies, single executable, cross-platform.

---

## License

MIT License

Copyright (c) 2026 Bennie Shearer (Retired)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

DISCLAIMER: This software provides Enterprise Investment Management Platform
Prototype capabilities. Users are responsible for proper configuration, security
testing, and compliance with applicable regulations and licensing requirements.

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

---

*Metis Genie Platform v5.5.11 — BACKGROUND.md*
