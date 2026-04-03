#pragma once

#include <coroutine>

#include "Executor.hpp"

template <typename R> struct TaskState {
  std::optional<R> result;
  std::exception_ptr exception;
  std::coroutine_handle<> caller;
  Executor *executor = nullptr;
  std::atomic<bool> done = false;
  std::atomic<bool> callerSet = false;
};

template <> struct TaskState<void> {
  std::exception_ptr exception;
  std::coroutine_handle<> caller;
  Executor *executor = nullptr;
  std::atomic<bool> done = false;
  std::atomic<bool> callerSet = false;
};
