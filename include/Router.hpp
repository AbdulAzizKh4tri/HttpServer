#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpTypes.hpp"

enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };

struct RouteNode {
  std::unordered_map<std::string, RouteNode> children;
  std::unique_ptr<RouteNode> paramChild;
  std::unique_ptr<RouteNode> wildcardChild;
  std::unique_ptr<RouteNode> deepWildcardChild;
  std::unordered_map<std::string, Handler> requestHandlers;
  std::vector<std::string> patternParts;
};

class Router {
public:
  Router(ErrorFactory &errorFactory);

  void get(std::string path, Handler handler);

  void post(std::string path, Handler handler);

  void put(std::string path, Handler handler);

  void patch(std::string path, Handler handler);

  void delete_(std::string path, Handler handler);

  void use(Middleware middleware);

  Task<Response> dispatch(HttpRequest &request);

  RouterResponse validate(const std::string &path, const std::string &method);

  std::string getAllowedMethodsString(const std::string &path);

  std::string getAllowedMethodsString(const RouteNode *pathNode);

private:
  RouteNode pathTreeRoot_;
  std::vector<Middleware> middlewares_;
  ErrorFactory &errorFactory_;
  std::unordered_set<std::string> registeredMethods_;

  Task<Response> runChain(HttpRequest &request, Handler &handler,
                          size_t startIndex);

  RouteNode *findMatchingRouteEntry(const std::vector<std::string> &pathParts);

  std::vector<std::pair<std::string, std::string>>
  getPathParams(const std::vector<std::string> &patternParts,
                const std::vector<std::string> &pathParts);

  void addRoute(const std::string &routePattern, const std::string &method,
                Handler &handler);

  void validatePattern(const std::string &pattern,
                       const std::vector<std::string> &parts);
};
