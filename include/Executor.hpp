#pragma once

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <queue>
#include <unordered_map>

#include "EpollInstance.hpp"
#include "ExecutorContext.hpp"
#include "IoUringInstance.hpp"
#include "Task.hpp"

class Executor {
  struct SuspendedTask {
    std::coroutine_handle<> handle;
    bool waitingForWrite;
    int suspensionSeq;
  };

  struct TaskDeadline {
    std::chrono::steady_clock::time_point deadline;
    int fd;
    int suspensionSeq;

    bool operator>(const TaskDeadline &other) const { return deadline > other.deadline; }
  };

  struct ReadyTask {
    std::coroutine_handle<> task;
    bool timedOut;
  };

public:
  Executor();
  ~Executor() = default;

  void spawn(Task<void> task);

  void unregister(int fd);

  void registerReadOnlyFd(int fd);
  void registerFd(int fd);

  void waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  void waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline);

  void submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  void submitFileWrite(int fd, const void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr, uint64_t offset);

  void run(std::atomic<bool> &shutdown);

  void post(std::coroutine_handle<> h);

private:
  EpollInstance epoll_;
  std::vector<Task<void>> ownedTasks_;
  std::unordered_map<void *, size_t> ownedTaskMap_;
  std::queue<ReadyTask> readyQueue_;

  int nextSeq_ = 0;
  std::unordered_map<int, SuspendedTask> suspendedTasks_;
  std::priority_queue<TaskDeadline, std::vector<TaskDeadline>, std::greater<TaskDeadline>> taskDeadlines_;

  IoUringInstance ioUring_;
  uint64_t nextUserData_ = 0;
  std::unordered_map<uint64_t, std::pair<std::coroutine_handle<>, int *>> pendingFileOps_;

  int eventFd_;
  std::mutex poolResumeQueueMutex_;
  std::queue<std::coroutine_handle<>> poolResumeQueue_;
};
