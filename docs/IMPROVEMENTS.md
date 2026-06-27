# Metis Genie Platform - Recommended Improvements

**Version:** 5.5.11
**Note:** All improvements use C++20 for Windows, Linux, and macOS.
Excludes Docker, Doxygen, PyTest, GTest, and Jupyter.

---

## Implemented in v5.3.2

1. **Full Client-Server Wiring** - 12 HTML pages connected to REST API endpoints
   (backtesting, IBOR, ML alpha, order routing, position sizing, rebalancing,
   reporting, settlement, TCA, trade blotter, trade journal, workflows)
2. **16 New REST API Routes** - Complete server feature coverage through API
3. **Comprehensive Configuration** - 100+ parameters in config.json.template
   covering all subsystems (trading, risk, portfolio, compliance, tax, analytics,
   market data, notifications, GPU, containers, K8s, pipelines, workflows)
4. **Documentation Overhaul** - Re-crafted BACKGROUND.md, updated all docs
5. **Version Synchronization** - All 370+ files updated to 5.3.2

## Recommended Future Improvements

### High Priority

1. **GPU Monte Carlo Implementation** - Replace stub with platform-native compute:
   - Windows: DirectCompute via Windows SDK (d3d12.h, dxcapi.h)
   - Linux: CUDA via nvcc or OpenCL via vendor SDK
   - macOS: Metal compute shaders via Metal.framework
   - Estimated: 2,000-3,000 lines C++20 per platform

2. **Container Orchestration Implementation** - Replace stub with native process management:
   - Windows: Job Objects (CreateJobObject, AssignProcessToJobObject)
   - Linux: cgroups v2 / namespaces (clone3, mount, pivot_root)
   - macOS: sandbox-exec / App Sandbox APIs
   - Estimated: 1,500-2,500 lines C++20 per platform

3. **Kubernetes API Client** - HTTP-based K8s client using existing HTTP client:
   - Pod management (create, delete, list, watch)
   - Service discovery and load balancing
   - ConfigMap and Secret management
   - Health check integration with existing endpoints
   - Estimated: 1,500 lines C++20

4. **WebSocket Live Data** - Implement real WebSocket server for push updates:
   - Live position updates without polling
   - Real-time alert notifications
   - Market data streaming (when API keys configured)
   - Estimated: 800 lines C++20

### Medium Priority

5. **PDF Report Enhancement** - Expand PDF writer capabilities:
   - Charts and graphs embedded in reports
   - Multi-page table pagination
   - Header/footer with page numbers
   - Estimated: 600 lines C++20

6. **Database Migration Framework** - Versioned schema migrations:
   - Migration files with up/down SQL
   - Version tracking table
   - Rollback support
   - Estimated: 400 lines C++20

7. **Rate Limiter Enhancement** - Sliding window algorithm:
   - Per-user and per-endpoint limits
   - Token bucket with burst allowance
   - Configurable via config.json
   - Estimated: 300 lines C++20

8. **CSV/Excel Export** - Enhanced data export:
   - Multi-sheet Excel-compatible output
   - Configurable column selection
   - Date range filtering
   - Estimated: 500 lines C++20

### Lower Priority

9. **Client Build Optimization** - Minify JS/CSS for production:
   - Simple whitespace/comment stripping (no npm required)
   - Concatenate JS files into single bundle
   - Estimated: 200 lines C++20 build tool

10. **API Documentation Generator** - Auto-generate OpenAPI spec:
    - Extract route definitions from rest_api.hpp
    - Generate JSON/YAML OpenAPI 3.0 spec
    - Serve spec at /api/v1/openapi.json
    - Estimated: 500 lines C++20

11. **Integration Test Expansion** - Additional test coverage:
    - WebSocket connection tests
    - Multi-user concurrent session tests
    - Large dataset performance benchmarks
    - Estimated: 800 lines C++20

12. **Configuration Validation** - Enhanced startup checks:
    - Type validation for all config values
    - Range checking for numeric settings
    - Warning for unused/unknown keys
    - Estimated: 300 lines C++20

---

*This document is part of the Metis Genie Platform v5.5.11 distribution.*
*Copyright (c) 2026 Bennie Shearer (Retired). MIT License.*
