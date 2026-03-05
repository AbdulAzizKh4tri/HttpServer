#pragma once

#include <string>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "utils.hpp"

using Handler = std::function<HttpResponse(const HttpRequest &)>;

struct CorsConfig {
  std::vector<std::string> allowedOrigins;
  std::vector<std::string> allowedHeaders = {"Authorization", "Content-Type"};
  int maxAge = 10;
};

class Router {
public:
  void get(std::string path, Handler handler) {
    addRoute(path, "GET", handler);
  }

  void put(std::string path, Handler handler) {
    addRoute(path, "PUT", handler);
  }

  void post(std::string path, Handler handler) {
    addRoute(path, "POST", handler);
  }

  HttpResponse dispatch(HttpRequest &request) {
    HttpResponse response;
    handle(request, response);
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
  void addRoute(std::string path, std::string method, Handler handler) {
    routes_[path][method] = handler;
  }

  void handle(const HttpRequest &request, HttpResponse &response) {
    auto pathIt = routes_.find(request.path);

    if (pathIt == routes_.end()) {
      response = HttpResponse(404);
      return;
    }

    auto &definedMethods = pathIt->second;
    std::string origin = request.getHeader("Origin");

    if (request.method == "OPTIONS") {
      response = HttpResponse(204);

      std::string allowedMethods = getAllowedMethodsString(definedMethods);

      if (origin == "") {
        response.addHeader("Allow", allowedMethods);
        return;
      }

      if (!isOriginAllowed(origin)) {
        return;
      }

      std::string allowedHeaders =
          getCommaSeparatedString(corsConfig_.allowedHeaders);

      response.addHeader("Access-Control-Allow-Origin", origin);
      response.addHeader("Access-Control-Allow-Headers", allowedHeaders);
      response.addHeader("Access-Control-Allow-Methods", allowedMethods);
      response.addHeader("Access-Control-Max-Age",
                         std::to_string(corsConfig_.maxAge));
      return;
    }

    auto requestedMethodIt = definedMethods.find(request.method);
    if (requestedMethodIt == definedMethods.end()) {
      std::string allowedMethods = getAllowedMethodsString(definedMethods);
      response = HttpResponse(405);
      response.addHeader("Allow", allowedMethods);
      return;
    }

    response = requestedMethodIt->second(request);

    if (origin != "" && isOriginAllowed(origin)) {
      response.addHeader("Access-Control-Allow-Origin", origin);
    }
  }

  std::string getAllowedMethodsString(
      const std::unordered_map<std::string, Handler> &methods) {
    std::string result;
    for (const auto &[method, _] : methods) {
      result += method + ", ";
    }
    if (!result.empty()) {
      result.erase(result.length() - 2);
    }
    return result;
  }

  bool isOriginAllowed(const std::string &origin) {
    for (auto &allowedOrigin : corsConfig_.allowedOrigins) {
      if (allowedOrigin == origin || allowedOrigin == "*") {
        return true;
      }
    }
    return false;
  }

  std::unordered_map<std::string, std::unordered_map<std::string, Handler>>
      routes_;
  CorsConfig corsConfig_;
};
