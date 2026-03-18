#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "CorsMiddleware.hpp"
#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpServer.hpp"
#include "Router.hpp"
#include "logUtils.hpp"
#include "utils.hpp"

using json = nlohmann::json;

int main() {
  configureLog(false, "");
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  ErrorFactory errorFactory;

  auto jsonFormatter = [](int statusCode,
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

  auto htmlFormatter = [](int statusCode,
                          const std::string_view &message = "") {
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
    response.setHeader("Content-Type", "text/html");
    response.setBody(body);
    return response;
  };

  errorFactory.setFormatter("application/json", jsonFormatter);
  errorFactory.setFormatter("text/html", htmlFormatter);

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
    json j = toJsonObject(request.getAllQueryParams());
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
    json j = toJsonObject(request.getAllHeaders());
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
    json j = toJsonObject(request.getAllQueryParams());
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
    auto gen = []() -> Generator<std::string> {
      co_yield "Hello";
      co_yield ", ";
      co_yield "World";
      co_yield "!";
    }();
    return HttpStreamResponse(200, std::move(gen));
  });

  // GET /tests/stream/single
  // Exactly one chunk, then done. Assembled body: "hello"
  router.get("/tests/stream/single", [](const HttpRequest &) {
    auto gen = []() -> Generator<std::string> { co_yield "hello"; }();
    return HttpStreamResponse(200, std::move(gen));
  });

  // GET /tests/stream/empty
  // Returns nullopt on the very first call — terminal chunk immediately.
  // Assembled body is empty, status still 200.
  router.get("/tests/stream/empty", [](const HttpRequest &) {
    auto gen = []() -> Generator<std::string> { co_return; }();
    return HttpStreamResponse(200, std::move(gen));
  });

  // GET /tests/stream/count/<n>
  // Streams exactly n chunks: "chunk-1", "chunk-2", …, "chunk-n".
  // n=0  → empty body (same as /empty)
  // n<0  → clamped to 0
  // non-numeric n → std::stoi throws before the HttpStreamResponse is
  //   constructed, generateResponse() catches it → plain 500 JSON response.
  //   This is intentional; the caller is responsible for valid input.
  router.get("/tests/stream/count/<n>", [](const HttpRequest &request) {
    int n = std::stoi(request.getPathParam(
        "n")); // throws here → caught by generateResponse → 500
    if (n < 0)
      n = 0;

    auto gen = [](int n) -> Generator<std::string> {
      for (int i = 0; i < n; i++)
        co_yield "chunk-" + std::to_string(i + 1);
    }(n);

    return HttpStreamResponse(200, std::move(gen));
  });

  // GET /tests/stream/throw
  // Emits two chunks successfully, then the lambda throws.
  // After the fix: the error is caught, "Internal Server Error" is sent as
  // a final chunk, the stream is properly terminated, and the connection
  // closes cleanly. Status is still 200 (headers already sent).
  // Assembled body: "chunk-1chunk-2Internal Server Error"
  router.get("/tests/stream/throw", [](const HttpRequest &) {
    auto gen = []() -> Generator<std::string> {
      co_yield "chunk-1";
      co_yield "chunk-2";
      throw std::runtime_error("Deliberate stream error");
    }();
    return HttpStreamResponse(200, std::move(gen));
  });

  // POST /tests/stream/echo
  // Reads the request body and streams it back in 4-byte chunks.
  // Empty body → empty stream (terminal chunk only).
  // Mirrors Content-Type if provided.
  router.post("/tests/stream/echo", [](const HttpRequest &request) {
    std::string body = request.getBody();
    std::string ctype = request.getHeader("Content-Type");

    auto gen = [](std::string body) -> Generator<std::string> {
      size_t offset = 0;
      while (offset < body.size()) {
        auto len = std::min(size_t(4), body.size() - offset);
        co_yield body.substr(offset, len);
        offset += len;
      }
    }(std::move(body));

    auto res = HttpStreamResponse(200, std::move(gen));
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
