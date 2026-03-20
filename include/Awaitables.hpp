#pragma once
#include "Executor.hpp"
#include <coroutine>

struct ReadAwaitable {
  int fd;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept {
    tl_executor->waitForRead(fd, h);
  }

  void await_resume() const noexcept {}
};

struct WriteAwaitable {
  int fd;

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> h) const noexcept {
    tl_executor->waitForWrite(fd, h);
  }

  void await_resume() const noexcept {}
};
