# How to Read the RuKh Codebase

#### *AI GENERATED, So if you legit want to use this, feel free to contact me. I will do my own proper documentation... eventually*

A complete guide to understanding the HTTP/1.1 server framework built from scratch in C++23. This guide is intended for **new contributors** who want to understand how the system works end-to-end.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Directory Structure](#directory-structure)
3. [Reading Order](#reading-order)
4. [Core Concepts](#core-concepts)
5. [Module Breakdown](#module-breakdown)
6. [Data Flow](#data-flow)
7. [Key Design Patterns](#key-design-patterns)

---

## Project Overview

RuKh is a high-performance HTTP/1.1 server **built entirely from scratch**—no async libraries, no HTTP frameworks. Key characteristics:

- **C++23** with hand-rolled event loops, coroutine schedulers, and connection lifecycle management
- **Zero-copy philosophy**: One heap allocation per connection lifetime (happy path)
- **Coroutines everywhere**: Every handler and I/O operation returns `Task<T>` (C++20 coroutines)
- **Linux-only**: Uses edge-triggered epoll and io_uring for async file I/O
- **Production-ready logging**, error handling, middleware chain, and routing

The project is split into two parts:
- **`rukh/`** — The framework library (core HTTP server engine)
- **`app/`** — An example application using the framework (your handlers, routes, middleware)

---

## Directory Structure

```
HttpServer/
├── rukh/                          # The framework library
│   ├── include/rukh/              # Public headers
│   │   ├── core/                  # Low-level event loop, I/O, sockets
│   │   ├── middleware/            # Middleware implementations
│   │   ├── HttpServer.hpp         # Main server class
│   │   ├── Router.hpp             # Trie-based routing
│   │   ├── HttpRequest.hpp        # Request object
│   │   ├── HttpResponse.hpp       # Response object
│   │   ├── Task.hpp               # Coroutine promise type
│   │   ├── ServerConfig.hpp       # Configuration constants
│   │   └── ... (other utilities)
│   └── src/                       # Implementations
│       ├── core/                  # Event loop, sockets, TLS, compression
│       ├── middleware/            # Middleware implementations
│       └── ... (implementation files matching headers)
│
├── app/                           # Example application
│   ├── main.cpp                   # Entry point
│   ├── include/
│   │   ├── routes.hpp             # Route registration signature
│   │   ├── middlewares.hpp        # Middleware setup
│   │   └── errors.hpp             # Error formatter registration
│   ├── src/
│   │   └── routes.cpp             # Application route handlers
│   ├── public/                    # Static files served by StaticMiddleware
│   ├── cert.pem, key.pem          # Self-signed TLS certificates
│   └── CMakeLists.txt
│
└── CMakeLists.txt                 # Build configuration
```

---

## Reading Order

### Phase 1: Understand the Entry Point & Setup (15 min)

**Start here:**

1. **`app/main.cpp`** (53 lines)
   - Shows how to instantiate `HttpServer`, set up middleware, register routes, and run the server
   - Gives you the "big picture" of how the framework is used
   - Key calls: `registerMiddlewares()`, `registerRoutes()`, `server.run(N)`

2. **`rukh/include/rukh/ServerConfig.hpp`** (62 lines)
   - Configuration constants: timeouts, size limits, io_uring ring size, compression thresholds
   - Understand the tuning knobs before diving into implementation

3. **`app/include/routes.hpp`** (5 lines)
   - Just shows the `registerRoutes()` function signature
   - Understand what the app provides to the framework

---

### Phase 2: Core Data Structures (30 min)

These are the main objects flowing through the system:

1. **`rukh/include/rukh/HttpRequest.hpp`** (150 lines approx)
   - Immutable request object passed to handlers
   - Contains: method, path, query params, headers, cookies, body
   - Key methods: `getQuery()`, `getHeader()`, `getCookie()`, `getBody()`

2. **`rukh/include/rukh/HttpResponse.hpp`** (150 lines approx)
   - Mutable response object built by handlers
   - Contains: status code, headers, cookies, body
   - Key: All headers use `setHeaderLower()` (lowercase keys for HTTP/2 compatibility)

3. **`rukh/include/rukh/HttpStreamResponse.hpp`** (50 lines)
   - For streaming responses—pass a generator coroutine returning `Task<std::optional<std::string>>`
   - Used for large files or real-time data

4. **`rukh/include/rukh/Task.hpp`** (100 lines approx)
   - Custom coroutine promise type for C++20 coroutines
   - Every async function returns `Task<T>`
   - Shows how `co_await` and `co_return` work with the event loop

---

### Phase 3: Routing & Request Dispatch (20 min)

1. **`rukh/include/rukh/Router.hpp`** (80 lines approx)
   - Trie-based router with path parameters (`/users/<id>`), wildcards (`*`), deep wildcards (`**`)
   - Key methods: `registerRoute()`, `match(path)`, `use()` for middleware

2. **`rukh/src/Router.cpp`** (250 lines approx)
   - Implementation of trie structure and path matching
   - Understand how `GET /users/<id>` extracts `id` and makes it available in the request

3. **`app/src/routes.cpp`** (24 KB)
   - Real route handlers showing how to use the framework
   - Examples: JSON responses, file uploads, streaming, error handling
   - **This is where you see the framework in action—study these handlers**

---

### Phase 4: Middleware Chain (20 min)

The middleware system is **registration-ordered**, running in sequence:

1. **`rukh/include/rukh/middleware/`** (5 middleware files)
   - Middleware signature: `[](HttpRequest& req, Next next) -> Task<Response>`
   - `next` is a callable that continues the chain

2. **Key middleware in order:**
   - **CorsMiddleware** — CORS headers for all routes
   - **StaticMiddleware** — Short-circuits for files in `./public`
   - **CompressionMiddleware** — gzip/brotli response compression
   - **CacheControlMiddleware** — Cache-Control headers by route or MIME type
   - **SessionMiddleware** — Cookie-based session management

3. **`app/include/middlewares.hpp`** (80 lines approx)
   - Shows how to instantiate and register the middleware chain
   - This is your pattern for adding custom middleware

---

### Phase 5: The Event Loop & Low-Level I/O (45 min)

**This is where the "hand-rolled" magic happens:**

1. **`rukh/include/rukh/core/Executor.hpp`** (60 lines)
   - Event loop for one worker thread
   - Key: `tl_executor` global pointer available to all coroutines
   - Methods: `schedule()` (add work), `run()` (main loop), `drain()` (finish pending work)

2. **`rukh/src/core/Executor.cpp`** (200 lines approx)
   - Implementation: edge-triggered epoll, coroutine scheduling
   - Understand how the loop wakes on file descriptor readiness

3. **`rukh/include/rukh/core/EpollInstance.hpp`** (40 lines)
   - Wrapper around Linux epoll
   - Edge-triggered with `SO_REUSEPORT` for multi-threaded listeners

4. **`rukh/include/rukh/core/ConnectionIO.hpp`** (200 lines approx)
   - Low-level socket read/write for a single connection
   - Handles buffering, partial writes, and readiness events

---

### Phase 6: Connection Lifecycle (60 min)

**The most complex part—understand this for everything to click:**

1. **`rukh/include/rukh/core/HttpConnection.hpp`** (500+ lines)
   - **THE CORE LOOP**: One coroutine per connection (`HttpConnection::run()`)
   - Lifecycle: TLS handshake → read headers → parse → read body → dispatch handler → send response → keep-alive loop
   - **Key insight**: Uses `resetForNextRequest()` for HTTP keep-alive without re-allocation
   - Timeout handling via timerfd for inactivity and request formation

2. **`rukh/src/core/HttpConnection.cpp`** (600+ lines)
   - Full implementation including:
     - Header parsing (state machine)
     - Chunked transfer encoding (for request bodies)
     - Response streaming
     - TLS integration
   - **Study the state machine** to understand how the parser works

---

### Phase 7: HTTP Server & Threading (15 min)

1. **`rukh/include/rukh/HttpServer.hpp`** (60 lines)
   - Main server class: creates listeners and worker threads

2. **`rukh/src/HttpServer.cpp`** (200 lines approx)
   - `run(N)` spawns N worker threads
   - Each thread: binds a listener socket (via `SO_REUSEPORT`), accepts connections, creates `HttpConnection` coroutines
   - Graceful shutdown handling

---

### Phase 8: I/O Utilities & File Operations (20 min)

1. **`rukh/include/rukh/core/IoUringInstance.hpp`** (50 lines)
   - Linux io_uring integration for async file I/O

2. **`rukh/include/rukh/AsyncFileReader.hpp`** (70 lines)
   - Used by StaticMiddleware to read files asynchronously
   - Methods: `open()`, `readAll()`, `readChunk()`

3. **`rukh/include/rukh/core/GzipCompressor.hpp`** & **`BrotliCompressor.hpp`**
   - Compression implementations (called by CompressionMiddleware)

---

### Phase 9: Error Handling & Content Negotiation (10 min)

1. **`rukh/include/rukh/ErrorFactory.hpp`** (50 lines)
   - Content-negotiated error responses
   - Registered formatters: JSON, HTML, plain text
   - Inspects `Accept` header to pick format

2. **`app/include/errors.hpp`** (80 lines approx)
   - Shows how to register custom error formatters
   - Pattern for returning errors from handlers

---

### Phase 10: Utilities & Support (10 min)

Skim these to understand available utilities:

- **`rukh/include/rukh/HeaderStore.hpp`** — Case-insensitive header map
- **`rukh/include/rukh/CookieStore.hpp`** — Cookie parsing and generation
- **`rukh/include/rukh/MultipartParser.hpp`** — Multipart form data parsing (file uploads)
- **`rukh/include/rukh/ThreadPool.hpp`** — Thread pool for offloading CPU-heavy work

---

## Core Concepts

### 1. **Coroutines Everywhere**

Every async operation returns `Task<T>`:

```cpp
Task<Response> handler(const HttpRequest& req) {
    // Can use co_await for I/O operations
    auto file = co_await asyncFileRead("path");
    co_return HttpResponse(200, file);
}
```

- `Task<T>` is a promise type that integrates with the `Executor` event loop
- **Key**: No nested coroutine chains—`HttpConnection::run()` is one flat coroutine that handles the entire connection

### 2. **Event Loop with Executor**

Each worker thread has an `Executor` (event loop):

```cpp
// Global per-thread pointer
extern thread_local Executor* tl_executor;

// Schedule work
tl_executor->schedule(std::move(task));

// Main loop drains task queue on each epoll wake
```

- Edge-triggered epoll: one epoll event per state change (not per readable byte)
- Coroutines scheduled on the executor are resumed when file descriptors become ready

### 3. **Connection State Machine**

One coroutine per connection (`HttpConnection::run()`) manages:

```
TLS Handshake (if HTTPS)
    ↓
Read Headers (with timeout)
    ↓
Parse Headers & Extract Body Size
    ↓
Read Body (chunked or content-length)
    ↓
Dispatch to Router → Handler
    ↓
Send Response
    ↓
Decide: Keep-Alive or Close
    ↓
Reset for Next Request (if keep-alive)
```

- Timeout handling: timerfd triggers if a request takes too long to form
- Keep-alive: `resetForNextRequest()` resets buffers in-place (no re-allocation)

### 4. **Middleware Chain**

Middleware runs in **registration order**. Each middleware can:
- Modify the request (e.g., parse cookies)
- Short-circuit (return early without calling `next`)
- Modify the response (e.g., add headers)

```cpp
router.use([](HttpRequest& req, Next next) -> Task<Response> {
    // Before
    auto resp = co_await next();
    // After (can modify response)
    return resp;
});
```

### 5. **Async File I/O via io_uring**

Large files or static assets are read via io_uring (not epoll):

```cpp
auto file = co_await AsyncFileReader::open("path");
auto chunk = co_await file.readChunk(4096);
```

- io_uring operations are submitted in batches and drained each event loop iteration
- Prevents blocking the epoll thread with disk I/O

### 6. **Threading Model**

- **`SO_REUSEPORT`**: Each worker thread binds the same port independently
- **Kernel distributes** connections across threads (no centralized queue)
- **Thread-local context**: `tl_executor` available globally
- **Graceful shutdown**: Set `HttpServer::shutdown_` flag; threads drain queued work

---

## Module Breakdown

### `rukh/include/rukh/` — Public API

| File | Purpose | Lines |
|------|---------|-------|
| `HttpServer.hpp` | Main server class | 60 |
| `Router.hpp` | Trie-based routing | 80 |
| `HttpRequest.hpp` | Request object | 150 |
| `HttpResponse.hpp` | Response object | 150 |
| `Task.hpp` | Coroutine promise type | 100 |
| `ServerConfig.hpp` | Configuration constants | 62 |
| `HeaderStore.hpp` | Case-insensitive headers | 100 |
| `CookieStore.hpp` | Cookie management | 100 |
| `MultipartParser.hpp` | Multipart form parser | 400 |
| `AsyncFileReader.hpp` | Async file I/O | 70 |
| `ErrorFactory.hpp` | Content-negotiated errors | 50 |

### `rukh/include/rukh/core/` — Event Loop & I/O

| File | Purpose | Lines |
|------|---------|-------|
| `Executor.hpp` | Event loop (one per thread) | 60 |
| `HttpConnection.hpp` | Connection state machine | 500+ |
| `ConnectionIO.hpp` | Socket read/write | 200 |
| `EpollInstance.hpp` | epoll wrapper | 40 |
| `IoUringInstance.hpp` | io_uring wrapper | 50 |
| `TcpStream.hpp` | TCP socket abstraction | 80 |
| `TlsStream.hpp` | TLS socket abstraction | 100 |

### `rukh/include/rukh/middleware/` — Built-in Middleware

| Middleware | Purpose |
|---|---|
| `CorsMiddleware` | CORS headers |
| `StaticMiddleware` | Serve files from `./public` |
| `CompressionMiddleware` | gzip/brotli compression |
| `CacheControlMiddleware` | Cache headers by route/MIME |
| `SessionMiddleware` | Cookie-based sessions |

### `app/` — Example Application

| File | Purpose |
|---|---|
| `main.cpp` | Entry point |
| `src/routes.cpp` | Route handlers (JSON, uploads, streaming, etc.) |
| `include/routes.hpp` | `registerRoutes()` signature |
| `include/middlewares.hpp` | Middleware setup |
| `include/errors.hpp` | Error formatters |

---

## Data Flow

### A Request Arrives: Step-by-Step

```
1. Worker Thread (Executor running)
   └─ Listener socket readable
   
2. Accept Connection → Create HttpConnection coroutine
   └─ Schedule on Executor
   
3. HttpConnection::run() starts:
   a) TLS handshake (if HTTPS)
   b) Read headers into buffer
   c) Parse headers (extract method, path, Content-Length, etc.)
   d) Read body (if Content-Length or chunked)
   e) Dispatch to Router
   
4. Router Match:
   a) Find matching route via trie
   b) Extract path parameters
   
5. Middleware Chain (in order):
   a) CorsMiddleware → CacheControlMiddleware → ...
   b) Each middleware calls next()
   
6. Handler Execution:
   a) Handler returns Task<Response>
   b) Can use co_await for I/O
   c) Receives request, returns response
   
7. Response Sent:
   a) Write status line + headers to socket
   b) Write body (or stream chunks)
   c) Compress if configured
   
8. Keep-Alive Decision:
   a) Check Connection header
   b) If keep-alive: resetForNextRequest()
   c) Loop back to step 3b
   
9. Connection Closes:
   a) Socket removed from epoll
   b) HttpConnection destructor cleans up
```

---

## Key Design Patterns

### Pattern 1: Coroutine Task

Every async operation:

```cpp
Task<int> asyncRead() {
    auto data = co_await socket.read();
    co_return data.size();
}

// Called:
int bytes = co_await asyncRead();
```

- `Task<T>` is a coroutine promise type
- Integrated with `Executor` scheduling

### Pattern 2: Middleware Chain

```cpp
router.use(middleware1);  // Runs first
router.use(middleware2);  // Runs second
// Handler runs last
```

- Each middleware calls `co_await next()` to continue
- Can short-circuit by not calling `next()`

### Pattern 3: Handler Signature

```cpp
// Regular response
[](const HttpRequest& req) -> Task<Response> {
    co_return HttpResponse(200, "body");
}

// Streaming response
[](const HttpRequest& req) -> Task<Response> {
    auto gen = [](const HttpRequest& req) -> Task<std::optional<std::string>> {
        co_yield "chunk1";
        co_yield "chunk2";
        co_return std::nullopt;
    };
    co_return HttpStreamResponse(200, gen);
}
```

### Pattern 4: Error Handling

```cpp
// Content-negotiated error (respects Accept header)
co_return errorFactory.build(request, 400);

// Or manually:
auto resp = HttpResponse(400, errorJson);
resp.headers.setHeaderLower("content-type", "application/json");
co_return resp;
```

### Pattern 5: Async File I/O

```cpp
auto file = co_await AsyncFileReader::open("path/to/file");
if (!file) co_return errorFactory.build(request, 404);

auto content = co_await file->readAll();
co_return HttpResponse(200, content);
```

### Pattern 6: ThreadPool for Blocking Work

```cpp
// In registerRoutes:
// ThreadPool* pool is passed in

auto result = co_await PoolTask{
    [&]() { return cpu_expensive_work(); },
    pool
};
```

---

## Quick Navigation

**Want to understand something specific?**

| Topic | Start With |
|-------|-----------|
| How routes work | `Router.hpp` → `Router.cpp` → `app/src/routes.cpp` |
| How a request is parsed | `HttpConnection.hpp` (header parsing section) |
| How middleware runs | `app/include/middlewares.hpp` |
| How static files are served | `StaticMiddleware.hpp/cpp` |
| How responses are compressed | `CompressionMiddleware.hpp/cpp` |
| How the event loop works | `Executor.hpp/cpp` → `ConnectionIO.hpp` |
| How TLS is set up | `TlsStream.hpp/cpp` |
| How sessions work | `SessionMiddleware.hpp/cpp` |
| How errors are formatted | `ErrorFactory.hpp/cpp` → `app/include/errors.hpp` |
| How file uploads work | `MultipartParser.hpp` → `app/src/routes.cpp` (file upload route) |

---

## Conventions & Key Files

### Configuration

- **All tuning constants** live in `ServerConfig.hpp` (timeouts, limits, etc.)
- Set server name: `ServerConfig::setServerName("my-server/1.0")`

### Naming Conventions

- **Classes**: PascalCase (`HttpServer`, `Router`)
- **Member variables**: snake_case with trailing `_` (`executor_`, `routes_`)
- **Functions**: camelCase (`registerRoutes`, `configureLog`)
- **Enum values**: UPPERCASE (`HTTP_OK`, `INACTIVITY_TIMEOUT_S`)

### Include Paths

- Project headers: `#include "Header.hpp"` (no subdirectories)
- All project headers in flat `include/rukh/` directory
- Heavy third-party (json, spdlog, OpenSSL) via precompiled headers

### Response Construction

```cpp
// Regular response
HttpResponse(200, "body")

// With headers
HttpResponse resp(200, "body");
resp.headers.setHeaderLower("content-type", "application/json");

// With cookies
resp.cookies.setCookie(Cookie("name", "value"));

// Streaming
HttpStreamResponse(200, generatorCoroutine)
```

### Error Responses

```cpp
// Content-negotiated (respects Accept header)
co_return errorFactory.build(request, 400);

// Or custom format
auto resp = HttpResponse(404, errorJson);
resp.headers.setHeaderLower("content-type", "application/json");
co_return resp;
```

---

## Building & Testing

```bash
# Build debug
cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j$(nproc)

# Run
./build/debug/server
# Prompts for logging preference and thread count

# Or use Makefile
make debug

# Test with curl
curl -X GET http://localhost:8080/
curl -X POST -d '{"key":"value"}' http://localhost:8080/api/endpoint
```

---

## Next Steps

1. **Clone and build** (`make debug`)
2. **Read `app/main.cpp`** to see framework usage
3. **Study `app/src/routes.cpp`** to see handlers in action
4. **Trace a request**: Pick a route, follow it through routing → middleware → handler
5. **Run locally** and test with `curl`
6. **Modify a route** to understand the patterns
7. **Deep dive**: Pick a middleware or core component that interests you

---

## Common Questions

**Q: Where does my handler code go?**  
A: `app/src/routes.cpp` — use the `registerRoutes()` function to add your routes.

**Q: How do I add custom middleware?**  
A: Define it in `app/include/middlewares.hpp` and register it in the middleware setup.

**Q: Why is `HttpConnection::run()` not broken into sub-coroutines?**  
A: To ensure one heap allocation per connection lifetime. The entire lifecycle is one flat coroutine.

**Q: How are threads distributed?**  
A: Each thread binds the same port via `SO_REUSEPORT`. The kernel distributes new connections.

**Q: Can I use custom async libraries?**  
A: Not directly—but you can offload blocking work to the `ThreadPool` to avoid stalling the event loop.

**Q: What about HTTP/2 or HTTP/3?**  
A: Currently HTTP/1.1 only. Future roadmap items.

---

## Summary

To understand RuKh:

1. **Start**: `app/main.cpp` (how to use it)
2. **Core concepts**: `Task`, `HttpRequest`, `HttpResponse`
3. **Routing**: `Router.hpp` and `app/src/routes.cpp`
4. **Middleware**: Read one middleware implementation
5. **The magic**: `HttpConnection.hpp` (state machine) and `Executor.hpp` (event loop)
6. **Study the patterns**: Look at actual routes in `app/src/routes.cpp`

Good luck exploring! 🚀
