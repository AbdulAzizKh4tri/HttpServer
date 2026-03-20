#pragma once
#include "Executor.hpp"
#include <coroutine>

struct ReadAwaitable {
  int fd;
  std::chrono::steady_clock::time_point deadline;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept {
    tl_executor->waitForRead(fd, h, deadline);
  }

  void await_resume() const noexcept {}
};

struct WriteAwaitable {
  int fd;
  std::chrono::steady_clock::time_point deadline;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept {
    tl_executor->waitForWrite(fd, h, deadline);
  }

  void await_resume() const noexcept {}
};
