#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpTypes.hpp"

struct StaticConfig {
  std::string root;
  std::string prefix;
  std::unordered_map<std::string, std::string> mimeCacheControl;
  std::string defaultCacheControl = "max-age=5, public";
};

class StaticMiddleware {
public:
  StaticMiddleware(ErrorFactory &errorFactory, StaticConfig);
  StaticMiddleware(ErrorFactory &errorFactory, const std::string &root, const std::string &prefix);

  Task<Response> operator()(const HttpRequest &request, Next next);

  HttpResponse buildErrorResponse(const HttpRequest &request, const int statusCode,
                                  const std::string &message = "") const;

  void setRoot(const std::string &root);
  void setPrefix(const std::string &prefix);
  void setErrorFactory(const ErrorFactory &errorFactory);
  void setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader);
  void setDefaultCacheControl(const std::string &cacheControlHeader);

private:
  StaticConfig config_;
  ErrorFactory errorFactory_;
  std::filesystem::path canonical_root_;

  std::unordered_map<std::string, std::filesystem::path> canonicalPathCache_;
};
