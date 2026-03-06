#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>

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

enum class ChunkState {
  READING_SIZE,
  READING_DATA,
  READING_TRAILING_CRLF,
};

enum class ReadResult { DATA, CLOSED, WOULD_BLOCK };

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router)
      : stream_(std::move(stream)), router_(router),
        state_(ConnectionState::HANDSHAKING) {
    request_.ip = stream_->getIp();
    request_.port = stream_->getPort();

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
    case ConnectionState::SENDING_CONTINUE:
      break;
    case ConnectionState::READING_BODY:
      handleReadingBody();
      break;
    case ConnectionState::WRITING_RESPONSE:
      break;
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
    SPDLOG_DEBUG("Connection timed out {}:{}", stream_->getIp(),
                 stream_->getPort());
    state_ = ConnectionState::CLOSING;
  }

  bool wantsWrite() const {
    return !writeBuffer_.empty() || handshakeWantsWrite_;
  }
  bool isClosing() const { return state_ == ConnectionState::CLOSING; }

  int getFd() const { return stream_->getFd(); }
  int getTimerFd() const { return timerFd_; }
  std::string getIp() const { return stream_->getIp(); }
  uint16_t getPort() const { return stream_->getPort(); }

private:
  static constexpr int IDLE_TIMEOUT_SECS = 30;

  std::shared_ptr<IStream> stream_;
  ConnectionState state_;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
  bool handshakeWantsWrite_ = false, keepAlive_ = false;
  HttpRequest request_;
  Router &router_;
  int timerFd_ = -1;

  ChunkState chunkState_ = ChunkState::READING_SIZE;
  size_t currentChunkSize_ = 0;
  size_t currentChunkBytesRead_ = 0;
  std::string assembledBody_;

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

  void resetForNextRequest() {
    request_ = HttpRequest();
    request_.ip = stream_->getIp();
    request_.port = stream_->getPort();
    state_ = ConnectionState::READING_HEADERS;
    chunkState_ = ChunkState::READING_SIZE;
    currentChunkSize_ = 0;
    currentChunkBytesRead_ = 0;
    assembledBody_.clear();
    resetTimer();
  }

  void handleHandshake() {
    handshakeWantsWrite_ = false;
    HandshakeResult result = stream_->handshake();
    switch (result) {
    case HandshakeResult::DONE:
    case HandshakeResult::NO_TLS:
      state_ = ConnectionState::READING_HEADERS;
      break;
    case HandshakeResult::WANT_READ:
      break;
    case HandshakeResult::WANT_WRITE:
      handshakeWantsWrite_ = true;
      break;
    case HandshakeResult::ERROR:
      SPDLOG_ERROR("TLS handshake failed for {}:{}", stream_->getIp(),
                   stream_->getPort());
      state_ = ConnectionState::CLOSING;
      break;
    }
  }

  void handleReadingHeaders() {
    ReadResult result = drainIntoBuffer();
    if (result != ReadResult::DATA)
      return;

    while (readBuffer_.size() >= 2 && readBuffer_[0] == '\r' &&
           readBuffer_[1] == '\n') {
      readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + 2);
    }

    std::string data(readBuffer_.begin(), readBuffer_.end());
    size_t end = data.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (readBuffer_.size() > HttpRequest::MAX_HEADER_SIZE) {
        sendErrorResponseAndClose(431); // 431 = Request Header Fields Too Large
      }
      return;
    }

    std::string headerString = data.substr(0, end);
    if (!request_.parseHeader(headerString)) {
      SPDLOG_DEBUG("PARSE ERROR... {}", headerString);
      sendErrorResponseAndClose(400); // malformed header
      return;
    }

    if (request_.getHeader("Host") == "") {
      SPDLOG_DEBUG("HOST ERROR... {}", headerString);
      sendErrorResponseAndClose(400); // no Host header
      return;
    }

    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + end + 4);

    auto expect = request_.getHeader("Expect");
    if (expect == "100-continue") {
      std::string contentLength = request_.getHeader("Content-Length");
      int code =
          router_.validate(request_.path, request_.method, contentLength);

      if (code != 100) {
        sendErrorResponse(code);
        return;
      }

      state_ = ConnectionState::SENDING_CONTINUE;
      std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
      auto responseBytes =
          std::vector<unsigned char>(response.begin(), response.end());
      writeBuffer_.insert(writeBuffer_.end(), responseBytes.begin(),
                          responseBytes.end());
      handleSendingContinue();
      return;
    }
    if (expect != "") {
      SPDLOG_ERROR("Expect header not supported: {}", expect);
      sendErrorResponseAndClose(417);
      return;
    }

    state_ = ConnectionState::READING_BODY;
    handleReadingBody();
  }

  void handleSendingContinue() {
    flushWriteBuffer();
    if (!wantsWrite()) {
      state_ = ConnectionState::READING_BODY;
      handleReadingBody();
    }
  }

  void handleReadingBody() {
    bool hasContentLengthHeader = request_.getHeader("Content-Length") != "";
    auto transferEncodingHeader = request_.getHeader("Transfer-Encoding");

    if (hasContentLengthHeader && transferEncodingHeader != "") {
      SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
      sendErrorResponseAndClose(400);
      return;
    }

    if (transferEncodingHeader == "chunked") {
      handleReadingBodyChunked();
      return;
    }

    handleReadingBodyContentLength();
  }

  void handleReadingBodyChunked() {
    ReadResult result = drainIntoBuffer();
    if (result == ReadResult::CLOSED)
      return;

    while (true) {
      switch (chunkState_) {
      case ChunkState::READING_SIZE:
        if (!readChunkSize()) // returns false if needs more data
          return;
        break;
      case ChunkState::READING_DATA:
        if (!readChunkData())
          return;
        break;
      case ChunkState::READING_TRAILING_CRLF:
        if (!readTrailingCrlf())
          return;
        break;
      }
    }
  }

  bool readChunkSize() {
    std::string data(readBuffer_.begin(), readBuffer_.end());
    size_t end = data.find("\r\n");
    if (end == std::string::npos)
      return false; // need more data

    std::stringstream ss(data.substr(0, end));
    ss >> std::hex >> currentChunkSize_;
    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + end + 2);

    if (currentChunkSize_ == 0) {
      request_.body = assembledBody_;
      sendResponse();
      return false; // done, stop looping
    }

    chunkState_ = ChunkState::READING_DATA;
    return true;
  }

  bool readChunkData() {
    if (readBuffer_.size() < currentChunkSize_)
      return false;

    currentChunkBytesRead_ += currentChunkSize_;
    if (currentChunkBytesRead_ > HttpRequest::MAX_CONTENT_LENGTH) {
      sendErrorResponseAndClose(413);
      return false;
    }

    assembledBody_.append(readBuffer_.begin(),
                          readBuffer_.begin() + currentChunkSize_);
    readBuffer_.erase(readBuffer_.begin(),
                      readBuffer_.begin() + currentChunkSize_);

    currentChunkSize_ = 0;
    chunkState_ = ChunkState::READING_TRAILING_CRLF;
    return true;
  }

  bool readTrailingCrlf() {
    if (readBuffer_.size() < 2)
      return false;
    if (readBuffer_[0] != '\r' || readBuffer_[1] != '\n') {
      sendErrorResponseAndClose(400);
      return false;
    }
    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + 2);
    chunkState_ = ChunkState::READING_SIZE;
    return true;
  }

  void handleReadingBodyContentLength() {
    int contentLength = request_.getContentLength();

    if (contentLength == HttpRequest::INVALID_CONTENT_LENGTH) {
      sendErrorResponseAndClose(400);
      return;
    }

    if (contentLength == HttpRequest::CONTENT_LENGTH_TOO_LARGE) {
      sendErrorResponseAndClose(413);
      return;
    }

    bool bodyComplete =
        (contentLength == HttpRequest::NO_CONTENT_LENGTH_HEADER) ||
        ((int)readBuffer_.size() >= contentLength);

    if (!bodyComplete) {
      ReadResult result = drainIntoBuffer();
      if (result == ReadResult::CLOSED)
        return;
      bodyComplete = (contentLength == HttpRequest::NO_CONTENT_LENGTH_HEADER) ||
                     ((int)readBuffer_.size() >= contentLength);
    }

    if (!bodyComplete)
      return;

    size_t bodySize = (contentLength == HttpRequest::NO_CONTENT_LENGTH_HEADER)
                          ? 0
                          : (size_t)contentLength;

    request_.body =
        std::string(readBuffer_.begin(), readBuffer_.begin() + bodySize);
    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + bodySize);

    sendResponse();
  }

  void sendResponse() {
    bool keepAlive = shouldKeepAlive();

    state_ = ConnectionState::WRITING_RESPONSE;
    HttpResponse response;
    try {
      response = router_.dispatch(request_);
    } catch (const std::exception &e) {
      SPDLOG_ERROR("Handler threw exception: {}", e.what());
      response = HttpResponse(500);
      keepAlive = false;
    } catch (...) {
      SPDLOG_ERROR("Handler threw unknown exception");
      response = HttpResponse(500);
      keepAlive = false;
    }
    response.setHeader("Connection", keepAlive ? "keep-alive" : "close");

    std::vector<unsigned char> serialized = response.serialize();
    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), serialized.begin(),
                        serialized.end());

    logRequest(request_, response);

    // store decision for after flush
    keepAlive_ = keepAlive;
    handleWritingResponse();
  }

  void handleWritingResponse() {
    flushWriteBuffer();
    if (!wantsWrite()) {
      if (keepAlive_)
        resetForNextRequest();
      else
        state_ = ConnectionState::CLOSING;
    }
  }

  ReadResult drainIntoBuffer() {
    bool gotData = false;
    for (;;) {
      std::vector<unsigned char> buf(4096);
      auto result = stream_->receive(buf);

      switch (result.status) {
      case ReceiveResult::Status::DATA:
        buf.resize(result.bytes);
        readBuffer_.insert(readBuffer_.end(), buf.begin(), buf.end());
        gotData = true;
        break;
      case ReceiveResult::Status::WOULD_BLOCK:
        return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;
      case ReceiveResult::Status::CLOSED:
        SPDLOG_DEBUG("Connection closed by peer, {}:{}", stream_->getIp(),
                     stream_->getPort());
        state_ = ConnectionState::CLOSING;
        return ReadResult::CLOSED;
      case ReceiveResult::Status::ERROR:
        SPDLOG_ERROR("Receive error for {}:{}", stream_->getIp(),
                     stream_->getPort());
        state_ = ConnectionState::CLOSING;
        return ReadResult::CLOSED;
      }
    }
  }

  void flushWriteBuffer() {
    while (!writeBuffer_.empty()) {
      ssize_t n = stream_->send(writeBuffer_);
      if (n < 0) {
        SPDLOG_ERROR("Send error for {}:{}, {}", stream_->getIp(),
                     stream_->getPort(), strerror(errno));
        state_ = ConnectionState::CLOSING;
        return;
      }
      if (n == 0)
        return;
      writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + n);
    }
  }

  void sendErrorResponseAndClose(int statusCode) {
    state_ = ConnectionState::WRITING_RESPONSE;
    HttpResponse response(statusCode);
    keepAlive_ = false;
    response.setHeader("Connection", "close");
    std::vector<unsigned char> serialized = response.serialize();
    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), serialized.begin(),
                        serialized.end());
    logRequest(request_, response);
    handleWritingResponse();
  }

  void sendErrorResponse(int statusCode) {
    state_ = ConnectionState::WRITING_RESPONSE;
    HttpResponse response(statusCode);
    response.setHeader("Connection", keepAlive_ ? "keep-alive" : "close");

    std::vector<unsigned char> serialized = response.serialize();
    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), serialized.begin(),
                        serialized.end());
    logRequest(request_, response);
    handleWritingResponse();
  }
};
