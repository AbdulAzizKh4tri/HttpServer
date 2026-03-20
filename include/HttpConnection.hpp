#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include "ChunkedBodyParser.hpp"
#include "ConnectionIO.hpp"
#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "HttpTypes.hpp"
#include "IStream.hpp"
#include "Router.hpp"
#include "logUtils.hpp"
#include "utils.hpp"

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router,
                 ErrorFactory &errorFactory)
      : io_(std::move(stream)), router_(router), errorFactory_(errorFactory) {

    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
  }

  HttpConnection(const HttpConnection &) = delete;
  HttpConnection &operator=(const HttpConnection &) = delete;

  Task<void> run() {
    if (!co_await handshake())
      co_return;

    for (;;) {
      if (not keepAlive_)
        co_return;

      if (not co_await readHeaders())
        co_return;

      if (request_.getHeader("Expect") != "") {
        if (not co_await handleExpectHeader())
          co_return;
      }

      if (not co_await readBody())
        co_return;

      if (not co_await generateAndSendResponse())
        co_return;

      resetForNextRequest();
    }
  }

  Task<void> timeOut() { co_await sendErrorResponseAndClose(408); }

  int getFd() const { return io_.getFd(); }
  std::string getIp() const { return io_.getIp(); }
  uint16_t getPort() const { return io_.getPort(); }

