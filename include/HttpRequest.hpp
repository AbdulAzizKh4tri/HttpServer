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

  static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;

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

      auto pos = line.find(": ");
      if (pos == std::string::npos)
        continue;
      headers[line.substr(0, pos)] = line.substr(pos + 2);
    }
    return true;
  }

  int getContentLength() const {
    auto it = headers.find("Content-Length");
    if (it == headers.end())
      return -1;
    int len = std::stoi(it->second);
    if (len < 0 || (size_t)len > MAX_CONTENT_LENGTH)
      return -1;
    return len;
  }
};
