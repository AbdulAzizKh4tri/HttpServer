#pragma once

#include <string>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

using Handler = std::function<HttpResponse(const HttpRequest &)>;

class Router {
public:
  void get(std::string path, Handler handler) {
    addRoute(path, "GET", handler);
  }

  void post(std::string path, Handler handler) {
    addRoute(path, "POST", handler);
  }

  HttpResponse dispatch(HttpRequest &request) {
    HttpResponse response;
    handle(request, response);
    return response;
  }

private:
  void addRoute(std::string path, std::string method, Handler handler) {
    routes_[path][method] = handler;
  }

  void handle(const HttpRequest &request, HttpResponse &response) {
    auto it = routes_.find(request.path);
    if (it == routes_.end()) {
      response = HttpResponse(404);
      return;
    }

    auto it2 = it->second.find(request.method);
    if (it2 == it->second.end()) {
      response = HttpResponse(405);
      return;
    }

    response = it2->second(request);
  }

  std::unordered_map<std::string, std::unordered_map<std::string, Handler>>
      routes_;
};
