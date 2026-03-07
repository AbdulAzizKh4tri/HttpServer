#pragma once
#include <charconv>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "HttpRequest.hpp"

enum class ChunkState { READING_SIZE, READING_DATA, READING_TRAILING_CRLF };
enum class ChunkError { MALFORMED, TOO_LARGE };

class ChunkedBodyParser {

public:
  // nullopt = need more data, string = complete body, unexpected = error
  std::expected<std::optional<std::string>, ChunkError>
  feed(std::vector<unsigned char> &buffer) {
    for (;;) {
      std::expected<bool, ChunkError> result = stepOnce(buffer);
      if (!result)
        return std::unexpected(result.error());
      if (!result.value())
        return std::nullopt; // needs more data
      if (done_) {
        std::string body = std::move(assembled_);
        reset();
        return body;
      }
    }
  }

  void reset() {
    state_ = ChunkState::READING_SIZE;
    currentChunkSize_ = 0;
    totalBytesRead_ = 0;
    assembled_.clear();
    done_ = false;
  }

private:
  ChunkState state_ = ChunkState::READING_SIZE;
  size_t currentChunkSize_ = 0;
  size_t totalBytesRead_ = 0;
  std::string assembled_;
  bool done_ = false;

  // returns true = made progress, false = needs more data, unexpected = error
  std::expected<bool, ChunkError> stepOnce(std::vector<unsigned char> &buffer) {
    switch (state_) {
    case ChunkState::READING_SIZE:
      return readChunkSize(buffer);
    case ChunkState::READING_DATA:
      return readChunkData(buffer);
    case ChunkState::READING_TRAILING_CRLF:
      return readTrailingCrlf(buffer);
    }
    std::unreachable();
  }

  std::expected<bool, ChunkError>
  readChunkSize(std::vector<unsigned char> &buffer) {
    std::string data(buffer.begin(), buffer.end());
    size_t end = data.find("\r\n");
    if (end == std::string::npos)
      return false;

    size_t chunkSize = 0;
    auto [ptr, ec] =
        std::from_chars(data.data(), data.data() + end, chunkSize, 16);
    if (ec != std::errc{})
      return std::unexpected(ChunkError::MALFORMED);

    buffer.erase(buffer.begin(), buffer.begin() + end + 2);
    currentChunkSize_ = chunkSize;

    if (currentChunkSize_ == 0) {
      state_ = ChunkState::READING_TRAILING_CRLF;
      done_ = true;
    } else {
      state_ = ChunkState::READING_DATA;
    }

    return true;
  }

  std::expected<bool, ChunkError>
  readChunkData(std::vector<unsigned char> &buffer) {
    if (buffer.size() < currentChunkSize_)
      return false;

    totalBytesRead_ += currentChunkSize_;
    if (totalBytesRead_ > HttpRequest::MAX_CONTENT_LENGTH)
      return std::unexpected(ChunkError::TOO_LARGE);

    assembled_.append(buffer.begin(), buffer.begin() + currentChunkSize_);
    buffer.erase(buffer.begin(), buffer.begin() + currentChunkSize_);

    currentChunkSize_ = 0;
    state_ = ChunkState::READING_TRAILING_CRLF;
    return true;
  }

  std::expected<bool, ChunkError>
  readTrailingCrlf(std::vector<unsigned char> &buffer) {
    if (buffer.size() < 2)
      return false;
    if (buffer[0] != '\r' || buffer[1] != '\n')
      return std::unexpected(ChunkError::MALFORMED);

    buffer.erase(buffer.begin(), buffer.begin() + 2);
    state_ = ChunkState::READING_SIZE;
    return true;
  }
};
