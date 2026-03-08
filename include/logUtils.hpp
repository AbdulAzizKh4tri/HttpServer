#pragma once

#include <spdlog/spdlog.h>
#include <string_view>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

inline void configureLog() {
#ifdef NDEBUG
  spdlog::set_level(spdlog::level::info);
#else
  spdlog::set_level(spdlog::level::debug);
#endif
  //[thread %t]
  spdlog::set_pattern("[%m-%d %H:%M] [%^%l%$] %v");
}

// ANSI color codes
namespace Color {
constexpr std::string_view Reset = "\033[0m";
constexpr std::string_view Green = "\033[32m";
constexpr std::string_view Yellow = "\033[33m";
constexpr std::string_view Red = "\033[31m";
} // namespace Color

inline std::string_view statusColor(int statusCode) {
  if (statusCode < 300)
    return Color::Green;
  if (statusCode < 500)
    return Color::Yellow;
  return Color::Red;
}

inline void logRequest(const HttpRequest &req, const HttpResponse &res) {
  int status = res.getStatusCode();
  SPDLOG_INFO("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status,
              Color::Reset, req.getMethod(), req.getPath(), req.getIp(),
              req.getPort());
}
