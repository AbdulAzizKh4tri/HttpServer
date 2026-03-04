#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "IStream.hpp"
#include "Router.hpp"

enum class ConnectionState {
  HANDSHAKING,
  READING_HEADERS,
  READING_BODY,
  WRITING_RESPONSE,
  CLOSING
};

enum class ReadResult { DATA, CLOSED, WOULD_BLOCK };

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router)
      : stream_(std::move(stream)), router_(router),
        state_(ConnectionState::HANDSHAKING) {
    request_.ip = stream_->getIp();
    request_.port = stream_->getPort();

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
    case ConnectionState::WRITING_RESPONSE:
      handleWritingResponse();
      break;
    case ConnectionState::CLOSING:
    default:
      break;
    }
  }

  void onWriteable() {
    resetTimer();
    if (state_ == ConnectionState::HANDSHAKING) {
      handleHandshake();
      return;
    }
    handleWritingResponse();
  }

  void onTimeout() {
    SPDLOG_DEBUG("Connection timed out {}:{}", stream_->getIp(),
                 stream_->getPort());
    state_ = ConnectionState::CLOSING;
  }

  void disarmTimer() {
    itimerspec spec = {};
    timerfd_settime(timerFd_, 0, &spec, nullptr);
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

  void armTimer() {
    itimerspec spec = {};
    spec.it_value.tv_sec = IDLE_TIMEOUT_SECS;
    timerfd_settime(timerFd_, 0, &spec, nullptr);
  }

  void resetTimer() { armTimer(); }

  bool shouldKeepAlive() const {
    auto it = request_.headers.find("Connection");
    if (it != request_.headers.end())
      return it->second != "close";
    return true; // HTTP/1.1 default
  }

  void resetForNextRequest() {
    request_ = HttpRequest();
    request_.ip = stream_->getIp();
    request_.port = stream_->getPort();
    readBuffer_.clear();
    state_ = ConnectionState::READING_HEADERS;
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

    std::string data(readBuffer_.begin(), readBuffer_.end());
    size_t end = data.find("\r\n\r\n");
    if (end == std::string::npos)
      return;

    std::string headerString = data.substr(0, end);
    if (!request_.parseHeader(headerString)) {
      sendErrorResponse(400);
      return;
    }

    if (request_.headers.find("Host") == request_.headers.end()) {
      sendErrorResponse(400);
      return;
    }

    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + end + 4);
    state_ = ConnectionState::READING_BODY;
    handleReadingBody();
  }

  void handleReadingBody() {
    int contentLength = request_.getContentLength();

    if (contentLength == HttpRequest::CONTENT_LENGTH_TOO_LARGE) {
      sendErrorResponse(413);
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
    readBuffer_.clear();

    bool keepAlive = shouldKeepAlive();

    state_ = ConnectionState::WRITING_RESPONSE;
    HttpResponse response = router_.dispatch(request_);
    response.addHeader("Connection", keepAlive ? "keep-alive" : "close");

    std::vector<unsigned char> serialized = response.serialize();
    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), serialized.begin(),
                        serialized.end());

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
      ssize_t n = stream_->receive(buf);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;
        SPDLOG_ERROR("Receive error for {}:{} : {}", stream_->getIp(),
                     stream_->getPort(), strerror(errno));
        state_ = ConnectionState::CLOSING;
        return ReadResult::CLOSED;
      }
      if (n == 0) {
        SPDLOG_DEBUG("Connection closed by peer, {}:{}", stream_->getIp(),
                     stream_->getPort());
        state_ = ConnectionState::CLOSING;
        return ReadResult::CLOSED;
      }
      buf.resize(n);
      readBuffer_.insert(readBuffer_.end(), buf.begin(), buf.end());
      gotData = true;
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

  void sendErrorResponse(int statusCode) {
    keepAlive_ = false;
    state_ = ConnectionState::WRITING_RESPONSE;
    HttpResponse response(statusCode);
    response.addHeader("Connection", "close");
    std::vector<unsigned char> serialized = response.serialize();
    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), serialized.begin(),
                        serialized.end());
    handleWritingResponse();
  }
};
