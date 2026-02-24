#pragma once

#include <cstdint>
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <unistd.h>

class EpollInstance final {
public:
  EpollInstance(EpollInstance const &) = delete;
  EpollInstance &operator=(EpollInstance const &) = delete;

  EpollInstance() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
      SPDLOG_ERROR("ERROR: {}", strerror(errno));
      throw std::runtime_error("Failed to create epoll instance");
    }
  }

  EpollInstance(EpollInstance &&other) noexcept : epoll_fd_(other.epoll_fd_) {
    other.epoll_fd_ = -1;
  }

  EpollInstance &operator=(EpollInstance &&other) noexcept {
    if (this == &other)
      return *this;
    close_epoll_instance();
    epoll_fd_ = other.epoll_fd_;
    other.epoll_fd_ = -1;
    return *this;
  }

  ~EpollInstance() noexcept { close_epoll_instance(); }

  int getFd() const noexcept { return epoll_fd_; }
  bool isValid() const noexcept { return epoll_fd_ != -1; }

  explicit operator bool() const noexcept { return epoll_fd_ != -1; }

  int release() noexcept {
    int fd = epoll_fd_;
    epoll_fd_ = -1;
    return fd;
  }

  int add(int fd, uint32_t events, int data) {
    epoll_event event = {};
    event.events = events;
    event.data.fd = data;

    int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1) {
      if (errno == EEXIST) {
        return modify(fd, events, data);
      }
      SPDLOG_ERROR("ERROR (fd: {}): {}", fd, strerror(errno));
      throw std::runtime_error("Failed to add fd " + std::to_string(fd) +
                               " to epoll");
    }
    return ret;
  }

  int modify(int fd, uint32_t events, int data) {
    epoll_event event = {};
    event.events = events;
    event.data.fd = data;

    int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
    if (ret == -1) {
      SPDLOG_ERROR("ERROR (fd: {}): {}", fd, strerror(errno));
      throw std::runtime_error("Failed to modify epoll, fd=" +
                               std::to_string(fd));
    }
    return ret;
  }

  int remove(int fd) {
    int ret = ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (ret == -1) {
      if (errno == ENOENT) {
        return 0;
      }
      SPDLOG_ERROR("ERROR (fd: {}): {}", fd, strerror(errno));
      throw std::runtime_error("Failed to remove fd " + std::to_string(fd) +
                               " from epoll");
    }
    return ret;
  }

  int wait(epoll_event *events, int maxevents, int timeout = -1) {
    for (;;) {
      int numEvents = ::epoll_wait(epoll_fd_, events, maxevents, timeout);
      if (numEvents == -1) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("Failed to wait for events");
      }
      return numEvents;
    }
  }

  std::vector<epoll_event> wait(int maxevents, int timeout = -1) {
    if (maxevents <= 0)
      return {};
    std::vector<epoll_event> events(maxevents);
    int numEvents = wait(events.data(), maxevents, timeout);
    events.resize(numEvents);
    return events;
  }

private:
  int epoll_fd_ = -1;

  void close_epoll_instance() noexcept {
    if (epoll_fd_ != -1) {
      ::close(epoll_fd_);
      epoll_fd_ = -1;
    }
  }
};
