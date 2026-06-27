# Metis Genie Platform - Build Instructions

**Version:** 5.5.11
**Platforms:** Windows (MinGW/MSVC), Linux (GCC/Clang), macOS (Apple Clang)

## Prerequisites

- C++20 compatible compiler
- CMake 3.20 or newer

## Build with CMake Presets (Recommended)

```bash
cmake --preset default          # Debug + tests + examples
cmake --build --preset default
ctest --preset default
```

Available presets:
- `default` - Debug build with tests and examples
- `release` - Optimized release build, no tests
- `dev` - Debug with tests only (no examples)
- `mingw-release` - MinGW optimized release

## Build with CLion (MinGW)

1. Open project directory as CMake project
2. Settings > Build > Toolchains > MinGW (auto-detected)
3. Build and Run

## Build from Command Line

```bash
mkdir build && cd server
 mkdir build
 cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

## Run

```bash
./build/bin/metis-genie-platform          # Linux/macOS
.\build\bin\metis-genie-platform.exe      # Windows
```

Server starts on port 8080. Open http://localhost:8080 in a browser.
Default credentials: admin/demo, trader/trade, user/user.

## Configuration

Copy config.pson.template to config.pson and customize parameters.
All 100+ parameters are documented in the template file.

## Test Suites

| Suite | Tests | Description |
|-------|-------|-------------|
| test_genie | 128 | Core module tests |
| test_integration | 30+2 skip | API integration tests |
| test_tier3 | 131 | Tier 3 module tests |
| test_rest_endpoints | 39 | REST endpoint tests |

---

*Metis Genie Platform v5.5.11 -- Build Instructions*
