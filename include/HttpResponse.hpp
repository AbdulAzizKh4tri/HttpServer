#pragma once

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils.hpp"

class HttpResponse {
public:
  static std::string statusText(int statusCode) {
    return getOrDefault(statusStrings_, statusCode, "Unknown");
  };

  static size_t headerLineSize(const std::string &k, const std::string &v) {
    return k.size() + 2 + v.size() + 2; // "key: value\r\n"
  }

  HttpResponse() : statusCode_(-1) {
    setHeader("Server", "Azooz's Chad Compiled C++ Server");
  }

  HttpResponse(int statusCode) : statusCode_(statusCode) {
    setHeader("Content-Length", std::to_string(body_.size()));
    setHeader("Server", "Azooz's Chad Compiled C++ Server");
  }

  HttpResponse(int statusCode, std::string body)
      : statusCode_(statusCode), body_(body) {
    setHeader("Content-Length", std::to_string(body_.size()));
    setHeader("Server", "Azooz's Chad Compiled C++ Server");
  }

  std::vector<unsigned char> serialize() const {
    const std::string &statusTxt = HttpResponse::statusText(statusCode_);

    size_t size = version_.size() + 1 + 3 + 1 + statusTxt.size() + 2;

    for (auto &[k, v] : headers_)
      size += headerLineSize(k, v);
    size += 2; // final \r\n
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
      write(k);
      write(": ");
      write(v);
      write("\r\n");
    }
    write("\r\n");
    write(body_);

    return serializedResponse;
  }

  void setHeader(const std::string &name, const std::string &value) {
    headers_[toLowerCase(name)] = value;
  }

  void setBody(const std::string &body) {
    body_ = body;
    setHeader("Content-Length", std::to_string(body_.size()));
  }

  void stripBody() { body_ = ""; }

  void setVersion(const std::string &version) { version_ = version; }
  void setStatusCode(int statusCode) { statusCode_ = statusCode; }

  std::string getBody() const { return body_; }
  size_t getBodySize() const { return body_.size(); }

  std::string getVersion() const { return version_; }
  int getStatusCode() const { return statusCode_; }

  std::string getHeader(const std::string &name) const {
    return getOrDefault(headers_, toLowerCase(name), "");
  }
  std::unordered_map<std::string, std::string> getAllHeaders() const {
    return headers_;
  }

private:
  std::string body_, version_ = "HTTP/1.1";
  int statusCode_;
  std::unordered_map<std::string, std::string> headers_;

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
