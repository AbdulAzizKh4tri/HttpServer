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
  static constexpr size_t MAX_HEADER_SIZE = 8 * 1024;
  static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;
  static const int NO_CONTENT_LENGTH_HEADER = -1;
  static const int CONTENT_LENGTH_TOO_LARGE = -2;

  std::string method, path, version, body, ip;
  uint16_t port;
  std::unordered_map<std::string, std::string> headers, params;

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

    // Extract query parameters
    std::string rawPath = tokens[1];
    auto qpos = rawPath.find('?');
    if (qpos == std::string::npos) {
      path = rawPath;
    } else {
      path = rawPath.substr(0, qpos);
      std::string queryString = rawPath.substr(qpos + 1);

      for (auto &&pair : split(queryString, "&")) {
        auto eqpos = pair.find('=');
        if (eqpos == std::string::npos)
          continue;
        params[pair.substr(0, eqpos)] = pair.substr(eqpos + 1);
      }
    }

    version = tokens[2];

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

  std::string getHeader(const std::string &key) const {
    auto it = headers.find(key);
    if (it == headers.end())
      return "";
    return it->second;
  }

  int getContentLength() const {
    auto it = headers.find("Content-Length");
    if (it == headers.end())
      return NO_CONTENT_LENGTH_HEADER;
    int len;
    try {
      len = std::stoi(it->second);
    } catch (...) {
      return NO_CONTENT_LENGTH_HEADER;
    }
    if (len < 0 || (size_t)len > MAX_CONTENT_LENGTH)
      return CONTENT_LENGTH_TOO_LARGE;
    return len;
  }
};
