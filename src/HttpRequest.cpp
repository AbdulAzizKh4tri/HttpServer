#include "HttpRequest.hpp"

#include <charconv>
#include <spdlog/spdlog.h>
#include <string_view>

#include "SessionHandle.hpp"
#include "utils.hpp"

HttpRequest::HttpRequest() {}

bool HttpRequest::parseRequestHeader(std::string_view headerView) {
  auto lineEnd = headerView.find('\n');
  if (lineEnd == std::string_view::npos)
    return false;

  auto requestLine = headerView.substr(0, lineEnd);
  if (not requestLine.empty() && requestLine.back() == '\r')
    requestLine.remove_suffix(1);

  if (not parseRequestLine(requestLine))
    return false;

  headerView.remove_prefix(lineEnd + 1);
  if (not parseRequestHeaders(headerView))
    return false;
  return true;
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getCookies() const {
  auto cookieHeader = getHeaderLower("cookie");
  if (cookieHeader.empty())
    return {};

  std::vector<std::pair<std::string, std::string>> cookies;

  for (;;) {
    auto cookieDelim = cookieHeader.find(';');
    auto cookiePair = cookieDelim == std::string_view::npos ? cookieHeader : cookieHeader.substr(0, cookieDelim);
    trim(cookiePair);
    auto eqpos = cookiePair.find('=');
    if (eqpos != std::string::npos) {
      cookies.emplace_back(cookiePair.substr(0, eqpos), cookiePair.substr(eqpos + 1));
    }

    if (cookieDelim == std::string_view::npos)
      break;

    cookieHeader.remove_prefix(cookieDelim + 1);
  }

  return cookies;
}

std::optional<std::string> HttpRequest::getCookie(const std::string &name) const {
  for (const auto &cookie : getCookies()) {
    if (cookie.first == name)
      return cookie.second;
  }
  return std::nullopt;
}

Task<Session *> HttpRequest::getSession() {
  if (not sessionHandle_)
    co_return nullptr;
  co_return co_await sessionHandle_->get();
}

std::expected<size_t, ContentLengthError> HttpRequest::getContentLength() const {
  auto lenStr = getHeaderLower("content-length");
  if (lenStr.empty())
    return std::unexpected(ContentLengthError::NO_CONTENT_LENGTH_HEADER);

  size_t len;

  auto [ptr, ec] = std::from_chars(lenStr.data(), lenStr.data() + lenStr.size(), len);
  if (ec != std::errc{})
    return std::unexpected(ContentLengthError::INVALID_CONTENT_LENGTH);

  if (len > MAX_CONTENT_LENGTH)
    return std::unexpected(ContentLengthError::CONTENT_LENGTH_TOO_LARGE);
  return len;
}

std::string_view HttpRequest::getHeader(const std::string &name) const {
  auto key = toLowerCase(name);
  auto it = std::find_if(headers_.rbegin(), headers_.rend(), [&key](const auto &p) { return p.first == key; });
  if (it != headers_.rend())
    return it->second;
  return {};
}

std::string_view HttpRequest::getHeaderLower(const std::string &lowerKey) const {
  auto it =
      std::find_if(headers_.rbegin(), headers_.rend(), [&lowerKey](const auto &p) { return p.first == lowerKey; });
  if (it != headers_.rend())
    return it->second;
  return {};
}

std::vector<std::string> HttpRequest::getHeaders(const std::string &name) const {
  return getAllValues(headers_, toLowerCase(name));
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllHeaders() const { return headers_; }

void HttpRequest::setHeader(const std::string &name, const std::string &value) {
  std::string key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  headers_.emplace_back(key, value);
}

void HttpRequest::setHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  std::erase_if(headers_, [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; });
  headers_.emplace_back(lowercaseKey, value);
}

void HttpRequest::addHeader(const std::string &name, const std::string &value) {
  auto key = toLowerCase(name);
  if (std::ranges::contains(singletonHeaders_, key)) {
    if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) { return p.first == key; }) ==
        headers_.end())
      headers_.emplace_back(key, value);
    return;
  }
  headers_.emplace_back(key, value);
}

