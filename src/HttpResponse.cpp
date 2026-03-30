#include "HttpResponse.hpp"
#include "serverConfig.hpp"
#include <chrono>

HttpResponse::HttpResponse() : statusCode_(-1) { headers_.reserve(4); }

HttpResponse::HttpResponse(int statusCode) : statusCode_(statusCode) {
  headers_.reserve(4);
  setHeaderLower("content-length", std::to_string(body_.size()));
}

HttpResponse::HttpResponse(int statusCode, std::string body) : statusCode_(statusCode), body_(body) {
  headers_.reserve(4);
  setHeaderLower("content-length", std::to_string(body_.size()));
}

void HttpResponse::serializeInto(std::vector<unsigned char> &buf) const {
  const std::string &statusTxt = HttpResponse::statusText(statusCode_);
  bool hasBody = !std::ranges::contains(noBody, statusCode_);

  size_t size = version_.size() + 1 + 3 + 1 + statusTxt.size() + 2;

  for (auto &[k, v] : headers_) {
    if (!hasBody && k == "content-length")
      continue;
    size += k.size() + 2 + v.size() + 2;
  }
  size += strlen("server") + strlen(SERVER_NAME) + 4;

  const auto &date = getCurrentHttpDate();
  size += strlen("date") + date.size() + 4;
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
}

void HttpResponse::setCookie(Cookie cookie) {
  std::string cookieString = cookie.name + "=" + cookie.value;
  cookieString += "; Path=" + cookie.path;
  cookieString += "; SameSite=" + cookie.sameSite;

  if (cookie.httpOnly)
    cookieString += "; HttpOnly";
  if (cookie.secure)
    cookieString += "; Secure";

  if (cookie.domain != "")
    cookieString += "; Domain=" + cookie.domain;

  if (cookie.expires != std::chrono::system_clock::time_point::max())
    cookieString += "; Expires=" + toHttpDate(cookie.expires);
  if (cookie.maxAge != -1)
    cookieString += "; Max-Age=" + std::to_string(cookie.maxAge);

  addHeaderLower("set-cookie", cookieString);
}

void HttpResponse::unsetCookie(const std::string &name) {
  std::erase_if(headers_,
                [&name](const auto &p) { return p.first == "set-cookie" && p.second.starts_with(name + "="); });
}

void HttpResponse::deleteCookie(const std::string &name, const std::string &path) {
  Cookie c;
  c.name = name;
  c.value = "";
  c.path = path;
  c.maxAge = 0;
  c.expires = std::chrono::system_clock::from_time_t(0);
  setCookie(c);
}

std::vector<std::pair<std::string, std::string>> HttpResponse::getCookies() const {
  std::vector<std::pair<std::string, std::string>> cookies;
  for (const auto &[k, v] : headers_) {
    if (k != "set-cookie")
      continue;
    auto firstSemi = v.find(';');
    auto nameValue = firstSemi == std::string::npos ? v : v.substr(0, firstSemi);
    auto eq = nameValue.find('=');
    if (eq == std::string::npos)
      continue;
    cookies.emplace_back(nameValue.substr(0, eq), nameValue.substr(eq + 1));
  }
  return cookies;
}

std::optional<std::string> HttpResponse::getCookie(const std::string &name) const {
  for (const auto &cookie : getCookies())
    if (cookie.first == name)
      return cookie.second;
  return std::nullopt;
}

std::string HttpResponse::getHeader(const std::string &name) const {
  return getLastOrDefault(headers_, toLowerCase(name), "");
}

std::vector<std::string> HttpResponse::getHeaders(const std::string &name) const {
  return getAllValues(headers_, toLowerCase(name));
}

std::vector<std::pair<std::string, std::string>> &HttpResponse::getAllHeaders() { return headers_; }

void HttpResponse::setHeader(const std::string &name, const std::string &value) {
  std::string lowerKey = toLowerCase(name);
  std::erase_if(headers_, [&lowerKey](const auto &p) { return p.first == lowerKey; });
  headers_.emplace_back(lowerKey, value);
}

void HttpResponse::setHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  std::erase_if(headers_, [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; });
  headers_.emplace_back(lowercaseKey, value);
}

void HttpResponse::addHeader(const std::string &name, const std::string &value) {
  auto key = toLowerCase(name);
  if (std::ranges::contains(singletonHeaders_, key)) {
    if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) { return p.first == key; }) ==
        headers_.end())
      headers_.emplace_back(key, value);
    return;
  }
  headers_.emplace_back(key, value);
}

void HttpResponse::addHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
    if (std::find_if(headers_.begin(), headers_.end(),
                     [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
      headers_.emplace_back(lowercaseKey, value);
    return;
  }
  headers_.emplace_back(lowercaseKey, value);
}

void HttpResponse::removeHeader(const std::string &name) {
  auto key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
}

void HttpResponse::setBody(const std::string &body) {
  body_ = body;
  setHeaderLower("content-length", std::to_string(body_.size()));
}

void HttpResponse::stripBody() { body_ = ""; }

void HttpResponse::setVersion(const std::string &version) { version_ = version; }
void HttpResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

std::string HttpResponse::getBody() const { return body_; }
size_t HttpResponse::getBodySize() const { return body_.size(); }

std::string HttpResponse::getVersion() const { return version_; }
int HttpResponse::getStatusCode() const { return statusCode_; }
