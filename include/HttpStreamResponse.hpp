#pragma once

#include <functional>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

#include "HttpResponse.hpp"

using NextChunkLambda = std::function<std::optional<std::string>()>;

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

  HttpStreamResponse() : statusCode_(-1) {
    setHeader("Server", "Azooz's Chad Compiled C++ Server");
  }

  HttpStreamResponse(int statusCode, NextChunkLambda nextChunkLambda)
      : statusCode_(statusCode), nextChunkLambda_(nextChunkLambda) {
    setHeader("Server", "Azooz's Chad Compiled C++ Server");
    setHeader("Transfer-Encoding", "chunked");
  }

  std::optional<std::string> getNextChunk() { return nextChunkLambda_(); }

  std::vector<unsigned char> getSerializedHeader() const {

    const std::string &statText = HttpResponse::statusText(statusCode_);

    size_t size = version_.size() + 1 + 3 + 1 + statText.size() + 2;

    for (const auto &[k, v] : headers_)
      size += k.size() + 2 + v.size() + 2;
    size += 2;

    std::vector<unsigned char> serializedHeader(size);
    size_t offset = 0;

    auto write = [&](std::string_view s) {
      std::memcpy(serializedHeader.data() + offset, s.data(), s.size());
      offset += s.size();
    };
    auto writeChar = [&](char c) { serializedHeader[offset++] = c; };

    char statusBuf[3];
    std::to_chars(statusBuf, statusBuf + 3, statusCode_);

    write(version_);
    writeChar(' ');
    write(std::string_view(statusBuf, 3));
    writeChar(' ');
    write(statText);
    write("\r\n");

    for (const auto &[k, v] : headers_) {
      write(k);
      write(": ");
      write(v);
      write("\r\n");
    }
    write("\r\n");

    return serializedHeader;
  }

  std::string getHeader(const std::string &name) const {
    return getLastOrDefault(headers_, toLowerCase(name), "");
  }

  std::vector<std::string> getHeaders(const std::string &name) const {
    return getAllValues(headers_, toLowerCase(name));
  }

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const {
    return headers_;
  }

  void setHeader(const std::string &name, const std::string &value) {
    std::string lowerKey = toLowerCase(name);
    std::erase_if(headers_,
                  [&lowerKey](const auto &p) { return p.first == lowerKey; });
    headers_.emplace_back(lowerKey, value);
  }

  void addHeader(const std::string &name, const std::string &value) {
    auto key = toLowerCase(name);
    if (std::ranges::contains(HttpResponse::singletonHeaders_, key)) {
      if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) {
            return p.first == key;
          }) == headers_.end())
        headers_.emplace_back(key, value);
      return;
    }
    headers_.emplace_back(key, value);
  }

  void removeHeader(const std::string &name) {
    auto key = toLowerCase(name);
    std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  }

  std::string getVersion() const { return version_; }
  int getStatusCode() const { return statusCode_; }

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";
  std::vector<std::pair<std::string, std::string>> headers_;

  NextChunkLambda nextChunkLambda_;
};
