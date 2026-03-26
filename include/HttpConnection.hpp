#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include "ChunkedBodyParser.hpp"
#include "ConnectionIO.hpp"
#include "ErrorFactory.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "HttpTypes.hpp"
#include "IStream.hpp"
#include "Router.hpp"

class HttpConnection {
public:
  HttpConnection(std::shared_ptr<IStream> stream, Router &router, ErrorFactory &errorFactory,
                 std::atomic<bool> &shutdown);

  HttpConnection(const HttpConnection &) = delete;
  HttpConnection &operator=(const HttpConnection &) = delete;

  Task<void> run();

  int getFd() const;
  std::string getIp() const;
  uint16_t getPort() const;

private:
  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

  ConnectionIO io_;
  ChunkedBodyParser chunkParser_;

  bool keepAlive_ = true;
  std::atomic<bool> &shutdown_;

  bool formationArmed_ = false;
  std::chrono::steady_clock::time_point inactivityDeadline_ = std::chrono::steady_clock::time_point::max();
  std::chrono::steady_clock::time_point formationDeadline_ = std::chrono::steady_clock::time_point::max();

  void resetInactivity();

  std::chrono::steady_clock::time_point activeDeadline() const;

  bool shouldKeepAlive() const;

  Task<bool> handshake();

  Task<bool> readHeaders();

  Task<bool> handleExpectHeader();

  Task<bool> readBody();

  Task<bool> handleReadingBodyChunked();

  Task<bool> handleReadingBodyContentLength();

  Task<bool> generateAndSendResponse();

  Task<bool> sendStreamResponse(HttpStreamResponse responseStream);

  Task<bool> serializeAndSendResponse(HttpResponse response);

  Task<void> sendErrorResponseAndClose(int statusCode, const std::string &message = "");

  Task<Response> generateResponse();

  HttpResponse buildErrorResponse(int statusCode, const std::string &message = "");

  void resetForNextRequest();
};
