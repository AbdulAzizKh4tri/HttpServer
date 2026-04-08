#pragma once

#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "CookieStore.hpp"
#include "HeaderStore.hpp"
#include "ServerConfig.hpp"
#include "Task.hpp"

using NextChunkFn = std::move_only_function<Task<std::optional<std::string>>()>;

class HttpStreamResponse {
public:
  HeaderStore headers;
  CookieStore cookies;

  static bool serializeChunkInto(std::string_view chunk, std::vector<unsigned char> &buf) {
    char hexBuf[16];
    auto [ptr, ec] = std::to_chars(hexBuf, hexBuf + sizeof(hexBuf), chunk.size(), 16);
    size_t hexLen = ptr - hexBuf;

    size_t oldSize = buf.size();

    if (oldSize + chunk.size() + hexLen + 4 > ServerConfig::MAX_WRITE_BUFFER_BYTES) {
      SPDLOG_WARN("Write buffer limit would be exceeded, Closing Connection");
      return false;
    }
    buf.resize(oldSize + chunk.size() + hexLen + 4);
    size_t offset = oldSize + hexLen;

    std::memcpy(buf.data() + oldSize, hexBuf, hexLen);
    buf[offset++] = '\r';
    buf[offset++] = '\n';
    std::memcpy(buf.data() + offset, chunk.data(), chunk.size());
    offset += chunk.size();
    buf[offset++] = '\r';
    buf[offset++] = '\n';
    return true;
  }

  HttpStreamResponse();

  HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn);

  HttpStreamResponse(int statusCode, const std::string &contentType, NextChunkFn nextChunkFn);

  Task<std::optional<std::string>> getNextChunk();

  bool serializeHeaderInto(std::vector<unsigned char> &buf) const;

  std::string getContentType() const;

  NextChunkFn takeNextChunkFn();
  void setNextChunkFn(NextChunkFn nextChunkFn);

  std::string getVersion() const;
  int getStatusCode() const;

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";

  NextChunkFn nextChunkFn_;
};
