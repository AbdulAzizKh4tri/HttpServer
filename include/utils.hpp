#pragma once

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <vector>

static constexpr std::array<unsigned char, 4> crlf2 = {'\r', '\n', '\r', '\n'};
static constexpr std::array<unsigned char, 2> crlf = {'\r', '\n'};

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

inline std::string
getCommaSeparatedString(const std::vector<std::string> &strings) {

  if (strings.empty())
    return {};

  size_t total = 2 * (strings.size() - 1); // ", " separators
  for (auto &s : strings)
    total += s.size();

  std::string result;
  result.reserve(total);

  for (size_t i = 0; i < strings.size(); i++) {
    if (i > 0)
      result += ", ";
    result += strings[i];
  }
  return result;
}

inline std::string toLowerCase(std::string_view s) {
  char buf[64];
  if (s.size() < sizeof(buf)) {
    std::transform(s.begin(), s.end(), buf,
                   [](unsigned char c) { return std::tolower(c); });
    return std::string(buf, s.size());
  }
  std::string result(s);
  std::ranges::transform(result, result.begin(),
                         [](unsigned char c) { return std::tolower(c); });
  return result;
}

inline std::vector<std::string> split(std::string_view s,
                                      std::string_view delim = " ") {
  auto parts =
      s | std::views::split(delim) | std::views::transform([](auto &&r) {
        return std::string(r.begin(), r.end());
      });

  return {parts.begin(), parts.end()};
}

inline void trim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));

  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

inline void trim(std::string_view &s) {
  while (!s.empty() && std::isspace((unsigned char)s.front()))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace((unsigned char)s.back()))
    s.remove_suffix(1);
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

inline auto
getLastOrDefault(const std::vector<std::pair<std::string, std::string>> &mp,
                 const std::string &key, std::string defaultValue)
    -> std::string {

  auto it = std::find_if(mp.rbegin(), mp.rend(),
                         [&key](const auto &p) { return p.first == key; });
  if (it != mp.rend())
    return it->second;
  return "";
}

inline auto
getAllValues(const std::vector<std::pair<std::string, std::string>> &mp,
             const std::string &key) -> std::vector<std::string> {
  std::vector<std::string> values;
  for (auto &[k, v] : mp) {
    if (k == key)
      values.push_back(v);
  }
  return values;
}

inline auto
toJsonObject(const std::vector<std::pair<std::string, std::string>> &pairs) {
  nlohmann::json j = nlohmann::json::object();
  for (const auto &[k, v] : pairs)
    j[k] = v;
  return j;
}

inline void normalizePath(std::string &path) {
  auto newEnd = std::unique(path.begin(), path.end(), [](char a, char b) {
    return a == '/' && b == '/';
  });
  path.erase(newEnd, path.end());

  if (!path.empty() && path.front() == '/')
    path.erase(0, 1);

  if (!path.empty() && path.back() == '/')
    path.pop_back();
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

inline const std::string &getCurrentHttpDate() {
  thread_local static std::string cached;
  thread_local static time_t lastTime = 0;

  time_t now =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  if (now != lastTime) {
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    cached = buf;
    lastTime = now;
  }
  return cached;
}

inline std::string toHttpDate(std::chrono::system_clock::time_point tp) {
  time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
  return buf;
}

inline bool mime_match(std::string_view p, std::string_view v) {
  if (p == "*/*")
    return true;
  auto ps = p.find('/'), vs = v.find('/');
  if (ps == std::string_view::npos || vs == std::string_view::npos)
    return false;
  return (p[0] == '*' || v[0] == '*' || p.substr(0, ps) == v.substr(0, vs)) &&
         (p[ps + 1] == '*' || v[vs + 1] == '*' ||
          p.substr(ps + 1) == v.substr(vs + 1));
}

inline std::chrono::steady_clock::time_point now() {
  return std::chrono::steady_clock::now();
}
