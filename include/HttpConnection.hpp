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
#include "IStream.hpp"
#include "Router.hpp"
#include "logUtils.hpp"

enum class ConnectionState {
  HANDSHAKING,
  READING_HEADERS,
  SENDING_CONTINUE,
  READING_BODY,
  WRITING_RESPONSE,
  CLOSING
};

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router,
                 ErrorFactory &errorFactory)
      : connIO_(std::move(stream)), router_(router),
        state_(ConnectionState::HANDSHAKING), errorFactory_(errorFactory) {

    request_.setIp(connIO_.getIp());
    request_.setPort(connIO_.getPort());
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

    handleWritingResponse();
  }

  void onTimeout() {
    SPDLOG_DEBUG("Connection timed out {}:{}", connIO_.getIp(),
                 connIO_.getPort());
    setState(ConnectionState::CLOSING);
  }

  bool wantsWrite() const {
    return connIO_.hasPendingWrites() || handshakeWantsWrite_;
  }
  bool isClosing() const { return state_ == ConnectionState::CLOSING; }

  int getFd() const { return connIO_.getFd(); }
  int getTimerFd() const { return timerFd_; }
  std::string getIp() const { return connIO_.getIp(); }
  uint16_t getPort() const { return connIO_.getPort(); }

private:
  static constexpr int IDLE_TIMEOUT_SECS = 30;

  int timerFd_ = -1;

  ConnectionState state_;
  bool handshakeWantsWrite_ = false, keepAlive_ = false;

  ConnectionIO connIO_;
  ChunkedBodyParser chunkParser_;

  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

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
    auto connection = request_.getHeader("Connection");
    if (connection == "close")
      return false;
    return true; // HTTP/1.1 default
  }

  void setState(ConnectionState newState) { state_ = newState; }

  void resetForNextRequest() {
    request_ = HttpRequest();
    request_.setIp(connIO_.getIp());
    request_.setPort(connIO_.getPort());
    setState(ConnectionState::READING_HEADERS);
    chunkParser_.reset();
    resetTimer();
  }

  void handleHandshake() {
    handshakeWantsWrite_ = false;
    HandshakeResult result = connIO_.handshake();
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
      SPDLOG_ERROR("TLS handshake failed for {}:{}", connIO_.getIp(),
                   connIO_.getPort());
      setState(ConnectionState::CLOSING);
      break;
    }
  }

  void handleReadingHeaders() {
    ReadResult result = connIO_.drainIntoReadBuffer();
    if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
      setState(ConnectionState::CLOSING);
      return;
    }
    if (result == ReadResult::WOULD_BLOCK && connIO_.readBuffer().empty()) {
      return;
    }

    while (connIO_.getReadBufferSize() >= 2 &&
           connIO_.readBuffer()[0] == '\r' && connIO_.readBuffer()[1] == '\n') {
      connIO_.eraseFromReadBuffer(2);
    }

    auto data = connIO_.getReadBufferString();
    size_t end = data.find("\r\n\r\n"); // Looking for end of header.
    if (end == std::string::npos) {
      if (connIO_.getReadBufferSize() > HttpRequest::MAX_HEADER_SIZE) {
        sendErrorResponseAndClose(431); // 431 = Request Header Fields Too Large
      }
      return;
    }

    std::string headerString = data.substr(0, end);
    if (!request_.parseRequestHeader(headerString)) {
      SPDLOG_DEBUG("PARSE ERROR... {}", headerString);
      sendErrorResponseAndClose(400, "Malformed Header");
      return;
    }

    if (request_.getHeader("Host") == "") {
      SPDLOG_DEBUG("HOST ERROR... {}", headerString);
      sendErrorResponseAndClose(400, "No Host Header Provided");
      return;
    }

    connIO_.eraseFromReadBuffer(end + 4);

    if (request_.getHeader("Expect") != "") {
      handleExpectHeader();
      return;
    }

    setState(ConnectionState::READING_BODY);
    handleReadingBody();
  }

  void handleExpectHeader() {
    auto expect = request_.getHeader("Expect");
    if (expect == "100-continue") {
      RouterResponse result =
          router_.validate(request_.getPath(), request_.getMethod());
      switch (result) {
      case RouterResponse::NOT_FOUND:
        sendErrorResponse(404);
        return;
      case RouterResponse::METHOD_NOT_ALLOWED:
        sendErrorResponse(405);
        return;
      case RouterResponse::OK:
        setState(ConnectionState::SENDING_CONTINUE);
        std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
        auto responseBytes =
            std::vector<unsigned char>(response.begin(), response.end());
        connIO_.enqueue(responseBytes);
        handleSendingContinue();
        return;
      }
    }
    if (expect != "") {
      SPDLOG_ERROR("Expect value not supported: {}", expect);
      sendErrorResponseAndClose(417);
      return;
    }
  }

  void handleSendingContinue() {
    if (!connIO_.flushFromWriteBuffer()) {
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
    auto transferEncodingHeader = request_.getHeader("Transfer-Encoding");

    if (hasContentLengthHeader && transferEncodingHeader != "") {
      SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
      sendErrorResponseAndClose(400,
                                "Request cannot contain both Content-Length "
                                "and Transfer-Encoding headers");
      return;
    }

    if (transferEncodingHeader == "chunked") {
      handleReadingBodyChunked();
      return;
    }

    if (hasContentLengthHeader) {
      handleReadingBodyContentLength();
      return;
    }

    sendResponse();
  }

  void handleReadingBodyChunked() {
    ReadResult readResult = connIO_.drainIntoReadBuffer();
    if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
      setState(ConnectionState::CLOSING);
      return;
    }

    auto result = chunkParser_.feed(connIO_.readBuffer());
    if (!result)
      switch (result.error()) {
      case ChunkError::TOO_LARGE:
        sendErrorResponseAndClose(413);
        return;
      case ChunkError::MALFORMED:
        sendErrorResponseAndClose(400);
        return;
      }
    auto res = result.value();
    if (res.has_value()) {
      request_.setBody(*res);
      sendResponse();
      return;
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
    bool bodyComplete = connIO_.getReadBufferSize() >= contentLength;

    if (!bodyComplete) { // read more data if body is incomplete
      ReadResult result = connIO_.drainIntoReadBuffer();
      if (result == ReadResult::CLOSED || result == ReadResult::ERROR) {
        setState(ConnectionState::CLOSING);
        return;
      }
      bodyComplete = connIO_.getReadBufferSize() >= contentLength;
    }

    if (!bodyComplete) // if still incomplete, try again later
      return;

    request_.setBody(connIO_.getReadBufferString(contentLength));
    connIO_.eraseFromReadBuffer(contentLength);

    sendResponse();
  }

  void sendResponse() {
    HttpResponse response;

    HttpRequest dispatchRequest = request_;

    if (request_.getMethod() == "HEAD")
      dispatchRequest.setMethod("GET");

    try {
      auto result = router_.dispatch(dispatchRequest);
      if (!result) {
        switch (result.error()) {
        case RouteError::NOT_FOUND:
          sendErrorResponse(404);
          return;
        case RouteError::METHOD_NOT_ALLOWED:
          sendErrorResponse(405);
          return;
        }
      }
      response = result.value();
    } catch (const std::exception &e) {
      SPDLOG_ERROR("Handler threw exception: {}", e.what());
      sendErrorResponse(500, e.what());
      return;
    } catch (...) {
      sendErrorResponse(500);
      return;
    }

    keepAlive_ = shouldKeepAlive();
    response.setHeader("Connection", keepAlive_ ? "keep-alive" : "close");

    if (request_.getMethod() == "HEAD")
      response.stripBodyForHeadRequest();

    serializeAndSendResponse(response);
  }

  void serializeAndSendResponse(const HttpResponse &response) {

    setState(ConnectionState::WRITING_RESPONSE);
    std::vector<unsigned char> serialized = response.serialize();
    connIO_.enqueue(serialized);
    logRequest(request_, response);
    handleWritingResponse();
  }

  void buildErrorResponse(HttpResponse &response, int statusCode,
                          const std::string &message) {
    response =
        errorFactory_.build(request_.getHeader("Accept"), statusCode, message);
    if (request_.getMethod() == "HEAD")
      response.stripBodyForHeadRequest();
    if (statusCode == 405)
      response.setHeader("Allow",
                         router_.getAllowedMethodsString(request_.getPath()));
  }

  void sendErrorResponseAndClose(int statusCode,
                                 const std::string &message = "") {
    HttpResponse response;
    buildErrorResponse(response, statusCode, message);
    keepAlive_ = false;
    response.setHeader("Connection", "close");
    serializeAndSendResponse(response);
  }

  void sendErrorResponse(int statusCode, const std::string &message = "") {
    HttpResponse response;
    buildErrorResponse(response, statusCode, message);
    serializeAndSendResponse(response);
  }

  void handleWritingResponse() {
    if (!connIO_.flushFromWriteBuffer()) {
      setState(ConnectionState::CLOSING);
      return;
    }
    if (!wantsWrite()) {
      if (keepAlive_) {
        resetForNextRequest();
        if (!connIO_.readBuffer().empty())
          handleReadingHeaders();
      } else
        setState(ConnectionState::CLOSING);
    }
  }
};
