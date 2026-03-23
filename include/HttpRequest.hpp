#pragma once

#include <expected>

#include "serverConfig.hpp"

enum class ContentLengthError {
  NO_CONTENT_LENGTH_HEADER,
  INVALID_CONTENT_LENGTH,
  CONTENT_LENGTH_TOO_LARGE,
};

class HttpRequest {

public:
  static constexpr size_t MAX_HEADER_SIZE = MAX_HEADER_BYTES;
  static constexpr size_t MAX_CONTENT_LENGTH = MAX_BODY_BYTES;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("host"),          std::string_view("content-length"),
      std::string_view("content-type"),  std::string_view("transfer-encoding"),
      std::string_view("authorization"), std::string_view("expect"),
  };

  HttpRequest();

  bool parseRequestHeader(std::string_view headerView);

  std::vector<std::pair<std::string, std::string>> getCookies() const;

  std::optional<std::string> getCookie(const std::string &name) const;

  std::expected<size_t, ContentLengthError> getContentLength() const;

  std::string getHeader(const std::string &name) const;

  std::vector<std::string> getHeaders(const std::string &name) const;

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  void setHeader(const std::string &name, const std::string &value);

  void addHeader(const std::string &name, const std::string &value);

  void removeHeader(const std::string &name);

  void setAttribute(const std::string &key, const std::string &value);

  std::string getAttribute(const std::string &key) const;

  std::string getQueryParam(const std::string &key) const;

  std::vector<std::string> getQueryParams(const std::string &key) const;

  std::vector<std::pair<std::string, std::string>> getAllQueryParams() const;

  std::string getPathParam(const std::string &key) const;

  void setPathParams(
      const std::vector<std::pair<std::string, std::string>> &pathParams);

  std::vector<std::pair<std::string, std::string>> getAllPathParams() const;

  std::string getPath() const;
  std::string getRawPath() const;
  std::string getVersion() const;

  std::string getMethod() const;
  void setMethod(const std::string &method);

  std::string getBody() const;
  void setBody(const std::string &body);

  std::string getIp() const;
  uint16_t getPort() const;

  void setIp(const std::string &ip);
  void setPort(uint16_t port);

private:
  std::vector<std::pair<std::string, std::string>> headers_;
  std::vector<std::pair<std::string, std::string>> queryParams_;
  std::vector<std::pair<std::string, std::string>> attributes_;
  std::vector<std::pair<std::string, std::string>> pathParams_;

  std::string method_, rawPath_, path_, version_, body_, ip_;
  uint16_t port_ = 0;

  bool parseRequestLine(std::string_view requestLine);

  bool parseRequestHeaders(std::string_view headerView);

  void parsePathAndQueryParams(std::string_view rawPathView);
};
