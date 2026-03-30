## [5.5.8] - 2026-03-30

### Fixed
- **client/js/connection.js** — Rewrote connection lifecycle to fix three bugs:

  1. **Reconnect happening when it should not.** `startHealthCheck()` previously
     called `check()` immediately on startup. On session restore via `init()`,
     `connected` was set to `true` before any health check confirmed the server
     was reachable, so a transient failure at exactly that moment triggered
     `startReconnect()` unnecessarily. Fixed: `init()` now leaves `connected`
     as `false` and uses `startReconnect()` to verify the server before declaring
     connected, avoiding any false reconnect trigger.

  2. **Health check and reconnect running concurrently.** The 30-second
     `setInterval` health check continued firing while `attemptReconnect()` was
     also running, so a health check success could call `stopReconnect()` while
     reconnect was mid-flight, or a health check failure could call
     `startReconnect()` again immediately after reconnect had just succeeded.
     Fixed: `startHealthCheck()` no longer calls `check()` on the first tick.
     When a health check fails it calls `stopHealthCheck()` before starting
     reconnect. When reconnect succeeds it calls `startHealthCheck()`. The two
     loops are mutually exclusive at all times.

  3. **Retry count and delay wrong.** Max retries reduced from 10 to 5 (covers
     2+4+8+16+30 = ~60 seconds of retrying before giving up, vs the previous
     5+ minutes). Max delay reduced from 60s to 30s so the system recovers
     faster when the server comes back. The final "max retries reached" path now
     goes directly to `showServerStopped()` rather than making an additional
     fetch attempt, since `_attemptReconnect()` already made the final check.

## [5.5.7] - 2026-03-30

### Fixed
- **client/js/app.js** — `navigate()` now guards against empty/undefined page
  argument with an early return. The `sidebarLogsBtn` nav item had no `data-page`
  attribute, so clicking it passed `undefined` to `navigate()`, logging
  "[App] Navigate: undefined" on every click.
- **client/index.html** — `sidebarLogsBtn` given `data-page=""` so the nav
  delegate event handler short-circuits correctly via the new `navigate()` guard.
- **client/index.html** — Server Logs tab now checks for a valid auth token before
  making the `/api/v1/logs` fetch. If no token is present it shows "Not authenticated"
  in the modal instead of making an unauthenticated request that logs a 401 WARN
  in the server log. If the token is present but stale, shows "session expired".

## [5.5.6] - 2026-03-30

### Fixed
- **net/rest_api.hpp** — `session_timeout_minutes` was hardcoded to `60` in the
  `/api/v1/config` and `/api/v1/security/overview` endpoint responses. Added
  `session_timeout_minutes_` member (default 480) and `set_session_timeout()` setter.
  Both endpoints now return the configured value.
- **net/rest_api.hpp** — Demo user credentials (`admin/demo`, `user/user`,
  `trader/trade`) were hardcoded in `enable_persistent_storage()`. Added
  `demo_admin_user_`, `demo_admin_pass_`, etc. members and `set_demo_credentials()`
  setter. Credentials now read from `config.pson` `auth.demo_*` keys with the
  original values as fallback defaults.
- **src/main.cpp** — `auth.session_timeout_minutes` was read from config.pson and
  validated but never passed to `RestApi`. Now calls `api_.set_session_timeout()` and
  `api_.set_demo_credentials()` from the configured values.
- **server/config.pson** — Added `auth.demo_admin_user`, `auth.demo_admin_pass`,
  `auth.demo_trader_user`, `auth.demo_trader_pass`, `auth.demo_analyst_user`,
  `auth.demo_analyst_pass` keys so demo credentials can be changed without recompiling.

## [5.5.5] - 2026-03-30

### Fixed
- **net/rest_api.hpp** — `is_public_route` was computed but not yet used,
  producing a `-Wunused-variable` warning on every TU. Suppressed with
  `[[maybe_unused]]`; the variable is retained for the planned auth-bypass
  optimization where PUBLIC routes skip the `require_auth()` call.

## [5.5.4] - 2026-03-30

### Fixed (build errors from v5.5.3)
- **net/sse_server.hpp** — Rewrote cleanly from scratch. Three issues fixed:
  (1) Literal newlines inside C++ string literals (Python `\n` written as raw
  bytes, not escape sequences) caused "missing terminating quote" errors on every
  TU that included the header. (2) `make_sse_streaming_handler()` referenced
  `SseChannel` before it was declared in the same file. (3) Duplicate
  `namespace genie::net {` blocks from failed incremental edits.
- **net/http_server.hpp** — `using StreamingHandler = std::function<...>` was
  defined in the private section at line 734, but `set_streaming_handler(StreamingHandler)`
  was in the public section at line 398. The compiler saw an undeclared type.
  Moved the `using` declaration to the public section before the setter.
- **net/rest_api.hpp** — Anonymous namespace containing `cx_flags(Method, path)`
  was inserted before the `enum class Method` declaration (line 65 vs line 245).
  Moved the anonymous namespace to after the `Method` enum.

## [5.5.3] - 2026-03-30

### Added — High Priority Items (all four)

**True SSE Streaming (`net/http_server.hpp`, `net/sse_server.hpp`, `src/main.cpp`)**
- Added `StreamingHandler` type and `streaming_handler_` member to `HttpServer`.
  When set, the handler is called before the normal request/response cycle. If it
  returns `true`, the socket is taken over and keep-alive is terminated for that
  worker thread — the connection becomes a long-lived SSE stream.
- Added `make_sse_streaming_handler()` free function in `sse_server.hpp`. Intercepts
  `/api/v1/stream/<channel>` requests, sends SSE response headers directly on the
  socket, registers the client in `SseChannel`, and blocks in a `select()` keep-alive
  loop until the client disconnects. `broadcast()` now pushes events to both the poll
  queue and all live streaming sockets simultaneously.
- Wired in `main.cpp` via `server.set_streaming_handler(make_sse_streaming_handler())`.

**File Watcher → Cache Invalidation + Config Reload (`src/main.cpp`)**
- `FileWatcher` is now started in `cmd_serve()` watching `config.pson`. On change:
  `config().load_from_file()` reloads all 240 parameters, then `api_.clear_cache()`
  flushes the response cache so stale data is not served after a config change.

**Prometheus from Live Server Stats (`net/rest_api.hpp`, `src/main.cpp`)**
- Added `StatsProvider` injectable callback and `set_stats_provider()` to `RestApi`.
  Added `refresh_prometheus_stats()` helper that calls the provider and feeds live
  `HttpServer::ServerStats` values (total requests, active connections, bytes sent/recv,
  4xx/5xx counts, uptime, cache hits/misses) into `PrometheusRegistry` before each
  `/metrics` response. Wired in `main.cpp` via a lambda capturing `&server`.

**RouteTable Flags in Dispatch (`net/rest_api.hpp`)**
- Added `cx_route_table()` (a `constexpr`-initialized `RouteTable<32>`) in an
  anonymous namespace in `rest_api.hpp`. Defines PUBLIC, SSE_STREAM, and CACHEABLE
  flags for well-known endpoints. `handle()` now calls `cx_flags(req.method, req.path)`
  before the cache check; SSE_STREAM routes bypass the response cache entirely.

## [5.5.2] - 2026-03-29

### Changed
- **docs/BACKGROUND.md** — Updated Near-term/Long-term roadmap section to reflect
  actual implementation status. Moved SSE, file watcher, binary serializer, Prometheus,
  and constexpr route table from "future" to "Implemented in v5.x" with accurate
  descriptions of each header, wiring status, and any remaining work. Added accurate
  status for FIX engine (substantial header, full certification pending), message bus
  (header present, inter-process transport pending), and WebAssembly (COOP/COEP active,
  Emscripten compilation pending).
- **docs/ROADMAP_STATUS.md** — Bumped version header to 5.5.2. Updated feature status
  table: marked SSE poll bridge, binary serializer, Prometheus, constexpr route table,
  FIX session/parser, WASM stub, log formatting, server logs modal, PSON fix as
  Complete. Updated HTML page count from 61 to 64.
- **docs/TODO.md** — Updated version header to 5.5.2.

## [5.5.1] - 2026-03-29

