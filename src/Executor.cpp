#include "Executor.hpp"

#include <sys/eventfd.h>

#include "ExecutorContext.hpp"
#include "ServerConfig.hpp"
#include "utils.hpp"

Executor::Executor() {
  eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (eventFd_ < 0)
    throw std::runtime_error("eventfd failed");
  registerReadOnlyFd(eventFd_);
}

void Executor::spawn(Task<void> task) {
  readyQueue_.push({task.handle(), false});
  ownedTasks_.push_back(std::move(task));
}

void Executor::unregister(int fd) {
  epoll_.remove(fd);
  suspendedTasks_.erase(fd);
}

void Executor::post(std::coroutine_handle<> h) {
  {
    std::unique_lock lock(poolResumeQueueMutex_);
    poolResumeQueue_.push(h);
  }
  uint64_t one = 1;
  ::write(eventFd_, &one, sizeof(one));
}

void Executor::registerReadOnlyFd(int fd) { epoll_.add(fd, EPOLLIN | EPOLLET, fd); }
void Executor::registerFd(int fd) { epoll_.add(fd, EPOLLIN | EPOLLOUT | EPOLLET, fd); }

void Executor::waitForRead(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = {caller, false, deadline};
}

void Executor::waitForWrite(int fd, std::coroutine_handle<> caller, std::chrono::steady_clock::time_point deadline) {
  suspendedTasks_[fd] = {caller, true, deadline};
}

void Executor::submitFileRead(int fd, void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
                              uint64_t offset) {

  pendingFileOps_[nextUserData_] = {h, resultPtr};
  ioUring_.prepRead(fd, buf, len, nextUserData_, offset);
  nextUserData_++;
}

void Executor::submitFileWrite(int fd, const void *buf, size_t len, std::coroutine_handle<> h, int *resultPtr,
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
      auto timeNow = now();
      if (shutdownDeadline < timeNow) {
        SPDLOG_INFO("Timeout on Shutdown");
        return;
      }
      if (shutdownDeadline == std::chrono::steady_clock::time_point::max()) {
        shutdownDeadline = timeNow + std::chrono::seconds(ServerConfig::GRACEFUL_SHUTDOWN_TIMEOUT_S);
      } else {
        if (ownedTasks_.empty() || timeNow > shutdownDeadline) {
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

    while (not readyQueue_.empty()) {
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

      if (fd == eventFd_) {
        uint64_t val;
        ::read(eventFd_, &val, sizeof(val));
        std::unique_lock lock(poolResumeQueueMutex_);
        while (!poolResumeQueue_.empty()) {
          readyQueue_.push({poolResumeQueue_.front(), false});
          poolResumeQueue_.pop();
        }
        continue;
      }

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
    }

    auto timeNow = now();
    for (auto it = suspendedTasks_.begin(); it != suspendedTasks_.end();) {
      if (it->second.deadline <= timeNow) {
        readyQueue_.push({it->second.handle, true});
        it = suspendedTasks_.erase(it);
      } else {
        ++it;
      }
    }
    ioUring_.ioSubmit();
  }
}
