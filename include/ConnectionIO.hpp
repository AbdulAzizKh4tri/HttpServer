#pragma once

#include <memory>
#include <spdlog/spdlog.h>

#include "HttpRequest.hpp"
#include "IStream.hpp"

enum class ReadResult { DATA, CLOSED, WOULD_BLOCK, ERROR };

class ConnectionIO {
public:
  ConnectionIO(std::shared_ptr<IStream> stream) : stream_(std::move(stream)) {}

  ReadResult
  drainIntoReadBuffer(size_t maxBufferSize = HttpRequest::MAX_CONTENT_LENGTH) {
    bool gotData = false;
    for (;;) {
      if (readBuffer_.size() >= maxBufferSize)
        return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;

      std::vector<unsigned char> buf(4096);
      ReceiveResult result = stream_->receive(buf);

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
        return ReadResult::CLOSED;
      case ReceiveResult::Status::ERROR:
        SPDLOG_ERROR("Receive error for {}:{}", stream_->getIp(),
                     stream_->getPort());
        return ReadResult::ERROR;
      }
    }
  }

  bool flushFromWriteBuffer() {
    while (!writeBuffer_.empty()) {
      ssize_t n = stream_->send(writeBuffer_);
      if (n < 0)
        return false; // error
      if (n == 0)
        return true; // would block, try later
      writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + n);
    }
    return true;
  }

  void enqueue(std::vector<unsigned char> data) {
    writeBuffer_.insert(writeBuffer_.end(), data.begin(), data.end());
  }

  bool hasPendingWrites() const { return !writeBuffer_.empty(); }

  std::vector<unsigned char> &readBuffer() { return readBuffer_; }

  void eraseFromReadBuffer(size_t n) {
    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + n);
  };

  std::string getReadBufferString(int end = -1) const {
    if (end < 0)
      return std::string(readBuffer_.begin(), readBuffer_.end());
    else
      return std::string(readBuffer_.begin(), readBuffer_.begin() + end);
  }

  size_t getReadBufferSize() const { return readBuffer_.size(); }

  std::string getIp() const { return stream_->getIp(); };
  uint16_t getPort() const { return stream_->getPort(); };
  int getFd() const { return stream_->getFd(); };
  HandshakeResult handshake() { return stream_->handshake(); }

private:
  std::shared_ptr<IStream> stream_;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
};
