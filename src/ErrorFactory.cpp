#include "ErrorFactory.hpp"

#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "utils.hpp"

using json = nlohmann::json;

ErrorFactory::ErrorFactory() {
  auto fallbackFormatter = [](int statusCode,
                              const std::string_view &message = "") {
    HttpResponse response(statusCode);

    json body = {{"errorCode", statusCode},
                 {"errorMessage", message == ""
                                      ? HttpResponse::statusText(statusCode)
                                      : message}};

    response.setHeader("Content-Type", "application/json");
    response.setBody(body.dump());
    return response;
  };
  fallbackFormatterPair_ = {"application/json", fallbackFormatter};
}

void ErrorFactory::setFallbackFormatter(std::string type, Formatter formatter) {
  fallbackFormatterPair_ = {type, formatter};
}

std::pair<std::string, Formatter> ErrorFactory::getFallbackFormatter() {
  return fallbackFormatterPair_;
}

void ErrorFactory::setFormatter(std::string type, Formatter formatter) {
  registeredFormatters_.emplace_back(toLowerCase(type), formatter);
}

HttpResponse ErrorFactory::build(const HttpRequest &req, int statusCode,
                                 const std::string_view &message) const {
  std::vector<std::pair<std::string, float>> typePrefs;
  std::vector<std::string> excluded;

  const auto &acceptVector = req.getHeaders("Accept");
  if (acceptVector.empty())
    return fallbackFormatterPair_.second(statusCode, message);

  std::string acceptString;
  for (const auto &accept : acceptVector)
    acceptString += accept + ",";

  for (auto typeQ : split(acceptString, ",")) {
    trim(typeQ);
    auto sepIt = typeQ.find(';');
    std::string type = typeQ.substr(0, sepIt);
    trim(type);
    if (type.empty())
      continue;
    float q = 1.0f;
    if (sepIt != std::string::npos) {
      std::string params = typeQ.substr(sepIt + 1);
      auto qpos = params.find("q=");
      if (qpos != std::string::npos) {
        auto qval = params.substr(qpos + 2);
        trim(qval);
        q = std::strtof(qval.c_str(), nullptr);
      }
    }
    if (q == 0.0f) {
      excluded.push_back(type);
      continue;
    }
    typePrefs.emplace_back(type, q);
  }

  if (typePrefs.empty() && excluded.empty())
    return fallbackFormatterPair_.second(statusCode, message);

  if (typePrefs.empty()) {
    auto formatter =
        std::find_if(registeredFormatters_.begin(), registeredFormatters_.end(),
                     [&excluded](const auto &p) {
                       return std::none_of(excluded.begin(), excluded.end(),
                                           [&p](const auto &ex) {
                                             return mime_match(ex, p.first);
                                           });
                     });
    if (formatter != registeredFormatters_.end())
      return formatter->second(statusCode, message);
    return fallbackFormatterPair_.second(406, "");
  }

  std::sort(typePrefs.begin(), typePrefs.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  for (const auto &type : typePrefs) {
    auto formatter = std::find_if(
        registeredFormatters_.begin(), registeredFormatters_.end(),
        [&type](const auto &p) { return mime_match(p.first, type.first); });
    if (formatter != registeredFormatters_.end())
      return formatter->second(statusCode, message);
  }

  return fallbackFormatterPair_.second(406, "");
}
