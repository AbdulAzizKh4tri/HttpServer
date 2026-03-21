#pragma once

#include <chrono>
#include <coroutine>
#include <queue>

#include "EpollInstance.hpp"
#include "ExecutorContext.hpp"
#include "Task.hpp"

class Executor {

  struct ReadyTask {
    std::coroutine_handle<> task;
    bool timedOut;
  };

public:
  static constexpr int EPOLL_WAIT_TIMEOUT = 1;

  Executor();
  ~Executor() = default;

  void spawn(Task<void> task);

  void unregister(int fd);

  void waitForRead(int fd, std::coroutine_handle<> caller,
                   std::chrono::steady_clock::time_point deadline);

  void waitForWrite(int fd, std::coroutine_handle<> caller,
                    std::chrono::steady_clock::time_point deadline);

  void run();

private:
  EpollInstance epoll_;
  std::vector<Task<void>> ownedTasks_;
  std::queue<ReadyTask> readyQueue_;
  std::unordered_map<int, std::coroutine_handle<>> suspendedTasks_;
  std::unordered_map<int, std::chrono::steady_clock::time_point> deadlines_;
};
