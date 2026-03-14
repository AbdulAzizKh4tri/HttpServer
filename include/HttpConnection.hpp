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

enum class ConnectionState {
  HANDSHAKING,
  READING_HEADERS,
  SENDING_CONTINUE,
  READING_BODY,
  WRITING_RESPONSE,
  STREAMING_RESPONSE,
  CLOSING
};

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router,
                 ErrorFactory &errorFactory)
      : io_(std::move(stream)), router_(router),
        state_(ConnectionState::HANDSHAKING), errorFactory_(errorFactory) {

    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
    // create timer for Connection: keep alive
    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerFd_ == -1)
      throw std::runtime_error("Failed to create timerfd");
    armTimer();
  }

  ~HttpConnection() {
    if (timerFd_ != -1)
      ::close(timerFd_);
  }

  // non-copyable — owns a fd
  HttpConnection(const HttpConnection &) = delete;
  HttpConnection &operator=(const HttpConnection &) = delete;

  void onReadable() {
    resetTimer();
    switch (state_) {
    case ConnectionState::HANDSHAKING:
      handleHandshake();
      break;
    case ConnectionState::READING_HEADERS:
      handleReadingHeaders();
      break;
    case ConnectionState::READING_BODY:
      handleReadingBody();
      break;
    case ConnectionState::SENDING_CONTINUE:
    case ConnectionState::STREAMING_RESPONSE:
    case ConnectionState::WRITING_RESPONSE:
    case ConnectionState::CLOSING:
      break;
    }
  }

  void onWriteable() {
    resetTimer();
    if (state_ == ConnectionState::HANDSHAKING) {
      handleHandshake();
      return;
    }

    if (state_ == ConnectionState::SENDING_CONTINUE) {
      handleSendingContinue();
      return;
    }

    if (state_ == ConnectionState::STREAMING_RESPONSE) {
      handleStreamingResponse();
      return;
    }

    handleWritingResponse();
  }

  void onTimeout() {
    SPDLOG_DEBUG("Connection timed out {}:{}", io_.getIp(), io_.getPort());
    sendErrorResponseAndClose(408);
  }

  bool wantsWrite() const {

    return io_.hasPendingWrites() || handshakeWantsWrite_ ||
           state_ == ConnectionState::STREAMING_RESPONSE;
    ;
  }

  bool isClosing() const { return state_ == ConnectionState::CLOSING; }

  int getFd() const { return io_.getFd(); }
  int getTimerFd() const { return timerFd_; }
  std::string getIp() const { return io_.getIp(); }
  uint16_t getPort() const { return io_.getPort(); }

