#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Cookie.hpp"
#include "utils.hpp"

class HttpResponse {
public:
  static constexpr std::array singletonHeaders_ = {
      std::string_view("content-length"),
      std::string_view("content-type"),
      std::string_view("transfer-encoding"),
      std::string_view("date"),
      std::string_view("server"),
      std::string_view("location"),
  };

  static constexpr std::array noBody = {100, 101, 102, 103, 204, 304};

  static std::string statusText(int statusCode) {
    return getOrDefault(statusStrings_, statusCode, "Unknown");
  };

  static size_t headerLineSize(const std::string &k, const std::string &v) {
    return k.size() + 2 + v.size() + 2; // "key: value\r\n"
  }

  HttpResponse();

  HttpResponse(int statusCode);

  HttpResponse(int statusCode, std::string body);

  std::vector<unsigned char> serialize() const;

  void setCookie(Cookie cookie);

  void unsetCookie(const std::string &name);

  void deleteCookie(const std::string &name, const std::string &path = "/");

  std::vector<std::pair<std::string, std::string>> getCookies() const;

  std::optional<std::string> getCookie(const std::string &name) const;

  std::string getHeader(const std::string &name) const;

  std::vector<std::string> getHeaders(const std::string &name) const;

  std::vector<std::pair<std::string, std::string>> getAllHeaders() const;

  void setHeader(const std::string &name, const std::string &value);

  void addHeader(const std::string &name, const std::string &value);

  void removeHeader(const std::string &name);

  void setBody(const std::string &body);

  void stripBody();

  void setVersion(const std::string &version);
  void setStatusCode(int statusCode);

  std::string getBody() const;
  size_t getBodySize() const;

  std::string getVersion() const;
  int getStatusCode() const;

private:
  std::string body_, version_ = "HTTP/1.1";
  int statusCode_;
  std::vector<std::pair<std::string, std::string>> headers_;

  inline static const std::unordered_map<int, std::string> statusStrings_ = {
      // 1xx Informational
      {100, "Continue"},
      {101, "Switching Protocols"},
      {102, "Processing"},
      {103, "Early Hints"},

      // 2xx Success
      {200, "OK"},
      {201, "Created"},
      {202, "Accepted"},
      {203, "Non-Authoritative Information"},
      {204, "No Content"},
      {205, "Reset Content"},
      {206, "Partial Content"},
      {207, "Multi-Status"},
      {208, "Already Reported"},
      {226, "IM Used"},

      // 3xx Redirection
      {300, "Multiple Choices"},
      {301, "Moved Permanently"},
      {302, "Found"},
      {303, "See Other"},
      {304, "Not Modified"},
      {305, "Use Proxy"},
      {307, "Temporary Redirect"},
      {308, "Permanent Redirect"},

      // 4xx Client Error
      {400, "Bad Request"},
      {401, "Unauthorized"},
      {402, "Payment Required"},
      {403, "Forbidden"},
      {404, "Not Found"},
      {405, "Method Not Allowed"},
      {406, "Not Acceptable"},
      {407, "Proxy Authentication Required"},
      {408, "Request Timeout"},
      {409, "Conflict"},
      {410, "Gone"},
      {411, "Length Required"},
      {412, "Precondition Failed"},
      {413, "Content Too Large"},
      {414, "URI Too Long"},
      {415, "Unsupported Media Type"},
      {416, "Range Not Satisfiable"},
      {417, "Expectation Failed"},
      {418, "I'm a teapot"}, // :D
      {421, "Misdirected Request"},
      {422, "Unprocessable Content"},
      {423, "Locked"},
      {424, "Failed Dependency"},
      {425, "Too Early"},
      {426, "Upgrade Required"},
      {428, "Precondition Required"},
      {429, "Too Many Requests"},
      {431, "Request Header Fields Too Large"},
      {451, "Unavailable For Legal Reasons"},

      // 5xx Server Error
      {500, "Internal Server Error"},
      {501, "Not Implemented"},
      {502, "Bad Gateway"},
      {503, "Service Unavailable"},
      {504, "Gateway Timeout"},
      {505, "HTTP Version Not Supported"},
      {506, "Variant Also Negotiates"},
      {507, "Insufficient Storage"},
      {508, "Loop Detected"},
      {510, "Not Extended"},
      {511, "Network Authentication Required"}};
};
