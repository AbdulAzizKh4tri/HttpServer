#pragma once

#include <memory>
#include <spdlog/spdlog.h>

#include "Awaitables.hpp"
#include "HttpRequest.hpp"
#include "IStream.hpp"
#include "Task.hpp"

enum class ReadResult {
  DATA,
  CLOSED,
  WOULD_BLOCK,
  BUFFER_LIMIT_EXCEEDED,
  ERROR
};

class ConnectionIO {
public:
  ConnectionIO(std::shared_ptr<IStream> stream) : stream_(std::move(stream)) {}

  ReadResult drainIntoReadBuffer(size_t maxBufferSize) {
    bool gotData = false;
    for (;;) {
      if (getReadBufferSize() >= maxBufferSize)
        return ReadResult::BUFFER_LIMIT_EXCEEDED;

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

  auto readBufferBegin() const { return readBuffer_.begin() + readOffset_; }
  auto readBufferEnd() const { return readBuffer_.end(); }

  auto writeBufferBegin() const { return writeBuffer_.begin() + writeOffset_; }
  auto writeBufferEnd() const { return writeBuffer_.end(); }

  bool flushFromWriteBuffer() {
    while (hasPendingWrites()) {
      ssize_t n =
          stream_->send(std::span(writeBufferBegin(), writeBufferEnd()));
      if (n < 0)
        return false; // error
      if (n == 0) {
        return true;
      }

      eraseFromWriteBuffer(n);
    }
    return true;
  }

  Task<ReadResult>
  read(size_t maxBufferSize = HttpRequest::MAX_CONTENT_LENGTH) {
    ReadResult result = drainIntoReadBuffer(maxBufferSize);
    if (result == ReadResult::WOULD_BLOCK) {
      co_await ReadAwaitable{getFd()};
      result = drainIntoReadBuffer(maxBufferSize);
    }
    co_return result;
  }

  Task<bool> write() {
    while (hasPendingWrites()) {
      if (!flushFromWriteBuffer())
        co_return false;
      if (hasPendingWrites())
        co_await WriteAwaitable{getFd()};
    }
    co_return true;
  }

  void enqueue(std::vector<unsigned char> data) {
    writeBuffer_.insert(writeBuffer_.end(), data.begin(), data.end());
  }

  bool hasPendingWrites() const { return getWriteBufferSize() > 0; }

  void eraseFromReadBuffer(size_t n) {
    readOffset_ += n;

    if (readOffset_ > readBuffer_.size() / 2) {
      readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + readOffset_);
      readOffset_ = 0;
    }
  };

  void eraseFromWriteBuffer(size_t n) {
    writeOffset_ += n;

    if (writeOffset_ > writeBuffer_.size() / 2) {
      writeBuffer_.erase(writeBuffer_.begin(),
                         writeBuffer_.begin() + writeOffset_);
      writeOffset_ = 0;
    }
  };

  std::string getReadBufferString(int end = -1) const {
    if (end < 0)
      return std::string(readBuffer_.begin() + readOffset_, readBuffer_.end());
    else
      return std::string(readBuffer_.begin() + readOffset_,
                         readBuffer_.begin() + readOffset_ + end);
  }

  std::string getWriteBufferString(int end = -1) const {
    if (end < 0)
      return std::string(writeBuffer_.begin() + writeOffset_,
                         writeBuffer_.end());
    else
      return std::string(writeBuffer_.begin() + writeOffset_,
                         writeBuffer_.begin() + writeOffset_ + end);
  }

  const unsigned char *readBufferData() const {
    return readBuffer_.data() + readOffset_;
  }
  size_t getReadOffset() const { return readOffset_; }
  size_t getWriteOffset() const { return writeOffset_; }

  size_t getReadBufferSize() const { return readBuffer_.size() - readOffset_; }
  size_t getWriteBufferSize() const {
    return writeBuffer_.size() - writeOffset_;
  }

  std::string getIp() const { return stream_->getIp(); };
  uint16_t getPort() const { return stream_->getPort(); };
  int getFd() const { return stream_->getFd(); };
  HandshakeResult handshake() { return stream_->handshake(); }

private:
  std::shared_ptr<IStream> stream_;
  size_t readOffset_ = 0, writeOffset_ = 0;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
};
