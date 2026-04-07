#pragma once

#include "BrotliCompressor.hpp"
#include "GzipCompressor.hpp"
#include "ICompressor.hpp"

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
