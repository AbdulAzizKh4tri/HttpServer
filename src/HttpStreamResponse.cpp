#include "HttpStreamResponse.hpp"

#include <spdlog/spdlog.h>

#include "HttpResponse.hpp"
#include "Task.hpp"
#include "serverConfig.hpp"

HttpStreamResponse::HttpStreamResponse() : statusCode_(-1) {
  setHeader("Server", SERVER_NAME);
}

HttpStreamResponse::HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn)
    : statusCode_(statusCode), nextChunkFn(std::move(nextChunkFn)) {
  setHeader("Server", SERVER_NAME);
  setHeader("Transfer-Encoding", "chunked");
}

Task<std::optional<std::string>> HttpStreamResponse::getNextChunk() {
  co_return co_await nextChunkFn();
}

std::vector<unsigned char> HttpStreamResponse::getSerializedHeader() const {

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

std::string HttpStreamResponse::getHeader(const std::string &name) const {
  return getLastOrDefault(headers_, toLowerCase(name), "");
}

std::vector<std::string>
HttpStreamResponse::getHeaders(const std::string &name) const {
  return getAllValues(headers_, toLowerCase(name));
}

std::vector<std::pair<std::string, std::string>>
HttpStreamResponse::getAllHeaders() const {
  return headers_;
}

void HttpStreamResponse::setHeader(const std::string &name,
                                   const std::string &value) {
  std::string lowerKey = toLowerCase(name);
  std::erase_if(headers_,
                [&lowerKey](const auto &p) { return p.first == lowerKey; });
  headers_.emplace_back(lowerKey, value);
}

void HttpStreamResponse::addHeader(const std::string &name,
                                   const std::string &value) {
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

void HttpStreamResponse::removeHeader(const std::string &name) {
  auto key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
}

std::string HttpStreamResponse::getVersion() const { return version_; }
int HttpStreamResponse::getStatusCode() const { return statusCode_; }
