#pragma once
#include <ranges>
#include <spdlog/spdlog.h>
#include <sstream>

inline std::vector<std::string> split(std::string_view s,
                                      std::string_view delim = " ") {
  auto parts =
      s | std::views::split(delim) | std::views::transform([](auto &&r) {
        return std::string(r.begin(), r.end());
      });

  return {parts.begin(), parts.end()};
}

class HttpRequest {
public:
  std::string method, path, version;
  std::unordered_map<std::string, std::string> headers;

  HttpRequest() {}

  bool parseHeader(const std::string &headerSection) {
    std::istringstream iss(headerSection);
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

    method = tokens[0];
    path = tokens[1];
    version = tokens[2];

    SPDLOG_DEBUG("Method: {}, Path: {}, Version: {}", method, path, version);

    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      auto header = split(line, ": ");
      headers[header[0]] = header[1];
    }
    return true;
  }

  int getContentLength() const {
    auto it = headers.find("Content-Length");
    if (it == headers.end())
      return -1;
    return std::stoi(it->second);
  }
};
