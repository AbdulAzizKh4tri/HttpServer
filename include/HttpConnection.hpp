#pragma once

#include <memory>
#include <spdlog/spdlog.h>
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
        state_(ConnectionState::HANDSHAKING), request_(HttpRequest()) {}

  void onReadable() {
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
    if (state_ == ConnectionState::HANDSHAKING) {
      handleHandshake();
      return;
    }
    handleWritingResponse();
  }

  bool wantsWrite() const {
    return !writeBuffer_.empty() || handshakeWantsWrite_;
  }

  bool isClosing() const { return state_ == ConnectionState::CLOSING; }

  std::string getIp() const { return stream_->getIp(); }
  uint16_t getPort() const { return stream_->getPort(); }
  int getFd() const { return stream_->getFd(); }

private:
  std::shared_ptr<IStream> stream_;
  ConnectionState state_;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
  bool handshakeWantsWrite_ = false;
  HttpRequest request_;
  Router router_;

  void handleHandshake() {
    handshakeWantsWrite_ = false;

    HandshakeResult result = stream_->handshake();
    switch (result) {
    case HandshakeResult::DONE:
      state_ = ConnectionState::READING_HEADERS;
      break;

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
      state_ = ConnectionState::CLOSING;
      return;
    }

    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + end + 4);

    state_ = ConnectionState::READING_BODY;
    handleReadingBody();
  };

  void handleReadingBody() {

    int contentLength = request_.getContentLength();
    bool bodyComplete =
        (contentLength == -1) || ((int)readBuffer_.size() >= contentLength);

    if (!bodyComplete) {
      ReadResult result = drainIntoBuffer();
      if (result == ReadResult::CLOSED) {
        return;
      }
      bodyComplete =
          (contentLength == -1) || ((int)readBuffer_.size() >= contentLength);
    }

    if (!bodyComplete)
      return;

    size_t bodySize = (contentLength == -1) ? 0 : (size_t)contentLength;

    request_.body =
        std::string(readBuffer_.begin(), readBuffer_.begin() + bodySize);
    readBuffer_.clear();

    state_ = ConnectionState::WRITING_RESPONSE;
    std::vector<unsigned char> response =
        router_.dispatch(request_).serialize();

    writeBuffer_.clear();
    writeBuffer_.insert(writeBuffer_.end(), response.begin(), response.end());

    handleWritingResponse();
  };

  void handleWritingResponse() {
    if (wantsWrite()) {
      flushWriteBuffer();
    } else {
      state_ = ConnectionState::CLOSING;
    }
  }

  ReadResult drainIntoBuffer() {
    bool gotData = false;
    for (;;) {
      std::vector<unsigned char> buf(4096);
      ssize_t n = stream_->receive(buf);

      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;
        }
        SPDLOG_ERROR("Receive error for {}:{} : {}", stream_->getIp(),
                     stream_->getPort(), strerror(errno));
        state_ = ConnectionState::CLOSING;
        return ReadResult::CLOSED;
      }

      if (n == 0) {
        SPDLOG_INFO("Connection closed by peer, {}:{}", stream_->getIp(),
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
    if (writeBuffer_.empty())
      return;

    ssize_t n = stream_->send(writeBuffer_);

    if (n < 0) {
      SPDLOG_ERROR("Send error for {}:{}, {}", stream_->getIp(),
                   stream_->getPort(), strerror(errno));
      state_ = ConnectionState::CLOSING;
      return;
    }

    if (n > 0)
      writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + n);
  }
};