private:
  bool keepAlive_ = true;

  ConnectionIO io_;
  ChunkedBodyParser chunkParser_;

  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

  bool shouldKeepAlive() const {
    const std::string &connection =
        toLowerCase(request_.getHeader("Connection"));
    return connection.find("close") == std::string::npos;
  }

  Task<bool> handshake() {
    for (;;) {
      HandshakeResult result = io_.handshake();
      switch (result) {
      case HandshakeResult::DONE:
      case HandshakeResult::NO_TLS:
        co_return true;
      case HandshakeResult::WANT_READ:
        co_await ReadAwaitable{io_.getFd()};
        break;
      case HandshakeResult::WANT_WRITE:
        co_await WriteAwaitable{io_.getFd()};
        break;
      case HandshakeResult::ERROR:
        co_return false;
      }
    }
  }

  Task<bool> readHeaders() {
    size_t headerSize = 0;
    while (headerSize == 0) {
      while (io_.getReadBufferSize() >= 2 && *(io_.readBufferBegin()) == '\r' &&
             *(io_.readBufferBegin() + 1) == '\n') {
        io_.eraseFromReadBuffer(2);
      }

      auto it = std::search(io_.readBufferBegin(), io_.readBufferEnd(),
                            crlf2.begin(), crlf2.end());

      if (it != io_.readBufferEnd()) {
        headerSize = std::distance(io_.readBufferBegin(), it);
        break;
      }

      auto readResult = co_await io_.read();

      if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR)
        co_return false;

      if (it == io_.readBufferEnd() &&
          readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
        co_await sendErrorResponseAndClose(431);
        co_return false;
      }
    }

    if (headerSize > HttpRequest::MAX_HEADER_SIZE) {
      co_await sendErrorResponseAndClose(431);
      co_return false;
    }

    std::string_view headerView(
        reinterpret_cast<const char *>(io_.readBufferData()), headerSize);

    if (!request_.parseRequestHeader(headerView)) {
      SPDLOG_DEBUG(
          "PARSE ERROR... {}",
          std::string_view(reinterpret_cast<const char *>(io_.readBufferData()),
                           headerSize));
      co_await sendErrorResponseAndClose(400, "Malformed Header");
      co_return false;
    }

    if (request_.getVersion() != "HTTP/1.1") {
      SPDLOG_DEBUG("VERSION ERROR... {}", request_.getVersion());
      co_await sendErrorResponseAndClose(505);
      co_return false;
    }

    if (request_.getHeader("Host") == "") {
      SPDLOG_DEBUG("HOST ERROR... {}", headerView);
      co_await sendErrorResponseAndClose(400, "No Host Header Provided");
      co_return false;
    }

    io_.eraseFromReadBuffer(headerSize + 4);

    co_return true;
  }

  Task<bool> handleExpectHeader() {
    auto expect = toLowerCase(request_.getHeader("Expect"));
    if (expect == "100-continue") {
      RouterResponse result =
          router_.validate(request_.getPath(), request_.getMethod());
      switch (result) {
      case RouterResponse::NOT_FOUND:
        co_await sendErrorResponseAndClose(404);
        co_return false;
      case RouterResponse::METHOD_NOT_ALLOWED:
        co_await sendErrorResponseAndClose(405);
        co_return false;
      case RouterResponse::OK: {
        std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
        auto responseBytes =
            std::vector<unsigned char>(response.begin(), response.end());
        io_.enqueue(responseBytes);
        if (not co_await io_.write())
          co_return false;
        co_return true;
      }
      }
    } else {
      SPDLOG_ERROR("Expect value not supported: {}", expect);
      co_await sendErrorResponseAndClose(417);
      co_return false;
    }
  }

  Task<bool> readBody() {
    bool hasContentLengthHeader = request_.getHeader("Content-Length") != "";
    auto transferEncodingHeader =
        toLowerCase(request_.getHeader("Transfer-Encoding"));

    if (hasContentLengthHeader && transferEncodingHeader != "") {
      SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
      co_await sendErrorResponseAndClose(
          400, "Request cannot contain both Content-Length "
               "and Transfer-Encoding headers");
      co_return false;
    }

    if (transferEncodingHeader.find("chunked") != std::string::npos) {
      co_return co_await handleReadingBodyChunked();
    }

    if (hasContentLengthHeader) {
      co_return co_await handleReadingBodyContentLength();
    }

    co_return true;
  }

  Task<bool> handleReadingBodyChunked() {
    for (;;) {
      auto result = chunkParser_.feed(io_);

      if (!result)
        switch (result.error()) {
        case ChunkError::TOO_LARGE:
          co_await sendErrorResponseAndClose(413);
          co_return false;
        case ChunkError::MALFORMED:
          co_await sendErrorResponseAndClose(400);
          co_return false;
        }

      if (result.value().has_value()) {
        request_.setBody(*result.value());
        co_return true;
      }

      // parser needs more data — now read
      ReadResult readResult = co_await io_.read();
      if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
        co_await sendErrorResponseAndClose(413);
        co_return false;
      }
      if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR)
        co_return false;
    }
  }

  Task<bool> handleReadingBodyContentLength() {
    auto contentLengthResult = request_.getContentLength();

    if (!contentLengthResult) {
      switch (contentLengthResult.error()) {
      case ContentLengthError::NO_CONTENT_LENGTH_HEADER:
        std::unreachable();
      case ContentLengthError::INVALID_CONTENT_LENGTH:
        co_await sendErrorResponseAndClose(400,
                                           "Invalid Content-Length header");
        co_return false;
      case ContentLengthError::CONTENT_LENGTH_TOO_LARGE:
        co_await sendErrorResponseAndClose(413);
        co_return false;
      }
    }

    size_t contentLength = contentLengthResult.value();

    while (io_.getReadBufferSize() <
           contentLength) { // read more data if body is incomplete
      ReadResult result = co_await io_.read();

      if (result == ReadResult::BUFFER_LIMIT_EXCEEDED) {
        co_await sendErrorResponseAndClose(413);
        co_return false;
      } else if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
        co_return false;
      }
    }

    request_.setBody(io_.getReadBufferString(contentLength));
    io_.eraseFromReadBuffer(contentLength);

    co_return true;
  }

  Task<bool> generateAndSendResponse() {
    auto response = co_await generateResponse();
    if (std::holds_alternative<HttpResponse>(response))
      co_return co_await serializeAndSendResponse(
          std::get<HttpResponse>(response));
    else
      co_return co_await sendStreamResponse(
          std::move(std::get<HttpStreamResponse>(response)));
  }

  Task<bool> sendStreamResponse(HttpStreamResponse responseStream) {
    std::vector<unsigned char> serializedHeader =
        responseStream.getSerializedHeader();
    io_.enqueue(serializedHeader);

    if (not co_await io_.write())
      co_return false;

    std::optional<std::string> chunkOpt = "init";
    bool error = false;
    while (chunkOpt.has_value()) {
      try {
        chunkOpt = co_await responseStream.getNextChunk();
      } catch (const std::exception &e) {
        SPDLOG_ERROR("Stream handler threw exception: {}", e.what());
        io_.enqueue(HttpStreamResponse::serializeChunk(
            "Internal Server Error: " + std::string(e.what())));
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        error = true;
      } catch (...) {
        SPDLOG_ERROR("Stream handler threw unknown exception");
        io_.enqueue(
            HttpStreamResponse::serializeChunk("Internal Server Error"));
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        error = true;
      }

      if (error) {
        co_await io_.write();
        co_return false;
      }

      std::string chunk = chunkOpt.value_or("");

      if (chunk.empty()) {
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        logRequest(request_, responseStream);
        co_return co_await io_.write();
      } else {
        io_.enqueue(HttpStreamResponse::serializeChunk(chunk));
      }

      if (not co_await io_.write())
        co_return false;
    }
    std::unreachable();
  }

  Task<bool> serializeAndSendResponse(HttpResponse response) {
    if (response.getStatusCode() == -1) {
      SPDLOG_ERROR("Prevented: Trying to send response with status code -1");
      co_return false;
    }

    std::vector<unsigned char> serialized = response.serialize();
    io_.enqueue(serialized);
    logRequest(request_, response);
    if (not co_await io_.write())
      co_return false;
    co_return true;
  }

  Task<void> sendErrorResponseAndClose(int statusCode,
                                       const std::string &message = "") {
    HttpResponse response = buildErrorResponse(statusCode, message);
    keepAlive_ = false;
    response.setHeader("Connection", "close");
    co_await serializeAndSendResponse(response);
  }

  Task<Response> generateResponse() {
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
                 keepAlive_ = shouldKeepAlive();
                 res.setHeader("Connection",
                               keepAlive_ ? "keep-alive" : "close");
                 res.setHeader("Date", getCurrentHttpDate());
               }},
               response);

    co_return response;
  }

  HttpResponse buildErrorResponse(int statusCode,
                                  const std::string &message = "") {
    HttpResponse response = errorFactory_.build(request_, statusCode, message);
    if (request_.getMethod() == "HEAD")
      response.stripBody();
    if (statusCode == 405)
      response.setHeader("Allow",
                         router_.getAllowedMethodsString(request_.getPath()));
    return response;
  }

  void resetForNextRequest() {
    request_ = HttpRequest();
    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
    chunkParser_.reset();
  }
};