void HttpRequest::addHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
  if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
    if (std::find_if(headers_.begin(), headers_.end(),
                     [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
      headers_.emplace_back(lowercaseKey, value);
    return;
  }
  headers_.emplace_back(lowercaseKey, value);
}

void HttpRequest::removeHeader(const std::string &name) {
  auto key = toLowerCase(name);
  std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
}

void HttpRequest::setAttribute(const std::string &key, const std::string &value) {
  std::erase_if(attributes_, [&key](const auto &p) { return p.first == key; });
  attributes_.emplace_back(key, value);
}

std::string HttpRequest::getAttribute(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(attributes_, key, defaultValue);
}

std::string HttpRequest::getQueryParam(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(queryParams_, key, defaultValue);
}

std::vector<std::string> HttpRequest::getQueryParams(const std::string &key) const {
  return getAllValues(queryParams_, key);
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllQueryParams() const { return queryParams_; }

std::string HttpRequest::getPathParam(const std::string &key, std::string defaultValue) const {
  return getLastOrDefault(pathParams_, key, defaultValue);
}

void HttpRequest::setPathParams(const std::vector<std::pair<std::string, std::string>> &pathParams) {
  pathParams_ = pathParams;
}

std::vector<std::pair<std::string, std::string>> HttpRequest::getAllPathParams() const { return pathParams_; }

const std::string &HttpRequest::getPath() const { return path_; }
const std::string &HttpRequest::getRawPath() const { return rawPath_; }
const std::string &HttpRequest::getVersion() const { return version_; }

const std::string &HttpRequest::getMethod() const { return method_; }
void HttpRequest::setMethod(const std::string &method) { method_ = method; }

const std::string &HttpRequest::getBody() const { return body_; }
void HttpRequest::setBody(const std::string &body) { body_ = body; }

const std::string &HttpRequest::getIp() const { return ip_; }
uint16_t HttpRequest::getPort() const { return port_; }

const std::vector<std::string_view> &HttpRequest::getPathParts() const { return pathParts_; }

void HttpRequest::setSessionHandle(SessionHandle *sessionHandle) { sessionHandle_ = sessionHandle; }

void HttpRequest::setIp(const std::string &ip) { ip_ = ip; }
void HttpRequest::setPort(uint16_t port) { port_ = port; }

bool HttpRequest::parseRequestLine(std::string_view requestLine) {
  auto method_end = requestLine.find(' ');
  if (method_end == std::string_view::npos)
    return false;

  method_ = requestLine.substr(0, method_end);
  requestLine.remove_prefix(method_end + 1);

  auto path_end = requestLine.find(' ');
  if (path_end == std::string_view::npos)
    return false;

  std::string_view rawPath = requestLine.substr(0, path_end);
  rawPath_ = rawPath;

  auto hostStart = rawPath.find("://");
  if (hostStart != std::string_view::npos) {
    rawPath.remove_prefix(hostStart + 3);
    // host ends at either '/' or '?' or end of string
    auto hostEnd = rawPath.find_first_of("/?");
    if (hostEnd != std::string_view::npos) {
      setHeaderLower("host", std::string(rawPath.substr(0, hostEnd)));
      rawPath.remove_prefix(hostEnd);
    } else {
      setHeaderLower("host", std::string(rawPath));
      rawPath = "/"; // host with no path — treat as root
    }
  }

  parsePathAndQueryParams(rawPath);
  requestLine.remove_prefix(path_end + 1);

  version_ = requestLine;
  return true;
}

bool HttpRequest::parseRequestHeaders(std::string_view headerView) {
  while (not headerView.empty()) {
    auto lineEnd = headerView.find('\n');
    auto line = lineEnd == std::string_view::npos ? headerView : headerView.substr(0, lineEnd);

    if (not line.empty() && (line.front() == ' ' || line.front() == '\t'))
      return false;

    headerView.remove_prefix(lineEnd == std::string_view::npos ? headerView.size() : lineEnd + 1);

    if (not line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    auto pos = line.find(':');
    if (pos == std::string_view::npos)
      continue;

    auto key = line.substr(0, pos);
    auto value = line.substr(pos + 1);
    trim(value);
    addHeader(std::string(key), std::string(value));
  }
  return true;
}

void HttpRequest::parsePathAndQueryParams(std::string_view rawPathView) {
  auto qpos = rawPathView.find('?');
  if (qpos == std::string_view::npos) {
    path_ = rawPathView;
    normalizePath(path_);
  } else {
    path_ = rawPathView.substr(0, qpos);
    normalizePath(path_);

    rawPathView.remove_prefix(qpos + 1);
    auto fragmentpos = rawPathView.find('#');
    if (fragmentpos != std::string_view::npos) {
      rawPathView.remove_suffix(rawPathView.size() - fragmentpos);
    }

    for (;;) {
      auto paramDelim = rawPathView.find('&');
      auto pair = paramDelim == std::string_view::npos ? rawPathView : rawPathView.substr(0, paramDelim);

      auto eqpos = pair.find('=');
      if (eqpos != std::string_view::npos) {
        const auto &val = percentDecode(pair.substr(eqpos + 1));
        queryParams_.emplace_back(percentDecode(pair.substr(0, eqpos)), val == "" ? "true" : val);
      } else {
        queryParams_.emplace_back(percentDecode(pair), "true");
      }

      if (paramDelim == std::string_view::npos)
        break;

      rawPathView.remove_prefix(paramDelim + 1);
    }
  }

  std::string_view remaining = path_;
  while (!remaining.empty()) {
    auto pos = remaining.find('/');
    if (pos == std::string_view::npos) {
      pathParts_.push_back(remaining);
      break;
    }
    if (pos > 0)
      pathParts_.push_back(remaining.substr(0, pos));
    remaining.remove_prefix(pos + 1);
  }
}

void HttpRequest::reset(const std::string &ip, uint16_t port) {
  headers_.clear(); // keeps capacity
  queryParams_.clear();
  attributes_.clear();
  pathParams_.clear();
  method_.clear(); // keeps capacity
  rawPath_.clear();
  path_.clear();
  version_.clear();
  body_.clear();
  pathParts_.clear();
  sessionHandle_ = nullptr;
  ip_ = ip;
  port_ = port;
}
