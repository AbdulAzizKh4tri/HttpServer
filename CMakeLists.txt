cmake_minimum_required(VERSION 3.28)
project(HttpServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(ExternalProject)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(server server.cpp routes.cpp ${SOURCES})

target_compile_definitions(server PRIVATE
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=0,SPDLOG_ACTIVE_LEVEL=2>
)
target_compile_options(server PRIVATE $<IF:$<CONFIG:Release>,-O3,-O0 -g>)

target_include_directories(server PRIVATE 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}
)
target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/external)

ExternalProject_Add(liburing_ext
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/liburing
  CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(liburing_ext INSTALL_DIR)

file(MAKE_DIRECTORY ${INSTALL_DIR}/include)  # ← add this

add_library(liburing STATIC IMPORTED)
set_target_properties(liburing PROPERTIES
  IMPORTED_LOCATION ${INSTALL_DIR}/lib/liburing.a
  INTERFACE_INCLUDE_DIRECTORIES ${INSTALL_DIR}/include
)
add_dependencies(liburing liburing_ext)

# OpenSSL (system)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(BROTLIENC REQUIRED IMPORTED_TARGET libbrotlienc)
pkg_check_modules(BROTLICOMMON REQUIRED IMPORTED_TARGET libbrotlicommon)

target_link_libraries(server PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ZLIB::ZLIB
  liburing
  PkgConfig::BROTLIENC
  PkgConfig::BROTLICOMMON
)

target_precompile_headers(server PRIVATE
  # heavy third-party headers — compiled once, reused every build
  <nlohmann/json.hpp>
  <spdlog/spdlog.h>
  <spdlog/sinks/basic_file_sink.h>
  <openssl/ssl.h>
  <openssl/err.h>
  <brotli/encode.h>
  <liburing.h>

  # stdlib headers used across nearly every file
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
  <expected>          # C++23
  <memory>
  <functional>
  <algorithm>
  <numeric>
  <chrono>
  <thread>
  <mutex>
  <shared_mutex>
  <atomic>
  <filesystem>        # very common in servers
  <format>            # C++20/23
  <ranges>            # C++20
  <source_location>   # very useful with C++23
  <cstdint>
  <cstddef>
  <cassert>
  <exception>
  <stdexcept>
)
