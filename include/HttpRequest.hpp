#pragma once

#include <charconv>
#include <expected>
#include <spdlog/spdlog.h>

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

  static constexpr std::array singletonHeaders_ = {
      std::string_view("host"),          std::string_view("content-length"),
      std::string_view("content-type"),  std::string_view("transfer-encoding"),
      std::string_view("authorization"), std::string_view("expect"),
  };

  HttpRequest() {}

  bool parseRequestHeader(std::string_view headerView) {
    auto lineEnd = headerView.find('\n');
    if (lineEnd == std::string_view::npos)
      return false;

    auto requestLine = headerView.substr(0, lineEnd);
    if (!requestLine.empty() && requestLine.back() == '\r')
      requestLine.remove_suffix(1);

    if (!parseRequestLine(requestLine))
      return false;

    headerView.remove_prefix(lineEnd + 1);
    parseRequestHeaders(headerView);
    return true;
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
    std::string key = toLowerCase(name);
    std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
    headers_.emplace_back(key, value);
  }

  void addHeader(const std::string &name, const std::string &value) {
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

  void removeHeader(const std::string &name) {
    auto key = toLowerCase(name);
    std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  }

  void setAttribute(const std::string &key, const std::string &value) {
    std::erase_if(attributes_,
                  [&key](const auto &p) { return p.first == key; });
    attributes_.emplace_back(key, value);
  }

  std::string getAttribute(const std::string &key) const {
    return getLastOrDefault(attributes_, key, "");
  }

  std::string getQueryParam(const std::string &key) const {
    return getLastOrDefault(queryParams_, key, "");
  }

  std::vector<std::string> getQueryParams(const std::string &key) const {
    return getAllValues(queryParams_, key);
  }

  std::vector<std::pair<std::string, std::string>> getAllQueryParams() const {
    return queryParams_;
  }

  std::string getPathParam(const std::string &key) const {
    return getLastOrDefault(pathParams_, key, "");
  }

  void setPathParams(
      const std::vector<std::pair<std::string, std::string>> &pathParams) {
    pathParams_ = pathParams;
  }

  std::vector<std::pair<std::string, std::string>> getAllPathParams() const {
    return pathParams_;
  }

  std::string getPath() const { return path_; }
  std::string getRawPath() const { return rawPath_; }
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
  std::vector<std::pair<std::string, std::string>> headers_, queryParams_,
      attributes_, pathParams_;
  std::string method_, rawPath_, path_, version_, body_, ip_;
  uint16_t port_ = 0;

  bool parseRequestLine(std::string_view requestLine) {
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
        setHeader("Host", std::string(rawPath.substr(0, hostEnd)));
        rawPath.remove_prefix(hostEnd);
      } else {
        setHeader("Host", std::string(rawPath));
        rawPath = "/"; // host with no path — treat as root
      }
    }

    parsePathAndQueryParams(rawPath);
    requestLine.remove_prefix(path_end + 1);

    version_ = requestLine;
    return true;
  }

  void parseRequestHeaders(std::string_view headerView) {
    while (!headerView.empty()) {
      auto lineEnd = headerView.find('\n');
      auto line = lineEnd == std::string_view::npos
                      ? headerView
                      : headerView.substr(0, lineEnd);

      headerView.remove_prefix(
          lineEnd == std::string_view::npos ? headerView.size() : lineEnd + 1);

      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);

      auto pos = line.find(':');
      if (pos == std::string_view::npos)
        continue;

      auto key = line.substr(0, pos);
      auto value = line.substr(pos + 1);
      trim(value);
      addHeader(std::string(key), std::string(value));
    }
  }

  void parsePathAndQueryParams(std::string_view rawPathView) {
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
        auto pair = paramDelim == std::string_view::npos
                        ? rawPathView
                        : rawPathView.substr(0, paramDelim);

        auto eqpos = pair.find('=');
        if (eqpos != std::string_view::npos) {
          const auto &val = percentDecode(pair.substr(eqpos + 1));
          queryParams_.emplace_back(percentDecode(pair.substr(0, eqpos)),
                                    val == "" ? "true" : val);
        } else {
          queryParams_.emplace_back(percentDecode(pair), "true");
        }

        if (paramDelim == std::string_view::npos)
          break;

        rawPathView.remove_prefix(paramDelim + 1);
      }
    }
  }
};
