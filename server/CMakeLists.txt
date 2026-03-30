# ==========================================================================
# Metis Genie Platform v5.5.8
# CMakeLists.txt -- CLion server project (C++20)
# Version read from server/VERSION.txt (single source of truth)
# ==========================================================================
#
# Directory layout:
#   server/    <- This CMake project (CLion)
#   client/    <- WebStorm project (HTML/JS -- no CMake)
#   docs/      <- All .md and .txt documentation
#
# Configuration:
#   All parameters are in server/config.pson (PSON format).
#   Copy config.pson to the build output directory before running.
#
# Build instructions:
#   cd server
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . --parallel
#
# Using CMakePresets.json (recommended):
#   cmake --preset default          # Debug + tests + examples
#   cmake --preset release          # Optimized, no tests
#   cmake --preset dev              # Debug + tests only
#   cmake --build --preset default
#   ctest --preset default
#
# CLion (MinGW):
#   Open this directory as CMake project.
#   Settings > Build > Toolchains > MinGW (auto-detected)
#   Build > Build Project (Ctrl+F9)
#
# MinGW command line:
#   mkdir build && cd build
#   cmake -G "MinGW Makefiles" .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . --parallel
#
# Options:
#   -DGENIE_BUILD_TESTS=ON/OFF      Build test executables (default: ON)
#   -DGENIE_BUILD_EXAMPLES=ON/OFF   Build example programs  (default: ON)
#   -DGENIE_STATIC_LINK=ON/OFF      Static link on MinGW    (default: ON)
#   -DGENIE_INSTALL_CONFIG=ON/OFF   Install config template (default: ON)
#
# Targets:
#   metis-genie-platform           Production server executable
#   test_genie            Unit tests
#   test_integration      Integration tests (network/API)
#   test_tier3            Tier 3 feature tests
#   test_rest_endpoints   REST API endpoint tests (all 34)
#   demo_genie            Quick demo
#   example_rest_api      REST API example
#   example_portfolio     Portfolio example
#   example_middleware    Middleware example
#   example_trading       Trading system example
#   example_tier2         Tier 2 analytics example
#
# Copyright (c) 2026 Bennie Shearer. MIT License.
# ==========================================================================

cmake_minimum_required(VERSION 3.16)

# Read version from VERSION.txt (single source of truth)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" METIS_VERSION)
string(STRIP "${METIS_VERSION}" METIS_VERSION)

project(Metis_Genie_Platform
    VERSION ${METIS_VERSION}
    DESCRIPTION "Metis Genie Platform Enterprise Investment Management Platform"
    LANGUAGES C CXX
)

# ---- Options ----
option(GENIE_BUILD_TESTS    "Build test executables"     ON)
option(GENIE_BUILD_EXAMPLES "Build example programs"     ON)
option(GENIE_STATIC_LINK    "Static link on MinGW"       ON)
option(GENIE_INSTALL_CONFIG "Install config template"    ON)

# ---- C++ Standard ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Build Type Default ----
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ---- Compiler Warnings ----
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    else()
        add_compile_options(-O0 -g)
    endif()
    # MinGW: large header-only TUs need big-obj
    if(WIN32)
        add_compile_options(-Wa,-mbig-obj)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive- /bigobj)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
endif()

# ---- Static Linking (MinGW) ----
if(MINGW AND GENIE_STATIC_LINK)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    message(STATUS "MinGW: static linking enabled (no DLL dependencies)")
endif()