### Changed
- **README.md** — Complete rewrite for GitHub publication: clean overview,
  quick-start with platform table, accurate project layout tree, endpoint
  domain table, architecture diagram, configuration example, design principles,
  security summary, testing summary, acknowledgments.
- **docs/BACKGROUND.md** — Fixed all stale v5.3.2 version references to v5.5.1.
  Updated all stale counts (130+ → 144 routes, 61 → 64 pages, removed stale
  header count annotation). Version history table extended through v5.5.1.
- **.gitignore** — Added `bin/` and `server/cmake-build-*/bin/web/` (CMake-synced
  static file output directories should not be committed to the repository).

## [5.5.0] - 2026-03-29

### Changed
- Version bumped from v5.4.9 to v5.5.0. Previous packaging reused the 5.4.9
  label after making changes (two-tab logs modal, README fix, cache-buster fix)
  without incrementing. All version references verified consistent across
  version.hpp, VERSION.txt, config.pson (server + client), README.md, all HTML
  files, and all script/CSS cache-bust parameters.

## [5.4.9] - 2026-03-29

### Fixed
- **server/src/main.cpp** — Log output had two inconsistent formats: startup lines from
  `StartupLogger` used `[timestamp] [LEVEL] message` while subsystem lines from
  `::genie::logger()` used `timestamp [LEVEL ] [component] message`. Fixed by adding a
  `delegate_` flag and `delegate_to_main_logger()` method to `StartupLogger`. Once the
  main logger has its file sink configured, all subsequent `SLOG()` calls are routed
  through `::genie::logger()` with component `"Main"`, producing a single consistent
  format for the entire log output.

## [5.4.8] - 2026-03-29

### Fixed
- **client/index.html** — Logs modal was not appearing when the button was clicked.
  The `openModal()` function set `bg.style.display = "flex"` but all other modals in
  the app use `bg.classList.add("show")` to become visible (the CSS uses
  `.modal-bg.show { opacity:1; visibility:visible }`). Setting `style.display` had
  no visual effect. Fixed to use `classList.add("show")` / `classList.remove("show")`
  matching the existing modal pattern. Also removed the redundant `style="display:none"`
  inline attribute from the modal bg element since the CSS already hides it by default.

## [5.4.7] - 2026-03-29

### Fixed
- **client/index.html** — Removed the server-side `/api/v1/logs` fetch from the logs
  modal entirely. Despite multiple guards (`modalOpen` flag, `display` checks), a fetch
  was firing on every page load causing a persistent 401. Root cause was never isolated
  through static analysis. The fix eliminates the problem at the source: the modal now
  reads directly from `U.log._entries` (the client-side log buffer already populated by
  `utils.js`) instead of fetching from the server. This gives instant results, requires
  no authentication, and shows the same session logs that appear in the browser console.
  The `/api/v1/logs` server endpoint remains available for direct API use.

## [5.4.6] - 2026-03-29

### Fixed
- **client/index.html** — Replaced all DOM-inspection guards in the logs modal
  (`bg.style.display === "none"`, `getComputedStyle`) with an explicit boolean
  flag `modalOpen`. The flag starts `false`, is set `true` only when `open()` is
  called by the user, and reset to `false` when `closeModal()` is called.
  `fetchLogs()` returns immediately at the top if `modalOpen` is `false`.
  This eliminates all possible spurious fetches on page load regardless of what
  triggers the function, since the cause of the page-load call could not be
  isolated through static analysis alone.

## [5.4.5] - 2026-03-29

### Fixed
- **client/index.html** — Added hard guard at the top of `fetchLogs()`: returns
  immediately if the modal is not visible, regardless of what triggered the call.
  Prevents any spurious network request to `/api/v1/logs` on page load.
- **Version bump** — v5.4.4 was repackaged without a version increment after an
  additional fix. Corrected to v5.4.5.

## [5.4.4] - 2026-03-29

### Fixed
- **client/index.html** — All 60 HTML files had `?v=5.3.1` cache-bust parameters on
  `<script>` and `<link>` tags. The browser served the old cached versions of every JS
  and CSS file regardless of server changes, meaning fixes to config.js and other scripts
  were never loaded. All version parameters updated to `v=5.4.4`.
- **client/index.html** — The logs modal level and count `<select>` change event listeners
  were firing `fetchLogs()` on page load during browser session restore (Chrome/Edge restore
  select values and dispatch change events). Guarded both handlers with a visibility check
  so they only fetch when the modal is actually open.

## [5.4.3] - 2026-03-29

### Fixed
- **client/js/config.js** — PSON comment stripper used the regex `//[^\n]*` which
  also matched `//` inside quoted string values (e.g. `"http://localhost:8080"`),
  truncating the URL at the `//` and leaving a bare newline inside the JSON string —
  causing a JSON parse error on every startup. Fixed with a regex that matches quoted
  strings first and passes them through unchanged, only stripping `//` that appear
  outside of quotes.
- **client/index.html** — Logs modal token retrieval simplified to read directly from
  `sessionStorage.getItem("genie_token")`, which `Connection` writes on every
  login and session restore. This is more reliable than calling `Connection.authHeaders()`
  from an IIFE that may execute before `Connection.init()` completes.

## [5.4.2] - 2026-03-29

### Fixed
- **client/index.html** — Logs modal was sending unauthenticated requests to
  `/api/v1/logs`, causing 401 errors. The previous code called `App.getToken()`
  which does not exist. Fixed to use `Connection.authHeaders()` (the same mechanism
  used by all other API calls in the app), with a fallback to `sessionStorage`
  for the token in case `Connection` is not yet initialized.

## [5.4.1] - 2026-03-29

### Fixed
- **client/css/main.css** — KPI row `minmax` minimum corrected to `280px`. The previous
  value of `200px` caused `auto-fill` to create 6 columns at ~1266px content width
  (floor(1266/200)=6), each only 211px wide — too narrow for the card content (icon 42px
  + data + canvas 64px + gaps ≈ 240px minimum), causing text and charts to overflow and
  overlap. At `280px`, the grid produces 4 columns at ~1200px, 3 at ~900px, 2 at ~600px.
- **client/css/main.css** — Added `max-width:600px` to `.header-center` (market strip)
  so it cannot grow wide enough to crowd the header toolbar buttons off screen.
- **client/index.html** — Added "Server Logs" nav item to the System section of the
  sidebar. This is now the primary entry point for the logs modal; the header toolbar
  button remains as a secondary shortcut.

## [5.4.0] - 2026-03-29

### Fixed
- **client/css/main.css** — KPI row grid minimum changed from `180px` to
  `min(100%, 200px)` so the browser uses the lesser of the container width or 200px,
  preventing a single-column blowout on very narrow panes while still fitting 4 cards
  at typical widths.
- **client/index.html** — KPI spark canvas `width` attribute reduced from 80px to 64px.
  The hardcoded HTML attribute sets the intrinsic minimum width of the canvas element,
  which was forcing each KPI card wider than the CSS grid minimum, causing the 4th card
  to overflow and clip.
- **client/css/main.css** — Header toolbar buttons (`.header-right`) were being clipped
  at narrower viewports because `.header-right` had `gap:8px` and `flex-shrink:0` but
  the market strip (`.header-center`) had `flex:1` without `min-width:0`. Reduced
  header padding from 16px to 12px, button gap from 8px to 4px, added `min-width:0`
  to `.header-center`, and `overflow:visible` to `.header` so the logs button and all
  toolbar icons are always reachable.

## [5.3.9] - 2026-03-28

### Added
- **logging.hpp** — `MemorySink` class: thread-safe in-memory ring buffer (500 entries)
  that captures every log entry. Registered automatically in the `Logger` singleton.
  Exposes `recent(n, min_level)` and `to_json(n, min_level)` for API retrieval.
  Accessible via `::genie::logger().memory()`.
- **rest_api.hpp** — `GET /api/v1/logs` endpoint: returns the last N server log entries
  from the MemorySink as JSON. Supports `?n=` (count, default 100) and `?level=`
  (DEBUG/INFO/WARN/ERROR filter). Requires authentication.
