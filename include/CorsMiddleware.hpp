#pragma once

#include <string>
#include <vector>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Middleware.hpp"
#include "utils.hpp"

struct CorsConfig {
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHeaders = {"Authorization", "Content-Type"};
  int maxAge = 86400;
};

class CorsMiddleware {
public:
  CorsMiddleware() {}
  CorsMiddleware(CorsConfig corsConfig) : corsConfig_(corsConfig) {}

  HttpResponse operator()(const HttpRequest &request, Next next) {
    std::string origin = request.getHeader("Origin");

    if (request.getMethod() == "OPTIONS" && origin != "") {
      HttpResponse response(204);
      std::string allowedMethods = request.getAttribute("allowedMethods");

      if (!isOriginAllowed(origin))
        return response;

      response.setHeader("Access-Control-Allow-Origin", origin);
      response.setHeader("Access-Control-Allow-Methods", allowedMethods);
      response.setHeader("Access-Control-Allow-Headers",
                         getCommaSeparatedString(corsConfig_.allowedHeaders));
      response.setHeader("Access-Control-Max-Age",
                         std::to_string(corsConfig_.maxAge));
      return response;
    }

    HttpResponse response = next();

    if (origin != "" && isOriginAllowed(origin))
      response.setHeader("Access-Control-Allow-Origin", origin);

    return response;
  }

  void setCorsOrigins(const std::vector<std::string> &origins) {
    corsConfig_.allowedOrigins = origins;
  }

  void setCorsHeaders(const std::vector<std::string> &headers) {
    corsConfig_.allowedHeaders = headers;
  }

  void setCorsMaxAge(int maxAge) { corsConfig_.maxAge = maxAge; }

private:
  CorsConfig corsConfig_;

  bool isOriginAllowed(const std::string &origin) {
    for (auto &allowedOrigin : corsConfig_.allowedOrigins) {
      if (allowedOrigin == origin || allowedOrigin == "*") {
        return true;
      }
    }
    return false;
  }
};
