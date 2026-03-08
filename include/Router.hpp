#pragma once

#include <string>

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Middleware.hpp"

using Handler = std::function<HttpResponse(const HttpRequest &)>;

enum class RouterResponse { OK, NOT_FOUND, METHOD_NOT_ALLOWED };

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

  HttpResponse dispatch(HttpRequest &request) {
    auto pathIt = routes_.find(request.getPath());

    if (pathIt != routes_.end()) {
      auto &definedMethods = pathIt->second;
      auto allowedMethods = getAllowedMethodsString(request.getPath());
      request.setAttribute("allowedMethods", allowedMethods);

      Handler terminal = [&](const HttpRequest &req) -> HttpResponse {
        auto methodIt = definedMethods.find(req.getMethod());
        if (methodIt != definedMethods.end())
          return methodIt->second(req);

        if (req.getMethod() == "OPTIONS" && req.getHeader("Origin") == "") {
          HttpResponse response(204);
          response.setHeader("Allow", allowedMethods);
          return response;
        }

        HttpResponse response =
            errorFactory_.build(req.getHeader("Accept"), 405);
        response.setHeader("Allow", allowedMethods);
        return response;
      };

      return runChain(request, terminal, 0);
    }
    return errorFactory_.build(request.getHeader("Accept"), 404);
  }

  RouterResponse validate(const std::string &path, const std::string &method) {

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

  void setErrorFactory(ErrorFactory &errorFactory) {
    errorFactory_ = errorFactory;
  }

private:
  std::unordered_map<std::string, std::unordered_map<std::string, Handler>>
      routes_;
  std::vector<Middleware> middlewares_;
  ErrorFactory &errorFactory_;

  void addRoute(const std::string &path, const std::string &method,
                const Handler &handler) {
    routes_[path][method] = handler;
  }

  HttpResponse runChain(HttpRequest &request, const Handler &handler,
                        size_t startIndex) {
    if (startIndex >= middlewares_.size()) {
      return handler(request);
    }

    auto next = [&] { return runChain(request, handler, startIndex + 1); };
    auto middleware = middlewares_[startIndex];
    return middleware(request, next);
  }
};
