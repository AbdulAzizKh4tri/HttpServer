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

HttpStreamResponse::HttpStreamResponse(int statusCode, const std::string &contentType, NextChunkFn nextChunkFn)
    : statusCode_(statusCode), nextChunkFn(std::move(nextChunkFn)) {
  headers.setHeaderLower("transfer-encoding", "chunked");
  headers.setHeaderLower("content-type", contentType);
}

Task<std::optional<std::string>> HttpStreamResponse::getNextChunk() { co_return co_await nextChunkFn(); }

void HttpStreamResponse::serializeHeaderInto(std::vector<unsigned char> &buf) const {

  const std::string_view statusLine = HttpResponse::getStatusLine(statusCode_);

  size_t size = statusLine.size();

  for (const auto &[k, v] : headers.getAllHeaders())
    size += k.size() + 2 + v.size() + 2;

  size += ServerConfig::SERVER_LINE.size();

  const auto &date = getCurrentHttpDate();
  size += (sizeof("date") - 1) + date.size() + 4;

  size += cookies.getSerializedSize();

  size += 2; // final \r\n

  size_t oldSize = buf.size();
  buf.resize(oldSize + size);
  unsigned char *out = buf.data() + oldSize;

  auto write = [&out](std::string_view s) {
    std::memcpy(out, s.data(), s.size());
    out += s.size();
  };

  write(statusLine);

  write(ServerConfig::SERVER_LINE);

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
