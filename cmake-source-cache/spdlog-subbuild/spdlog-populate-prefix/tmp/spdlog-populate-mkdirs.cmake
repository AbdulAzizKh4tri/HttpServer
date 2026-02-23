# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-src")
  file(MAKE_DIRECTORY "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-src")
endif()
file(MAKE_DIRECTORY
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-build"
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix"
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/tmp"
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp"
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/src"
  "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ubkn0909/dev/pair/HttpServer/cmake-source-cache/spdlog-subbuild/spdlog-populate-prefix/src/spdlog-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
