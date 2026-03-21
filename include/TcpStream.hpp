#pragma once

#include <sys/socket.h>

#include "IStream.hpp"
#include "Socket.hpp"

class TcpStream : public IStream {
public:
  TcpStream(int fd, sockaddr_storage addr, socklen_t len);

  ssize_t send(const std::span<const unsigned char> &data) const override;

  ReceiveResult receive(std::vector<unsigned char> &data) const override;

  HandshakeResult handshake() override;

  int setSocketNonBlocking();

  std::string getIp() const override;
  uint16_t getPort() const override;

  int getFd() const override;

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
