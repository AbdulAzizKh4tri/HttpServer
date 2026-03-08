#pragma once

#include <algorithm>
#include <arpa/inet.h>
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
