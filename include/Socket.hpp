#pragma once

class Socket final {
public:
  Socket(Socket const &) = delete;
  Socket &operator=(Socket const &) = delete;

  Socket();

  Socket(int fd);

  Socket(Socket &&other) noexcept;

  Socket &operator=(Socket &&other) noexcept;

  ~Socket() noexcept;

  int getFd() const noexcept;
  bool isValid() const noexcept;

  explicit operator bool() const noexcept;

  int release() noexcept;
  int setNonBlocking();

private:
  int socket_fd_;
  void close_socket();
};
