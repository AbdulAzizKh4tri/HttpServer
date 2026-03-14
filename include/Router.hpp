#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "HttpTypes.hpp"
#include "utils.hpp"

enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };
struct RouteEntry {
  std::string pattern;
  std::vector<std::string> patternParts;
  std::unordered_map<std::string, Handler> requestHandlers;
};

class Router {
public:
  Router(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

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

  void use(Middleware middleware) { middlewares_.push_back(middleware); }

  Response dispatch(HttpRequest &request) {

    if (request.getMethod() == "TRACE" || request.getMethod() == "CONNECT") {
      return errorFactory_.build(request.getHeader("Accept"), 501);
    }

    const auto &requestPath = request.getPath();
    auto pathParts = split(requestPath, "/");

    auto routeEntry = findMatchingRouteEntry(pathParts);

    if (routeEntry == nullptr) {
      auto response = errorFactory_.build(request.getHeader("Accept"), 404);
      if (request.getMethod() == "HEAD")
        response.stripBody();
      return response;
    }

    auto &definedMethods = routeEntry->requestHandlers;
    auto allowedMethods = getAllowedMethodsString(requestPath);
    request.setAttribute("allowedMethods", allowedMethods);

    auto pathParams = getPathParams(routeEntry->patternParts, pathParts);
    request.setPathParams(pathParams);

    Handler terminal = [&](const HttpRequest &req) -> Response {
      auto lookupMethod = req.getMethod() == "HEAD" ? "GET" : req.getMethod();
      auto methodIt = definedMethods.find(lookupMethod);
      if (methodIt != definedMethods.end()) {
        auto response = methodIt->second(req);
        std::visit(overloaded{[&req, &response](HttpResponse &res) {
                                if (req.getMethod() == "HEAD")
                                  res.stripBody();
                              },
                              [&req, &response](HttpStreamResponse &stream) {
                                if (req.getMethod() == "HEAD") {
                                  HttpResponse res(stream.getStatusCode());
                                  for (auto &[k, v] : stream.getAllHeaders()) {
                                    res.setHeader(k, v);
                                  }
                                  res.stripBody();
                                  response = res;
                                }
                              }},
                   response);
        return response;
      }

      if (req.getMethod() == "OPTIONS" && req.getHeader("Origin") == "") {
        HttpResponse response(204);
        response.setHeader("Allow", allowedMethods);
        return response;
      }

      HttpResponse response = errorFactory_.build(req.getHeader("Accept"), 405);
      if (request.getMethod() == "HEAD")
        response.stripBody();
      response.setHeader("Allow", allowedMethods);
      return response;
    };

    return runChain(request, terminal, 0);
  }

  RouterResponse validate(const std::string &path, const std::string &method) {
    auto lookupMethod = (method == "HEAD") ? "GET" : method;

    auto pathParts = split(path, "/");
    auto routeEntry = findMatchingRouteEntry(pathParts);
    if (routeEntry == nullptr)
      return RouterResponse::NOT_FOUND;

    if (!routeEntry->requestHandlers.contains(lookupMethod))
      return RouterResponse::METHOD_NOT_ALLOWED;

    return RouterResponse::OK;
  }

  std::string getAllowedMethodsString(const std::string &path) {

    auto pathParts = split(path, "/");
    auto routeEntry = findMatchingRouteEntry(pathParts);
    if (routeEntry == nullptr)
      return "";

    auto methods = routeEntry->requestHandlers;

    std::string result;
    for (const auto &[method, _] : methods) {
      result += method + ", ";
      if (method == "GET")
        result += "HEAD, ";
    }
    if (!result.empty()) {
      result += "OPTIONS";
    }
    return result;
  }

private:
  std::vector<RouteEntry> routes_;
  std::vector<Middleware> middlewares_;
  ErrorFactory &errorFactory_;

  Response runChain(HttpRequest &request, const Handler &handler,
                    size_t startIndex) {
    if (startIndex >= middlewares_.size()) {
      return handler(request);
    }

    auto next = [&] { return runChain(request, handler, startIndex + 1); };
    const auto &middleware = middlewares_[startIndex];
    return middleware(request, next);
  }

  RouteEntry *
  findMatchingRouteEntry(const std::vector<std::string> &pathParts) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](const RouteEntry &entry) {
                             return matches(entry.patternParts, pathParts);
                           });

    return it != routes_.end() ? &(*it) : nullptr;
  }

  std::unordered_map<std::string, std::string>
  getPathParams(const std::vector<std::string> &patternParts,
                const std::vector<std::string> &pathParts) {

    std::unordered_map<std::string, std::string> pathParams;

    for (size_t i = 0; i < patternParts.size(); i++) {
      if (patternParts[i] == "**") {
        std::string captured;
        for (size_t j = i; j < pathParts.size(); j++)
          captured += pathParts[j] + "/";
        if (!captured.empty())
          captured.pop_back();
        pathParams["**"] = captured;
        return pathParams;
      }
      if (patternParts[i] == "*") {
        pathParams["*"] = pathParts[i];
        continue;
      }
      if (patternParts[i][0] == '<' && patternParts[i].back() == '>') {
        auto paramKey = patternParts[i].substr(1, patternParts[i].size() - 2);
        pathParams[percentDecode(paramKey)] = percentDecode(pathParts[i]);
      }
    }

    return pathParams;
  }

  void addRoute(const std::string &routePattern, const std::string &method,
                const Handler &handler) {

    auto pattern = routePattern;
    normalizePath(pattern);
    validatePattern(pattern);

    auto it = std::find_if(
        routes_.begin(), routes_.end(),
        [&](const RouteEntry &entry) { return entry.pattern == pattern; });

    if (it != routes_.end()) {
      if (it->requestHandlers.contains(method))
        throw std::invalid_argument("Duplicate route: " + pattern);
      it->requestHandlers[method] = handler;
    } else {
      RouteEntry entry;
      entry.pattern = pattern;
      entry.patternParts = split(pattern, "/");
      entry.requestHandlers[method] = handler;
      routes_.push_back(entry);
    }
  }

  void validatePattern(const std::string &pattern) {

    auto parts = split(pattern, "/");
    std::unordered_set<std::string> params;

    auto isValidParamChar = [](char c) { return std::isalnum(c) || c == '_'; };

    for (size_t i = 0; i < parts.size(); i++) {
      if (parts[i].empty())
        throw std::invalid_argument("Empty segment in pattern: " + pattern);

      bool isLast = (i == parts.size() - 1);
      if ((parts[i] == "*" || parts[i] == "**") && !isLast)
        throw std::invalid_argument(
            "Wildcard '" + parts[i] +
            "' must be the last segment in pattern: " + pattern);

      if (parts[i][0] == '<' && parts[i].back() == '>') {
        std::string param = parts[i].substr(1, parts[i].size() - 2);

        if (param.empty())
          throw std::invalid_argument("Parameter name cannot be empty: " +
                                      pattern);

        if (!std::ranges::all_of(param, isValidParamChar))
          throw std::invalid_argument(
              "Parameter name '" + param +
              "' contains invalid characters in pattern: " + pattern);

        if (params.contains(param))
          throw std::invalid_argument("Duplicate parameter '" + param +
                                      "' in pattern: " + pattern);
        params.insert(param);
      }
    }
  }

  bool matches(const std::vector<std::string> &patternParts,
               const std::vector<std::string> &pathParts) {

    size_t i;
    for (i = 0; i < patternParts.size(); i++) {
      if (i >= pathParts.size())
        return false;
      if (patternParts[i] == "**")
        return true;
      if (patternParts[i] == "*")
        continue;
      if (patternParts[i][0] == '<' && patternParts[i].back() == '>')
        continue;
      if (patternParts[i] != pathParts[i])
        return false;
    }

    return i == pathParts.size();
  }
};
