#pragma once

#include <expected>
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

enum class RouteError { NOT_FOUND, METHOD_NOT_ALLOWED };
enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };

class Router {
public:
  void get(std::string path, Handler handler) {
    addRoute(path, "GET", handler);
  }

  void post(std::string path, Handler handler) {
    addRoute(path, "POST", handler);
  }

  void put(std::string path, Handler handler) {
    addRoute(path, "PUT", handler);
  }

  void patch(std::string path, Handler handler) {
    addRoute(path, "PATCH", handler);
  }

  void delete_(std::string path, Handler handler) {
    addRoute(path, "DELETE", handler);
  }

  std::expected<HttpResponse, RouteError> dispatch(const HttpRequest &request) {
    auto pathIt = routes_.find(request.path);
    if (pathIt == routes_.end())
      return std::unexpected(RouteError::NOT_FOUND);

    auto &definedMethods = pathIt->second;
    std::string origin = request.getHeader("Origin");

    if (auto methodIt = definedMethods.find(request.method);
        methodIt != definedMethods.end()) {
      HttpResponse response = methodIt->second(request);
      if (origin != "" && isOriginAllowed(origin))
        response.setHeader("Access-Control-Allow-Origin", origin);
      return response;
    }

    if (request.method == "OPTIONS") {
      HttpResponse response(204);
      std::string allowedMethods = getAllowedMethodsString(definedMethods);
      if (origin == "") {
        response.setHeader("Allow", allowedMethods);
        return response;
      }
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

    return std::unexpected(RouteError::METHOD_NOT_ALLOWED);
  }

  void setCorsOrigins(const std::vector<std::string> &origins) {
    corsConfig_.allowedOrigins = origins;
  }

  void setCorsHeaders(const std::vector<std::string> &headers) {
    corsConfig_.allowedHeaders = headers;
  }

  void setCorsMaxAge(int maxAge) { corsConfig_.maxAge = maxAge; }

  RouterResponse validate(std::string &path, std::string &method,
                          std::string &contentLength) {

    auto pathIt = routes_.find(path);
    if (pathIt == routes_.end())
      return RouterResponse::NOT_FOUND;
    if (pathIt->second.find(method) == pathIt->second.end())
      return RouterResponse::METHOD_NOT_ALLOWED;

    return RouterResponse::OK;
  }

  std::string getAllowedMethodsString(const std::string &path) {

    auto pathIt = routes_.find(path);
    if (pathIt == routes_.end())
      return "";
    auto methods = pathIt->second;

    std::string result;
    for (const auto &[method, _] : methods) {
      result += method + ", ";
      if (method == "GET")
        result += "HEAD, ";
    }
    if (!result.empty()) {
      result.erase(result.length() - 2);
    }
    return result;
  }

private:
  void addRoute(const std::string &path, const std::string &method,
                const Handler &handler) {
    routes_[path][method] = handler;
  }

  std::string getAllowedMethodsString(
      const std::unordered_map<std::string, Handler> &methods) {
    std::string result;
    for (const auto &[method, _] : methods) {
      result += method + ", ";
      if (method == "GET")
        result += "HEAD, ";
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
