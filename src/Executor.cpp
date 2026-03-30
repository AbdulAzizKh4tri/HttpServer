#include "Executor.hpp"

#include "serverConfig.hpp"
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

void Executor::registerReadOnlyFd(int fd) { epoll_.add(fd, EPOLLIN | EPOLLET, fd); }
void Executor::registerFd(int fd) { epoll_.add(fd, EPOLLIN | EPOLLOUT | EPOLLET, fd); }

void Executor::waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = {caller, false};
  ;
  if (deadline != std::chrono::steady_clock::time_point::max())
    deadlines_[fd] = deadline;
}

void Executor::waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = {caller, true};
  if (deadline != std::chrono::steady_clock::time_point::max())
    deadlines_[fd] = deadline;
}

void Executor::submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
                              uint64_t offset) {

  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepRead(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::submitFileWrite(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
                               uint64_t offset) {
  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepWrite(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::run(std::atomic<bool> &shutdown) {
  tl_executor = this;
  std::chrono::steady_clock::time_point shutdownDeadline = std::chrono::steady_clock::time_point::max();

  for (;;) {
    if (shutdown) {
      if (shutdownDeadline < now()) {
        SPDLOG_INFO("Timeout on Shutdown");
        return;
      }
      if (shutdownDeadline == std::chrono::steady_clock::time_point::max()) {
        shutdownDeadline = now() + std::chrono::seconds(GRACEFUL_SHUTDOWN_TIMEOUT_S);
      } else {
        if (ownedTasks_.empty() || now() > shutdownDeadline) {
          SPDLOG_INFO("Graceful Shutdown");
          return;
        }
      }
    }

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
      if (it == suspendedTasks_.end())
        continue;

      bool isError = event.events & (EPOLLERR | EPOLLHUP);
      bool readReady = event.events & EPOLLIN;
      bool writeReady = event.events & EPOLLOUT;

      bool shouldWake =
          isError || (it->second.waitingForWrite && writeReady) || (!it->second.waitingForWrite && readReady);

      if (not shouldWake)
        continue;

      readyQueue_.push({it->second.handle, false});
      suspendedTasks_.erase(it);
      deadlines_.erase(fd);
    }

    for (auto it = deadlines_.begin(); it != deadlines_.end();) {
      if (it->second <= now()) {
        auto taskIt = suspendedTasks_.find(it->first);
        if (taskIt != suspendedTasks_.end()) {
          readyQueue_.push({taskIt->second.handle, true});
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