private:
  static constexpr int IDLE_TIMEOUT_SECS = 30;

  int timerFd_ = -1;

  ConnectionState state_;
  bool handshakeWantsWrite_ = false, keepAlive_ = false;

  ConnectionIO io_;
  ChunkedBodyParser chunkParser_;

  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

  HttpStreamResponse responseStream_;

  void armTimer() {
    itimerspec spec = {};
    spec.it_value.tv_sec = IDLE_TIMEOUT_SECS;
    timerfd_settime(timerFd_, 0, &spec, nullptr);
  }

  void disarmTimer() {
    // For websockets eventually, nothing useful yet
    itimerspec spec = {};
    timerfd_settime(timerFd_, 0, &spec, nullptr);
  }

  void resetTimer() { armTimer(); }

  bool shouldKeepAlive() const {
    const std::string &connection =
        toLowerCase(request_.getHeader("Connection"));
    return connection.find("close") == std::string::npos;
  }

  void setState(ConnectionState newState) {
    state_ = newState;

    switch (state_) {
    case ConnectionState::HANDSHAKING:
      SPDLOG_TRACE("State set to Handshaking for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::READING_HEADERS:
      SPDLOG_TRACE("State set to Reading Headers for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::READING_BODY:
      SPDLOG_TRACE("State set to Reading Bodyfor {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::SENDING_CONTINUE:
      SPDLOG_TRACE("State set to Sending Continue for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::STREAMING_RESPONSE:
      SPDLOG_TRACE("State set to Streaming Response for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::WRITING_RESPONSE:
      SPDLOG_TRACE("State set to Writing Response for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
    case ConnectionState::CLOSING:
      SPDLOG_TRACE("State set to Closing for {}:{}", io_.getIp(),
                   io_.getPort());
      break;
      break;
    }
  }

  void resetForNextRequest() {
    request_ = HttpRequest();
    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
    responseStream_ = HttpStreamResponse();
    setState(ConnectionState::READING_HEADERS);
    chunkParser_.reset();
    resetTimer();
  }

  void handleHandshake() {
    handshakeWantsWrite_ = false;
    HandshakeResult result = io_.handshake();
    switch (result) {
    case HandshakeResult::DONE:
    case HandshakeResult::NO_TLS:
      setState(ConnectionState::READING_HEADERS);
      break;
    case HandshakeResult::WANT_READ:
      break;
    case HandshakeResult::WANT_WRITE:
      handshakeWantsWrite_ = true;
      break;
    case HandshakeResult::ERROR:
      SPDLOG_ERROR("TLS handshake failed for {}:{}", io_.getIp(),
                   io_.getPort());
      setState(ConnectionState::CLOSING);
      break;
    }
  }

  void handleReadingHeaders() {
    ReadResult result = io_.drainIntoReadBuffer(HttpRequest::MAX_HEADER_SIZE);

    if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
      setState(ConnectionState::CLOSING);
      return;
    }
    if (result == ReadResult::WOULD_BLOCK && io_.getReadBufferSize() == 0) {
      return;
    }

    while (io_.getReadBufferSize() >= 2 && *(io_.readBufferBegin()) == '\r' &&
           *(io_.readBufferBegin() + 1) == '\n') {
      io_.eraseFromReadBuffer(2);
    }

    auto it = std::search(io_.readBufferBegin(), io_.readBufferEnd(),
                          crlf2.begin(), crlf2.end());

    if (it == io_.readBufferEnd()) {
      if (result == ReadResult::BUFFER_LIMIT_EXCEEDED) {
        sendErrorResponseAndClose(431);
      }
      return;
    }

    size_t headerSize = std::distance(io_.readBufferBegin(), it);

    if (headerSize > HttpRequest::MAX_HEADER_SIZE) {
      sendErrorResponseAndClose(431);
      return;
    }

    std::string_view headerView(
        reinterpret_cast<const char *>(io_.readBufferData()), headerSize);

    if (!request_.parseRequestHeader(headerView)) {
      SPDLOG_DEBUG(
          "PARSE ERROR... {}",
          std::string_view(reinterpret_cast<const char *>(io_.readBufferData()),
                           headerSize));
      sendErrorResponseAndClose(400, "Malformed Header");
      return;
    }

    if (request_.getVersion() != "HTTP/1.1") {
      SPDLOG_DEBUG("VERSION ERROR... {}", request_.getVersion());
      sendErrorResponseAndClose(505);
      return;
    }

    if (request_.getHeader("Host") == "") {
      SPDLOG_DEBUG("HOST ERROR... {}", headerView);
      sendErrorResponseAndClose(400, "No Host Header Provided");
      return;
    }

    io_.eraseFromReadBuffer(headerSize + 4);

    if (request_.getHeader("Expect") != "") {
      handleExpectHeader();
      return;
    }

    setState(ConnectionState::READING_BODY);
    handleReadingBody();
  }

  void handleExpectHeader() {
    auto expect = toLowerCase(request_.getHeader("Expect"));
    if (expect == "100-continue") {
      RouterResponse result =
          router_.validate(request_.getPath(), request_.getMethod());
      switch (result) {
      case RouterResponse::NOT_FOUND:
        sendErrorResponseAndClose(404);
        return;
      case RouterResponse::METHOD_NOT_ALLOWED:
        sendErrorResponseAndClose(405);
        return;
      case RouterResponse::OK: {
        setState(ConnectionState::SENDING_CONTINUE);
        std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
        auto responseBytes =
            std::vector<unsigned char>(response.begin(), response.end());
        io_.enqueue(responseBytes);
        handleSendingContinue();
        return;
      }
      }
    }
    if (expect != "") {
      SPDLOG_ERROR("Expect value not supported: {}", expect);
      sendErrorResponseAndClose(417);
      return;
    }
  }

  void handleSendingContinue() {
    if (!io_.flushFromWriteBuffer()) {
      setState(ConnectionState::CLOSING);
      return;
    }
    if (!wantsWrite()) {
      setState(ConnectionState::READING_BODY);
      handleReadingBody();
    }
  }

  void handleReadingBody() {
    bool hasContentLengthHeader = request_.getHeader("Content-Length") != "";
    auto transferEncodingHeader =
        toLowerCase(request_.getHeader("Transfer-Encoding"));

    if (hasContentLengthHeader && transferEncodingHeader != "") {
      SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
      sendErrorResponseAndClose(400,
                                "Request cannot contain both Content-Length "
                                "and Transfer-Encoding headers");
      return;
    }

    if (transferEncodingHeader.find("chunked") != std::string::npos) {
      handleReadingBodyChunked();
      return;
    }

    if (hasContentLengthHeader) {
      handleReadingBodyContentLength();
      return;
    }

    generateAndHandleResponse();
  }

  void handleReadingBodyChunked() {
    ReadResult readResult =
        io_.drainIntoReadBuffer(HttpRequest::MAX_CONTENT_LENGTH);

    if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
      sendErrorResponseAndClose(413);
      return;
    }
    if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
      setState(ConnectionState::CLOSING);
      return;
    }

    auto result = chunkParser_.feed(io_);
    if (!result)
      switch (result.error()) {
      case ChunkError::TOO_LARGE:
        sendErrorResponseAndClose(413);
        return;
      case ChunkError::MALFORMED:
        sendErrorResponseAndClose(400);
        return;
      }
    auto resultVal = result.value();

    if (resultVal.has_value()) {
      request_.setBody(*resultVal);
      generateAndHandleResponse();
    }
  }

  void handleReadingBodyContentLength() {
    auto contentLengthResult = request_.getContentLength();

    if (!contentLengthResult) {
      switch (contentLengthResult.error()) {
      case ContentLengthError::NO_CONTENT_LENGTH_HEADER:
        std::unreachable();
      case ContentLengthError::INVALID_CONTENT_LENGTH:
        sendErrorResponseAndClose(400, "Invalid Content-Length header");
        return;
      case ContentLengthError::CONTENT_LENGTH_TOO_LARGE:
        sendErrorResponseAndClose(413);
        return;
      }
    }

    size_t contentLength = contentLengthResult.value();
    bool bodyComplete = io_.getReadBufferSize() >= contentLength;

    if (!bodyComplete) { // read more data if body is incomplete
      ReadResult result =
          io_.drainIntoReadBuffer(HttpRequest::MAX_CONTENT_LENGTH);

      if (result == ReadResult::BUFFER_LIMIT_EXCEEDED) {
        sendErrorResponseAndClose(413);
        return;
      }
      if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
        setState(ConnectionState::CLOSING);
        return;
      }

      bodyComplete = io_.getReadBufferSize() >= contentLength;
    }

    if (!bodyComplete) // if still incomplete, try again later
      return;

    request_.setBody(io_.getReadBufferString(contentLength));
    io_.eraseFromReadBuffer(contentLength);

    generateAndHandleResponse();
  }

  Response generateResponse() {
    Response response;

    try {
      response = router_.dispatch(request_);
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

    return response;
  }

  void generateAndHandleResponse() {
    auto response = generateResponse();
    std::visit(overloaded{[this](const HttpResponse &res) {
                            serializeAndSendResponse(res);
                          },
                          [this](const HttpStreamResponse &res) {
                            startStreamResponse(res);
                          }},
               response);
  }

  void startStreamResponse(HttpStreamResponse response) {
    setState(ConnectionState::STREAMING_RESPONSE);
    responseStream_ = std::move(response);

    std::vector<unsigned char> serializedHeader =
        responseStream_.getSerializedHeader();
    io_.enqueue(serializedHeader);
    handleStreamingResponse();
  }

  void handleStreamingResponse() {
    if (!io_.hasPendingWrites()) {
      std::optional<std::string> chunkOpt;
      try {
        chunkOpt = responseStream_.getNextChunk();
      } catch (const std::exception &e) {
        SPDLOG_ERROR("Stream handler threw exception: {}", e.what());
        io_.enqueue(HttpStreamResponse::serializeChunk(
            "Internal Server Error: " + std::string(e.what())));
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        keepAlive_ = false;
        setState(ConnectionState::WRITING_RESPONSE);
        return;
      } catch (...) {
        SPDLOG_ERROR("Stream handler threw unknown exception");
        io_.enqueue(
            HttpStreamResponse::serializeChunk("Internal Server Error"));
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        keepAlive_ = false;
        setState(ConnectionState::WRITING_RESPONSE);
        return;
      }

      std::string chunk = chunkOpt.value_or("");

      if (chunk.empty()) {
        io_.enqueue(HttpStreamResponse::serializeChunk(""));
        logRequest(request_, responseStream_);
        if (keepAlive_) {
          resetForNextRequest();
          if (io_.getReadBufferSize() > 0)
            handleReadingHeaders();
        } else {
          setState(ConnectionState::WRITING_RESPONSE);
        }
        return;
      }

      io_.enqueue(HttpStreamResponse::serializeChunk(chunk));
    }

    if (io_.hasPendingWrites() && !io_.flushFromWriteBuffer()) {
      setState(ConnectionState::CLOSING);
      return;
    }
  }

  void serializeAndSendResponse(HttpResponse response) {
    if (response.getStatusCode() == -1) {
      SPDLOG_ERROR("Prevented: Trying to send response with status code -1");
      return;
    }

    setState(ConnectionState::WRITING_RESPONSE);
    std::vector<unsigned char> serialized = response.serialize();
    io_.enqueue(serialized);
    logRequest(request_, response);
    handleWritingResponse();
  }

  HttpResponse buildErrorResponse(int statusCode,
                                  const std::string &message = "") {
    HttpResponse response =
        errorFactory_.build(request_, statusCode, message);
    if (request_.getMethod() == "HEAD")
      response.stripBody();
    if (statusCode == 405)
      response.setHeader("Allow",
                         router_.getAllowedMethodsString(request_.getPath()));
    return response;
  }

  void sendErrorResponseAndClose(int statusCode,
                                 const std::string &message = "") {
    HttpResponse response = buildErrorResponse(statusCode, message);
    keepAlive_ = false;
    response.setHeader("Connection", "close");
    serializeAndSendResponse(response);
  }

  void handleWritingResponse() {
    if (!io_.flushFromWriteBuffer()) {
      setState(ConnectionState::CLOSING);
      return;
    }
    if (!wantsWrite()) {
      if (keepAlive_) {
        resetForNextRequest();
        if (io_.getReadBufferSize() > 0)
          handleReadingHeaders();
      } else
        setState(ConnectionState::CLOSING);
    }
  }
};
