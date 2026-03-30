#include "HttpConnection.hpp"

#include <variant>

#include "Awaitables.hpp"
#include "ExecutorContext.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "logUtils.hpp"
#include "serverConfig.hpp"
#include "utils.hpp"

HttpConnection::HttpConnection(std::shared_ptr<IStream> stream, Router &router, ErrorFactory &errorFactory,
                               std::atomic<bool> &shutdown)
    : io_(std::move(stream)), router_(router), errorFactory_(errorFactory), shutdown_(shutdown) {

  request_.setIp(io_.getIp());
  request_.setPort(io_.getPort());
}

Task<void> HttpConnection::run() {
  resetInactivity();

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

  for (;;) {
    if (not keepAlive_)
      co_return;

    formationDeadline_ = now() + std::chrono::seconds(FORMATION_TIMEOUT_S);

    {
      size_t headerSize = 0;
      while (headerSize == 0) {
        while (io_.getReadBufferSize() >= 2 && *(io_.readBufferBegin()) == '\r' &&
               *(io_.readBufferBegin() + 1) == '\n') {
          io_.eraseFromReadBuffer(2);
        }

        auto it = std::search(io_.readBufferBegin(), io_.readBufferEnd(), crlf2.begin(), crlf2.end());

        if (it != io_.readBufferEnd()) {
          headerSize = std::distance(io_.readBufferBegin(), it);
          break;
        }

        auto readResult = co_await io_.read(activeDeadline(), HttpRequest::MAX_HEADER_SIZE);
        if (readResult == ReadResult::DATA) {
          resetInactivity();
          if (!formationArmed_) {
            formationDeadline_ = now() + std::chrono::seconds(FORMATION_TIMEOUT_S);
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

      io_.eraseFromReadBuffer(headerSize + 4);
    }

    if (not formationArmed_) {
      formationDeadline_ = now() + std::chrono::seconds(FORMATION_TIMEOUT_S);
      formationArmed_ = true;
    }

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
          if (const auto result = co_await io_.write(INACTIVITY_TIMEOUT_S); result != WriteResult::OK)
            co_return;
        }
        }
      } else {
        SPDLOG_ERROR("Expect value not supported: {}", expect);
        co_await sendErrorResponseAndClose(417);
        co_return;
      }
    }

    {
      bool hasContentLengthHeader = request_.getHeaderLower("content-length") != "";
      auto transferEncodingHeader = request_.getHeaderLower("transfer-encoding");

      if (hasContentLengthHeader && transferEncodingHeader != "") {
        SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
        co_await sendErrorResponseAndClose(400, "Request cannot contain both Content-Length "
                                                "and Transfer-Encoding headers");
        co_return;
      }

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
          // parser needs more data — now read
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

    formationDeadline_ = std::chrono::steady_clock::time_point::max();

    Response response;

    try {
      response = co_await router_.dispatch(request_);
    } catch (const std::exception &e) {
      SPDLOG_ERROR("Handler threw exception: {}", e.what());
      response = buildErrorResponse(500, e.what());
    } catch (...) {
      response = buildErrorResponse(500);
    }

    std::visit(overloaded{[this](auto &res) {
                 if (shutdown_) {
                   keepAlive_ = false;
                   res.setHeaderLower("connection", "close");
                 } else {
                   keepAlive_ = shouldKeepAlive();
                   res.setHeaderLower("connection", keepAlive_ ? "keep-alive" : "close");
                 }
               }},
               response);

    if (HttpResponse *res = std::get_if<HttpResponse>(&response)) {
      if (res->getStatusCode() == -1) {
        SPDLOG_ERROR("Prevented: Trying to send response with status code -1");
        co_return;
      }

      res->serializeInto(io_.getWriteBuffer());
      logRequest(request_, *res);
      if (const auto result = co_await io_.write(INACTIVITY_TIMEOUT_S); result != WriteResult::OK)
        co_return;
      resetForNextRequest();
      continue;
    } else if (HttpStreamResponse *responseStream = std::get_if<HttpStreamResponse>(&response)) {
      io_.enqueue(responseStream->getSerializedHeader());

      if (const auto result = co_await io_.write(INACTIVITY_TIMEOUT_S); result != WriteResult::OK)
        co_return;

      std::optional<std::string> chunkOpt = "init";
      bool error = false;
      while (chunkOpt.has_value()) {
        try {
          chunkOpt = co_await responseStream->getNextChunk();
        } catch (const std::exception &e) {
          SPDLOG_ERROR("Stream handler threw exception: {}", e.what());
          io_.enqueue(HttpStreamResponse::serializeChunk("Internal Server Error: " + std::string(e.what())));
          io_.enqueue(HttpStreamResponse::serializeChunk(""));
          error = true;
        } catch (...) {
          SPDLOG_ERROR("Stream handler threw unknown exception");
          io_.enqueue(HttpStreamResponse::serializeChunk("Internal Server Error"));
          io_.enqueue(HttpStreamResponse::serializeChunk(""));
          error = true;
        }

        if (error) {
          co_await io_.write(INACTIVITY_TIMEOUT_S);
          co_return;
        }

        std::string chunk = chunkOpt.value_or("");

        if (chunk.empty()) {
          io_.enqueue(HttpStreamResponse::serializeChunk(""));
          logRequest(request_, *responseStream);
          if (const auto result = co_await io_.write(INACTIVITY_TIMEOUT_S); result != WriteResult::OK)
            co_return;
          resetForNextRequest();
          continue;
        } else {
          io_.enqueue(HttpStreamResponse::serializeChunk(chunk));
        }

        if (const auto result = co_await io_.write(INACTIVITY_TIMEOUT_S); result != WriteResult::OK)
          co_return;
      }
    }

    resetForNextRequest();
  }
}

