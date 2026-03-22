#pragma once

#include <functional>
#include <vector>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

using Formatter = std::function<HttpResponse(int statusCode,
                                             const std::string_view &message)>;

class ErrorFactory {
public:
  ErrorFactory();

  void setFallbackFormatter(std::string type, Formatter formatter);

  std::pair<std::string, Formatter> getFallbackFormatter();

  void setFormatter(std::string type, Formatter formatter);

  HttpResponse build(const HttpRequest &req, int statusCode,
                     const std::string_view &message = "") const;

private:
  std::vector<std::pair<std::string, Formatter>> registeredFormatters_;
  std::pair<std::string, Formatter> fallbackFormatterPair_;
};
