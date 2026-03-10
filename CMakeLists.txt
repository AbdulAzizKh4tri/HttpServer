cmake_minimum_required(VERSION 3.28)
project(HttpServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(server server.cpp)

target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/external)

# OpenSSL (system)
find_package(OpenSSL REQUIRED)

target_link_libraries(server PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_precompile_headers(server PRIVATE
  # heavy third-party headers — compiled once, reused every build
  <nlohmann/json.hpp>
  <spdlog/spdlog.h>
  <spdlog/sinks/basic_file_sink.h>
  <openssl/ssl.h>
  <openssl/err.h>

  # stdlib headers used across nearly every file
  <string>
  <vector>
  <unordered_map>
  <functional>
  <memory>
  <optional>
  <variant>
  <expected>
  <span>
  <string_view>
)
