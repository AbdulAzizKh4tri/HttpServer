#pragma once

#include <memory>

#include "HttpRequest.hpp"
#include "IStream.hpp"
#include "Task.hpp"

enum class ReadResult {
  DATA,
  CLOSED,
  WOULD_BLOCK,
  BUFFER_LIMIT_EXCEEDED,
  ERROR,
  TIMED_OUT,
};

enum class WriteResult {
  OK,
  ERROR,
  TIMED_OUT,
};

class ConnectionIO {
public:
  ConnectionIO(std::shared_ptr<IStream> stream);

  ReadResult drainIntoReadBuffer(size_t maxBufferSize);

  std::vector<unsigned char>::const_iterator readBufferBegin() const;
  std::vector<unsigned char>::const_iterator readBufferEnd() const;

  std::vector<unsigned char>::const_iterator writeBufferBegin() const;
  std::vector<unsigned char>::const_iterator writeBufferEnd() const;

  bool flushFromWriteBuffer();

  Task<ReadResult> read(std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
                        size_t maxBufferSize = HttpRequest::MAX_CONTENT_LENGTH);

  Task<WriteResult> write(int inactivitySeconds = 0);

  void enqueue(std::vector<unsigned char> data);

  bool hasPendingWrites() const;

  void eraseFromReadBuffer(size_t n);

  void eraseFromWriteBuffer(size_t n);

  std::string getReadBufferString(int end = -1) const;

  std::string getWriteBufferString(int end = -1) const;

  const unsigned char *readBufferData() const;

  size_t getReadOffset() const;
  size_t getWriteOffset() const;

  size_t getReadBufferSize() const;
  size_t getWriteBufferSize() const;

  std::vector<unsigned char> &getReadBuffer();
  std::vector<unsigned char> &getWriteBuffer();

  std::string getIp() const;
  uint16_t getPort() const;
  int getFd() const;
  HandshakeResult handshake();

private:
  std::shared_ptr<IStream> stream_;
  size_t readOffset_ = 0, writeOffset_ = 0;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
};