# ---- Platform Detection ----
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ==========================================================================
# SQLite3 (bundled amalgamation - zero external deps)
# ==========================================================================
set(SQLITE3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3")
if(EXISTS "${SQLITE3_DIR}/sqlite3.c")
    add_library(sqlite3_lib STATIC "${SQLITE3_DIR}/sqlite3.c")
    target_include_directories(sqlite3_lib PUBLIC "${SQLITE3_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sqlite3_lib PRIVATE -w)
    elseif(MSVC)
        target_compile_options(sqlite3_lib PRIVATE /w)
    endif()
    target_compile_definitions(sqlite3_lib PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_JSON1
        SQLITE_DQS=0
    )
    message(STATUS "SQLite3: bundled amalgamation")
else()
    message(FATAL_ERROR "SQLite3 not found at ${SQLITE3_DIR}. "
        "Download sqlite-amalgamation from https://www.sqlite.org/download.html")
endif()

# ==========================================================================
# Header-Only Library Target
# ==========================================================================
add_library(genie INTERFACE)
target_include_directories(genie INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${SQLITE3_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(genie INTERFACE Threads::Threads sqlite3_lib)

target_compile_definitions(genie INTERFACE
    GENIE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    GENIE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    GENIE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ==========================================================================
# Platform TLS / Networking
# ==========================================================================
if(WIN32)
    target_link_libraries(genie INTERFACE ws2_32 winhttp crypt32)
    target_compile_definitions(genie INTERFACE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    message(STATUS "TLS: WinHTTP | Networking: Winsock2")
elseif(APPLE)
    find_library(SECURITY_FW Security)
    find_library(COREFOUNDATION_FW CoreFoundation)
    if(SECURITY_FW AND COREFOUNDATION_FW)
        target_link_libraries(genie INTERFACE ${SECURITY_FW} ${COREFOUNDATION_FW})
        message(STATUS "TLS: SecureTransport")
    else()
        message(WARNING "Security.framework not found - HTTPS disabled")
    endif()
else()
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        target_link_libraries(genie INTERFACE OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "TLS: OpenSSL ${OPENSSL_VERSION}")
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(OPENSSL QUIET openssl)
            if(OPENSSL_FOUND)
                target_include_directories(genie INTERFACE ${OPENSSL_INCLUDE_DIRS})
                target_link_libraries(genie INTERFACE ${OPENSSL_LIBRARIES})
                message(STATUS "TLS: OpenSSL (pkg-config)")
            endif()
        endif()
        if(NOT OPENSSL_FOUND)
            target_link_libraries(genie INTERFACE ssl crypto)
            message(STATUS "TLS: OpenSSL (direct - install libssl-dev if build fails)")
        endif()
    endif()
endif()

# ==========================================================================
# Production Executable
# ==========================================================================
add_executable(metis-genie-platform src/main.cpp)
target_link_libraries(metis-genie-platform PRIVATE genie)
set_target_properties(metis-genie-platform PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "metis-genie-platform"
)

# ==========================================================================
# Copy runtime assets to bin/ on every build so the exe finds them at startup.
# Layout after sync:
#   bin/metis-genie-platform.exe  <- executable
#   bin/config.pson               <- server config (read at startup)
#   bin/web/                      <- client HTML/JS/CSS (served as static files)
#   bin/web/config.pson           <- client JS config (fetched by browser)
# server.static_dir in config.pson must be "web" (relative to exe location).
# ==========================================================================
add_custom_target(sync_web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../client"
        "${CMAKE_BINARY_DIR}/bin/web"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.pson"
        "${CMAKE_BINARY_DIR}/bin/config.pson"
    COMMENT "Syncing client web/ and server config.pson to bin/"
)
add_dependencies(metis-genie-platform sync_web_assets)

# ==========================================================================
# Tests
# ==========================================================================
if(GENIE_BUILD_TESTS)
    enable_testing()

    macro(add_genie_test name src_file test_label)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${src_file}")
            add_executable(${name} tests/${src_file})
            target_link_libraries(${name} PRIVATE genie)
            set_target_properties(${name} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            )
            add_test(NAME ${test_label} COMMAND ${name})
            message(STATUS "Test: ${name} (${test_label})")
        endif()
    endmacro()

    add_genie_test(test_genie          test_genie.cpp          UnitTests)
    add_genie_test(test_integration    test_integration.cpp    IntegrationTests)
    add_genie_test(test_tier3          test_tier3.cpp          Tier3Tests)
    add_genie_test(test_rest_endpoints test_rest_endpoints.cpp RestEndpointTests)
endif()

# ==========================================================================
# Examples
# ==========================================================================
if(GENIE_BUILD_EXAMPLES)
    macro(add_genie_example name)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/${name}.cpp")
            add_executable(${name} examples/${name}.cpp)
            target_link_libraries(${name} PRIVATE genie)
            set_target_properties(${name} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            )
        endif()
    endmacro()

    add_genie_example(demo_genie)
    add_genie_example(example_rest_api)
    add_genie_example(example_portfolio)
    add_genie_example(example_middleware)
    add_genie_example(example_trading)
    add_genie_example(example_tier2_analytics)
endif()

# ==========================================================================
# Install
# ==========================================================================
include(GNUInstallDirs)

install(TARGETS metis-genie-platform
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/genie
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

if(GENIE_INSTALL_CONFIG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.pson")
    install(FILES config.pson
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/metis-genie-platform
    )
endif()

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    OPTIONAL
    FILES_MATCHING PATTERN "*.md"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../client")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../client/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/web
    )
    install(FILES config.pson
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ==========================================================================
# CPack
# ==========================================================================
set(CPACK_PACKAGE_NAME "metis-genie-platform")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Bennie Shearer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl-dev")
endif()

include(CPack)

# ==========================================================================
# Summary
# ==========================================================================
message(STATUS "")
message(STATUS "=== Metis Genie Platform v${PROJECT_VERSION} ===")
message(STATUS "  Build:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Tests:    ${GENIE_BUILD_TESTS}")
message(STATUS "  Examples: ${GENIE_BUILD_EXAMPLES}")
message(STATUS "  Install:  ${CMAKE_INSTALL_PREFIX}")
message(STATUS "================================")
message(STATUS "")
