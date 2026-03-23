#include "HttpResponse.hpp"
#include "serverConfig.hpp"

HttpResponse::HttpResponse() : statusCode_(-1) {
  setHeader("Server", SERVER_NAME);
}

HttpResponse::HttpResponse(int statusCode) : statusCode_(statusCode) {
  setHeader("Content-Length", std::to_string(body_.size()));
  setHeader("Server", SERVER_NAME);
}

HttpResponse::HttpResponse(int statusCode, std::string body)
    : statusCode_(statusCode), body_(body) {
  setHeader("Content-Length", std::to_string(body_.size()));
  setHeader("Server", SERVER_NAME);
}

std::vector<unsigned char> HttpResponse::serialize() const {
  const std::string &statusTxt = HttpResponse::statusText(statusCode_);
  bool hasBody = !std::ranges::contains(noBody, statusCode_);

  size_t size = version_.size() + 1 + 3 + 1 + statusTxt.size() + 2;

  for (auto &[k, v] : headers_) {
    if (!hasBody && k == "content-length")
      continue;
    size += headerLineSize(k, v);
  }
  size += 2; // final \r\n

  if (hasBody)
    size += body_.size();

  std::vector<unsigned char> serializedResponse(size);
  size_t offset = 0;

  auto write = [&](std::string_view s) {
    std::memcpy(serializedResponse.data() + offset, s.data(), s.size());
    offset += s.size();
  };
  auto writeChar = [&](char c) { serializedResponse[offset++] = c; };

  char statusBuf[3];
  std::to_chars(statusBuf, statusBuf + 3, statusCode_);

  write(version_);
  writeChar(' ');
  write(std::string_view(statusBuf, 3));
  writeChar(' ');
  write(statusTxt);
  write("\r\n");

  for (auto &[k, v] : headers_) {
    if (!hasBody && k == "content-length")
      continue;
    write(k);
    write(": ");
    write(v);
    write("\r\n");
  }
  write("\r\n");

  if (hasBody)
    write(body_);

  return serializedResponse;
}

std::string HttpResponse::getHeader(const std::string &name) const {
  return getLastOrDefault(headers_, toLowerCase(name), "");
}

std::vector<std::string>
HttpResponse::getHeaders(const std::string &name) const {
  return getAllValues(headers_, toLowerCase(name));
}

std::vector<std::pair<std::string, std::string>>
HttpResponse::getAllHeaders() const {
  return headers_;
}

void HttpResponse::setHeader(const std::string &name,
                             const std::string &value) {
  std::string lowerKey = toLowerCase(name);
  std::erase_if(headers_,
                [&lowerKey](const auto &p) { return p.first == lowerKey; });
  headers_.emplace_back(lowerKey, value);
}

void HttpResponse::addHeader(const std::string &name,
                             const std::string &value) {
  auto key = toLowerCase(name);
  if (std::ranges::contains(singletonHeaders_, key)) {
    if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) {
          return p.first == key;
        }) == headers_.end())
      headers_.emplace_back(key, value);
    return;
  }
  headers_.emplace_back(key, value);
}

void HttpResponse::removeHeader(const std::string &name) {
  auto key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
}

void HttpResponse::setBody(const std::string &body) {
  body_ = body;
  setHeader("Content-Length", std::to_string(body_.size()));
}

void HttpResponse::stripBody() { body_ = ""; }

void HttpResponse::setVersion(const std::string &version) {
  version_ = version;
}
void HttpResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

std::string HttpResponse::getBody() const { return body_; }
size_t HttpResponse::getBodySize() const { return body_.size(); }

std::string HttpResponse::getVersion() const { return version_; }
int HttpResponse::getStatusCode() const { return statusCode_; }
