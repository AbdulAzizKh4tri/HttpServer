#include "HttpResponse.hpp"

#include "serverConfig.hpp"

HttpResponse::HttpResponse() : statusCode_(-1) {}

HttpResponse::HttpResponse(int statusCode) : statusCode_(statusCode) {
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

HttpResponse::HttpResponse(int statusCode, std::string body) : statusCode_(statusCode), body_(body) {
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

void HttpResponse::serializeInto(std::vector<unsigned char> &buf) const {
  const std::string &statusTxt = HttpResponse::statusText(statusCode_);
  bool hasBody = !std::ranges::contains(noBody, statusCode_);

  size_t size = version_.size() + 1 + 3 + 1 + statusTxt.size() + 2;

  for (auto &[k, v] : headers.getAllHeaders()) {
    if (!hasBody && k == "content-length")
      continue;
    size += k.size() + 2 + v.size() + 2;
  }

  size += strlen("server") + strlen(SERVER_NAME) + 4;

  const auto &date = getCurrentHttpDate();
  size += strlen("date") + date.size() + 4;

  size += cookies.getSerializedSize();
  size += 2; // final \r\n

  if (hasBody)
    size += body_.size();

  size_t oldSize = buf.size();
  buf.resize(oldSize + size);

  auto write = [&](std::string_view s) {
    std::memcpy(buf.data() + oldSize, s.data(), s.size());
    oldSize += s.size();
  };
  auto writeChar = [&](char c) { buf[oldSize++] = c; };

  char statusBuf[3];
  std::to_chars(statusBuf, statusBuf + 3, statusCode_);

  write(version_);
  writeChar(' ');
  write(std::string_view(statusBuf, 3));
  writeChar(' ');
  write(statusTxt);
  write("\r\n");

  write("server: ");
  write(SERVER_NAME);
  write("\r\n");

  write("date: ");
  write(date);
  write("\r\n");

  for (auto &[k, v] : headers.getAllHeaders()) {
    if (!hasBody && k == "content-length")
      continue;
    write(k);
    write(": ");
    write(v);
    write("\r\n");
  }

  cookies.serializeUsing(write);

  write("\r\n");

  if (hasBody)
    write(body_);
}

void HttpResponse::setBody(const std::string &body) {
  body_ = body;
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

void HttpResponse::stripBody() { body_ = ""; }

void HttpResponse::setVersion(const std::string &version) { version_ = version; }
void HttpResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

std::string HttpResponse::getBody() const { return body_; }
size_t HttpResponse::getBodySize() const { return body_.size(); }

std::string HttpResponse::getVersion() const { return version_; }
int HttpResponse::getStatusCode() const { return statusCode_; }
