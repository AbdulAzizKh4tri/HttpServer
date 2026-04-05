#pragma once

#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpTypes.hpp"

class CompressionMiddleware {
public:
  CompressionMiddleware(ErrorFactory &errorFactory);
  Task<Response> operator()(const HttpRequest &request, Next next);

private:
  ErrorFactory &errorFactory_;

  HttpResponse buildErrorResponse(const HttpRequest &request, const int statusCode,
                                  const std::string &message = "") const;
};
