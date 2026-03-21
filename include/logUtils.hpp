#pragma once

#include <string_view>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"

void configureLog(bool on = true, std::string file = "");

// ANSI color codes
namespace Color {
constexpr std::string_view Reset = "\033[0m";
constexpr std::string_view Green = "\033[32m";
constexpr std::string_view Yellow = "\033[33m";
constexpr std::string_view Red = "\033[31m";
} // namespace Color

std::string_view statusColor(int statusCode);

void logRequest(const HttpRequest &req, const HttpResponse &res);

void logRequest(const HttpRequest &req, const HttpStreamResponse &res);
