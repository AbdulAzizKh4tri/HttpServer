#pragma once

#include <charconv>
#include <expected>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_map>

#include "utils.hpp"

enum class ContentLengthError {
  NO_CONTENT_LENGTH_HEADER,
  INVALID_CONTENT_LENGTH,
  CONTENT_LENGTH_TOO_LARGE,
};

class HttpRequest {

public:
  static constexpr size_t MAX_HEADER_SIZE = 8 * 1024;
  static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;

  HttpRequest() {}

  bool parseRequestHeader(const std::string &headerSection) {
    std::istringstream iss(headerSection);
    if (!parseRequestLine(iss))
      return false;
    parseRequestHeaders(iss);
    return true;
  }

  bool parseRequestLine(std::istringstream &iss) {
    std::string line;

    std::getline(iss, line);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    std::istringstream requestLine(line);
    std::vector<std::string> tokens{
        std::istream_iterator<std::string>{requestLine},
        std::istream_iterator<std::string>{}};

    if (tokens.size() != 3)
      return false;

    method_ = tokens[0];

    // Extract query parameters
    std::string rawPath = tokens[1];
    parseRequestParams(rawPath);

    version_ = tokens[2];
    return true;
  }

  void parseRequestHeaders(std::istringstream &iss) {
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      auto pos = line.find(":");
      if (pos == std::string::npos)
        continue;
      setHeader(line.substr(0, pos), trim(line.substr(pos + 1)));
    }
  }

  void parseRequestParams(const std::string &rawPath) {
    auto qpos = rawPath.find('?');
    if (qpos == std::string::npos) {
      path_ = rawPath;
    } else {
      path_ = rawPath.substr(0, qpos);
      std::string queryString = rawPath.substr(qpos + 1);

      for (auto &&pair : split(queryString, "&")) {
        auto eqpos = pair.find('=');
        if (eqpos == std::string::npos)
          continue;
        queryParams_[pair.substr(0, eqpos)] = pair.substr(eqpos + 1);
      }
    }
  }

  std::expected<size_t, ContentLengthError> getContentLength() const {
    auto lenStr = getHeader("Content-Length");
    if (lenStr == "")
      return std::unexpected(ContentLengthError::NO_CONTENT_LENGTH_HEADER);
    size_t len;

    auto [ptr, ec] =
        std::from_chars(lenStr.data(), lenStr.data() + lenStr.size(), len);
    if (ec != std::errc{})
      return std::unexpected(ContentLengthError::INVALID_CONTENT_LENGTH);

    if (len > MAX_CONTENT_LENGTH)
      return std::unexpected(ContentLengthError::CONTENT_LENGTH_TOO_LARGE);
    return len;
  }

  void setHeader(const std::string &key, const std::string &value) {
    headers_[toLowerCase(key)] = value;
  }

  std::string getHeader(const std::string &key) const {
    return getOrDefault(headers_, toLowerCase(key), "");
  }

  std::unordered_map<std::string, std::string> getAllHeaders() const {
    return headers_;
  }

  void setAttribute(const std::string &key, const std::string &value) {
    attributes_[key] = value;
  }

  std::string getAttribute(const std::string &key) const {
    return getOrDefault(attributes_, key, "");
  }

  std::string getQueryParam(const std::string &key) const {
    return getOrDefault(queryParams_, key, "");
  }

  std::unordered_map<std::string, std::string> getAllQueryParams() const {
    return queryParams_;
  }

  std::string getPathParam(const std::string &key) const {
    return getOrDefault(pathParams_, key, "");
  }

  void setPathParams(
      const std::unordered_map<std::string, std::string> &pathParams) {
    pathParams_ = pathParams;
  }

  std::unordered_map<std::string, std::string> getAllPathParams() const {
    return pathParams_;
  }

  std::string getPath() const { return path_; }
  std::string getVersion() const { return version_; }

  std::string getMethod() const { return method_; }
  void setMethod(const std::string &method) { method_ = method; }

  std::string getBody() const { return body_; }
  void setBody(const std::string &body) { body_ = body; }

  std::string getIp() const { return ip_; }
  uint16_t getPort() const { return port_; }

  void setIp(const std::string &ip) { ip_ = ip; }
  void setPort(uint16_t port) { port_ = port; }

private:
  std::unordered_map<std::string, std::string> headers_, queryParams_,
      attributes_, pathParams_;
  std::string method_, path_, version_, body_, ip_;
  uint16_t port_ = 0;
};
