#pragma once

#include <stdexcept>

struct ServerException : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ThreadPoolFullException : public ServerException {
  using ServerException::ServerException;
};

struct ContentLimitExceededException : public ServerException {
  using ServerException::ServerException;
};

struct BufferLimitExceededException : public ServerException {
  using ServerException::ServerException;
};

struct ConnectionClosedException : public ServerException {
  using ServerException::ServerException;
};

struct ReadTimeOutException : public ServerException {
  using ServerException::ServerException;
};

struct MalformedChunkException : public ServerException {
  using ServerException::ServerException;
};

struct ChunkTooLargeException : public ServerException {
  using ServerException::ServerException;
};

struct RequestSizeLimitExceededException : public ServerException {
  using ServerException::ServerException;
};

struct BodyExhaustedException : public ServerException {
  using ServerException::ServerException;
};
