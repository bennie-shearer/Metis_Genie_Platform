# Metis Genie Platform v5.5.8 - TODO and Recommended Improvements

## Completed in v5.3.2 (Near-Term v5.x)
- [x] Server-Sent Events (SSE) -- SseChannel + EventSource-compatible poll bridge
- [x] Cross-platform file watcher -- inotify / ReadDirectoryChangesW / kqueue + poll
- [x] Binary serialization -- METI format, CRC-32, little-endian, ~10x vs JSON
- [x] Prometheus /metrics endpoint -- text v0.0.4, metis_genie_* namespace
- [x] Compile-time route table -- RouteDescriptor with RouteFlags bitmask
- [x] gzip/deflate response compression -- pure C++20 + optional zlib path

## Completed in v5.3.2 (Stub Infrastructure for v6.x/v7.x)
- [x] HTTP/2 stub -- frame types, Alt-Svc advertisement, Http2Adapter
- [x] Kubernetes API client stub -- K8sClient with get/list/scale methods
- [x] FIX 4.4/5.0 session state machine + message parser
- [x] WASM client stub -- COOP/COEP headers active

## Completed in v5.2.0
- [x] PSON configuration -- all parameters in config.pson with // comments
- [x] config.hpp upgraded with nested object parser and PSON support
- [x] Response caching layer (ApiCache) wired into RestApi::handle()
- [x] Request validation middleware (Content-Type + JSON body check)
- [x] Structured error responses with error codes and trace IDs
- [x] Graceful shutdown with in-flight request draining
- [x] Directory restructure: server/ (CLion) and client/ (WebStorm)

## Completed in v5.1.1
- [x] Full client-server endpoint coverage (130+ routes)
- [x] All 61 HTML pages connected to REST API
- [x] Comprehensive config parameters and documentation

## Next Up (v5.4.x)
### High Priority
- [ ] True SSE streaming: extend HttpServer with streaming response callbacks
      so SseChannel can write directly to open TCP connections (no poll required)
- [ ] File watcher wired to RestApi: trigger cache invalidation + config reload
      automatically when config.pson changes on disk
- [ ] Prometheus metrics populated from real server stats (HttpServer::stats())
      rather than fixed demo values -- wire PrometheusRegistry to ServerStats
- [ ] RouteTable integrated into RestApi dispatch: use constexpr flags for
      cache bypass (SSE_STREAM), public endpoints (no auth check overhead)

### Medium Priority
- [ ] Memory-mapped file I/O (mmap/MapViewOfFile) for large CSV imports
- [ ] ETag-based conditional GET wired to all API endpoints (not just static)
- [ ] OpenAPI 3.1 spec generation from RouteTable metadata at /api/openapi.json
- [ ] Connection pool for outgoing HTTP (market data, broker API reuse)
- [ ] HTTP pipelining improvements in HttpServer

### Low Priority
- [ ] Log rotation without restart (async, cross-platform)
- [ ] Multi-line PSON values (currently only single-line per key)

## Medium-Term (v6.x) -- Stub Headers Present
### GPU Compute (No Docker)
- [ ] DirectCompute via D3D11 Compute Shaders (Windows)
- [ ] CUDA via libcuda / NVAPI (Linux, Windows)
- [ ] Metal Performance Shaders (macOS, Apple Silicon + AMD)
- [ ] Vulkan Compute (cross-platform, any Vulkan 1.1 GPU)
- [ ] OpenCL fallback (Intel/AMD/NVIDIA)
- [ ] Monte Carlo VaR on GPU (target: 10,000 paths < 10ms)
- [ ] Correlation matrix on GPU (N=500 assets < 5ms)

### Native Container Isolation (No Docker Daemon)
- [ ] Linux: cgroups v2 + namespaces via clone()/unshare() syscalls
- [ ] Windows: Job Objects + AssignProcessToJobObject()
- [ ] macOS: sandbox-exec + launchd plist generation

### HTTP/2 Full Implementation
- [ ] HPACK header compression (RFC 7541)
- [ ] Stream multiplexing + flow control (RFC 9113)
- [ ] Server push (PUSH_PROMISE) for SSE replacement
- [ ] TLS: Schannel (Win), OpenSSL stub (Linux), SecureTransport (macOS)

## Long-Term (v7.x) -- Architecture Defined
### Kubernetes API Client
- [ ] GET deployment status via HttpClient
- [ ] PATCH deployment replicas
- [ ] LIST pods with field selectors
- [ ] Stream pod logs
- [ ] Service account token refresh

### Full FIX 4.4/5.0 Engine
- [ ] TCP session over TLS (platform native)
- [ ] Persistent session store (SQLite3) for replay
- [ ] FIXT 1.1 transport for FIX 5.0
- [ ] Multicast market data (FIX/FAST)

### Multi-Node Distributed Architecture
- [ ] Message bus federation across network
- [ ] Consistent hashing for request routing
- [ ] Shared SQLite3 via WAL replication

### WebAssembly Client
- [ ] Emscripten compilation of VaR and scenario analysis modules
- [ ] In-browser computation without server round-trips
- [ ] Service worker for offline capability

## Testing (No GTest, No External Runners)
- [ ] Load test harness -- custom C++20 concurrent request simulator
- [ ] Property-based tests for financial calculations (VaR monotonicity)
- [ ] Snapshot tests for Prometheus output format
- [ ] Fuzz tests for PSON parser and binary deserializer
- [ ] Cross-platform CI script (bash + PowerShell)

*Metis Genie Platform v5.3.2 -- TODO.md*