- **client/index.html** — Logs button added to the header toolbar (document icon, between
  Refresh and Theme). Clicking opens a modal showing server logs newest-first with
  level-colour coding (DEBUG grey, INFO blue, WARN amber, ERROR/FATAL red), a level
  filter, a count filter, and a Refresh button. Closes on Escape or click-outside.

## [5.3.8] - 2026-03-28

### Fixed
- **api_config_loader.hpp** — Missing `#include "logging.hpp"` caused `::genie::logger`
  and `::genie::LogLevel` to be undeclared when the header was included without `genie.hpp`
  already in scope (e.g. from `test_integration.cpp`).
- **smtp_client.hpp** — Same missing `#include "logging.hpp"`. Also fixed
  `std::to_string(code)` where `code` is `const std::string&` -- `to_string` takes a
  numeric type; removed the erroneous wrapper.
- **live_valuation.hpp**, **ip_whitelist.hpp** — Added `#include "../core/logging.hpp"`
  to make the logger dependency explicit rather than relying on transitive inclusion.
- **client/css/main.css** — KPI card `minmax` minimum reduced from `220px` to `180px`
  and stat-row from `200px` to `160px`. At 220px the four cards still overflowed the
  content area at common window widths with the sidebar open.

## [5.3.7] - 2026-03-28

### Fixed
- **api_config_loader.hpp** — Two `std::cerr` / `std::cout` calls bypassed the logger
  entirely, printing raw unformatted text to stdout mid-startup (`[CONFIG] No api_keys
  section found`). Replaced with `::genie::logger()` calls. The missing `api_keys` section
  is now logged at DEBUG level (it is normal when no market data providers are configured)
  rather than printing a confusing WARNING-like message at INFO level.
- **ip_whitelist.hpp** — `std::cout << "[AUDIT]"` replaced with `::genie::logger()` call.
- **live_valuation.hpp** — Two `std::cerr << "[LiveValuation]"` calls replaced with
  `::genie::logger()` WARN calls.
- **smtp_client.hpp** — Fourteen `std::cerr << "[SMTP]"` calls replaced with
  `::genie::logger()` ERROR calls with proper string construction.

## [5.3.6] - 2026-03-28

### Fixed
- **client/css/main.css** — KPI row, stat row, settings grid, metrics, var-grid, and port-grid
  all used `grid-template-columns: repeat(N, 1fr)` with fixed column counts. Because the
  breakpoints were based on viewport width (not content-area width), the sidebar (~240px)
  caused grids to overflow and clip at intermediate window sizes. All replaced with
  `repeat(auto-fill, minmax(Xpx, 1fr))` so columns reflow based on their actual container
  width with no media query involvement.
- **client/css/main.css** — Removed the now-redundant explicit column-count overrides from
  the 1024px, 768px, 480px, and landscape breakpoints (they had been fighting the new
  auto-fill behavior and are no longer needed).
- **client/css/operations.css** — Same fixed-column grid pattern fixed across status-kpis,
  health-cards, error-summary, job-stats, cache-stats, status-grid, deploy-info, and
  backup-config. Redundant media query overrides removed.
