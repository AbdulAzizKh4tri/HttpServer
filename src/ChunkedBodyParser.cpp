#include "ChunkedBodyParser.hpp"

#include <charconv>
#include <expected>
#include <optional>
#include <string>

#include "ConnectionIO.hpp"
#include "HttpRequest.hpp"

#include "utils.hpp"

// nullopt = need more data, string = complete body, unexpected = error
std::expected<std::optional<std::string>, ChunkError>
ChunkedBodyParser::feed(ConnectionIO &io) {
  for (;;) {
    std::expected<bool, ChunkError> result = stepOnce(io);
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

void ChunkedBodyParser::reset() {
  setState(ChunkState::READING_SIZE);
  currentChunkSize_ = 0;
  totalBytesRead_ = 0;
  assembled_.clear();
  done_ = false;
}

// returns true = made progress, false = needs more data, unexpected = error
std::expected<bool, ChunkError> ChunkedBodyParser::stepOnce(ConnectionIO &io) {
  switch (state_) {
  case ChunkState::READING_SIZE:
    return readChunkSize(io);
  case ChunkState::READING_DATA:
    return readChunkData(io);
  case ChunkState::READING_TRAILING_CRLF:
    return readTrailingCrlf(io);
  }
  std::unreachable();
}

std::expected<bool, ChunkError>
ChunkedBodyParser::readChunkSize(ConnectionIO &io) {
  auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(),
                        crlf.end());

  if (it == io.readBufferEnd())
    return false;

  size_t chunkSize = 0;
  auto [ptr, ec] =
      std::from_chars(reinterpret_cast<const char *>(io.readBufferData()),
                      reinterpret_cast<const char *>(&*it), chunkSize, 16);

  if (ec != std::errc{})
    return std::unexpected(ChunkError::MALFORMED);

  io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 2);

  currentChunkSize_ = chunkSize;

  if (currentChunkSize_ == 0) {
    setState(ChunkState::READING_TRAILING_CRLF);
    done_ = true;
  } else {
    setState(ChunkState::READING_DATA);
  }

  return true;
}

std::expected<bool, ChunkError>
ChunkedBodyParser::readChunkData(ConnectionIO &io) {
  if (io.getReadBufferSize() < currentChunkSize_)
    return false;

  totalBytesRead_ += currentChunkSize_;
  if (totalBytesRead_ > HttpRequest::MAX_CONTENT_LENGTH)
    return std::unexpected(ChunkError::TOO_LARGE);

  assembled_.append(io.readBufferBegin(),
                    io.readBufferBegin() + currentChunkSize_);

  io.eraseFromReadBuffer(currentChunkSize_);

  currentChunkSize_ = 0;
  setState(ChunkState::READING_TRAILING_CRLF);
  return true;
}

std::expected<bool, ChunkError>
ChunkedBodyParser::readTrailingCrlf(ConnectionIO &io) {
  if (io.getReadBufferSize() < 2)
    return false;

  if (io.readBufferData()[0] != '\r' || io.readBufferData()[1] != '\n') {
    return std::unexpected(ChunkError::MALFORMED);
  }

  io.eraseFromReadBuffer(2);
  setState(ChunkState::READING_SIZE);
  return true;
}

void ChunkedBodyParser::setState(ChunkState state) {
  state_ = state;

  switch (state_) {
  case ChunkState::READING_SIZE:
    SPDLOG_TRACE("ChunkState set to Reading Size");
    break;
  case ChunkState::READING_DATA:
    SPDLOG_TRACE("ChunkState set to Reading Data");
    break;
  case ChunkState::READING_TRAILING_CRLF:
    SPDLOG_TRACE("ChunkState set to Reading Trailing CRLF");
    break;
  }
}
