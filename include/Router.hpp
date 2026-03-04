#pragma once

#include <string>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

using Handler = std::function<HttpResponse(const HttpRequest &)>;

class Router {
public:
  void get(std::string path, Handler handler) {
    addRoute("GET", path, handler);
  }

  void post(std::string path, Handler handler) {
    addRoute("POST", path, handler);
  }

  HttpResponse dispatch(HttpRequest &request) {
    HttpResponse response;
    handle(request, response);

    SPDLOG_INFO("{} {} {}", response.getStatusCode(), request.method,
                request.path);

    return response;
  }

private:
  void addRoute(std::string method, std::string path, Handler handler) {
    routes_[method][path] = handler;
  }

  void handle(const HttpRequest &request, HttpResponse &response) {

    auto it = routes_.find(request.method);
    if (it == routes_.end())
      response = HttpResponse(404);

    auto it2 = it->second.find(request.path);
    if (it2 != it->second.end())
      response = HttpResponse(404);

    response = it2->second(request);
  }

  std::unordered_map<std::string, std::unordered_map<std::string, Handler>>
      routes_;
};
