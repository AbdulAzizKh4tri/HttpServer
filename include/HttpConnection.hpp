#pragma once

#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <variant>

#include "Awaitables.hpp"
#include "ChunkedBodyParser.hpp"
#include "ConnectionIO.hpp"
#include "ErrorFactory.hpp"
#include "ExecutorContext.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"
#include "StreamResults.hpp"
#include "ThreadPoolFullException.hpp"
#include "logUtils.hpp"
#include "utils.hpp"

template <typename Stream> class HttpConnection {
public:
  HttpConnection(std::unique_ptr<Stream> stream, Router &router, ErrorFactory &errorFactory,
                 std::atomic<bool> &shutdown, std::atomic<int> &globalConnectionCount)
      : io_(std::move(stream)), router_(router), errorFactory_(errorFactory), shutdown_(shutdown),
        ConnGuard_(globalConnectionCount) {
    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
  }

  Task<void> run() {
    // This is intentionally one flat coroutine rather than a chain of sub-coroutines
    // (readHeaders(), readBody(), etc.). Keeping everything in one coroutine means
    // one frame allocation for the entire lifetime of the connection (atleast on the happy path).

    resetInactivity();

    //=== TLS Handshake ===
    for (bool done = false; not done;) {
      HandshakeResult result = io_.handshake();
      switch (result) {
      case HandshakeResult::DONE:
      case HandshakeResult::NO_TLS:
        done = true;
        break;
      case HandshakeResult::WANT_READ:
        co_await ReadAwaitable{io_.getFd(), inactivityDeadline_};
        if (tl_timed_out)
          co_return;
        break;
      case HandshakeResult::WANT_WRITE:
        co_await WriteAwaitable{io_.getFd(), inactivityDeadline_};
        if (tl_timed_out)
          co_return;
        break;
      case HandshakeResult::ERROR:
        co_return;
      }
    }

    //=== Per-request loop ===
    for (;;) {
      if (not keepAlive_)
        co_return;

      formationDeadline_ = now() + std::chrono::seconds(ServerConfig::FORMATION_TIMEOUT_S);

      //=== Read headers ===
      {
        size_t headerSize = 0;
        while (headerSize == 0) {
          // Strip leading bare CRLF — HTTP/1.1 allows clients to send \r\n
          // between pipelined requests as a robustness measure (RFC 9112 §2.2)
          while (io_.getReadBufferSize() >= 2 && *(io_.readBufferBegin()) == '\r' &&
                 *(io_.readBufferBegin() + 1) == '\n') {
            io_.eraseFromReadBuffer(2);
          }

          // Search for the end-of-headers marker (\r\n\r\n)
          auto it = std::search(io_.readBufferBegin(), io_.readBufferEnd(), crlf2.begin(), crlf2.end());

          if (it != io_.readBufferEnd()) {
            headerSize = std::distance(io_.readBufferBegin(), it);
            break;
          }

          // Marker not found yet — suspend until the socket is readable or a deadline fires
          auto readResult = co_await io_.read(activeDeadline(), HttpRequest::MAX_HEADER_SIZE);
          if (readResult == ReadResult::DATA) {
            auto timeNow = now();
            resetInactivity(timeNow);
            if (!formationArmed_) {
              formationDeadline_ = timeNow + std::chrono::seconds(ServerConfig::FORMATION_TIMEOUT_S);
              formationArmed_ = true;
            }
          } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
            co_return;
          } else if (it == io_.readBufferEnd() && readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
            co_await sendErrorResponseAndClose(431);
            co_return;
          } else if (readResult == ReadResult::TIMED_OUT) {
            co_await sendErrorResponseAndClose(408);
            co_return;
          }
        }

        //=== Parse + validate headers ===
        if (headerSize > HttpRequest::MAX_HEADER_SIZE) {
          co_await sendErrorResponseAndClose(431);
          co_return;
        }

        std::string_view headerView(reinterpret_cast<const char *>(io_.readBufferData()), headerSize);

        if (not request_.parseRequestHeader(headerView)) {
          SPDLOG_DEBUG("PARSE ERROR... {}",
                       std::string_view(reinterpret_cast<const char *>(io_.readBufferData()), headerSize));
          co_await sendErrorResponseAndClose(400, "Malformed Header");
          co_return;
        }

        if (request_.getVersion() != "HTTP/1.1") {
          SPDLOG_DEBUG("VERSION ERROR... {}", request_.getVersion());
          co_await sendErrorResponseAndClose(505);
          co_return;
        }

        if (request_.getHeaderLower("host") == "") {
          SPDLOG_DEBUG("HOST ERROR... {}", headerView);
          co_await sendErrorResponseAndClose(400, "No Host Header Provided");
          co_return;
        }

        // Consume the header bytes + the \r\n\r\n terminator from the read buffer
        io_.eraseFromReadBuffer(headerSize + 4);
      }

      // If we had data buffered from a previous read (no suspension occurred),
      // the formation timer still needs to be armed
      if (not formationArmed_) {
        formationDeadline_ = now() + std::chrono::seconds(ServerConfig::FORMATION_TIMEOUT_S);
        formationArmed_ = true;
      }

      //=== Expect: 100-continue ===
      // Client is asking permission to send a body — validate the route exists
      // before committing to receiving potentially large data
      auto expect = request_.getHeaderLower("expect");

      if (not expect.empty()) {
        if (expect == "100-continue") {
          RouterResponse result = router_.validate(request_);
          switch (result) {
          case RouterResponse::NOT_FOUND:
            co_await sendErrorResponseAndClose(404);
            co_return;
          case RouterResponse::METHOD_NOT_ALLOWED:
            co_await sendErrorResponseAndClose(405);
            co_return;
          case RouterResponse::OK: {
            std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
            io_.enqueue(std::vector<unsigned char>(response.begin(), response.end()));
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
                co_return;
            }
          }
          }
        } else {
          SPDLOG_ERROR("Expect value not supported: {}", expect);
          co_await sendErrorResponseAndClose(417);
          co_return;
        }
      }

      //=== Read body ===
      {
        bool hasContentLengthHeader = request_.getHeaderLower("content-length") != "";
        auto transferEncodingHeader = request_.getHeaderLower("transfer-encoding");

        if (hasContentLengthHeader && transferEncodingHeader != "") {
          SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
          co_await sendErrorResponseAndClose(400, "Request cannot contain both Content-Length "
                                                  "and Transfer-Encoding headers");
          co_return;
        }

        //=== Body: chunked transfer encoding ===
        if (transferEncodingHeader.find("chunked") != std::string::npos) {
          for (;;) {
            auto result = chunkParser_.feed(io_, request_);

            if (!result)
              switch (result.error()) {
              case ChunkError::TOO_LARGE:
                co_await sendErrorResponseAndClose(413);
                co_return;
              case ChunkError::MALFORMED:
                co_await sendErrorResponseAndClose(400);
                co_return;
              }

            if (result.value().has_value()) {
              request_.setBody(*result.value());
              break;
            }
            // Parser needs more data before it can make progress — suspend
            ReadResult readResult = co_await io_.read(activeDeadline());
            if (readResult == ReadResult::DATA) {
              resetInactivity();
            } else if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
              co_await sendErrorResponseAndClose(413);
              co_return;
            } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
              co_return;
            } else if (readResult == ReadResult::TIMED_OUT) {
              co_await sendErrorResponseAndClose(408);
              co_return;
            }
          }

          //=== Body: content-length ===
        } else if (hasContentLengthHeader) {

          auto contentLengthResult = request_.getContentLength();

          if (!contentLengthResult) {
            switch (contentLengthResult.error()) {
            case ContentLengthError::NO_CONTENT_LENGTH_HEADER:
              std::unreachable();
            case ContentLengthError::INVALID_CONTENT_LENGTH:
              co_await sendErrorResponseAndClose(400, "Invalid Content-Length header");
              co_return;
            case ContentLengthError::CONTENT_LENGTH_TOO_LARGE:
              co_await sendErrorResponseAndClose(413);
              co_return;
            }
          }

          size_t contentLength = contentLengthResult.value();

          // Accumulate until the read buffer holds the full declared body
          while (io_.getReadBufferSize() < contentLength) {
            ReadResult result = co_await io_.read(activeDeadline());
            if (result == ReadResult::DATA) {
              resetInactivity();
            } else if (result == ReadResult::BUFFER_LIMIT_EXCEEDED) {
              co_await sendErrorResponseAndClose(413);
              co_return;
            } else if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
              co_return;
            } else if (result == ReadResult::TIMED_OUT) {
              co_await sendErrorResponseAndClose(408);
              co_return;
            }
          }

          request_.setBody(io_.getReadBufferString(contentLength));
          io_.eraseFromReadBuffer(contentLength);
        }
      }

      // Body is fully read — disarm the formation timer
      formationDeadline_ = std::chrono::steady_clock::time_point::max();

      //=== Dispatch ===
      Response response;

      try {
        response = co_await router_.dispatch(request_);
      } catch (ThreadPoolFullException &e) {
        SPDLOG_ERROR("Thread Pool Full!\n {}", e.what());
        response = buildErrorResponse(503, e.what());
      } catch (const std::exception &e) {
        SPDLOG_ERROR("Handler threw exception: {}", e.what());
        response = buildErrorResponse(500, e.what());
      } catch (...) {
        response = buildErrorResponse(500);
      }

      //=== Set Connection header ===
      std::visit(overloaded{[this](auto &res) {
                   if (shutdown_) {
                     keepAlive_ = false;
                     res.headers.setHeaderLower("connection", "close");
                   } else {
                     keepAlive_ = shouldKeepAlive();
                     if (not keepAlive_)
                       res.headers.setHeaderLower("connection", "close");
                   }
                 }},
                 response);

      //=== Send: plain response ===
      if (HttpResponse *res = std::get_if<HttpResponse>(&response)) {
        if (res->getStatusCode() < 0) {
          SPDLOG_ERROR("Prevented: Trying to send response with negative status code");
          co_return;
        }

        if (not res->serializeInto(io_.getWriteBuffer()))
          co_return;

        logRequest(request_, *res);
        while (io_.hasPendingWrites()) {
          if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
            co_return;
        }
        resetForNextRequest();
        continue;

        //=== Send: streaming response ===
      } else if (HttpStreamResponse *responseStream = std::get_if<HttpStreamResponse>(&response)) {
        // Send headers immediately — chunked body follows as chunks become available
        if (not responseStream->serializeHeaderInto(io_.getWriteBuffer()))
          co_return;

        while (io_.hasPendingWrites()) {
          if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
            co_return;
        }

        std::optional<std::string> chunkOpt = "init";
        bool error = false;
        while (chunkOpt.has_value()) {
          try {
            chunkOpt = co_await responseStream->getNextChunk();
          } catch (const std::exception &e) {
            SPDLOG_ERROR("Stream handler threw exception: {}", e.what());
            if (not HttpStreamResponse::serializeChunkInto("Internal Server Error: " + std::string(e.what()),
                                                           io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }

            if (not HttpStreamResponse::serializeChunkInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            error = true;
          } catch (...) {
            SPDLOG_ERROR("Stream handler threw unknown exception");
            if (not HttpStreamResponse::serializeChunkInto("Internal Server Error", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            if (not HttpStreamResponse::serializeChunkInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            error = true;
          }

          if (error) {
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
                co_return;
            }
            co_return;
          }

          if (not chunkOpt.has_value()) {
            // nullopt returned — send the terminal zero-length chunk to close the stream
            if (not HttpStreamResponse::serializeChunkInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            logRequest(request_, *responseStream);
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
                co_return;
            }
            break;
          } else {
            if (not HttpStreamResponse::serializeChunkInto(*chunkOpt, io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
          }

          while (io_.hasPendingWrites()) {
            if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK) {
              io_.resetConnection();
              co_return;
            }
          }
        }
      }

      resetForNextRequest();
    }
  }

  int getFd() const { return io_.getFd(); }
  std::string getIp() const { return io_.getIp(); }
  uint16_t getPort() const { return io_.getPort(); }

private:
  struct ConnGuard {
    std::atomic<int> &c;
    ConnGuard(std::atomic<int> &c) : c(c) {}
    ~ConnGuard() { c.fetch_sub(1, std::memory_order_relaxed); }
  };

  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

  ConnectionIO<Stream> io_;
  ChunkedBodyParser<Stream> chunkParser_;

  bool keepAlive_ = true;
  std::atomic<bool> &shutdown_;
  ConnGuard ConnGuard_;

  bool formationArmed_ = false;
  std::chrono::steady_clock::time_point inactivityDeadline_ = std::chrono::steady_clock::time_point::max();
  std::chrono::steady_clock::time_point formationDeadline_ = std::chrono::steady_clock::time_point::max();

  void resetInactivity(std::chrono::steady_clock::time_point timeNow = now()) {
    inactivityDeadline_ = timeNow + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
  }

  std::chrono::steady_clock::time_point activeDeadline() const {
    return formationDeadline_ != std::chrono::steady_clock::time_point::max()
               ? std::min(inactivityDeadline_, formationDeadline_)
               : inactivityDeadline_;
  }

  bool shouldKeepAlive() const { return not icontains(request_.getHeaderLower("connection"), "close"); }

  Task<void> sendErrorResponseAndClose(int statusCode, const std::string &message = "") {
    HttpResponse response = buildErrorResponse(statusCode, message);
    keepAlive_ = false;
    response.headers.setHeaderLower("connection", "close");
    if (response.getStatusCode() == -1) {
      SPDLOG_ERROR("Prevented: Trying to send response with status code -1");
      co_return;
    }

    if (not response.serializeInto(io_.getWriteBuffer()))
      co_return;
    logRequest(request_, response);
    do {
      if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != WriteResult::OK)
        co_return;
    } while (io_.hasPendingWrites());
  }

  HttpResponse buildErrorResponse(int statusCode, const std::string &message = "") {
    HttpResponse response = errorFactory_.build(request_, statusCode, message);
    if (request_.getMethod() == "HEAD")
      response.stripBody();
    if (statusCode == 405)
      response.headers.setHeaderLower("allow", router_.getAllowedMethodsString(request_));
    return response;
  }

  void resetForNextRequest() {
    request_.reset(io_.getIp(), io_.getPort());
    chunkParser_.reset();
    formationArmed_ = false;
    formationDeadline_ = std::chrono::steady_clock::time_point::max();
  }
};
