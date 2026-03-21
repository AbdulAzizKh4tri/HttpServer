#include "Executor.hpp"

#include "utils.hpp"

Executor::Executor() {}

void Executor::spawn(Task<void> task) {
  readyQueue_.push({task.handle(), false});
  ownedTasks_.push_back(std::move(task));
}

void Executor::unregister(int fd) {
  epoll_.remove(fd);
  suspendedTasks_.erase(fd);
  deadlines_.erase(fd);
}

void Executor::waitForRead(int fd, std::coroutine_handle<> caller,
                           std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = caller;
  if (deadline != std::chrono::steady_clock::time_point::max())
    deadlines_[fd] = deadline;
  epoll_.addOrModify(fd, EPOLLIN | EPOLLET, fd);
}

void Executor::waitForWrite(int fd, std::coroutine_handle<> caller,
                            std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = caller;
  if (deadline != std::chrono::steady_clock::time_point::max())
    deadlines_[fd] = deadline;
  epoll_.addOrModify(fd, EPOLLOUT | EPOLLET | EPOLLIN, fd);
}

void Executor::run() {
  tl_executor = this;
  for (;;) {
    while (!readyQueue_.empty()) {
      auto [task, timed_out] = readyQueue_.front();
      readyQueue_.pop();
      tl_timed_out = timed_out;
      task.resume();
      tl_timed_out = false;
    }

    std::erase_if(ownedTasks_, [](const Task<void> &t) { return t.done(); });

    auto events = epoll_.wait(64, EPOLL_WAIT_TIMEOUT * 1000);
    for (auto &event : events) {
      int fd = event.data.fd;
      auto it = suspendedTasks_.find(fd);
      if (it != suspendedTasks_.end()) {
        auto &task = it->second;
        readyQueue_.push({task, false});
        suspendedTasks_.erase(it);
        deadlines_.erase(fd);
      }
    }

    for (auto it = deadlines_.begin(); it != deadlines_.end();) {
      if (it->second <= now()) {
        auto taskIt = suspendedTasks_.find(it->first);
        if (taskIt != suspendedTasks_.end()) {
          readyQueue_.push({taskIt->second, true});
          suspendedTasks_.erase(taskIt);
        }
        it = deadlines_.erase(it);

      } else {
        ++it;
      }
    }
  }
}
