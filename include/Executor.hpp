#pragma once

#include <coroutine>
#include <queue>

#include "EpollInstance.hpp"
#include "ExecutorContext.hpp"
#include "Task.hpp"

class Executor {
public:
  Executor() {}
  ~Executor() = default;

  void spawn(Task<void> task) {
    readyQueue_.push(task.handle());
    ownedTasks_.push_back(std::move(task)); // executor now owns it
  }

  void unregister(int fd) {
    epoll_.remove(fd);
    suspendedTasks_.erase(fd);
  }

  void waitForRead(int fd, std::coroutine_handle<> caller) {
    suspendedTasks_[fd] = caller;
    epoll_.addOrModify(fd, EPOLLIN | EPOLLET, fd);
  }

  void waitForWrite(int fd, std::coroutine_handle<> caller) {
    suspendedTasks_[fd] = caller;
    epoll_.addOrModify(fd, EPOLLOUT | EPOLLET | EPOLLIN, fd);
  }

  void run() {
    tl_executor = this;
    for (;;) {
      while (!readyQueue_.empty()) {
        auto task = readyQueue_.front();
        readyQueue_.pop();
        task.resume();
      }

      std::erase_if(ownedTasks_, [](const Task<void> &t) { return t.done(); });

      for (auto &event : epoll_.wait(64)) {
        int fd = event.data.fd;
        auto it = suspendedTasks_.find(fd);
        if (it != suspendedTasks_.end()) {
          auto &task = it->second;
          readyQueue_.push(task);
          suspendedTasks_.erase(it);
        }
      }
    }
  }

private:
  EpollInstance epoll_;
  std::vector<Task<void>> ownedTasks_;
  std::queue<std::coroutine_handle<>> readyQueue_;
  std::unordered_map<int, std::coroutine_handle<>> suspendedTasks_;
};
