#pragma once

#include "IPoolTask.hpp"
#include "TaskState.hpp"

template <typename F, typename R> struct PoolTask : IPoolTask {
  F callable;
  std::shared_ptr<TaskState<R>> state;
  std::shared_ptr<PoolTask> self;

  PoolTask(F f, std::shared_ptr<TaskState<R>> s) : callable(std::move(f)), state(std::move(s)) {}

  PoolTask(const PoolTask &) = delete;
  PoolTask &operator=(const PoolTask &) = delete;

  void runTask() override {
    try {
      if constexpr (std::is_void_v<R>)
        callable();
      else
        state->result = callable();
    } catch (...) {
      state->exception = std::current_exception();
    }
    state->done = true;
    if (state->executor && state->callerSet.exchange(true)) {
      state->executor->post(state->caller);
    }
    self.reset();
  }
};
