#include "routes.hpp"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

void registerRoutes(Router &router) {
  router.get("/", [](const HttpRequest &request) -> Task<Response> {
    auto name = request.getQueryParam("name");

    co_return HttpResponse(200, "Hello " + name + "!");
  });

  router.post("/", [](const HttpRequest &request) -> Task<Response> {
    json data = json::parse(request.getBody());
    auto res = HttpResponse(200, "Hello, " + std::string(data["name"]) + "!");
    res.setHeader("Content-Type", "text/plain");
    co_return res;
  });

  router.put("/", [](const HttpRequest &request) -> Task<Response> {
    co_return HttpResponse(200, request.getBody());
  });

  // ── Test routes ────────────────────────────────────────────────────────────
  // All live under /tests/* so they're instantly readable in the server log.

  // GET /tests/ping
  // Simplest possible liveness check.
  router.get("/tests/ping", [](const HttpRequest &request) -> Task<Response> {
    co_return HttpResponse(200, "pong");
  });

  // GET /tests/echo
  // co_returns all query-string params as a JSON object.
  // e.g. ?name=Alice&foo=bar  →  {"name":"Alice","foo":"bar"}
  router.get("/tests/echo", [](const HttpRequest &request) -> Task<Response> {
    json j = toJsonObject(request.getAllQueryParams());
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    co_return res;
  });

  // POST /tests/echo
  // Echoes the raw request body back verbatim and mirrors the Content-Type.
  router.post("/tests/echo", [](const HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    co_return res;
  });

  // PUT /tests/echo
  // Same as POST echo — useful for testing PUT-specific behaviour (405 etc.).
  router.put("/tests/echo", [](const HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    co_return res;
  });

  // GET /tests/headers
  // co_returns every header the server received as a JSON object.
  // Keys are lowercased (that's how they're stored internally).
  // Useful for verifying CORS headers, Host, custom headers, etc.
  router.get("/tests/headers",
             [](const HttpRequest &request) -> Task<Response> {
               json j = toJsonObject(request.getAllHeaders());
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // GET /tests/error/throw
  // Deliberately throws a std::runtime_error to exercise the exception handler
  // in HttpConnection::generateResponse() — should produce a 500 JSON response.
  router.get("/tests/error/throw", [](const HttpRequest &) -> Task<Response> {
    throw std::runtime_error("Deliberate test error");
  });

  // GET /tests/users/<id>
  router.get("/tests/users/<id>",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"userId", request.getPathParam("id")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // GET /tests/users/<userId>/posts/<postId>
  router.get("/tests/users/<userId>/posts/<postId>",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"userId", request.getPathParam("userId")},
                         {"postId", request.getPathParam("postId")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // DELETE /tests/items/<id>
  router.delete_("/tests/items/<id>",
                 [](const HttpRequest &request) -> Task<Response> {
                   co_return HttpResponse(200);
                 });

  // GET /tests/wildcard/* — single segment
  router.get("/tests/wildcard/*",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"path", request.getPathParam("*")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // GET /tests/deepwildcard/** — greedy
  router.get("/tests/deepwildcard/**",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"path", request.getPathParam("**")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // ── URL decoding test routes ───────────────────────────────�
}
