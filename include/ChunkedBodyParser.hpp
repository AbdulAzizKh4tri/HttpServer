#pragma once

#include <expected>
#include <optional>
#include <string>

#include "ConnectionIO.hpp"
#include "HttpRequest.hpp"

enum class ChunkState { READING_SIZE, READING_DATA, READING_TRAILERS, READING_TRAILING_CRLF };
enum class ChunkError { MALFORMED, TOO_LARGE };

template <typename Stream> class ChunkedBodyParser {
public:
  std::expected<std::optional<std::string>, ChunkError> feed(ConnectionIO<Stream> &io, HttpRequest &request) {
    for (;;) {
      std::expected<bool, ChunkError> result = stepOnce(io, request);
      if (!result)
        return std::unexpected(result.error());
      if (!result.value())
        return std::nullopt;
      if (done_) {
        std::string body = std::move(assembled_);
        reset();
        return body;
      }
    }
  }

  void reset() {
    setState(ChunkState::READING_SIZE);
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

  std::expected<bool, ChunkError> stepOnce(ConnectionIO<Stream> &io, HttpRequest &request) {
    switch (state_) {
    case ChunkState::READING_SIZE:
      return readChunkSize(io);
    case ChunkState::READING_DATA:
      return readChunkData(io);
    case ChunkState::READING_TRAILING_CRLF:
      return readTrailingCrlf(io);
    case ChunkState::READING_TRAILERS:
      return readTrailers(io, request);
    }
    std::unreachable();
  }

  std::expected<bool, ChunkError> readChunkSize(ConnectionIO<Stream> &io) {
    auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(), crlf.end());
    if (it == io.readBufferEnd())
      return false;

    size_t chunkSize = 0;
    auto [ptr, ec] = std::from_chars(reinterpret_cast<const char *>(io.readBufferData()),
                                     reinterpret_cast<const char *>(&*it), chunkSize, 16);

    if (ec != std::errc{})
      return std::unexpected(ChunkError::MALFORMED);

    io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 2);
    currentChunkSize_ = chunkSize;

    if (currentChunkSize_ == 0) {
      setState(ChunkState::READING_TRAILERS); // CHANGED — trailers before done
    } else {
      setState(ChunkState::READING_DATA);
    }
    return true;
  }

  std::expected<bool, ChunkError> readChunkData(ConnectionIO<Stream> &io) {
    if (io.getReadBufferSize() < currentChunkSize_)
      return false;

    totalBytesRead_ += currentChunkSize_;
    if (totalBytesRead_ > HttpRequest::MAX_CONTENT_LENGTH)
      return std::unexpected(ChunkError::TOO_LARGE);

    assembled_.append(io.readBufferBegin(), io.readBufferBegin() + currentChunkSize_);
    io.eraseFromReadBuffer(currentChunkSize_);
    currentChunkSize_ = 0;
    setState(ChunkState::READING_TRAILING_CRLF);
    return true;
  }

  std::expected<bool, ChunkError> readTrailingCrlf(ConnectionIO<Stream> &io) {
    if (io.getReadBufferSize() < 2)
      return false;

    if (io.readBufferData()[0] != '\r' || io.readBufferData()[1] != '\n')
      return std::unexpected(ChunkError::MALFORMED);

    io.eraseFromReadBuffer(2);
    setState(ChunkState::READING_SIZE);
    return true;
  }

  std::expected<bool, ChunkError> readTrailers(ConnectionIO<Stream> &io, HttpRequest &request) {
    // no trailers — just \r\n terminator
    if (io.getReadBufferSize() >= 2 && io.readBufferData()[0] == '\r' && io.readBufferData()[1] == '\n') {
      io.eraseFromReadBuffer(2);
      done_ = true;
      return true;
    }

    // search for \r\n\r\n — end of trailer section
    auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf2.begin(), crlf2.end());
    if (it == io.readBufferEnd())
      return false;

    // parse whatever is between current position and \r\n\r\n as headers
    std::string_view trailerView(reinterpret_cast<const char *>(io.readBufferData()),
                                 std::distance(io.readBufferBegin(), it));

    while (!trailerView.empty()) {
      auto lineEnd = trailerView.find('\n');
      auto line = lineEnd == std::string_view::npos ? trailerView : trailerView.substr(0, lineEnd);
      trailerView.remove_prefix(lineEnd == std::string_view::npos ? trailerView.size() : lineEnd + 1);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.empty())
        continue;
      auto pos = line.find(':');
      if (pos == std::string_view::npos)
        continue;
      auto key = line.substr(0, pos);
      auto value = line.substr(pos + 1);
      trim(value);
      request.addHeader(std::string(key), std::string(value));
    }

    io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 4);
    done_ = true; // CHANGED — done only after trailers consumed
    return true;
  }

  void setState(ChunkState state) {
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
    case ChunkState::READING_TRAILERS:
      SPDLOG_TRACE("ChunkState set to Reading Trailers");
      break;
    }
  }
};
