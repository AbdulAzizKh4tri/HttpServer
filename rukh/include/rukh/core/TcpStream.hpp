#pragma once

#include <sys/socket.h>

#include <rukh/core/Socket.hpp>
#include <rukh/core/StreamResults.hpp>

namespace rukh {

class TcpStream {
public:
  TcpStream(int fd, sockaddr_storage addr, socklen_t len);

  ssize_t send(const std::span<const unsigned char> data) const;

  ReceiveResult receive(std::span<unsigned char> data) const;

  HandshakeResult handshake();

  int setSocketNonBlocking();

  void resetConnection();

  std::string getIp() const;
  uint16_t getPort() const;

  int getFd() const;

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
} // namespace rukh
