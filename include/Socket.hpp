#pragma once
#include <fcntl.h>
#include <unistd.h>

class Socket final {
public:
  Socket(Socket const &) = delete;
  Socket &operator=(Socket const &) = delete;

  Socket() { socket_fd_ = -1; }

  Socket(int fd) : socket_fd_(fd) {}

  Socket(Socket &&other) noexcept : socket_fd_(other.socket_fd_) {
    other.socket_fd_ = -1;
  }

  Socket &operator=(Socket &&other) noexcept {
    if (this == &other)
      return *this;
    close_socket();
    socket_fd_ = other.socket_fd_;
    other.socket_fd_ = -1;
    return *this;
  }

  ~Socket() noexcept { close_socket(); }

  int getFd() const noexcept { return socket_fd_; }
  bool isValid() const noexcept { return socket_fd_ != -1; }

  explicit operator bool() const noexcept { return socket_fd_ != -1; }

  int release() noexcept {
    int fd = socket_fd_;
    socket_fd_ = -1;
    return fd;
  }

  int setNonBlocking() { return fcntl(socket_fd_, F_SETFL, O_NONBLOCK); }

private:
  int socket_fd_;

  void close_socket() {
    if (socket_fd_ != -1) {
      close(socket_fd_);
      socket_fd_ = -1;
    }
  }
};
