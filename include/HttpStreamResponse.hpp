#pragma once

#include <optional>
#include <string>
#include <vector>

#include "CookieStore.hpp"
#include "HeaderStore.hpp"
#include "Task.hpp"

using NextChunkFn = std::move_only_function<Task<std::optional<std::string>>()>;

class HttpStreamResponse {
public:
  HeaderStore headers;
  CookieStore cookies;

  static void serializeChunkInto(std::string_view chunk, std::vector<unsigned char> &buf) {
    char hexBuf[16];
    auto [ptr, ec] = std::to_chars(hexBuf, hexBuf + sizeof(hexBuf), chunk.size(), 16);
    size_t hexLen = ptr - hexBuf;

    size_t oldSize = buf.size();
    buf.resize(oldSize + chunk.size() + hexLen + 4);
    size_t offset = oldSize + hexLen;

    std::memcpy(buf.data() + oldSize, hexBuf, hexLen);
    buf[offset++] = '\r';
    buf[offset++] = '\n';
    std::memcpy(buf.data() + offset, chunk.data(), chunk.size());
    offset += chunk.size();
    buf[offset++] = '\r';
    buf[offset++] = '\n';
  }

  HttpStreamResponse();

  HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn);

  Task<std::optional<std::string>> getNextChunk();

  void serializeHeaderInto(std::vector<unsigned char> &buf) const;

  std::string getVersion() const;
  int getStatusCode() const;

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";

  NextChunkFn nextChunkFn;
};
