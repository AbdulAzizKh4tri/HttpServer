#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "CorsMiddleware.hpp"
#include "ErrorFactory.hpp"
#include "ExecutorContext.hpp"
#include "HttpResponse.hpp"
#include "HttpServer.hpp"
#include "InMemorySessionStore.hpp"
#include "Router.hpp"
#include "SessionMiddleware.hpp"
#include "StaticMiddleware.hpp"
#include "config.hpp"
#include "logUtils.hpp"
#include "routes.hpp"

using json = nlohmann::json;
thread_local Executor *tl_executor = nullptr;
thread_local bool tl_timed_out = false;

int main() {

  int N;
  std::string logging;

  std::cout << "Do we want logging? (y/n)" << std::endl;
  std::cin >> logging;
  std::cout << "How many threads?" << std::endl;
  std::cin >> N;

  configureLog(logging.contains('y'), "");
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  ErrorFactory errorFactory;

  auto jsonFormatter = [](int statusCode, const std::string_view &message = "") {
    HttpResponse response(statusCode);
    json body = {{"errorCode", statusCode},
                 {"errorMessage", message == "" ? HttpResponse::statusText(statusCode) : message}};

    response.headers.setHeaderLower("content-type", "application/json");
    response.setBody(body.dump());
    return response;
  };

  auto htmlFormatter = [](int statusCode, const std::string_view &message = "") {
    HttpResponse response(statusCode);
    std::string statusText = HttpResponse::statusText(statusCode);
    std::string msg = message.empty() ? statusText : std::string(message);
    std::string body = "<!DOCTYPE html>"
                       "<html><head><title>" +
                       std::to_string(statusCode) + " " + statusText +
                       "</title></head>"
                       "<body>"
                       "<h1>" +
                       std::to_string(statusCode) + " " + statusText +
                       "</h1>"
                       "<p>" +
                       msg +
                       "</p>"
                       "</body></html>";
    response.headers.setHeaderLower("content-type", "text/html");
    response.setBody(body);
    return response;
  };

  errorFactory.setFormatter("application/json", jsonFormatter);
  errorFactory.setFormatter("text/html", htmlFormatter);

  Router router(errorFactory);

  CorsMiddleware corsMiddleware;
  corsMiddleware.setCorsOrigins({"http://localhost:8080", "https://localhost:8443"});
  corsMiddleware.setCorsMaxAge(10);

  StaticMiddleware staticMiddleware("./public", "static", errorFactory);

  SessionConfig sessionConfig;
  auto ttl = std::chrono::seconds(std::stoi(std::to_string(SESSION_TTL_S)));
  InMemorySessionStore sessionStore(ttl);
  SessionMiddleware sessionMiddleware(sessionConfig, sessionStore);

  router.use(sessionMiddleware);

  router.use(corsMiddleware);
  router.use(staticMiddleware);

  registerRoutes(router, errorFactory);

  HttpServer server(errorFactory);
  server.setTlsContext(TLS_CERT_PATH, TLS_KEY_PATH);
  server.setRouter(router);
  server.addListener(HTTP_HOST, HTTP_PORT);
  server.addTlsListener(HTTP_HOST, HTTPS_PORT);
  server.run(N);

  return 0;
}
