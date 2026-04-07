#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "CacheControlMiddleware.hpp"
#include "CompressionMiddleware.hpp"
#include "CorsMiddleware.hpp"
#include "ErrorFactory.hpp"
#include "ExecutorContext.hpp"
#include "HttpResponse.hpp"
#include "HttpServer.hpp"
#include "InMemorySessionStore.hpp"
#include "Router.hpp"
#include "SessionMiddleware.hpp"
#include "StaticMiddleware.hpp"
#include "ThreadPool.hpp"
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
    response.headers.setCacheControl("no-store");
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
    response.headers.setCacheControl("no-store");
    response.setBody(body);
    return response;
  };

  errorFactory.setFormatter("application/json", jsonFormatter);
  errorFactory.setFormatter("text/html", htmlFormatter);

  Router router(errorFactory);

  CorsMiddleware corsMiddleware;
  corsMiddleware.setCorsOrigins({"http://localhost:8080", "https://localhost:8443", "http://127.0.0.1:8080"});
  corsMiddleware.setCorsMaxAge(10);

  StaticMiddleware staticMiddleware(errorFactory, "./public", "static");
  staticMiddleware.setMimeCacheControl("text/css", "no-cache, no-store"); // just for testing

  SessionConfig sessionConfig;
  auto ttl = std::chrono::seconds(std::stoi(std::to_string(120)));
  InMemorySessionStore sessionStore(ttl);
  SessionMiddleware sessionMiddleware(sessionConfig, sessionStore);

  CacheControlMiddleware cacheControlMiddleware;
  cacheControlMiddleware.setRouteCacheControl("/tests/debug/*", "max-age=5, public");
  cacheControlMiddleware.setMimeCacheControl("text/html", "max-age=5, public");
  cacheControlMiddleware.setDefaultCacheControl("no-cache, no-store");

  CompressionMiddleware compressionMiddleware(errorFactory);

  // First because all routes potentially need CORS, it doesn't modify the body so it's fine to put here
  router.use(corsMiddleware);
  // Must come before others because it has it's own caching/compression, short circuits chain if it can serve the file
  router.use(staticMiddleware);
  // Order doesn't matter after this
  router.use(compressionMiddleware);
  router.use(cacheControlMiddleware);
  router.use(sessionMiddleware);

  HttpServer server(errorFactory);

  ThreadPool threadPool(N * 2);
  registerRoutes(router, errorFactory, &threadPool);

  server.setTlsContext("cert.pem", "key.pem");
  server.setRouter(router);
  server.addListener("localhost", "8080");
  server.addTlsListener("localhost", "8443");

  server.run(N);

  return 0;
}
