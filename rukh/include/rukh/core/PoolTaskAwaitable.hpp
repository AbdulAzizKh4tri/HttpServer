#pragma once

#include <rukh/core/TaskState.hpp>

namespace rukh {

template <typename R> struct PoolTaskAwaitable {
  std::shared_ptr<TaskState<R>> state;
  bool awaited_ = false;

  PoolTaskAwaitable(std::shared_ptr<TaskState<R>> s) : state(std::move(s)) {}

  PoolTaskAwaitable(const PoolTaskAwaitable &) = delete;
  PoolTaskAwaitable &operator=(const PoolTaskAwaitable &) = delete;

  PoolTaskAwaitable(PoolTaskAwaitable &&other) noexcept : state(std::move(other.state)), awaited_(other.awaited_) {
    other.awaited_ = true;
  }

  PoolTaskAwaitable &operator=(PoolTaskAwaitable &&other) noexcept {
    if (this == &other)
      return *this;
    if (!awaited_)
      SPDLOG_WARN("submit() result overriden without being awaited");
    state = std::move(other.state);
    awaited_ = other.awaited_;
    other.awaited_ = true;
    return *this;
  }

  ~PoolTaskAwaitable() {
    if (!awaited_)
      SPDLOG_WARN("submit() called without being awaited - use fireAndForget() instead");
  }

  bool await_ready() noexcept {
    awaited_ = true;
    return state->done;
  }

  void await_suspend(std::coroutine_handle<> h) noexcept {
    state->caller = h;
    if (state->callerSet.exchange(true)) {
      state->executor->post(h);
    }
  }

  auto await_resume() {
    if (state->exception)
      std::rethrow_exception(state->exception);
    if constexpr (not std::is_void_v<R>)
      return std::move(*state->result);
  }
};
} // namespace rukh
