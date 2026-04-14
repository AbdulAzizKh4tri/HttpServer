#pragma once

#include "ConnectionIO.hpp"
#include "RuKhExceptions.hpp"
#include "ServerConfig.hpp"

enum class ChunkError { MALFORMED, CHUNK_TOO_LARGE, REQUEST_SIZE_LIMIT_EXCEEDED };

template <typename Stream> class ChunkDecoder {
public:
  Task<std::expected<std::string, ChunkError>> getNextChunk(ConnectionIO<Stream> &io) {
    if (done_)
      co_return "";

    { // Reading chunk size
      auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(), crlf.end());
      while (it == io.readBufferEnd()) {
        auto inactivityDeadline_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
        auto readResult = co_await io.read(inactivityDeadline_);
        if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
          throw BufferLimitExceededException("Buffer limit exceeded");
        } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
          throw ConnectionClosedException("Connection closed");
        } else if (readResult == ReadResult::TIMED_OUT) {
          throw ReadTimeOutException("Read timed out");
        }
        it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(), crlf.end());
      }

      size_t chunkSize = 0;
      auto [ptr, ec] = std::from_chars(reinterpret_cast<const char *>(io.readBufferData()),
                                       reinterpret_cast<const char *>(&*it), chunkSize, 16);

      if (ec != std::errc{})
        co_return std::unexpected(ChunkError::MALFORMED);

      io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 2);
      chunkRemaining_ = chunkSize;
      done_ = chunkSize == 0;

      if (chunkRemaining_ > ServerConfig::MAX_TE_CHUNK_LENGTH)
        co_return std::unexpected(ChunkError::CHUNK_TOO_LARGE);

      totalBytes_ += chunkRemaining_;
      if (totalBytes_ > ServerConfig::MAX_TE_LENGTH)
        co_return std::unexpected(ChunkError::REQUEST_SIZE_LIMIT_EXCEEDED);
    }

    std::string chunk = "";
    { // Reading chunk body
      while (io.getReadBufferSize() < chunkRemaining_) {
        auto inactivityDeadline_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
        auto readResult = co_await io.read(inactivityDeadline_);
        if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
          throw BufferLimitExceededException("Buffer limit exceeded");
        } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
          throw ConnectionClosedException("Connection closed");
        } else if (readResult == ReadResult::TIMED_OUT) {
          throw ReadTimeOutException("Read timed out");
        }
      }

      chunk = std::string(reinterpret_cast<const char *>(io.readBufferData()),
                          reinterpret_cast<const char *>(io.readBufferData() + chunkRemaining_));
      io.eraseFromReadBuffer(chunkRemaining_);
      chunkRemaining_ = 0;
    }

    if (not done_) { // Reading trailing CRLF (Registered Nurse @primeagen)
      while (io.getReadBufferSize() < 2) {
        auto inactivityDeadline_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
        auto readResult = co_await io.read(inactivityDeadline_);
        if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
          throw BufferLimitExceededException("Buffer limit exceeded");
        } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
          throw ConnectionClosedException("Connection closed");
        } else if (readResult == ReadResult::TIMED_OUT) {
          throw ReadTimeOutException("Read timed out");
        }
      }

      if (io.readBufferData()[0] != '\r' || io.readBufferData()[1] != '\n')
        co_return std::unexpected(ChunkError::MALFORMED);

      io.eraseFromReadBuffer(2);
    }

    if (done_) {
      if (io.getReadBufferSize() >= 2 && io.readBufferData()[0] == '\r' && io.readBufferData()[1] == '\n') {
        io.eraseFromReadBuffer(2);
        co_return chunk;
      }

      // search for \r\n\r\n — end of trailer section
      auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf2.begin(), crlf2.end());
      while (it == io.readBufferEnd()) {
        auto inactivityDeadline_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
        auto readResult = co_await io.read(inactivityDeadline_);
        if (readResult == ReadResult::BUFFER_LIMIT_EXCEEDED) {
          throw BufferLimitExceededException("Buffer limit exceeded");
        } else if (readResult == ReadResult::CLOSED || readResult == ReadResult::ERROR) {
          throw ConnectionClosedException("Connection closed");
        } else if (readResult == ReadResult::TIMED_OUT) {
          throw ReadTimeOutException("Read timed out");
        }
        it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf2.begin(), crlf2.end());
      }

      trailers_ =
          std::string(reinterpret_cast<const char *>(io.readBufferData()), std::distance(io.readBufferBegin(), it));
      io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 4);
    }

    co_return chunk;
  }

  std::string getTrailers() { return trailers_; }
  bool isDone() { return done_; }

  void reset() {
    chunkRemaining_ = 0;
    totalBytes_ = 0;
    done_ = false;
    trailers_.clear();
  }

private:
  size_t chunkRemaining_ = 0;
  size_t totalBytes_ = 0;
  bool done_ = false;
  std::string trailers_;
};
