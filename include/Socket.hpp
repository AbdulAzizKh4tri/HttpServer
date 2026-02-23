#pragma once
#include <unistd.h>

class Socket final {
public:
  Socket(Socket const &) = delete;
  Socket &operator=(Socket const &) = delete;

  Socket() { socket_fd_ = -1; }

  Socket(int fd) : socket_fd_(fd) {}

  Socket(Socket &&other) : socket_fd_(other.socket_fd_) { other.socket_fd_ = -1; }

  Socket &operator=(Socket &&other) {
    if (this == &other)
      return *this;
    closeSocket();
    socket_fd_ = other.socket_fd_;
    other.socket_fd_ = -1;
    return *this;
  }

  ~Socket() noexcept { closeSocket(); }

  int get_fd() const { return socket_fd_; }
  bool is_valid() const { return socket_fd_ != -1; }

  explicit operator bool() const { return socket_fd_ != -1; }

  int release() {
    int fd = socket_fd_;
    socket_fd_ = -1;
    return fd;
  }

private:
  int socket_fd_;

  short closeSocket() {
    if (socket_fd_ != -1) {
      close(socket_fd_);
      return 1;
    }
    return 0;
  }
};
