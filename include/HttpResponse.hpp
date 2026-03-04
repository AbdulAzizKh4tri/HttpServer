#pragma once

#include <format>
#include <string>
#include <unordered_map>
#include <vector>

class HttpResponse {
public:
  inline static const std::unordered_map<int, std::string> statusStrings = {
      {200, "OK"},
      {400, "Bad Request"},
      {404, "Not Found"},
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

    headers_["Content-Length"] = std::to_string(body_.size());
    headers_["Connection"] = "close";
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
