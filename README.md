# RuKh

![RuKh logo, A RuKh(or Roc) the mythical bird](./logo.png)

A high-performance HTTP/1.1 server framework built entirely from scratch in C++23 — no async libraries, no HTTP frameworks. The event loop, coroutine scheduler, connection lifecycle, and request/response pipeline are all hand-rolled.

This is an ongoing solo learning project, built with production-quality goals in mind.

> **Note:** Benchmarks reflect a minimal ping-pong workload. RuKh is still in active development and may not yet handle every edge case that mature frameworks do. Results are shared transparently, not as a final claim.

---

## Table of Contents

- [Features](#features)
- [Build](#build)
- [Quick Start](#quick-start)
- [API](#api)
- [Architecture](#architecture)
- [Benchmarks](#benchmarks)
- [Roadmap](#roadmap)

---

## Features

- HTTP/1.1 with keep-alive
- TLS via OpenSSL
- Chunked transfer encoding (request + response)
- Streaming responses
- Express-style middleware chain
- Trie-based router with path parameters, wildcards (`*`), and deep wildcards (`**`)
- Cookie and session management
- Static file serving with async file I/O via io_uring
- Content negotiation for error responses
- Edge-triggered epoll with SO_REUSEPORT multi-threading
- C++20 coroutines throughout — one heap allocation per connection lifetime

---

## Build

### Dependencies

- Linux (epoll + io_uring required)
- C++23 compiler (GCC 13+ or Clang 17+)
- CMake 3.28+
- OpenSSL
- [spdlog](https://github.com/gabime/spdlog) + [nlohmann/json](https://github.com/nlohmann/json) — vendored under `external/`
- [liburing](https://github.com/axboe/liburing) — built automatically via CMake `ExternalProject`

### Steps

```bash
# Debug
cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j$(nproc)
./build/debug/server

# Release
cmake -B build/release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j$(nproc)
./build/release/server
```

Or use the Makefile shortcuts:

```bash
make configure BUILD_TYPE=debug
make configure BUILD_TYPE=release
make debug     # build + run debug
make release   # build + run release
```

### TLS

Generate a self-signed certificate for local development:

```bash
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
```

Paths are configured in `config.hpp`.

---

## Quick Start

```cpp
// server.cpp
#include "HttpServer.hpp"
#include "Router.hpp"
#include "ErrorFactory.hpp"

int main() {
    ErrorFactory errorFactory;
    Router router(errorFactory);

    router.get("/hello", [](const HttpRequest& req) -> Task<Response> {
        co_return HttpResponse(200, "Hello, World!");
    });

    HttpServer server(errorFactory);
    server.setRouter(router);
    server.addListener("localhost", "8080");
    server.run(4); // 4 worker threads
}
```

---

## API

### Routing

```cpp
router.get("/path", handler);
router.post("/path", handler);
router.put("/path", handler);
router.patch("/path", handler);
router.delete_("/path", handler); //I know I know, I didn't think about the delete keyword
```

Handlers have the signature:

```cpp
[](const HttpRequest& req) -> Task<Response> {
    co_return HttpResponse(200, "body");
}
```

`Response` is `std::variant<HttpResponse, HttpStreamResponse>`.

#### Path Parameters

```cpp
router.get("/users/<id>", [](const HttpRequest& req) -> Task<Response> {
    auto id = req.getPathParam("id");
    co_return HttpResponse(200, "User: " + id);
});
```

#### Wildcards

```cpp
router.get("/files/*",  handler); // matches one segment
router.get("/files/**", handler); // matches any depth
```

#### Query Parameters

```cpp
auto name = req.getQueryParam("name");          // single value
auto tags = req.getQueryParams("tag");          // multi-value
auto all  = req.getAllQueryParams();            // vector of pairs
```

#### Request Body

```cpp
const std::string& body = req.getBody();        // raw body string
```

#### Response

```cpp
HttpResponse res(200, "body text");
res.headers.setHeaderLower("content-type", "text/plain");
res.cookies.setCookie(Cookie("name", "value"));
co_return res;
```

#### Streaming Response

```cpp
co_return HttpStreamResponse(200, [i = 0]() mutable -> Task<std::optional<std::string>> {
    if (i >= 3) co_return std::nullopt;   // signals end of stream
    co_return "chunk-" + std::to_string(++i);
});
```

#### Middleware

```cpp
router.use([](HttpRequest& req, Next next) -> Task<Response> {
    // pre-processing
    auto res = co_await next();
    // post-processing
    co_return res;
});
```

Middleware runs in registration order. `next()` advances to the next middleware or the terminal handler.

---

## Architecture

### Runtime

Each worker thread owns an `Executor` — an event loop built on:

- **Edge-triggered epoll** (`EPOLLIN | EPOLLOUT | EPOLLET`) — fds registered once at accept time.
- **C++20 coroutines** — every connection is a single coroutine; one heap allocation covers the entire connection lifetime on the happy path
- **io_uring** — async file reads/writes for static file serving, submitted in batches and drained each loop iteration
- **SO_REUSEPORT** — each thread binds its own listener socket independently; the kernel distributes incoming connections

### Connection Lifecycle

```
accept4()
   │
   ▼
TLS handshake (if applicable)
   │
   ▼
┌─────────────────────────────┐
│  Per-request loop           │
│                             │
│  read headers               │
│  ──► parse + validate       │
│  read body                  │
│  ──► content-length         │
│  ──► chunked                │
│                             │
│  router.dispatch()          │
│  ──► middleware chain       │
│  ──► handler                │
│                             │
│  send response              │
│  ──► HttpResponse           │
│  ──► HttpStreamResponse     │
│       (chunked encoding)    │
└─────────────────────────────┘
   │
   ▼
resetForNextRequest()  ──►  keep-alive loop
```

### File Layout
##### (Subject to change)

```
include/        — all headers (framework is header + .cpp pairs)
src/            — implementations
server.cpp      — entry point, wires everything together
routes.cpp      — route definitions
ServerConfig.hpp — tuning constants (timeouts, limits, thread count)
external/       — vendored headers (spdlog, nlohmann/json)
```

---

## Benchmarks

All benchmarks run on a single machine with server and load generator pinned to **separate physical cores** via `taskset`. Release build, logging disabled.

Tool: [`wrk`](https://github.com/wg/wrk)

Workload: minimal `GET /ping → "pong"` — no database, no business logic.

> These numbers reflect the best-case scenario for all frameworks tested. Real-world workloads will be lower across the board.

### Single-Threaded (1 server thread, `taskset -c 6,8 wrk -t2 -c20 -d10s`)

| Framework | Language | req/s |
|---|---|---|
| Drogon | C++ | ~95,000 |
| **RuKh** | **C++** | **~90,000** |
| Crow | C++ | ~80,000 |
| Go net/http | Go | ~63,000 |
| Express | Node.js | ~19,000 |

RuKh reaches ~95% of Drogon's single-threaded throughput.

### Multi-Threaded (3 server threads, `taskset -c 6,8,10 wrk -t3 -c30 -d10s`)

| Framework | Language | req/s |
|---|---|---|
| Drogon | C++ | ~270,000 |
| **RuKh** | **C++** | **~260,000** |
| Go net/http | Go | ~175,000 |
| Crow | C++ | ~161,000 |

RuKh scales near-linearly with thread count (~2.9× from 1→3 threads).

### Methodology Notes

- Server and `wrk` pinned to separate physical cores — no shared cache interference
- Each result is the average of two consecutive runs; variance was low across all tests
- Drogon's response payload is slightly larger per-request (more headers) — it still edges ahead, meaning the gap is real
- Express tested single-process (standard deployment model for a single instance)
- Go tested with `GOMAXPROCS(1)` for the single-threaded comparison; Go's runtime still uses background goroutines

---

## Roadmap

The project is under active development. Planned and in-progress work, roughly in priority order:

- [ ] **ETag + Last-Modified** — conditional request support for static file serving
- [ ] **Thread pool + eventfd bridge** — offload blocking work without blocking the event loop
- [ ] **Content encoding** — gzip / brotli response compression
- [ ] **WebSocket** — upgrade path from HTTP/1.1
- [ ] **Server-Sent Events (SSE)** — largely free given existing streaming infrastructure

#### Known Issues 
- [ ] *Radix tree router* — fix known gap where a literal match shadows a param child even if the literal leads to a dead end

---

*Project is ongoing.*
