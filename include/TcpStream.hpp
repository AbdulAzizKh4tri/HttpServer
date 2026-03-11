#pragma once

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <span>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include "IStream.hpp"
#include "Socket.hpp"
#include "utils.hpp"

class TcpStream : public IStream {
public:
  TcpStream(int fd, sockaddr_storage addr, socklen_t len) : socket_(fd) {
    auto [ip, port] = resolvePeerAddress(addr, len);
    ip_ = ip;
    port_ = port;
  }

  ssize_t send(const std::span<const unsigned char> &data) const override {
    ssize_t n = ::send(socket_.getFd(), data.data(), data.size(), 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return 0;
    return n;
  }

  ReceiveResult receive(std::vector<unsigned char> &data) const override {
    ssize_t n = ::recv(socket_.getFd(), data.data(), data.size(), 0);
    if (n > 0)
      return ReceiveResult::data(n);
    if (n == 0)
      return ReceiveResult::closed();
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return ReceiveResult::wouldBlock();
    if (errno == ECONNRESET)
      return ReceiveResult::closed();
    return ReceiveResult::error();
  }

  HandshakeResult handshake() override { return HandshakeResult::NO_TLS; }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }

  std::string getIp() const override { return ip_; }
  uint16_t getPort() const override { return port_; }

  int getFd() const override { return socket_.getFd(); }

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
