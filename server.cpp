#include "ErrorFactory.hpp"
#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>

#include <nlohmann/json.hpp>

#include "CorsMiddleware.hpp"
#include "HttpServer.hpp"
#include "logUtils.hpp"

using json = nlohmann::json;

int main() {
  configureLog();
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  ErrorFactory errorFactory;

  Router router(errorFactory);
  CorsMiddleware corsMiddleware;
  corsMiddleware.setCorsOrigins(
      {"http://localhost:8080", "https://localhost:8443"});
  corsMiddleware.setCorsMaxAge(10);

  router.use(corsMiddleware);

  router.get("/", [](const HttpRequest &request) {
    auto name = request.getParam("name");

    return HttpResponse(200, "Hello " + name + "!");
  });

  router.post("/", [](const HttpRequest &request) {
    json data = json::parse(request.getBody());
    auto res = HttpResponse(200, "Hello, " + std::string(data["name"]) + "!");
    res.setHeader("Content-Type", "text/plain");
    return res;
  });

  router.put("/", [](const HttpRequest &request) {
    return HttpResponse(200, request.getBody());
  });

  // ── Test routes ────────────────────────────────────────────────────────────
  // All live under /tests/* so they're instantly readable in the server log.

  // GET /tests/ping
  // Simplest possible liveness check.
  router.get("/tests/ping", [](const HttpRequest &request) {
    return HttpResponse(200, "pong");
  });

  // GET /tests/echo
  // Returns all query-string params as a JSON object.
  // e.g. ?name=Alice&foo=bar  →  {"name":"Alice","foo":"bar"}
  router.get("/tests/echo", [](const HttpRequest &request) {
    json j(request.getAllParams());
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // POST /tests/echo
  // Echoes the raw request body back verbatim and mirrors the Content-Type.
  router.post("/tests/echo", [](const HttpRequest &request) {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    return res;
  });

  // PUT /tests/echo
  // Same as POST echo — useful for testing PUT-specific behaviour (405 etc.).
  router.put("/tests/echo", [](const HttpRequest &request) {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    return res;
  });

  // GET /tests/headers
  // Returns every header the server received as a JSON object.
  // Keys are lowercased (that's how they're stored internally).
  // Useful for verifying CORS headers, Host, custom headers, etc.
  router.get("/tests/headers", [](const HttpRequest &request) {
    json j(request.getAllHeaders());
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // GET /tests/error/throw
  // Deliberately throws a std::runtime_error to exercise the exception handler
  // in HttpConnection::generateResponse() — should produce a 500 JSON response.
  router.get("/tests/error/throw", [](const HttpRequest &) -> HttpResponse {
    throw std::runtime_error("Deliberate test error");
  });

  HttpServer server(errorFactory);
  server.setTlsContext("cert.pem", "key.pem");
  server.setRouter(router);
  server.addListener("localhost", "8080");
  server.addTlsListener("localhost", "8443");
  server.run();

  return 0;
}
