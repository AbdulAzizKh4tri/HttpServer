#pragma once

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <netinet/in.h>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <vector>

struct PeerAddress {
  std::string ip;
  uint16_t port;
};

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

inline PeerAddress resolvePeerAddress(sockaddr_storage addr, socklen_t len) {
  PeerAddress result;
  if (addr.ss_family == AF_INET) {
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in *)&addr)->sin_addr, ipstr,
              INET_ADDRSTRLEN);
    result.ip = ipstr;
    result.port = ntohs(((sockaddr_in *)&addr)->sin_port);
  } else if (addr.ss_family == AF_INET6) {
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &((sockaddr_in6 *)&addr)->sin6_addr, ipstr,
              INET6_ADDRSTRLEN);
    result.ip = ipstr;
    result.port = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
  } else {
    throw std::runtime_error("Unknown address family");
  }
  return result;
}

inline std::string getCommaSeparatedString(std::vector<std::string> strings) {
  std::string result;
  for (auto &&s : strings) {
    result += s + ", ";
  }
  if (!strings.empty()) {
    result.pop_back();
    result.pop_back();
  }
  return result;
}

inline std::string toLowerCase(std::string s) {
  std::ranges::transform(s, s.begin(),
                         [](unsigned char c) { return std::tolower(c); });
  return s;
}

inline std::vector<std::string> split(std::string_view s,
                                      std::string_view delim = " ") {
  auto parts =
      s | std::views::split(delim) | std::views::transform([](auto &&r) {
        return std::string(r.begin(), r.end());
      });

  return {parts.begin(), parts.end()};
}

inline std::string trim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));

  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());

  return s;
}

template <typename Map>
inline auto getOrDefault(const Map &map, const typename Map::key_type &key,
                         typename Map::mapped_type defaultValue) ->
    typename Map::mapped_type {

  if (const auto it = map.find(key); it != map.end()) {
    return it->second;
  }
  return defaultValue;
}

inline std::string normalizePath(const std::string &path) {
  std::string result;
  for (char c : path) {
    if (c == '/' && !result.empty() && result.back() == '/')
      continue;
    result += c;
  }
  if (!result.empty() && result.front() == '/')
    result.erase(0, 1);
  if (!result.empty() && result.back() == '/')
    result.pop_back();
  return result;
}

inline std::string percentDecode(std::string_view s) {
  std::string result;
  result.reserve(s.size());

  auto fromHex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };

  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      result += ' ';
    } else if (s[i] == '%' && i + 2 < s.size()) {
      int hi = fromHex(s[i + 1]);
      int lo = fromHex(s[i + 2]);
      if (hi != -1 && lo != -1) {
        result += static_cast<char>((hi << 4) | lo);
        i += 2;
      } else {
        result += s[i]; // malformed — keep as-is
      }
    } else {
      result += s[i];
    }
  }

  return result;
}

inline std::string getCurrentHttpDate() {
  auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&time, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return buf;
}
