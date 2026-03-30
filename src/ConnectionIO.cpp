#include "ConnectionIO.hpp"

#include <spdlog/spdlog.h>

#include "Awaitables.hpp"
#include "ExecutorContext.hpp"
#include "IStream.hpp"
#include "Task.hpp"
#include "utils.hpp"

ConnectionIO::ConnectionIO(std::shared_ptr<IStream> stream) : stream_(std::move(stream)) {}

ReadResult ConnectionIO::drainIntoReadBuffer(size_t maxBufferSize) {
  bool gotData = false;

  for (;;) {
    size_t currentSize = getReadBufferSize();
    if (currentSize >= maxBufferSize)
      return ReadResult::BUFFER_LIMIT_EXCEEDED;

    size_t spaceToAllow = std::min(maxBufferSize - currentSize, size_t{4096});
    size_t oldSize = readBuffer_.size();
    readBuffer_.resize(oldSize + spaceToAllow);

    auto span = std::span<unsigned char>(readBuffer_.data() + oldSize, spaceToAllow);
    ReceiveResult result = stream_->receive(span);

    switch (result.status) {
    case ReceiveResult::Status::DATA:
      readBuffer_.resize(oldSize + result.bytes);
      gotData = true;
      break;
    case ReceiveResult::Status::WOULD_BLOCK:
      readBuffer_.resize(oldSize);
      return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;
    case ReceiveResult::Status::CLOSED:
      SPDLOG_DEBUG("Connection closed by peer, {}:{}", stream_->getIp(), stream_->getPort());
      return ReadResult::CLOSED;
    case ReceiveResult::Status::ERROR:
      SPDLOG_ERROR("Receive error for {}:{}", stream_->getIp(), stream_->getPort());
      return ReadResult::ERROR;
    }
  }
}

std::vector<unsigned char>::const_iterator ConnectionIO::readBufferBegin() const {
  return readBuffer_.begin() + readOffset_;
}

std::vector<unsigned char>::const_iterator ConnectionIO::readBufferEnd() const { return readBuffer_.end(); }

std::vector<unsigned char>::const_iterator ConnectionIO::writeBufferBegin() const {
  return writeBuffer_.begin() + writeOffset_;
}
std::vector<unsigned char>::const_iterator ConnectionIO::writeBufferEnd() const { return writeBuffer_.end(); }

bool ConnectionIO::flushFromWriteBuffer() {
  while (hasPendingWrites()) {
    ssize_t n = stream_->send(std::span(writeBufferBegin(), writeBufferEnd()));
    if (n < 0)
      return false; // error
    if (n == 0) {
      return true;
    }

    eraseFromWriteBuffer(n);
  }
  return true;
}

Task<ReadResult> ConnectionIO::read(std::chrono::steady_clock::time_point deadline, size_t maxBufferSize) {
  ReadResult result = drainIntoReadBuffer(maxBufferSize);
  if (result == ReadResult::WOULD_BLOCK) {
    co_await ReadAwaitable{getFd(), deadline};
    if (tl_timed_out) {
      tl_timed_out = false;
      co_return ReadResult::TIMED_OUT;
    }
    result = drainIntoReadBuffer(maxBufferSize);
  }
  co_return result;
}

Task<WriteResult> ConnectionIO::write(int inactivitySeconds) {
  while (hasPendingWrites()) {
    if (not flushFromWriteBuffer())
      co_return WriteResult::ERROR;

    if (hasPendingWrites()) {
      std::chrono::steady_clock::time_point deadline = inactivitySeconds
                                                           ? now() + std::chrono::seconds(inactivitySeconds)
                                                           : std::chrono::steady_clock::time_point::max();
      co_await WriteAwaitable{getFd(), deadline};
      if (tl_timed_out) {
        tl_timed_out = false;
        co_return WriteResult::TIMED_OUT;
      }
    }
  }
  co_return WriteResult::OK;
}

void ConnectionIO::enqueue(std::vector<unsigned char> data) {
  writeBuffer_.insert(writeBuffer_.end(), data.begin(), data.end());
}

bool ConnectionIO::hasPendingWrites() const { return getWriteBufferSize() > 0; }

void ConnectionIO::eraseFromReadBuffer(size_t n) {
  readOffset_ += n;

  if (readOffset_ > readBuffer_.size() / 2) {
    readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + readOffset_);
    readOffset_ = 0;
  }
};

void ConnectionIO::eraseFromWriteBuffer(size_t n) {
  writeOffset_ += n;

  if (writeOffset_ > writeBuffer_.size() / 2) {
    writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + writeOffset_);
    writeOffset_ = 0;
  }
};

std::string ConnectionIO::getReadBufferString(int end) const {
  if (end < 0)
    return std::string(readBuffer_.begin() + readOffset_, readBuffer_.end());
  else
    return std::string(readBuffer_.begin() + readOffset_, readBuffer_.begin() + readOffset_ + end);
}

std::string ConnectionIO::getWriteBufferString(int end) const {
  if (end < 0)
    return std::string(writeBuffer_.begin() + writeOffset_, writeBuffer_.end());
  else
    return std::string(writeBuffer_.begin() + writeOffset_, writeBuffer_.begin() + writeOffset_ + end);
}

const unsigned char *ConnectionIO::readBufferData() const { return readBuffer_.data() + readOffset_; }
size_t ConnectionIO::getReadOffset() const { return readOffset_; }
size_t ConnectionIO::getWriteOffset() const { return writeOffset_; }

size_t ConnectionIO::getReadBufferSize() const { return readBuffer_.size() - readOffset_; }
size_t ConnectionIO::getWriteBufferSize() const { return writeBuffer_.size() - writeOffset_; }

std::vector<unsigned char> &ConnectionIO::getReadBuffer() { return readBuffer_; }
std::vector<unsigned char> &ConnectionIO::getWriteBuffer() { return writeBuffer_; }

std::string ConnectionIO::getIp() const { return stream_->getIp(); };
uint16_t ConnectionIO::getPort() const { return stream_->getPort(); };
int ConnectionIO::getFd() const { return stream_->getFd(); };
HandshakeResult ConnectionIO::handshake() { return stream_->handshake(); }