- **client/*.html** — All HTML files still referenced v5.3.1 in `<title>`, meta description,
  login screen, sidebar brand badge, and status bar. Updated to v5.3.6.

## [5.3.5] - 2026-03-28

### Fixed
- **config.pson** — `server.static_dir` was `"../client"` which resolved to
  `cmake-build-debug/client/` (does not exist). CMake's `sync_web_assets` target has
  always copied `client/` to `bin/web/`; the config now correctly says `"web"` to match.
  This was the sole reason the browser received `{"error":"Endpoint not found: GET /"}`.
- **CMakeLists.txt** — Install rule referenced `web/` (a directory that does not exist in
  the source tree). Fixed to reference `../client/` (the actual client source directory).
- **main.cpp** — `app_version` fallback was hardcoded `"5.3.1"` instead of
  `genie::VERSION_STRING`. The startup log now always shows the real build version.
- **main.cpp** — Static dir warning now explains the remediation step rather than just
  stating the directory was not found.

## [5.3.4] - 2026-03-28

### Fixed
- **rest_api.hpp** — `#include "sse_server.hpp"` was placed at line 318, inside the
  `namespace genie::net { }` block. Because `sse_server.hpp` opens its own
  `namespace genie::net { }`, this created a doubly-nested `genie::net::genie::net` scope,
  which broke every SSE type (`SseChannel`, `SseEvent`) and every `genie::logger()` call
  inside the class. Fixed by closing the namespace before the include and reopening it
  immediately after, so `sse_server.hpp`'s own namespace declaration lands correctly.
- **rest_api.hpp** — All `genie::logger()` calls inside `namespace genie::net` were resolved
  as `genie::net::genie::logger` (a non-existent nested path). Changed to `::genie::logger()`
  (fully qualified from the global root) at all eight call sites.
- **rest_api.hpp** — `Response` struct was missing the `chunked` field used by
  `make_sse_response()` in `sse_server.hpp`. Added `bool chunked{false}`.
- **http_server.hpp** — `genie::VERSION_FULL` inside `namespace genie::net` resolved as the
  non-existent `genie::net::genie::VERSION_FULL`. Changed to `::genie::VERSION_FULL`.
- **rest_api.hpp** — Stale runtime version strings `"5.3.1"` in the SSE startup broadcast
  and `/api/v1/api-versions` endpoint updated to `"5.3.3"` (matching prior release).

## [5.3.3] - 2026-03-28

### Fixed
- **rest_api.hpp** — `#include "sse_server.hpp"` was placed at line 57, before the `Request`
  and `Response` structs were declared (line 224 and 272 respectively). `make_sse_response()`
  in `sse_server.hpp` takes a `Response&` parameter, so it could not compile. Moved the include
  to immediately after the `Response` struct closing brace.
- **rest_api.hpp** — Private member `cache_default_ttl_` was assigned in
  `set_cache_default_ttl()` but never declared. Added `int cache_default_ttl_{60}` to the
  cache member block.
- **main.cpp** — Fixed `-Wrange-loop-construct` warning: changed `const std::string&` to
  `const std::string` in the cache TTL initializer loop (binding a reference to a temporary
  `std::string` constructed from a `const char*` literal is harmless but warned by GCC).

## [5.3.2] - 2026-03-28

### Fixed
- **rest_api.hpp** — Five handlers in `register_v530_endpoints()` contained unescaped inner
  double-quote characters inside C++ string literals, causing the compiler to lose track of the
  `RestApi` class boundary. This was the cascade root cause of all "invalid use of 'this' in
  non-member function" and "not declared in this scope" errors reported in the v5.3.1 build.
  Affected handlers: `/api/v1/stream/poll`, `/api/v1/compute/sse`,
  `/api/v1/compute/compression`, `/api/v1/compute/kubernetes/scale`,
  `/api/v1/config/binary-serialization`, `/api/v1/config/sse`.
- **rest_api.hpp** — `req.query()` calls in the `/api/v1/stream/poll` handler replaced with
  the correct `req.param()` method (`Request` exposes `param()`, not `query()`).
- **http2_server.hpp** — Added `#undef NO_ERROR` guard before `enum class ErrorCode` to
  prevent the Windows `<winerror.h>` `#define NO_ERROR 0` macro from clashing with the
  `ErrorCode::NO_ERROR` enumerator under MinGW.
- **main.cpp** — `config().to_string()` replaced with `config().to_pson()` (`Config` does
  not have a `to_string()` method; the correct serialization method is `to_pson()`).

## [5.3.1] - 2026-03-28

### Added
- Root-level README.md explaining the two-IDE project structure:
  why CMakeLists.txt lives in server/ (the CLion project root),
  how to open each project, quick-start build commands for
  Windows/Linux/macOS, full directory tree with annotations,
  and endpoint summary table. Makes the repo immediately
  navigable without reading docs/.

### Changed
- Version bumped from 5.3.0 to 5.3.1 across all files

## [5.3.1] - 2026-03-28

### Added (Near-Term v5.x -- Fully Implemented)
- Server-Sent Events (SSE): SseChannel singleton with broadcast/poll API.
  EventSource-compatible HTTP long-poll bridge at /api/v1/stream/poll.
  Channels: market, portfolio, alerts. New client page: sse.html.
  Seeded with demo events on startup.
- Cross-platform file watcher (file_watcher.hpp): native OS APIs for
  zero-overhead change detection -- inotify (Linux), ReadDirectoryChangesW
  (Windows), kqueue/kevent (macOS). Automatic poll fallback for network
  filesystems. Content-hash verification prevents false positives.
- Binary serializer (binary_serializer.hpp): METI wire format (magic 0x4D455449,
  CRC-32 IEEE 802.3, little-endian). Types: DOUBLE_ARRAY, FLOAT_ARRAY, INT64_ARRAY,
  STRING_ARRAY, MATRIX, PSON_MAP. ~10x faster than JSON serialization.
  Hex encode/decode for JSON embedding. New client page: binary-data.html.
- Prometheus-compatible /metrics endpoint (prometheus_endpoint.hpp):
  Text exposition format v0.0.4. PrometheusRegistry singleton with counter/gauge
  methods. Namespace prefix configurable via config.pson. Scrapeable by
  Prometheus, Grafana Agent, VictoriaMetrics, any OpenMetrics scraper.
  Also served at /api/v1/metrics/prometheus.
- Compile-time route table (route_table.hpp): RouteDescriptor array with
  RouteFlags bitmask (PUBLIC, CACHEABLE, SSE_STREAM, MUTATING, ADMIN_ONLY,
  BINARY_RESPONSE). make_genie_route_table() returns RouteTable<256> with 70+
  descriptors. Path pattern matching with :param segments.
- gzip/deflate response compression (response_compression.hpp): Pure C++20
  RFC 1951 store-only deflate wrapped in RFC 1952 gzip. Full zlib deflate
  when GENIE_USE_ZLIB=1. Negotiates from Accept-Encoding header. Configurable
  min-size, level, and prefer-gzip. Adds Content-Encoding + Vary headers.

### Added (Medium-Term v6.x -- Stub Infrastructure Present)
- HTTP/2 stub (http2_server.hpp): All frame types defined (RFC 9113 Section 6),
  all error codes (Section 7). Http2Adapter adds Alt-Svc: h2c advertisement
  header to responses now. Full implementation planned v6.x.
- gzip/deflate already covers the compression requirement above.

### Added (Long-Term v7.x -- Architecture Defined)
- Kubernetes API client stub (k8s_client.hpp): K8sClient with get_deployment(),
  list_pods(), scale(). Full implementation via HttpClient speaking K8s REST API.
- FIX 4.4/5.0 session layer (fix_engine_v2.hpp): Full message parser (tag=value
  + SOH), checksum verification, session state machine (DISCONNECTED -> ACTIVE),
  NewOrderSingle/ExecutionReport/Cancel builders. TCP session planned v7.x.
- WASM client stub (wasm_client.hpp): COOP/COEP headers active now for
  SharedArrayBuffer. Emscripten analytics compilation planned v7.x.

### Added (Configuration)
- config.pson: 7 new sections: sse, prometheus, file_watcher, compression,
  http2, fix, wasm -- all with full inline documentation
- config.pson: cache_ttl section with per-endpoint TTL values

### Added (Client)
- sse.html: Live event monitor with channel selector, poll start/stop,
  real-time event log with channel/event/data display
- binary-data.html: Binary format reference, type tag table, compression
  status, performance comparison vs JSON
- roadmap.html: Full technology roadmap with status badges (DONE/STUB/PLANNED)
  for all v5.x, v6.x, v7.x features and design principles

### Changed
- Version bumped from 5.2.0 to 5.3.1 across all files
- index.html: 3 new sidebar nav items (SSE Events, Binary & Compress, Roadmap)
- RestApi::configure_defaults() calls register_v530_endpoints() at end
- main.cpp: 7 new config.pson sections wired into RestApi at startup
- rest_api.hpp: 10 new includes, 10 new private members, register_v530_endpoints()
  method (15 new endpoints), 8 new configuration setters

### Metrics
- Server: 266 HPP files (+10), 17 module directories, 145+ REST endpoints (+15)
- Client: 64 HTML pages (+3, all wired to API), 18 JS files, 4 CSS stylesheets
- Config: 583-line config.pson (+107 lines), all parameters documented inline
- External dependencies: 0 (SQLite3 bundled, zlib optional via GENIE_USE_ZLIB=1)
- Platforms: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

## [5.2.0] - 2026-03-28

### Added
- PSON configuration format: all 100+ parameters now in config.pson with inline
  // comments and trailing-comma support. config.hpp upgraded with nested object
  parser that flattens "section.key" structure and strips PSON extensions.
- Response caching layer (ApiCache) fully wired into RestApi::handle():
  GET responses cached with per-endpoint TTL; mutating requests (POST/PUT/DELETE)
  automatically invalidate related cache entries. X-Cache: HIT/MISS header added.
  Per-endpoint TTLs configurable in config.pson under cache_ttl.* section.
- Request validation middleware: all POST/PUT requests validated for
  Content-Type: application/json and basic JSON structure ({/[) before reaching
  route handlers. Oversized bodies rejected with 413. Configurable via
  api.validation_enabled and api.validation_max_body_bytes in config.pson.
- Structured error responses: all error paths now use ErrorHandler::build()
  producing {"error","code","trace_id","timestamp","path"} JSON. trace_id
  enables client-to-server log correlation. ErrorHandler was already present
  in error_handler.hpp; this wires it throughout RestApi::handle().
- Graceful shutdown with drain: GracefulShutdown::RequestGuard registered at
  the top of every RestApi::handle() call. When draining, new requests receive
  503 with Retry-After header. Configurable via api.graceful_shutdown_enabled
  and api.graceful_drain_timeout_seconds in config.pson.
- RestApi configuration setters for all new features: set_cache_enabled(),
  set_cache_default_ttl(), set_cache_ttl(), clear_cache(), cache_stats(),
  set_validation_enabled(), set_validation_max_body(), set_graceful_shutdown_enabled(),
  set_drain_timeout(), begin_shutdown().
- Directory restructure: server/ (CLion, C++20 only) and client/ (WebStorm,
  HTML/JS only) are now separate top-level project directories. All .md and
  .txt files are in docs/.
- Re-crafted BACKGROUND.md (827 lines, 8 sections, full Table of Contents)
  covering: history of enterprise investment management libraries (mainframe
  through cloud-native), ecosystem comparison matrix, design philosophy with
  PSON section, architecture including new directory structure, GPU/K8s/Container
  strategy with planned C++20 implementation details, quality standards, and
  project evolution.
- GitHub-ready README.md with quick start, feature table, project structure,
  API sample, and acknowledgments.
- .gitignore covering CLion, WebStorm, MSVC, CMake artifacts, runtime data,
  OS files, and compiler output.
- config.pson includes GPU, Kubernetes, and Container sections with full
  parameter documentation and planned C++20 implementation comments.
- main.cpp wires config.pson settings for cache, validation, and graceful
  shutdown into RestApi at startup.

### Changed
- Version bumped from 5.1.1 to 5.3.1 across all files
- config.json.template replaced by config.pson (PSON format)
- web/config.json reference in client/js/config.js updated to config.pson
- All config.json references in main.cpp updated to config.pson
- RestApi::handle() rewritten with full v5.3.1 feature pipeline
- 404 error response now uses ErrorHandler::build() (structured + trace ID)
- 429 rate limit response now uses ErrorHandler::build()
- 500 exception response now uses ErrorHandler::build()
- BACKGROUND.md completely re-crafted (516 -> 827 lines)

### Fixed
- Stale VERSION_MAJOR/VERSION_MINOR constants in version.hpp (was 4/22, now 5/2)
- Version references synchronized across all .hpp, .cpp, .js, .html, .css, .md files

### Metrics
- Server: 256 HPP files, 17 module directories, 130+ REST endpoints
- Client: 61 HTML pages (all wired to API), 18 JS files, 4 CSS stylesheets
- Config: 476-line config.pson with 100+ parameters, all documented inline
- Tests: 330+ tests across 4 test executables (no GTest, no external runner)
- Documentation: 16 .md files, 1 .txt file, all in docs/
- External dependencies: 0 (SQLite3 bundled from source)
- Platforms: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

## [5.1.1] - 2026-02-16

### Added
- 16 new REST API endpoints for full client-server feature coverage:
  backtesting/strategies, backtesting/run, ibor/positions, ibor/reconciliation,
  trading/routing, trading/position-sizing, portfolio/rebalancing,
  trading/settlement, trading/tca, trading/blotter, trading/journal,
  workflows, reporting/templates (plus supporting sub-routes)
- API fetch bindings for 12 previously unwired HTML pages:
  backtesting.html, ibor.html, ml-alpha.html, order-routing.html,
  position-sizing.html, rebalancing.html, reporting.html, settlement.html,
  tca.html, trade-blotter.html, trade-journal.html, workflows.html
- Comprehensive config.json.template with 100+ configurable parameters covering:
  server, auth, trading, risk, portfolio, compliance, tax, reporting, analytics,
  market data, notifications, compute/GPU, containers, kubernetes, pipelines,
  workflows, export, feature flags, multi-tenant, websocket, circuit breaker
- New configuration sections: trading, risk, portfolio, compliance, tax,
  reporting, analytics, market_data, notifications, compute, containers,
  kubernetes, pipeline, workflow, export, feature_flags, multi_tenant,
  websocket, circuit_breaker
- Re-crafted BACKGROUND.md with 8-section structure and full Table of Contents
  covering history, ecosystem, design philosophy, architecture, GPU/K8s/Container
  strategy, quality standards, and project evolution

### Changed
- Version bumped from 5.0.1 to 5.1.0 across all 370+ files
- REST API endpoint count increased from 118 to 130+
- All 61 client HTML pages now have active REST API connections
- BACKGROUND.md completely re-crafted (516 lines, 8 sections, full TOC)
- config.json.template expanded from 48 to 100+ parameters
- Fixed stale version references in all JS files (5.0.0 -> 5.1.0)
- Documentation in docs/ directory only (removed web/docs/ duplicates)
- All documentation files updated with accurate counts and metrics

### Fixed
- 12 client HTML pages had no API fetch() calls (now fully wired)
- Stale 5.0.0 version references in web/js/*.js files
- Stale 4.22.0 comment in config.json.template
- Version consistency verified across all files

### Metrics
- Server: 256 HPP files, 17 module directories, 130+ REST endpoints
- Client: 61 HTML pages (all wired), 18 JS files, 4 CSS stylesheets
- Config: 100+ parameters in config.json.template
- Tests: 330+ tests across 4 test executables
- Documentation: 17 .md files, 2 .txt files, all in docs/
- External dependencies: 0 (SQLite3 bundled from source)
- Platforms: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

## [5.0.1] - 2026-02-14

### Added
- 11 new client HTML pages for complete server feature coverage:
  backtesting.html, ml-alpha.html, trade-blotter.html, trade-journal.html,
  settlement.html, position-sizing.html, order-routing.html, tca.html,
  ibor.html, rebalancing.html, workflows.html
- Full client-server feature parity: all 117+ REST API routes now accessible from 61 client pages
- Navigation links for all new pages in index.html dashboard
- GPU compute future-implementation stubs documented with platform-native APIs
  (DirectCompute/CUDA/Metal) for Windows, Linux, macOS
- Kubernetes/Container future-implementation stubs documented with platform-native
  process management (Job Objects/cgroups/sandbox) for Windows, Linux, macOS
- Comprehensive BACKGROUND.md re-crafted with full Table of Contents covering:
  history and purpose of libraries for Enterprise Investment Management Platform
  Prototype environments, broader ecosystem context, and design philosophy
- POST_BUILD CMake commands to auto-copy web/ and config.json.template to bin/
- Client files consolidated in web/ directory (WebStorm project)
- Server C++20 files in project root (CLion project)

### Changed
- Version bumped from 4.21.4 to 5.0.1 across all files
- BACKGROUND.md completely re-crafted with 8-section structure, library-focused
  narrative, and expanded comparison matrix including new features
- README.md updated with accurate project statistics (256 HPP, 61 HTML pages)
- Static file serving default changed from ./client to ./web
- CMakeLists.txt enhanced with POST_BUILD web/ copy commands
- All documentation files updated to v5.0.1 with accurate counts
- Navigation updated across all client pages for consistency

### Fixed
- Static file serving 404 error (server looked for ./client, content now in ./web)
- config.json and config.json.template static_dir corrected to ./web
- Version consistency verified across all 367+ files

### Metrics
- Server: 256 HPP files, 17 module directories, 117+ REST endpoints
- Client: 61 HTML pages (+11 new), 18 JS files, 4 CSS stylesheets
- Tests: 330+ tests across 4 test executables
- Documentation: 16 .md files, 2 .txt files, all in docs/
- External dependencies: 0 (SQLite3 bundled from source)
- Platforms: Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

## [5.0.1] - 2026-02-08

### Added
- Full client-server endpoint coverage: all 116 REST API routes now accessible from client HTML pages
- API wiring for 42 client pages with missing endpoint connections
- GPU/Kubernetes/Container future-implementation documentation in header files
- CMakePresets.json for named build configurations (default, release, dev, mingw-release)
- Comprehensive BACKGROUND.md with Table of Contents covering history, ecosystem, and design philosophy

### Changed
- BACKGROUND.md completely re-crafted with 10-section structure and full Table of Contents
- `/api/v1/status` endpoint made public (no auth required) for monitoring compatibility
- CMakeLists.txt now reads version from VERSION.txt (single source of truth)
- Project description updated to "Metis Genie Platform Enterprise Investment Management Prototype"
- All documentation files updated to v5.0.1 with accurate statistics

### Fixed
- 2 compiler warnings in main.cpp (unused `name` variable, unused `key` parameter)
- 10 missing server routes added for client HTML pages (health probes, versioning, aliases)
- test_rest_endpoints status test aligned with public endpoint change

## [5.0.1] - 2026-02-07

### Added - 8 New Modules (no overlap with existing)
- **Performance Attribution** (`performance/performance_attribution.hpp`)
  Brinson-Fachler allocation/selection/interaction effects, multi-period
  geometric linking (Carino smoothing), 5-factor attribution, FI attribution
  (carry/duration/curve/credit).
- **Trade Allocator** (`trading/trade_allocator.hpp`)
  Block trade splitting: pro-rata AUM, pro-rata target weight, equal, cash-
  directed, round-robin. Round-lot enforcement, minimum allocation, fairness
  scoring, rotation tracking, GIPS-compliant average pricing.
- **Cash Manager** (`portfolio/cash_manager.hpp`)
  Cash balance tracking, settlement projection ladder, sweep operations
  (excess to/deficit from money market), cash drag analysis (bps impact),
  cash reserves, multi-currency support.
- **Benchmark Tracker** (`performance/benchmark_tracker.hpp`)
  Tracking error (annualized), information ratio, active share, beta/alpha
  regression, R-squared, style drift detection (rolling R-squared), 8 pre-
  loaded benchmark indices.
- **Risk Budgeter** (`risk/risk_budgeter.hpp`)
  Risk budget allocation by strategy/desk/manager, utilization monitoring
  (VaR/TE/Vol/DD/Beta), breach severity levels, efficiency scoring
  (return per unit risk), reallocation recommendations, marginal risk.
- **Corporate Actions** (`ops/corporate_actions.hpp`)
  12 action types (cash/stock dividend, split, reverse split, merger,
  spinoff, rights issue, tender, name change, delisting, ROC). Position
  adjustment with cost basis, DRIP, fractional cash-in-lieu, tax estimation.
- **Approval Workflow** (`ops/approval_workflow.hpp`)
  Multi-level chains (Trader->PM->Compliance->Risk->Executive), threshold-
  based auto-approval (<0K), time-based escalation, proxy approval,
  sequential/any-of/all-of modes, SLA monitoring, workflow statistics.
- **Data Quality Monitor** (`persistence/data_quality_monitor.hpp`)
  Field validation (null/range/enum), completeness/validity/freshness/
  uniqueness scoring, z-score anomaly detection, composite quality score
  (0-100), feed health classification, quality trend tracking.

### Changed
- Version bumped from 4.11.0 to 5.0.1 across all files
- genie.hpp updated with 8 new module includes
- version.hpp updated with v5.0.1 history entry

### Metrics
- Server: 252 HPP files (+8 new), 16 module directories
- Client: 50 HTML, 18 JS, 4 CSS
- Compilation: 0 errors
- External dependencies: 0

## [4.11.0] - 2026-02-07

### Added - 8 New Modules
- **Position Sizer** (`trading/position_sizer.hpp`, ~310 lines)
  Kelly criterion (full/fractional), fixed fractional, volatility-based, equal weight,
  risk parity, max-drawdown-capped. Portfolio heat tracking, round-lot enforcement.
- **Strategy Backtester** (`analytics/strategy_backtester.hpp`, ~370 lines)
  Event-driven bar-by-bar simulation, commission/slippage modeling, Sharpe/Sortino/
  Calmar ratios, max drawdown, profit factor, MAE/MFE, equity curve, trade list.
- **Trade Journal** (`trading/trade_journal.hpp`, ~280 lines)
  Trade logging with strategy/setup tagging, emotion/confidence tracking, trade grading
  (A-F), plan adherence scoring, streak analysis, P&L by strategy/tag.
- **Currency Hedger** (`market/currency_hedger.hpp`, ~270 lines)
  FX exposure calculation, full/partial/dynamic/optimal-variance hedging, forward
  cost estimation, hedge effectiveness measurement, 6 pre-loaded currency pairs.
- **Fee Calculator** (`portfolio/fee_calculator.hpp`, ~290 lines)
  Management (flat/tiered), performance (HWM/hurdle), custody, admin fees. Expense
  ratio (TER), fee drag calculation, multi-schedule support.
- **Settlement Engine** (`trading/settlement_engine.hpp`, ~290 lines)
  T+0/T+1/T+2 settlement cycles, DvP/FoP instruction lifecycle, bilateral/multilateral
  netting, fail detection/aging, STP rate monitoring, 7 asset class defaults.
- **Peer Comparison** (`performance/peer_comparison.hpp`, ~290 lines)
  Percentile ranking across metrics, quartile classification, composite scoring,
  universe statistics, 8 pre-loaded benchmark funds.
- **Accrued Interest** (`assets/accrued_interest.hpp`, ~280 lines)
  Day count conventions (30/360, ACT/ACT, ACT/360, ACT/365, 30E/360, BUS/252),
  clean/dirty pricing, coupon schedule generation, ex-dividend handling.

### Changed
- Version bumped from 4.10.0 to 4.11.0 across all files
- genie.hpp updated with 8 new module includes
- version.hpp updated with v4.11.0 history entry

### Metrics
- Server: 244 HPP files (+8 new), 16 module directories
- Client: 50 HTML, 18 JS, 4 CSS
- Compilation: 0 errors
- New code: ~2,380 lines across 8 modules
- External dependencies: 0

## [4.10.0] - 2026-02-07

### Added - 4 New + 4 Wired Modules
- **Rebalance Optimizer** (`portfolio/rebalance_optimizer.hpp`, 344 lines)
  Target-weight rebalancing, drift-band/tax-optimized/cash-directed/min-turnover
  methods, round-lot adjustment, turnover constraints, proposal workflow.
- **Liquidity Scorer** (`risk/liquidity_scorer.hpp`, 315 lines) **[NEW]**
  Bid-ask spread/volume/market-cap/impact 4-factor scoring (1-10 scale), 5-tier
  classification, square-root market impact model, time-to-liquidation, portfolio
  aggregate liquidity, illiquidity premium estimation.
- **Notification Router** (`core/notification_router.hpp`, 320 lines) **[NEW]**
  Multi-channel dispatch (in-app/email/webhook/SMS/Slack), user preference routing,
  quiet hours, priority-based bypass, rate limiting, delivery tracking.
- **Dividend Tracker** (`portfolio/dividend_tracker.hpp`, 340 lines) **[NEW]**
  Ex-date/record/payment tracking, trailing and forward yield, DRIP modeling
  with 5-year projection, dividend growth CAGR, 8 pre-loaded profiles.
- **Margin Calculator** (`risk/margin_calculator.hpp`, 340 lines) **[NEW]**
  Reg-T/portfolio/day-trading margin, concentration surcharge, maintenance call
  detection, buying power, what-if trade impact analysis.

### Wired (existing, now included in genie.hpp)
- **Correlation Matrix** (`analytics/correlation_matrix.hpp`, 470 lines)
- **Order Book Simulator** (`trading/order_book_simulator.hpp`, 480 lines)
- **Data Export** (`core/data_export.hpp`, 446 lines) — already included

### Changed
- Version bumped from 4.9.0 to 4.10.0 across all files
- genie.hpp updated with 7 new module includes
- version.hpp updated with v4.10.0 history entry

### Metrics
- Server: 236 HPP files (+5 new), 16 module directories
- Client: 50 HTML, 18 JS, 4 CSS
- Compilation: 0 errors
- New code: ~1,359 lines across 4 new modules
- External dependencies: 0

## [4.9.0] - 2026-02-07

### Added - 7 New Modules Strengthening Thin Areas
- **Audit Trail** (`security/audit_trail.hpp`, ~330 lines)
  Immutable hash-chain event log, SOX/SEC compliance, event builder pattern,
  category/severity filtering, integrity verification, JSONL export.
- **Benchmark Manager** (`performance/benchmark_manager.hpp`, ~315 lines)
  S&P 500/AGG/60-40 benchmarks, tracking error, information ratio, active
  share, alpha/beta regression, sector weight comparison.
- **Instrument Master** (`assets/instrument_master.hpp`, ~314 lines)
  Security master (ISIN/CUSIP/SEDOL/FIGI), 24 asset types, corporate actions,
  lifecycle tracking, reverse index lookup, fuzzy search, 12 pre-loaded equities.
- **Session Manager** (`security/session_manager.hpp`, ~327 lines)
  Token-based sessions, configurable TTL/idle timeout, concurrent session limits,
  IP binding, role-based permissions, session statistics.
- **Task Scheduler** (`ops/task_scheduler.hpp`, ~340 lines)
  Cron expression parsing, priority queue, task dependencies, execution history,
  6 pre-built tasks (EOD/NAV/risk/rebalance/compliance/data).
- **Report Templates** (`reporting/report_templates.hpp`, ~370 lines)
  Column definitions with aggregates, multi-format rendering (text/CSV/JSON/HTML),
  parameter binding, 3 templates (holdings/risk/blotter).
- **Stress Scenarios** (`risk/stress_scenarios.hpp`, ~420 lines)
  7 pre-built scenarios (GFC 2008, COVID-19, dot-com, rate shocks, USD, stagflation),
  multi-asset shock application, portfolio P&L impact, limit breach detection.

### Changed
- Version bumped from 4.8.0 to 4.9.0 across all files
- genie.hpp updated with 7 new module includes
- version.hpp updated with v4.9.0 history entry

### Metrics
- Server: 231 HPP files (+7 new), 17 module directories
- Client: 50 HTML, 18 JS, 4 CSS
- Compilation: 0 errors
- New code: ~2,416 lines across 7 modules
- External dependencies: 0

## [4.8.0] - 2026-02-07

### Completed: 8-Module Enterprise Suite
All eight requested modules are now fully implemented and wired into genie.hpp:

1. **Config Manager** (`core/config_manager.hpp`, 453 lines)
   Hot-reload with change callbacks, schema-based validation, environment
   overlays (dev/staging/prod), INI-format reload, secret masking, audit trail.

2. **Metrics Collector** (`core/metrics_collector.hpp`, 429 lines)
   Prometheus-compatible counters/gauges/histograms, label dimensions, scrape
   endpoint, bucketed histogram percentiles, metric families.

3. **Feature Flags** (`core/feature_flags.hpp`, 205 lines)
   Boolean/percentage/segment/scheduled flag types, rollout percentages,
   user-segment targeting, A/B testing, evaluation statistics.

4. **Data Pipeline** (`core/data_pipeline.hpp`, 419 lines)
   ETL stage framework with transform chains, stage composition, error
   handling per stage, pipeline metrics, batch and streaming modes.

5. **FX Engine** (`market/fx_engine.hpp`, 286 lines)
   Multi-currency conversion with cross rates, triangulation via USD,
   bid/ask spreads, historical rate snapshots, 30+ default currency pairs.

6. **Alert Manager** (`core/alert_manager.hpp`, 493 lines) **[NEW]**
   Threshold-based alerting (>, <, range, rate-of-change), severity levels
   (Info/Warning/Critical/Emergency), notification channels (log/callback/
   webhook/email), cooldown periods, maintenance windows, escalation chains,
   acknowledgment workflow, 5 pre-registered financial alert rules.

7. **API Versioning** (`core/api_versioning.hpp`, 396 lines)
   Backward-compatible endpoint routing, version negotiation (URL path/header/
   query), deprecation tracking, version-specific middleware, migration guides.

8. **Cache Manager** (`core/cache_manager.hpp`, 449 lines) **[NEW]**
   LRU eviction with TTL expiration, named cache regions, hit/miss statistics,
   get-or-load pattern, prefix-based invalidation, bulk operations, memory
   estimation, multi-region management with typed access.

### Changed
- Version bumped from 4.7.0 to 4.8.0 across all 224 HPP + 50 HTML + 18 JS +
  CMakeLists.txt + VERSION.txt + documentation files
- genie.hpp updated with 5 new module includes (3 were already present)
- version.hpp updated with v4.8.0 history entry

### Metrics
- Server: 224 HPP files (+2 new), 17 module directories
- Client: 50 HTML pages, 18 JS files, 4 CSS files
- Compilation: 0 errors
- New code: ~942 lines (alert_manager + cache_manager)
- External dependencies: 0
- Platforms: Windows (MinGW/MSVC), Linux (GCC 13+), macOS (Clang 16+)

# Changelog -- Metis Genie Platform Server

All notable changes documented in [Keep a Changelog](https://keepachangelog.com/) format.

## [4.7.0] - 2026-02-07

### Added - Improvement Modules (8)
- **Wash Sale Engine** (`tax/wash_sale_engine.hpp`)
  IRS wash sale detection with 30-day lookback, substantially identical security
  matching, cross-account detection, cost basis adjustment, prospective warnings.
- **Tail Risk Analysis** (`risk/tail_risk.hpp`)
  CVaR/Expected Shortfall, Cornish-Fisher expansion, GPD fitting, Hill estimator,
  tail dependence, CVaR decomposition by position.
- **Workflow Engine** (`core/workflow_engine.hpp`)
  DAG-based task orchestration with topological sort, retry policies, conditional
  branching, execution history, and cancellation support.
- **Brinson Attribution** (`portfolio/brinson_attribution.hpp`)
  Brinson-Fachler allocation/selection/interaction effects, multi-period geometric
  linking (Carino method), tracking error and information ratio.
- **Compliance Calendar** (`compliance/compliance_calendar.hpp`)
  Regulatory deadline tracking (SEC/FINRA/NFA/CFTC/IRS), filing status lifecycle,
  assignee management, lead-time warnings, pre-registered common filings.
- **Data Reconciliation** (`persistence/data_reconciliation.hpp`)
  Cross-system reconciliation (OMS/custodian/IBOR), tolerance-based matching,
  break categorization, run history, auto-resolution.
- **Smart Order Routing** (`trading/routing_strategy.hpp`)
  Strategy framework (best price, VWAP, dark-first, spray, sequential), venue
  scoring, child order generation, toxicity scoring, Reg NMS compliance.
- **Data Quality Rules** (`market/data_quality_rules.hpp`)
  Price spike z-score detection, stale data, bid/ask consistency, volume anomaly,
  quality scoring per symbol, custom rule registration.

### Changed
- Version bumped from 4.6.0 to 4.7.0 across all 222 HPP + 50 HTML + 18 JS +
  CMakeLists.txt + VERSION.txt + documentation files
- genie.hpp updated with 8 new module includes

### Metrics
- Server: 222 HPP files (+8 new), 17 module directories
- Client: 50 HTML, 18 JS, 4 CSS (unchanged)
- Compilation: 0 errors
- New code: ~4,500 lines across 8 modules
- External dependencies: 0
- Platforms: Windows (MinGW/MSVC), Linux (GCC 13+), macOS (Clang 16+)

## [4.6.0] - 2026-02-07

### Added - Improvement Modules (9)
- **Configuration Manager** (`core/config_manager.hpp`)
  Hierarchical key-value config with typed access, environment overlays,
  validation rules, change notifications, snapshot/diff, INI serialization.
- **Metrics Collector** (`core/metrics_collector.hpp`)
  Prometheus-compatible Counter/Gauge/Histogram/Summary, text exposition
  export, JSON export, ScopedTimer, singleton registry.
- **FX Engine** (`market/fx_engine.hpp`)
  Multi-currency conversion with cross-rate triangulation through USD,
  bid/ask spreads, batch portfolio conversion, staleness detection.
- **Factor Model** (`analytics/factor_model.hpp`)
  Fama-French 5-factor + momentum, OLS regression for beta estimation,
  portfolio attribution, factor covariance matrix, R-squared calculation.
- **API Key Manager** (`security/api_key_manager.hpp`)
  Key generation/validation/rotation, scope-based permissions, FNV-1a
  hashing, rate limits per key, usage tracking, audit trail.
- **Execution Analytics** (`trading/execution_analytics.hpp`)
  TCA with implementation shortfall, VWAP/TWAP benchmarks, broker
  scorecards, fill rate tracking, latency analysis, best execution.
- **Scheduled Reports** (`reporting/scheduled_reports.hpp`)
  Cron-style scheduling, report templates (portfolio/risk/compliance/TCA),
  multi-format output, distribution lists, report archival.
- **Health Dashboard** (`ops/health_dashboard.hpp`)
  Component health registry, dependency graph, SLA tracking, incident
  management (MTTR), system score 0-100, status page generation.
- **Data Archiver** (`persistence/data_archiver.hpp`)
  Tiered storage (hot/warm/cold/frozen), age-based policy enforcement,
  compression on archive, regulatory retention, capacity planning.

### Changed
- Version bumped from 4.5.0 to 4.6.0 across all 214 HPP + 50 HTML + 18 JS +
  CMakeLists.txt + VERSION.txt + documentation files
- genie.hpp updated with 9 new module includes
- version.hpp updated with v4.6.0 history entry

### Metrics
- Server: 214 HPP files (+9 new), 17 module directories
- Client: 50 HTML pages, 18 JS files, 4 CSS files
- Compilation: 0 errors, 2 warnings
- New code: ~4,500 lines across 9 improvement modules
- External dependencies: 0
- Platforms: Windows (MinGW/MSVC), Linux (GCC 13+), macOS (Clang 16+)

## [4.5.0] - 2026-02-07

### Added - Improvement Modules (8)
- **Result<T,E> Error Handling** (`core/result.hpp`)
  Monadic Result type with error categories, map/flatMap/recover operations,
  void specialization, factory helpers (Ok/Err), and collection utilities.
- **Structured JSON Logger** (`core/json_logger.hpp`)
  Production logging with levels, fluent API, file rotation, console+file
  dual output, contextual fields, and log macros.
- **Time-Series Store** (`persistence/timeseries_store.hpp`)
  OHLCV bar storage with windowed queries, resampling (1m to 1M), rolling
  statistics (SMA/EMA/RSI/ATR/Bollinger), gap detection, CSV/JSON export.
- **Portfolio Snapshots** (`portfolio/portfolio_snapshot.hpp`)
  Point-in-time versioning, diff comparison, NAV history, snapshot triggers
  (EOD/on-trade/manual/regulatory), position-level change tracking.
- **Market Data Normalizer** (`market/data_normalizer.hpp`)
  Cross-provider normalization, symbol registry (ISIN/CUSIP/SEDOL/FIGI/RIC),
  corporate action adjustments, quality scoring, data provenance.
- **Pre-Trade Compliance Pipeline** (`compliance/pretrade_compliance.hpp`)
  Rule chain with veto/warn/pass semantics, restricted lists, concentration
  limits, override workflow, full audit trail.
- **Order Rate Throttler** (`trading/order_throttle.hpp`)
  Per-broker sliding window rate control, token bucket with burst, queue
  management, daily limits, throttle metrics.
- **Connection Resilience** (`net/connection_resilience.hpp`)
  Exponential backoff with jitter, per-endpoint circuit breaker, latency
  tracking, health monitoring, SLA reporting, state change callbacks.

### Changed
- Version bumped from 5.0.1 to 4.5.0 across all 205 HPP + 50 HTML + 18 JS +
  CMakeLists.txt + VERSION.txt + documentation files
- genie.hpp updated with 8 new module includes
- version.hpp updated with v4.5.0 history entry

### Metrics
- Server: 205 HPP files (+8 new), 17 module directories
- Client: 50 HTML pages, 18 JS files, 4 CSS files
- Compilation: 0 errors
- New code: ~4,000 lines across 8 improvement modules
- External dependencies: 0
- Platforms: Windows (MinGW/MSVC), Linux (GCC 13+), macOS (Clang 16+)

## [4.4.0] - 2026-02-07

### Added - New Modules
- **Cryptocurrency Trading** (`trading/crypto_trading.hpp`, ~1,100L)
  Multi-exchange support (Coinbase, Kraken, Binance, Gemini), wallet management
  (hot/cold/hardware/multisig), gas fee estimation by chain, staking manager,
  DeFi position tracking, bridge transfers, FATF Travel Rule compliance,
  token registry, and unified crypto portfolio analytics.
- **Event-Driven Message Bus** (`core/message_bus.hpp`, ~600L)
  Pub/sub with topic-based wildcard routing, priority-ordered dispatch,
  dead letter queue, message store with replay/event sourcing, request/reply
  pattern, pre-defined topic constants for market/trading/risk/system events.
- **Database Migration Framework** (`persistence/db_migrations.hpp`, ~700L)
  Versioned schema migrations with up/down support, dependency resolution via
  topological sort, dry-run executor, fluent SchemaBuilder API, seed data
  management, schema snapshots and diff comparison, migration history/audit.
  6 core migrations pre-registered (users, portfolios, positions, orders,
  audit_log, market_data_cache).
- **OpenAPI 3.0 Export** (`net/openapi_export.hpp`, ~600L)
  Generates OpenAPI 3.0.3 JSON specification from registered endpoints,
  Swagger UI HTML export, fluent EndpointBuilder API, pre-registered schemas
  (ErrorResponse, PaginationMeta, HealthCheck), security schemes (Bearer JWT,
  API Key). 15+ Genie API endpoints auto-documented.
- **Architecture Decision Records** (`core/adr_registry.hpp`, ~500L)
  Structured ADR lifecycle (Proposed/Accepted/Deprecated/Superseded),
  Markdown and JSON export, searchable catalog, statistics, alternatives
  with pros/cons tracking. 7 platform ADRs pre-registered.

### Fixed - Compilation Errors (4,698 to 0)
- Struct redefinitions resolved across 12 headers via targeted renames
- Template specialization: Added explicit BrokerResult<void>
- Missing declarations: Added OrderFillState struct
- Namespace conflicts: RouteHandler renamed in http_server.hpp
- Return type mismatches: cancel_order conversion fix
- Missing default constructors: OrderManager, BackupManager, JobProcessor
- Stray constructor removed in ml_alpha.hpp
- Undeclared function fixed in whatif_scenario.hpp
- Member name correction in algo_execution.hpp

### Changed
- Version bumped from 4.3.0 to 5.0.1 across all 197 HPP + 50 HTML + 18 JS +
  CMakeLists.txt + VERSION.txt + documentation files
- genie.hpp updated with 5 new module includes
- version.hpp updated with v5.0.1 history entry

### Metrics
- Server: 197 HPP files (+5 new), 17 module directories
- Client: 50 HTML pages, 18 JS files, 4 CSS files
- Compilation: 0 errors, 2 warnings
- New code: ~3,500 lines across 5 new modules
- External dependencies: 0
- Platforms: Windows (MinGW/MSVC), Linux (GCC 13+), macOS (Clang 16+)

## [4.3.0] - 2026-02-06

### Changed
- Version synchronization and documentation refresh across all modules

## [4.2.0] - 2026-02-06

### Added
- 20 new server improvement modules (Phase 1 + Phase 2) totaling ~9,400 lines
- 20 new REST API endpoints and 20 new client HTML pages
- Container orchestration stub module
- GPU compute abstraction (CUDA/Metal/Vulkan/OpenCL)

## [4.1.0] - 2026-02-06

### Added
- 24 new client HTML pages, GPU compute abstraction, Kubernetes stubs

## [4.0.0] - 2026-02-06

### Added
- Complete platform restructuring into 17 module directories
- REST API with 60+ endpoints, multi-tenant support, NLQ engine

---

*Metis Genie Platform v5.0.1 -- Server Changelog*

## v5.0.1 (2026-02-08)

### Added
- Comprehensive BACKGROUND.md with Table of Contents covering history, ecosystem,
  design philosophy, architecture, technology choices, and future directions
- CMakePresets.json with named build configurations (default, release, dev, mingw-release)
- VERSION.txt drives CMake project version (single source of truth)
- 10 new REST API routes for full client coverage: /health/live, /health/ready,
  /health/db, /health/external, /versioning, /batch-processor, /containers,
  /hot-reload, /market/quote, /market/ (trailing slash alias)
- GPU, Kubernetes, and Containers marked as future C++20 implementation stubs
  with platform-specific roadmap (Windows/Linux/macOS)
- Updated all documentation for accuracy and completeness

### Changed
- CMake DESCRIPTION updated to "Metis Genie Platform Enterprise Investment Management Prototype"
- /api/v1/status endpoint changed from authenticated to public (monitoring endpoint)
- All .md and .txt documentation files consolidated in docs/ directory

### Fixed
- 27 compiler warnings eliminated across 20 source files (unused parameters,
  unused variables in main.cpp signal_handler and config validator)
- test_rest_endpoints status test no longer requires auth token
- Double REST API version strings updated from stale 4.4.0 references

## v5.0.1 (2026-02-08)

### Changed
- Version bump for warning fixes and CMakePresets.json addition

## v4.18.0 (2026-02-08)

### Added
- CMakePresets.json with default, release, dev, and mingw-release presets

### Fixed
- main.cpp unused variable 'name' in signal_handler
- main.cpp unused parameter 'key' in config validator lambda

## v4.17.0 (2026-02-07)

### Added
- 10 missing REST API routes for complete client HTML endpoint coverage
- /api/v1/status changed to public (no auth required)

### Fixed
- test_rest_endpoints FAIL on /api/v1/status (was 401, now 200)

## v4.16.0 (2026-02-07)

### Changed
- Version bump with all v4.15.0 fixes carried forward

## v4.15.0 (2026-02-07)

### Fixed
- 25 unused parameter warnings eliminated across 18 header files
- REST API health response: "status":"ok" changed to "status":"healthy"
- REST API portfolio response wrapped in {"portfolios":[...]} object
- Reporting endpoint accepts both "template_id" and "type" fields
- 4 stale version strings updated (4.4.0 to 4.15.0)
- 4 test assertion failures corrected to match API changes

## v5.0.1 (2026-02-08)

### Added
- MIT License (LICENSE file in both server and client)
- Author and Acknowledgments sections in README.md
- JetBrains CLion, JetBrains WebStorm, Anthropic Claude acknowledgments
- DISCLAIMER for prototype usage responsibility

### Changed
- All copyright notices updated from "All rights reserved" to "MIT License"
- README.md License section references LICENSE file
- BACKGROUND.md footer updated to MIT License

## v5.0.1 (2026-02-08)

### Fixed
- CORS origin in main.cpp changed from "http://localhost:8080" to "*" to allow
  browser clients opened from file:// or different ports to connect to the server
- This resolves "Failed to fetch" errors when client HTML pages cannot reach the
  server due to browser same-origin policy enforcement

## v5.0.1 (2026-02-08)

### Fixed
- Static file serving default changed from "../client" to "./client" so client
  files can be placed inside the server project directory
- Added warning log when static dir not found on startup
- Client files now served at http://localhost:8080/ eliminating file:// CORS issues
