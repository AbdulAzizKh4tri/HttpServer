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

void Executor::submitFileRead(int fd, void *buf, size_t len,
                              std::coroutine_handle<> h, int *resultPtr,
                              uint64_t offset) {

  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepRead(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::submitFileWrite(int fd, void *buf, size_t len,
                               std::coroutine_handle<> h, int *resultPtr,
                               uint64_t offset) {
  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepWrite(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::run() {
  tl_executor = this;
  for (;;) {

    ioUring_.drainCompletions([this](uint64_t userData, int result) {
      auto it = pendingFileOps_.find(userData);
      if (it == pendingFileOps_.end())
        return;

      auto [handle, resultPtr] = it->second;
      *resultPtr = result;
      readyQueue_.push({handle, false});
      pendingFileOps_.erase(it);
      return;
    });

    while (!readyQueue_.empty()) {
      auto [task, timed_out] = readyQueue_.front();
      readyQueue_.pop();
      tl_timed_out = timed_out;
      task.resume();
      tl_timed_out = false;
    }

    std::erase_if(ownedTasks_, [](const Task<void> &t) { return t.done(); });

    // setting this directly to epoll_wait_timeout makes it so that
    // download speed = STATIC_STREAM_CHUNK_SIZE / EPOLL_WAIT_TIMEOUT seconds
    int timeout = pendingFileOps_.empty() ? EPOLL_WAIT_TIMEOUT * 1000 : 0;
    auto events = epoll_.wait(64, timeout);
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
    ioUring_.ioSubmit();
  }
}
