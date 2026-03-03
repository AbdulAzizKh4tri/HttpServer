cmake_minimum_required(VERSION 3.28)
project(HttpServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_BASE_DIR "/home/pair/cmake-source-cache" CACHE PATH "Global FetchContent cache" FORCE)

add_executable(server server.cpp)
target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/include)

# fmt
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# OpenSSL (system)
find_package(OpenSSL REQUIRED)

target_link_libraries(server PRIVATE
  fmt::fmt
  spdlog::spdlog
  OpenSSL::SSL
  OpenSSL::Crypto
)
