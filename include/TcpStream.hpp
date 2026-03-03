#pragma once

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include "IStream.hpp"
#include "Socket.hpp"
#include "utils.hpp"

class TcpStream : public IStream {
public:
  TcpStream(int fd) : socket_(fd) {
    auto [ip, port] = resolvePeerAddress(socket_.getFd());
    ip_ = ip;
    port_ = port;
    SPDLOG_INFO("Connected to {}:{}", ip_, port_);
  }

  ssize_t send(const std::span<const std::byte> &data) const override {
    ssize_t n = ::send(socket_.getFd(), data.data(), data.size(), 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return 0;
    return n;
  }

  ssize_t receive(std::vector<std::byte> &data) const override {
    return ::recv(socket_.getFd(), data.data(), data.size(), 0);
  }

  HandshakeResult handshake() const override { return HandshakeResult::NO_TLS; }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }

  std::string getIp() const override { return ip_; }
  uint16_t getPort() const override { return port_; }

  int getFd() const override { return socket_.getFd(); }

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
