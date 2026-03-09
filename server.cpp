#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "CorsMiddleware.hpp"
#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpServer.hpp"
#include "Router.hpp"
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
    auto name = request.getQueryParam("name");

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
    json j(request.getAllQueryParams());
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

  // GET /tests/users/<id>
  router.get("/tests/users/<id>", [](const HttpRequest &request) {
    json j = {{"userId", request.getPathParam("id")}};
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // GET /tests/users/<userId>/posts/<postId>
  router.get("/tests/users/<userId>/posts/<postId>",
             [](const HttpRequest &request) {
               json j = {{"userId", request.getPathParam("userId")},
                         {"postId", request.getPathParam("postId")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               return res;
             });

  // DELETE /tests/items/<id>
  router.delete_("/tests/items/<id>",
                 [](const HttpRequest &request) { return HttpResponse(200); });

  // GET /tests/wildcard/* — single segment
  router.get("/tests/wildcard/*", [](const HttpRequest &request) {
    json j = {{"path", request.getPathParam("*")}};
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // GET /tests/deepwildcard/** — greedy
  router.get("/tests/deepwildcard/**", [](const HttpRequest &request) {
    json j = {{"path", request.getPathParam("**")}};
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // ── URL decoding test routes ───────────────────────────────────────────────

  // GET /tests/decode/query
  // Returns all decoded query params as JSON.
  // Tests: %20, +, encoded keys, malformed sequences kept as-is.
  // e.g. ?name=John%20Doe  →  {"name":"John Doe"}
  // e.g. ?key%20hi=val     →  {"key hi":"val"}
  router.get("/tests/decode/query", [](const HttpRequest &request) {
    json j(request.getAllQueryParams());
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // GET /tests/decode/path/<name>
  // Returns the decoded path param.
  // Tests: %20 in path segments, %2F (slash) decoded inside param value,
  //        malformed sequences kept as-is.
  // e.g. /tests/decode/path/John%20Doe  →  {"name":"John Doe"}
  // e.g. /tests/decode/path/foo%2Fbar   →  {"name":"foo/bar"}
  router.get("/tests/decode/path/<name>", [](const HttpRequest &request) {
    json j = {{"name", request.getPathParam("name")}};
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // GET /tests/decode/rawpath
  // Returns the raw, un-decoded path + query string exactly as the client sent
  // it. Useful for verifying that getRawPath() is untouched while decoded
  // getters work. e.g. ?name=John%20Doe  →
  // {"rawPath":"/tests/decode/rawpath?name=John%20Doe"}
  router.get("/tests/decode/rawpath", [](const HttpRequest &request) {
    json j = {{"rawPath", request.getRawPath()}};
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    return res;
  });

  // ── Chunked / streaming response test routes ──────────────────────────────
  // All live under /tests/stream/*

  // GET /tests/stream/basic
  // A handful of small named chunks. Assembled body: "Hello, World!"
  // The canonical "does streaming work at all" check.
  router.get("/tests/stream/basic", [](const HttpRequest &) {
    std::vector<std::string> chunks = {"Hello", ", ", "World", "!"};
    return HttpStreamResponse(
        200, [chunks, i = 0]() mutable -> std::optional<std::string> {
          if (i == (int)chunks.size())
            return std::nullopt;
          return chunks[i++];
        });
  });

  // GET /tests/stream/single
  // Exactly one chunk, then done. Assembled body: "hello"
  router.get("/tests/stream/single", [](const HttpRequest &) {
    return HttpStreamResponse(
        200, [done = false]() mutable -> std::optional<std::string> {
          if (done)
            return std::nullopt;
          done = true;
          return "hello";
        });
  });

  // GET /tests/stream/empty
  // Returns nullopt on the very first call — terminal chunk immediately.
  // Assembled body is empty, status still 200.
  router.get("/tests/stream/empty", [](const HttpRequest &) {
    return HttpStreamResponse(
        200, []() -> std::optional<std::string> { return std::nullopt; });
  });

  // GET /tests/stream/count/<n>
  // Streams exactly n chunks: "chunk-1", "chunk-2", …, "chunk-n".
  // n=0  → empty body (same as /empty)
  // n<0  → clamped to 0
  // non-numeric n → std::stoi throws before the HttpStreamResponse is
  //   constructed, generateResponse() catches it → plain 500 JSON response.
  //   This is intentional; the caller is responsible for valid input.
  router.get("/tests/stream/count/<n>", [](const HttpRequest &request) {
    int n = std::stoi(request.getPathParam("n")); // throws on bad input → 500
    if (n < 0)
      n = 0;
    return HttpStreamResponse(
        200, [n, i = 0]() mutable -> std::optional<std::string> {
          if (i >= n)
            return std::nullopt;
          return "chunk-" + std::to_string(++i);
        });
  });

  // GET /tests/stream/throw
  // Emits two chunks successfully, then the lambda throws.
  // After the fix: the error is caught, "Internal Server Error" is sent as
  // a final chunk, the stream is properly terminated, and the connection
  // closes cleanly. Status is still 200 (headers already sent).
  // Assembled body: "chunk-1chunk-2Internal Server Error"
  router.get("/tests/stream/throw", [](const HttpRequest &) {
    return HttpStreamResponse(
        200, [i = 0]() mutable -> std::optional<std::string> {
          if (i++ < 2)
            return "chunk-" + std::to_string(i);
          throw std::runtime_error("Deliberate stream error");
        });
  });

  // POST /tests/stream/echo
  // Reads the request body and streams it back in 4-byte chunks.
  // Empty body → empty stream (terminal chunk only).
  // Mirrors Content-Type if provided.
  router.post("/tests/stream/echo", [](const HttpRequest &request) {
    std::string body = request.getBody();
    std::string ctype = request.getHeader("Content-Type");
    auto res = HttpStreamResponse(
        200,
        [body, offset = size_t(0)]() mutable -> std::optional<std::string> {
          if (offset >= body.size())
            return std::nullopt;
          auto len = std::min(size_t(4), body.size() - offset);
          auto chunk = body.substr(offset, len);
          offset += len;
          return chunk;
        });
    if (!ctype.empty())
      res.setHeader("Content-Type", ctype);
    return res;
  });

  HttpServer server(errorFactory);
  server.setTlsContext("cert.pem", "key.pem");
  server.setRouter(router);
  server.addListener("localhost", "8080");
  server.addTlsListener("localhost", "8443");
  server.run();

  return 0;
}
