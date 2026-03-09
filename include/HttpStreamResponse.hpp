#pragma once

#include <format>
#include <functional>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

#include "HttpResponse.hpp"

using NextChunkLambda = std::function<std::optional<std::string>()>;

class HttpStreamResponse {
public:
  static std::vector<unsigned char> serializeChunk(std::string chunk) {
    std::string response = std::format("{:x}\r\n{}\r\n", chunk.size(), chunk);
    return std::vector<unsigned char>(response.begin(), response.end());
  }

  HttpStreamResponse() : statusCode_(-1) {}

  HttpStreamResponse(int statusCode, NextChunkLambda nextChunkLambda)
      : statusCode_(statusCode), nextChunkLambda_(nextChunkLambda) {
    setHeader("Transfer-Encoding", "chunked");
  }

  std::optional<std::string> getNextChunk() { return nextChunkLambda_(); }

  std::vector<unsigned char> getSerializedHeader() const {
    std::string response = std::format("{} {} {}\r\n", version_, statusCode_,
                                       HttpResponse::statusText(statusCode_));

    for (auto &header : headers_) {
      response += std::format("{}: {}\r\n", header.first, header.second);
    }

    response += "\r\n";
    return std::vector<unsigned char>(response.begin(), response.end());
  }

  void setHeader(const std::string &name, const std::string &value) {
    headers_[toLowerCase(name)] = value;
  }

  std::string getHeader(const std::string &name) const {
    return getOrDefault(headers_, toLowerCase(name), "");
  }

  std::unordered_map<std::string, std::string> getAllHeaders() const {
    return headers_;
  }

  std::string getVersion() const { return version_; }
  int getStatusCode() const { return statusCode_; }

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";
  std::unordered_map<std::string, std::string> headers_;

  NextChunkLambda nextChunkLambda_;
};
