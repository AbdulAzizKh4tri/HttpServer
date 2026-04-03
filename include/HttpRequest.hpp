#pragma once

#include <expected>

#include "ServerConfig.hpp"
#include "Session.hpp"
#include "Task.hpp"

struct SessionHandle;

enum class ContentLengthError {
  NO_CONTENT_LENGTH_HEADER,
  INVALID_CONTENT_LENGTH,
  CONTENT_LENGTH_TOO_LARGE,
};

class HttpRequest {

public:
  static constexpr size_t MAX_HEADER_SIZE = ServerConfig::MAX_HEADER_BYTES;
  static constexpr size_t MAX_CONTENT_LENGTH = ServerConfig::MAX_CONTENT_LENGTH;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("host"),          std::string_view("content-length"),
      std::string_view("content-type"),  std::string_view("transfer-encoding"),
      std::string_view("authorization"), std::string_view("expect"),
  };

  HttpRequest();

  bool parseRequestHeader(std::string_view headerView);

  std::vector<std::pair<std::string, std::string>> getCookies() const;

  std::optional<std::string> getCookie(const std::string &name) const;

  Task<Session *> getSession();

  std::expected<size_t, ContentLengthError> getContentLength() const;

  std::string_view getHeader(const std::string &name) const;

  std::string_view getHeaderLower(const std::string &lowerKey) const;

  std::vector<std::string> getHeaders(const std::string &name) const;

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  void setHeader(const std::string &name, const std::string &value);

  // Always use the Lower version of these whenever possible, avoids a string allocation.
  // But be careful to only pass lowercased keys
  void setHeaderLower(const std::string_view &lowercaseKey, const std::string &value);

  void addHeader(const std::string &name, const std::string &value);
  void addHeaderLower(const std::string_view &lowercaseKey, const std::string &value);

  void removeHeader(const std::string &name);

  void setAttribute(const std::string &key, const std::string &value);

  std::string getAttribute(const std::string &key, std::string defaultValue = "") const;

  std::string getQueryParam(const std::string &key, std::string defaultValue = "") const;

  std::vector<std::string> getQueryParams(const std::string &key) const;

  std::vector<std::pair<std::string, std::string>> getAllQueryParams() const;

  std::string getPathParam(const std::string &key, std::string defaultValue = "") const;

  void setPathParams(const std::vector<std::pair<std::string, std::string>> &pathParams);

  std::vector<std::pair<std::string, std::string>> getAllPathParams() const;

  const std::string &getPath() const;
  const std::string &getRawPath() const;
  const std::string &getVersion() const;
  const std::vector<std::string_view> &getPathParts() const;

  const std::string &getMethod() const;
  void setMethod(const std::string &method);

  const std::string &getBody() const;
  void setBody(const std::string &body);

  const std::string &getIp() const;
  uint16_t getPort() const;

  void setIp(const std::string &ip);
  void setPort(uint16_t port);
  void setSessionHandle(SessionHandle *sessionHandle);

  void reset(const std::string &ip, uint16_t port);

private:
  std::vector<std::pair<std::string, std::string>> headers_;
  std::vector<std::pair<std::string, std::string>> queryParams_;
  std::vector<std::pair<std::string, std::string>> attributes_;
  std::vector<std::pair<std::string, std::string>> pathParams_;

  SessionHandle *sessionHandle_ = nullptr;

  std::string method_, rawPath_, path_, version_, body_, ip_;
  uint16_t port_ = 0;
  std::vector<std::string_view> pathParts_;

  bool parseRequestLine(std::string_view requestLine);

  bool parseRequestHeaders(std::string_view headerView);

  void parsePathAndQueryParams(std::string_view rawPathView);
};
