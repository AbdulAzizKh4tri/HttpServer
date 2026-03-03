#pragma once

#include "IStream.hpp"
#include <memory>
#include <spdlog/spdlog.h>

enum class ConnectionState {
  HANDSHAKING,
  ECHOING,
  READING_HEADERS,
  READING_BODY,
  WRITING_RESPONSE,
  CLOSING
};

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream)
      : stream_(std::move(stream)), state_(ConnectionState::HANDSHAKING) {}

  void onReadable() {
    switch (state_) {
    case ConnectionState::HANDSHAKING:
      handleHandshake();
      break;
    case ConnectionState::ECHOING:
      handleEcho();
      break;
    case ConnectionState::READING_HEADERS:
    case ConnectionState::READING_BODY:
    case ConnectionState::WRITING_RESPONSE:
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
    flushWriteBuffer();
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

  void handleHandshake() {
    handshakeWantsWrite_ = false;

    HandshakeResult result = stream_->handshake();
    switch (result) {
    case HandshakeResult::DONE:
      SPDLOG_INFO("TLS handshake complete for {}:{}", stream_->getIp(),
                  stream_->getPort());
      state_ = ConnectionState::ECHOING;
      break;

    case HandshakeResult::NO_TLS:
      SPDLOG_INFO("No TLS handshake was needed for stream {}:{}",
                  stream_->getIp(), stream_->getPort());
      state_ = ConnectionState::ECHOING;
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

  void handleEcho() {
    std::vector<std::byte> buf(4096);
    ssize_t n = stream_->receive(buf);

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return;

    if (n <= 0) {
      SPDLOG_INFO("Connection {}:{} closed", stream_->getIp(),
                  stream_->getPort());
      state_ = ConnectionState::CLOSING;
      return;
    }

    buf.resize(n);
    SPDLOG_DEBUG("Echoing {} bytes to {}:{}", n, stream_->getIp(),
                 stream_->getPort());

    writeBuffer_.insert(writeBuffer_.end(), buf.begin(), buf.end());
    flushWriteBuffer();
  }

  void flushWriteBuffer() {
    if (writeBuffer_.empty())
      return;

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
