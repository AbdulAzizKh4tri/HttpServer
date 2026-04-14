#pragma once

#include "RuKhExceptions.hpp"
#include "ServerConfig.hpp"
#include "Task.hpp"

using BodyReadFn = std::move_only_function<Task<size_t>(std::vector<unsigned char> &, size_t)>;
using BodyDrainFn = std::move_only_function<Task<void>(size_t)>;

class BodyStream {
public:
  BodyStream(size_t contentLength, BodyReadFn readFn, BodyDrainFn drainFn)
      : remaining_(contentLength), readFn_(std::move(readFn)), drainFn_(std::move(drainFn)) {}

  Task<size_t> read(std::vector<unsigned char> &buf) {
    if (exhausted_)
      co_return 0;
    size_t n = co_await readFn_(buf, remaining_);
    remaining_ -= n;
    if (n == 0 || remaining_ == 0)
      exhausted_ = true;
    co_return n;
  }

  Task<std::string> readAll(size_t limit = ServerConfig::MAX_CONTENT_LENGTH) {
    std::vector<unsigned char> data;
    if (remaining_ > 0)
      data.reserve(remaining_);
    size_t n = 0;

    while (data.size() < limit) {
      n = co_await read(data);
      if (n == 0)
        break;
    }

    if (n > 0)
      throw ContentLimitExceededException("Content limit exceeded");

    co_return std::string(reinterpret_cast<char *>(data.data()), data.size());
  }

  Task<void> drain() {
    if (exhausted_)
      co_return;
    co_await drainFn_(remaining_);
  }

  bool isExhausted() { return exhausted_; }

private:
  bool exhausted_ = false;
  size_t remaining_ = 0;
  BodyReadFn readFn_;
  BodyDrainFn drainFn_;
};
