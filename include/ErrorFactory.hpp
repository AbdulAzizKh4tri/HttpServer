#pragma once

#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "HttpResponse.hpp"
#include "utils.hpp"

using Formatter =
    std::function<HttpResponse(int statusCode, const std::string &message)>;
using json = nlohmann::json;

class ErrorFactory {
public:
  ErrorFactory() {
    defaultFormatter_ = [](int statusCode, const std::string &message = "") {
      HttpResponse response(statusCode);
      response.setHeader("Content-Type", "application/json");

      json body = {{"errorCode", statusCode},
                   {"errorMessage", message == ""
                                        ? HttpResponse::statusText(statusCode)
                                        : message}};

      response.setBody(body.dump());

      return response;
    };
  }

  void setDefaultFormatter(Formatter formatter) {
    defaultFormatter_ = formatter;
  }

  void setFormatter(std::string type, Formatter formatter) {
    registeredFormatters_[toLowerCase(type)] = formatter;
  }

  HttpResponse build(std::string acceptHeader, int statusCode,
                     const std::string &message = "") {

    std::string headerString = acceptHeader;
    if (auto it = acceptHeader.find(";"); it != std::string::npos) {
      headerString = acceptHeader.substr(0, it);
    }
    for (auto type : split(headerString, ",")) {
      type = trim(type);
      if (auto it = registeredFormatters_.find(type);
          it != registeredFormatters_.end()) {
        return it->second(statusCode, message);
      }
    }

    return defaultFormatter_(statusCode, message);
  }

private:
  std::unordered_map<std::string, Formatter> registeredFormatters_;
  Formatter defaultFormatter_;
};
