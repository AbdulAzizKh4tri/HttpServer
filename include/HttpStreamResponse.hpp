#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Task.hpp"

using NextChunkFn = std::move_only_function<Task<std::optional<std::string>>()>;

class HttpStreamResponse {
public:
  static std::vector<unsigned char> serializeChunk(std::string_view chunk) {
    char hexBuf[16];
    auto [ptr, ec] =
        std::to_chars(hexBuf, hexBuf + sizeof(hexBuf), chunk.size(), 16);
    size_t hexLen = ptr - hexBuf;

    std::vector<unsigned char> chunkBytes(chunk.size() + hexLen + 4);
    size_t offset = hexLen;

    std::memcpy(chunkBytes.data(), hexBuf, hexLen);
    chunkBytes[offset++] = '\r';
    chunkBytes[offset++] = '\n';
    std::memcpy(chunkBytes.data() + offset, chunk.data(), chunk.size());
    offset += chunk.size();
    chunkBytes[offset++] = '\r';
    chunkBytes[offset++] = '\n';

    return chunkBytes;
  }

  HttpStreamResponse();

  HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn);

  Task<std::optional<std::string>> getNextChunk();

  std::vector<unsigned char> getSerializedHeader() const;

  std::string getHeader(const std::string &name) const;

  std::vector<std::string> getHeaders(const std::string &name) const;

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  void setHeader(const std::string &name, const std::string &value);

  void addHeader(const std::string &name, const std::string &value);

  void removeHeader(const std::string &name);

  std::string getVersion() const;
  int getStatusCode() const;

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";
  std::vector<std::pair<std::string, std::string>> headers_;

  NextChunkFn nextChunkFn;
};
