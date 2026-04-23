#pragma once

#include <rukh/core/BrotliCompressor.hpp>
#include <rukh/core/GzipCompressor.hpp>
#include <rukh/core/ICompressor.hpp>

namespace rukh {

inline std::unique_ptr<ICompressor> getCompressor(std::string_view encoding) {
  if (encoding == "identity")
    return nullptr;
  if (encoding == "br")
    return std::make_unique<BrotliCompressor>();
  if (encoding == "gzip")
    return std::make_unique<GzipCompressor>();
  if (encoding == "*")
    return std::make_unique<BrotliCompressor>(); // default
  return nullptr;
}
} // namespace rukh
