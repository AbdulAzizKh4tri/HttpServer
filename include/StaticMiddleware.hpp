#pragma once

#include <filesystem>
#include <string>

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpTypes.hpp"
#include "serverConfig.hpp"

class StaticMiddleware {
public:
  StaticMiddleware(const std::string root, const std::string prefix,
                   ErrorFactory &errorFactory);

  Task<Response> operator()(const HttpRequest &request, Next next);

  HttpResponse buildErrorResponse(const HttpRequest &request,
                                  const int statusCode,
                                  const std::string &message = "") const;

private:
  ErrorFactory &errorFactory_;
  std::string root_, prefix_;
  std::filesystem::path canonical_root_;
};
