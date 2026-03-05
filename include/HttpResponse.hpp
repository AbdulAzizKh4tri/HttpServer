#pragma once

#include <format>
#include <string>
#include <unordered_map>
#include <vector>

class HttpResponse {
public:
  inline static const std::unordered_map<int, std::string> statusStrings = {
      {100, "Continue"},
      {200, "OK"},
      {204, "No Content"},
      {400, "Bad Request"},
      {413, "Content Too Large"},
      {417, "Expectation Failed"},
      {431, "Header Fields Too Large"},
      {404, "Not Found"},
      {405, "Method Not Allowed"},
      {500, "Internal Server Error"}};

  HttpResponse() {}

  HttpResponse(int statusCode) : statusCode_(statusCode) {}

  HttpResponse(int statusCode, std::string body)
      : statusCode_(statusCode), body_(body) {
    headers_["Content-Length"] = std::to_string(body_.size());
  }

  HttpResponse(int statusCode,
               const std::unordered_map<std::string, std::string> &headers,
               const std::string &body)
      : statusCode_(statusCode), body_(body) {

    headers_ = headers;
  }

  std::vector<unsigned char> serialize() {
    auto it = statusStrings.find(statusCode_);
    std::string reason = (it != statusStrings.end()) ? it->second : "Unknown";

    std::string response =
        std::format("{} {} {}\r\n", version_, statusCode_, reason);

    if (headers_.find("Content-Length") == headers_.end()) {
      headers_["Content-Length"] = std::to_string(body_.size());
    }
    for (auto &header : headers_) {
      response += std::format("{}: {}\r\n", header.first, header.second);
    }

    response += "\r\n";
    response += body_;
    return std::vector<unsigned char>(response.begin(), response.end());
  }

  void addHeader(const std::string &name, const std::string &value) {
    headers_[name] = value;
  }

  void setBody(const std::string &body) { body_ = body; }
  void setVersion(const std::string &version) { version_ = version; }
  void setStatusCode(int statusCode) { statusCode_ = statusCode; }

  std::string getBody() const { return body_; }
  size_t getBodySize() const { return body_.size(); }

  std::string getVersion() const { return version_; }
  int getStatusCode() const { return statusCode_; }

  std::unordered_map<std::string, std::string> getHeaders() const {
    return headers_;
  }

private:
  std::string body_, version_ = "HTTP/1.1";
  int statusCode_;
  std::unordered_map<std::string, std::string> headers_;
};
