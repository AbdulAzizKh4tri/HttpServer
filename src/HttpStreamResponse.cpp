#include "HttpStreamResponse.hpp"

#include <spdlog/spdlog.h>

#include "HttpResponse.hpp"
#include "ServerConfig.hpp"
#include "Task.hpp"
#include "utils.hpp"

HttpStreamResponse::HttpStreamResponse() : statusCode_(-1) {}

HttpStreamResponse::HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn)
    : statusCode_(statusCode), nextChunkFn(std::move(nextChunkFn)) {
  headers.setHeaderLower("transfer-encoding", "chunked");
}

Task<std::optional<std::string>> HttpStreamResponse::getNextChunk() { co_return co_await nextChunkFn(); }

void HttpStreamResponse::serializeHeaderInto(std::vector<unsigned char> &buf) const {

  const std::string &statText = HttpResponse::statusText(statusCode_);

  size_t size = version_.size() + 1 + 3 + 1 + statText.size() + 2;

  for (const auto &[k, v] : headers.getAllHeaders())
    size += k.size() + 2 + v.size() + 2;

  size += strlen("server") + strlen(ServerConfig::SERVER_NAME) + 4;

  const auto &date = getCurrentHttpDate();
  size += strlen("date") + date.size() + 4;

  size += cookies.getSerializedSize();

  size += 2; // final \r\n

  size_t oldSize = buf.size();
  buf.resize(oldSize + size);
  size_t offset = oldSize;

  auto write = [&](std::string_view s) {
    std::memcpy(buf.data() + offset, s.data(), s.size());
    offset += s.size();
  };
  auto writeChar = [&](char c) { buf[offset++] = c; };

  char statusBuf[3];
  std::to_chars(statusBuf, statusBuf + 3, statusCode_);

  write(version_);
  writeChar(' ');
  write(std::string_view(statusBuf, 3));
  writeChar(' ');
  write(statText);
  write("\r\n");

  write("server: ");
  write(ServerConfig::SERVER_NAME);
  write("\r\n");

  write("date: ");
  write(date);
  write("\r\n");

  for (const auto &[k, v] : headers.getAllHeaders()) {
    write(k);
    write(": ");
    write(v);
    write("\r\n");
  }

  cookies.serializeUsing(write);
  write("\r\n");
}

std::string HttpStreamResponse::getVersion() const { return version_; }
int HttpStreamResponse::getStatusCode() const { return statusCode_; }
