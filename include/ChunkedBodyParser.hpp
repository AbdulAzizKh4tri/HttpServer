#pragma once

#include <expected>
#include <optional>
#include <string>

#include "ConnectionIO.hpp"
#include "HttpRequest.hpp"

enum class ChunkState {
  READING_SIZE,
  READING_DATA,
  READING_TRAILERS,
  READING_TRAILING_CRLF
};
enum class ChunkError { MALFORMED, TOO_LARGE };

class ChunkedBodyParser {
public:
  std::expected<std::optional<std::string>, ChunkError>
  feed(ConnectionIO &io, HttpRequest &request);
  void reset();

private:
  ChunkState state_ = ChunkState::READING_SIZE;
  size_t currentChunkSize_ = 0;
  size_t totalBytesRead_ = 0;
  std::string assembled_;
  bool done_ = false;

  std::expected<bool, ChunkError> stepOnce(ConnectionIO &io,
                                           HttpRequest &request);

  std::expected<bool, ChunkError> readChunkSize(ConnectionIO &io);

  std::expected<bool, ChunkError> readChunkData(ConnectionIO &io);

  std::expected<bool, ChunkError> readTrailers(ConnectionIO &io,
                                               HttpRequest &request);

  std::expected<bool, ChunkError> readTrailingCrlf(ConnectionIO &io);

  void setState(ChunkState state);
};
