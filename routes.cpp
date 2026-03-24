#include "routes.hpp"

#include <nlohmann/json.hpp>

#include "AsyncFileReader.hpp"
#include "ErrorFactory.hpp"
#include "HttpResponse.hpp"

using json = nlohmann::json;

void registerRoutes(Router &router, const ErrorFactory &errorFactory) {

  router.get(
      "/", [&errorFactory](const HttpRequest &request) -> Task<Response> {
        auto name = request.getQueryParam("name");

        std::filesystem::path resolved = std::filesystem::weakly_canonical(
            std::filesystem::path("./public/home/index.html"));
        std::optional<AsyncFileReader> fileOpt =
            AsyncFileReader::open(resolved);

        if (not fileOpt.has_value())
          co_return errorFactory.build(request, 403);

        AsyncFileReader &file = fileOpt.value();

        std::string body = co_await file.readAll();
        HttpResponse response(200, body);
        response.setHeader("Content-Type", "text/html");
        if (request.getMethod() == "HEAD")
          response.stripBody();
        co_return response;
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

  // WARNING: BLOCKS THE ENTIRE THREAD FOR 5 SECONDS!
  router.get("/tests/slow", [](const HttpRequest &) -> Task<Response> {
    // simulate slow work -- busy wait for 5 seconds
    auto end = now() + std::chrono::seconds(5);
    while (now() < end) {
    }
    co_return HttpResponse(200, "slow response");
  });

  // -- Test routes ------------------------------------------------------------
  // All live under /tests/* so they're instantly readable in the server log.

  // GET /tests/ping
  // Simplest possible liveness check.
  router.get("/tests/ping", [](const HttpRequest &) -> Task<Response> {
    co_return HttpResponse(200, "pong");
  });

  // GET|POST /tests/debug/request
  // Full request introspection -- returns every observable property of the
  // incoming request as a single JSON object. Replaces the old per-concern
  // routes (GET /tests/echo, /tests/headers, /tests/decode/query,
  // /tests/decode/rawpath) which scattered the same information across four
  // endpoints. Use this as the single source of truth for header, query,
  // cookie, path, and body tests.
  //
  // Response shape:
  // {
  //   "method":  "GET",
  //   "path":    "/tests/debug/request",
  //   "rawPath": "/tests/debug/request?foo=bar",
  //   "headers": { "host": "localhost:8080", ... },
  //   "query":   { "foo": "bar" },
  //   "cookies": { "session": "abc123" },
  //   "body":    ""
  // }
  auto debugRequestHandler = [](const HttpRequest &request) -> Task<Response> {
    json cookies = json::object();
    for (const auto &[name, value] : request.getCookies())
      cookies[name] = value;

    json j = {
        {"method", request.getMethod()},
        {"path", request.getPath()},
        {"rawPath", request.getRawPath()},
        {"headers", toJsonObject(request.getAllHeaders())},
        {"query", toJsonObject(request.getAllQueryParams())},
        {"cookies", cookies},
        {"body", request.getBody()},
    };
    auto res = HttpResponse(200, j.dump());
    res.setHeader("Content-Type", "application/json");
    co_return res;
  };

  router.get("/tests/debug/request", debugRequestHandler);
  router.post("/tests/debug/request", debugRequestHandler);

  // POST /tests/echo
  // Echoes the raw request body back verbatim and mirrors the Content-Type.
  // Kept because body-echo tests (empty body, near-limit, chunked) need a
  // route that returns the body directly, not wrapped in JSON.
  router.post("/tests/echo", [](const HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    co_return res;
  });

  // PUT /tests/echo
  // Same as POST echo -- useful for testing PUT-specific behaviour (405 etc.).
  router.put("/tests/echo", [](const HttpRequest &request) -> Task<Response> {
    auto res = HttpResponse(200, request.getBody());
    auto ct = request.getHeader("Content-Type");
    if (!ct.empty())
      res.setHeader("Content-Type", ct);
    co_return res;
  });

  // GET /tests/error/throw
  // Deliberately throws a std::runtime_error to exercise the exception handler
  // in HttpConnection::generateResponse() -- should produce a 500 JSON
  // response with Connection: keep-alive (handler exceptions are non-fatal).
  router.get("/tests/error/throw", [](const HttpRequest &) -> Task<Response> {
    throw std::runtime_error("Deliberate test error");
  });

  // -- Cookie routes ----------------------------------------------------------
  // All live under /tests/cookies/*

  // GET /tests/cookies/set
  // Sets a cookie on the response, controlled by query params:
  //
  //   name=<str>     cookie name  (default: "test")
  //   value=<str>    cookie value (default: "hello")
  //   path=<str>     Path attribute   (optional, default: "/")
  //   domain=<str>   Domain attribute (optional)
  //   maxage=<int>   Max-Age in seconds (optional)
  //   samesite=<str> SameSite value: Strict | Lax | None (optional)
  //   httponly       if present (key-only), sets HttpOnly
  //   secure         if present (key-only), sets Secure
  //
  // Also responds with a JSON body describing the cookie that was set, so
  // tests can assert both the Set-Cookie response header and the attributes.
  router.get("/tests/cookies/set",
             [](const HttpRequest &request) -> Task<Response> {
               std::string name = request.getQueryParam("name");
               std::string value = request.getQueryParam("value");
               if (name.empty())
                 name = "test";
               if (value.empty())
                 value = "hello";

               Cookie c;
               c.name = name;
               c.value = value;

               auto path = request.getQueryParam("path");
               if (!path.empty())
                 c.path = path;

               auto domain = request.getQueryParam("domain");
               if (!domain.empty())
                 c.domain = domain;

               auto maxage = request.getQueryParam("maxage");
               if (!maxage.empty())
                 c.maxAge = std::stoi(maxage);

               auto samesite = request.getQueryParam("samesite");
               if (!samesite.empty())
                 c.sameSite = samesite;

               // key-only params arrive with value "true" -- presence is enough
               if (!request.getQueryParam("httponly").empty())
                 c.httpOnly = true;
               if (!request.getQueryParam("secure").empty())
                 c.secure = true;

               json body = {
                   {"name", c.name},         {"value", c.value},
                   {"path", c.path},         {"domain", c.domain},
                   {"maxAge", c.maxAge},     {"sameSite", c.sameSite},
                   {"httpOnly", c.httpOnly}, {"secure", c.secure},
               };

               HttpResponse res(200, body.dump());
               res.setHeader("Content-Type", "application/json");
               res.setCookie(c);
               co_return res;
             });

  // GET /tests/cookies/read
  // Echoes all cookies from the incoming Cookie request header as a JSON
  // object. Use in tandem with /tests/cookies/set: set a cookie in one
  // request, then send it back here to confirm the round-trip.
  //
  // Response: { "<name>": "<value>", ... }
  router.get("/tests/cookies/read",
             [](const HttpRequest &request) -> Task<Response> {
               json j = json::object();
               for (const auto &[name, value] : request.getCookies()) {
                 SPDLOG_DEBUG("{}: {}", name, value);
                 j[name] = value;
               }
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // GET /tests/cookies/delete
  // Instructs the client to remove a named cookie by issuing a Set-Cookie
  // header with Max-Age=0 via removeCookie().
  // Query param: name=<str> (default: "test")
  //
  // Response: { "removed": "<name>" }
  router.get("/tests/cookies/delete",
             [](const HttpRequest &request) -> Task<Response> {
               std::string name = request.getQueryParam("name");
               if (name.empty())
                 name = "test";
               HttpResponse res(200, json{{"removed", name}}.dump());
               res.setHeader("Content-Type", "application/json");
               res.deleteCookie(name);
               co_return res;
             });

  // -- Path parameter routes --------------------------------------------------

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

  // GET /tests/wildcard/* -- single segment
  router.get("/tests/wildcard/*",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"path", request.getPathParam("*")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // GET /tests/deepwildcard/** -- greedy
  router.get("/tests/deepwildcard/**",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"path", request.getPathParam("**")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // -- URL decoding test routes -----------------------------------------------

  // GET /tests/decode/path/<name>
  // Returns the percent-decoded path segment under the key "name".
  // Query and raw-path decode tests use /tests/debug/request instead, which
  // exposes both "query" and "rawPath" fields directly.
  //
  // e.g. /tests/decode/path/John%20Doe  ->  {"name":"John Doe"}
  // e.g. /tests/decode/path/foo%2Fbar   ->  {"name":"foo/bar"}
  // e.g. /tests/decode/path/caf%C3%A9   ->  {"name":"cafe"} (UTF-8 bytes)
  router.get("/tests/decode/path/<name>",
             [](const HttpRequest &request) -> Task<Response> {
               json j = {{"name", request.getPathParam("name")}};
               auto res = HttpResponse(200, j.dump());
               res.setHeader("Content-Type", "application/json");
               co_return res;
             });

  // -- Chunked / streaming response test routes -------------------------------
  // All live under /tests/stream/*

  // GET /tests/stream/basic
  // A handful of small named chunks. Assembled body: "Hello, World!"
  // The canonical "does streaming work at all" check.
  router.get("/tests/stream/basic", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(
        200, [i = 0]() mutable -> Task<std::optional<std::string>> {
          static constexpr std::array chunks = {"Hello", ", ", "World", "!"};
          if (i >= 4)
            co_return std::nullopt;
          co_return std::string(chunks[i++]);
        });
  });

  // GET /tests/stream/single
  // Exactly one chunk, then done. Assembled body: "hello"
  router.get("/tests/stream/single", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(
        200, [done = false]() mutable -> Task<std::optional<std::string>> {
          if (done)
            co_return std::nullopt;
          done = true;
          co_return std::string("hello");
        });
  });

  // GET /tests/stream/empty
  // Returns nullopt on the very first call -- terminal chunk immediately.
  // Assembled body is empty, status still 200.
  router.get("/tests/stream/empty", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(200, []() -> Task<std::optional<std::string>> {
      co_return std::nullopt;
    });
  });

  // GET /tests/stream/count/<n>
  // Streams exactly n chunks: "chunk-1", "chunk-2", ..., "chunk-n".
  // n=0  -> empty body (same as /empty)
  // n<0  -> clamped to 0
  // non-numeric n -> std::stoi throws before the HttpStreamResponse is
  //   constructed, generateResponse() catches it -> plain 500 JSON response.
  router.get("/tests/stream/count/<n>",
             [](const HttpRequest &request) -> Task<Response> {
               int n = std::stoi(request.getPathParam("n"));
               if (n < 0)
                 n = 0;
               co_return HttpStreamResponse(
                   200,
                   [i = 0, n]() mutable -> Task<std::optional<std::string>> {
                     if (i >= n)
                       co_return std::nullopt;
                     co_return "chunk-" + std::to_string(++i);
                   });
             });

  // GET /tests/stream/throw
  // Emits two chunks successfully, then the lambda throws.
  // The error is caught, "Internal Server Error: ..." is appended as a final
  // chunk, the stream is terminated, and the connection closes cleanly.
  // Status is still 200 (headers already sent).
  // Assembled body: "chunk-1chunk-2Internal Server Error: Deliberate stream
  // error"
  router.get("/tests/stream/throw", [](const HttpRequest &) -> Task<Response> {
    co_return HttpStreamResponse(
        200, [i = 0]() mutable -> Task<std::optional<std::string>> {
          if (i == 2)
            throw std::runtime_error("Deliberate stream error");
          co_return "chunk-" + std::to_string(++i);
        });
  });

  // POST /tests/stream/echo
  // Reads the request body and streams it back in 4-byte chunks.
  // Empty body -> empty stream (terminal chunk only).
  // Mirrors Content-Type if provided.
  router.post("/tests/stream/echo",
              [](const HttpRequest &request) -> Task<Response> {
                std::string body = request.getBody();
                std::string ctype = request.getHeader("Content-Type");
                auto res = HttpStreamResponse(
                    200,
                    [offset = size_t(0), body = std::move(body)]() mutable
                        -> Task<std::optional<std::string>> {
                      if (offset >= body.size())
                        co_return std::nullopt;
                      auto len = std::min(size_t(4), body.size() - offset);
                      auto chunk = body.substr(offset, len);
                      offset += len;
                      co_return chunk;
                    });
                if (!ctype.empty())
                  res.setHeader("Content-Type", ctype);
                co_return res;
              });
}