int HttpConnection::getFd() const { return io_.getFd(); }
std::string HttpConnection::getIp() const { return io_.getIp(); }
uint16_t HttpConnection::getPort() const { return io_.getPort(); }

void HttpConnection::resetInactivity() { inactivityDeadline_ = now() + std::chrono::seconds(INACTIVITY_TIMEOUT_S); }

std::chrono::steady_clock::time_point HttpConnection::activeDeadline() const {
  return formationDeadline_ != std::chrono::steady_clock::time_point::max()
             ? std::min(inactivityDeadline_, formationDeadline_)
             : inactivityDeadline_;
}

bool HttpConnection::shouldKeepAlive() const {
  const std::string &connection = toLowerCase(request_.getHeaderLower("connection"));
  return connection.find("close") == std::string::npos;
}

Task<void> HttpConnection::sendErrorResponseAndClose(int statusCode, const std::string &message) {
  HttpResponse response = buildErrorResponse(statusCode, message);
  keepAlive_ = false;
  response.setHeaderLower("connection", "close");
  if (response.getStatusCode() == -1) {
    SPDLOG_ERROR("Prevented: Trying to send response with status code -1");
    co_return;
  }

  response.serializeInto(io_.getWriteBuffer());
  logRequest(request_, response);
  co_await io_.write(INACTIVITY_TIMEOUT_S);
}

HttpResponse HttpConnection::buildErrorResponse(int statusCode, const std::string &message) {
  HttpResponse response = errorFactory_.build(request_, statusCode, message);
  if (request_.getMethod() == "HEAD")
    response.stripBody();
  if (statusCode == 405)
    response.setHeaderLower("allow", router_.getAllowedMethodsString(request_));
  return response;
}

void HttpConnection::resetForNextRequest() {
  request_.reset(io_.getIp(), io_.getPort());
  chunkParser_.reset();
  formationArmed_ = false;
  formationDeadline_ = std::chrono::steady_clock::time_point::max();
}
