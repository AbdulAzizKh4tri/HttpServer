#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <sys/types.h>

#include "HttpRequest.hpp"
#include "IStream.hpp"

enum class ConnectionState {
  HANDSHAKING,
  READING_HEADERS,
  READING_BODY,
  WRITING_RESPONSE,
  CLOSING
};

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream)
      : stream_(std::move(stream)), state_(ConnectionState::HANDSHAKING),
        request_(HttpRequest()) {}

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
  std::vector<std::byte> readBuffer_;
  std::vector<std::byte> writeBuffer_;
  bool handshakeWantsWrite_ = false;
  HttpRequest request_;

  void handleHandshake() {
    handshakeWantsWrite_ = false;

    HandshakeResult result = stream_->handshake();
    switch (result) {
    case HandshakeResult::DONE:
      SPDLOG_INFO("TLS handshake complete for {}:{}", stream_->getIp(),
                  stream_->getPort());
      state_ = ConnectionState::READING_HEADERS;
      break;

    case HandshakeResult::NO_TLS:
      SPDLOG_INFO("No TLS handshake was supported {}:{}", stream_->getIp(),
                  stream_->getPort());
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
    std::vector<std::byte> buf(4096);
    if (!receiveData(buf)) {
      return;
    }

    readBuffer_.insert(readBuffer_.end(), buf.begin(), buf.end());

    std::string data(reinterpret_cast<char *>(readBuffer_.data()),
                     readBuffer_.size()); // ✅
    size_t end = data.find("\r\n\r\n");

    if (end == std::string::npos)
      return;

    std::string headerString = data.substr(0, end);
    if (!request_.parseHeader(headerString)) {
      state_ = ConnectionState::CLOSING;
      return;
    }

    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + end + 4);

    if (state_ == ConnectionState::READING_HEADERS) {
      state_ = ConnectionState::READING_BODY;
      handleReadingBody();
    }
  };

  void handleReadingBody() {
    std::vector<std::byte> buf(4096);
    receiveData(buf);

    readBuffer_.insert(readBuffer_.end(), buf.begin(), buf.end());

    if (readBuffer_.size() >= request_.getContentLength() ||
        request_.getContentLength() == -1) {
      state_ = ConnectionState::WRITING_RESPONSE;

      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: " +
                             std::to_string(request_.getContentLength() == -1
                                                ? 0
                                                : request_.getContentLength()) +
                             "\r\n"
                             "Connection: close\r\n"
                             "\r\n";
      std::vector<std::byte> response_bytes(
          reinterpret_cast<const std::byte *>(response.data()),
          reinterpret_cast<const std::byte *>(response.data() +
                                              response.size()));
      response_bytes.insert(response_bytes.end(), readBuffer_.begin(),
                            readBuffer_.end());

      writeBuffer_.insert(writeBuffer_.end(), response_bytes.begin(),
                          response_bytes.end());
      SPDLOG_DEBUG("writeBuffer_.size() = {}", writeBuffer_.size());
      handleWritingResponse();
    }
  };

  void handleWritingResponse() {
    if (wantsWrite()) {
      flushWriteBuffer();
    } else {
      state_ = ConnectionState::CLOSING;
    }
  }

  bool receiveData(std::vector<std::byte> &buf) {
    ssize_t n = stream_->receive(buf);

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      buf.resize(0);
      return false;
    }

    if (n <= 0) {
      SPDLOG_INFO("Connection {}:{} closed", stream_->getIp(),
                  stream_->getPort());
      state_ = ConnectionState::CLOSING;
      return false;
    }

    buf.resize(n);
    return true;
  }

  void flushWriteBuffer() {
    if (writeBuffer_.empty())
      return;

    SPDLOG_DEBUG("writing {} bytes", writeBuffer_.size());
    std::string writeBuffer(reinterpret_cast<const char *>(writeBuffer_.data()),
                            writeBuffer_.size());
    SPDLOG_DEBUG("writeBuffer = {}", writeBuffer);

    ssize_t n = stream_->send(std::span<const std::byte>(writeBuffer_));

    if (n < 0) {
      SPDLOG_ERROR("Send error for {}:{}", stream_->getIp(),
                   stream_->getPort());
      state_ = ConnectionState::CLOSING;
      return;
    }

    if (n > 0)
      writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + n);
  }
};
