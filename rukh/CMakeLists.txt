cmake_minimum_required(VERSION 3.28)
project(rukh)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(ExternalProject)

# ── Sources ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE RUKH_SOURCES "src/*.cpp")
add_library(rukh STATIC ${RUKH_SOURCES})

# ── Compile options ───────────────────────────────────────────────────────────
target_compile_definitions(rukh PRIVATE
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=0,SPDLOG_ACTIVE_LEVEL=2>
)
target_compile_options(rukh PRIVATE
  $<IF:$<CONFIG:Release>,-O3,-O0 -g>
)

# ── Include paths ─────────────────────────────────────────────────────────────
# PUBLIC  → consumers get <rukh/HttpRequest> etc.
# PRIVATE → src/*.cpp can write #include "HttpRequest.hpp"
#            or #include "core/Executor.hpp"
target_include_directories(rukh
  PUBLIC  include/
  PRIVATE external/
)

# ── SQLite (amalgamation) ──────────────────────────────────────────────────
option(RUKH_ENABLE_SQLITE "Enable SQLite database backend" ON)

if(RUKH_ENABLE_SQLITE)
  # OBJECT, not STATIC: it's a single .c file only rukh consumes — no
  # reason to pay for building a standalone .a archive.
  add_library(sqlite3_amalgamation OBJECT external/sqlite3/sqlite3.c)

  target_include_directories(sqlite3_amalgamation PUBLIC external/sqlite3)

  target_compile_definitions(sqlite3_amalgamation PUBLIC
    SQLITE_THREADSAFE=2
  )

  # Always optimize the amalgamation, even in a Debug build of rukh.
  # You'll never want to single-step into SQLite's own internals, and
  # its assert-heavy debug paths are dramatically slower.
  target_compile_options(sqlite3_amalgamation PRIVATE -O2)

  target_compile_definitions(rukh PRIVATE RUKH_ENABLE_SQLITE)

  # PRIVATE is the whole point here: rukh's *usage requirements* from
  # sqlite3_amalgamation (its include path, its defines) stay private to
  # rukh and do not propagate to anything that links rukh::rukh.
  target_link_libraries(rukh PRIVATE sqlite3_amalgamation)
endif()

# ── liburing (ExternalProject) ────────────────────────────────────────────────
ExternalProject_Add(liburing_ext
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/liburing
  CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(liburing_ext INSTALL_DIR)
file(MAKE_DIRECTORY ${INSTALL_DIR}/include)

add_library(liburing_imported STATIC IMPORTED)
set_target_properties(liburing_imported PROPERTIES
  IMPORTED_LOCATION             ${INSTALL_DIR}/lib/liburing.a
  INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include
)
add_dependencies(liburing_imported liburing_ext)

# ── System dependencies ───────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(ZLIB    REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(BROTLIENC    REQUIRED IMPORTED_TARGET libbrotlienc)
pkg_check_modules(BROTLICOMMON REQUIRED IMPORTED_TARGET libbrotlicommon)

# ── Link ──────────────────────────────────────────────────────────────────────
# PUBLIC because consumers transitively need these symbols
# (e.g. OpenSSL types appear in TlsStream.hpp which is in core/)
target_link_libraries(rukh PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  ZLIB::ZLIB
  liburing_imported
  PkgConfig::BROTLIENC
  PkgConfig::BROTLICOMMON
)

# ── PCH ───────────────────────────────────────────────────────────────────────
target_precompile_headers(rukh PRIVATE
  # third-party
  <openssl/ssl.h>
  <openssl/err.h>
  <brotli/encode.h>
  <liburing.h>

  # stdlib
  <string>
  <string_view>
  <vector>
  <unordered_map>
  <unordered_set>
  <map>
  <set>
  <array>
  <deque>
  <span>
  <optional>
  <variant>
  <expected>
  <memory>
  <functional>
  <algorithm>
  <numeric>
  <chrono>
  <thread>
  <mutex>
  <shared_mutex>
  <atomic>
  <filesystem>
  <format>
  <ranges>
  <source_location>
  <cstdint>
  <cstddef>
  <cassert>
  <exception>
  <stdexcept>
)

# ── Alias ─────────────────────────────────────────────────────────────────────
add_library(rukh::rukh ALIAS rukh)
