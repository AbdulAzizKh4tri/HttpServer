#include "CorsMiddleware.hpp"

#include "HttpResponse.hpp"
#include "utils.hpp"

CorsMiddleware::CorsMiddleware() {}
CorsMiddleware::CorsMiddleware(CorsConfig corsConfig) : corsConfig_(corsConfig) {}

Task<Response> CorsMiddleware::operator()(const HttpRequest &request, Next next) {
  auto originView = request.getHeaderLower("origin");

  if (request.getMethod() == "OPTIONS" && not originView.empty()) {
    HttpResponse response(204);
    std::string allowedMethods = request.getAttribute("allowedMethods");

    std::string origin = std::string(originView);
    if (!isOriginAllowed(origin))
      co_return response;

    response.headers.addHeaderLower("vary", "origin");
    response.headers.setHeaderLower("access-control-allow-origin", origin);
    response.headers.setHeaderLower("access-control-allow-methods", allowedMethods);
    response.headers.setHeaderLower("access-control-allow-headers",
                                    getCommaSeparatedString(corsConfig_.allowedHeaders));
    response.headers.setHeaderLower("access-control-max-age", std::to_string(corsConfig_.maxAge));
    co_return response;
  }

  Response response = co_await next();

  std::visit(overloaded{[&originView, this](auto &res) {
               if (not originView.empty()) {
                 auto origin = std::string(originView);
                 if (isOriginAllowed(origin))
                   res.headers.setHeaderLower("access-control-allow-origin", origin);
               }
             }},
             response);

  co_return response;
}

void CorsMiddleware::setCorsOrigins(const std::vector<std::string> &origins) { corsConfig_.allowedOrigins = origins; }

void CorsMiddleware::setCorsHeaders(const std::vector<std::string> &headers) { corsConfig_.allowedHeaders = headers; }

void CorsMiddleware::setCorsMaxAge(int maxAge) { corsConfig_.maxAge = maxAge; }

bool CorsMiddleware::isOriginAllowed(const std::string &origin) {
  for (auto &allowedOrigin : corsConfig_.allowedOrigins) {
    if (allowedOrigin == origin || allowedOrigin == "*") {
      return true;
    }
  }
  return false;
}
