#pragma once

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <vector>

#include "Socket.hpp"
#include "utils.hpp"

class TcpStream {
public:
  std::vector<std::byte> writeBuffer;

  TcpStream(int fd) : socket_(fd) {
    auto [ip, port] = resolvePeerAddress(socket_.getFd());
    ip_ = ip;
    port_ = port;
    SPDLOG_INFO("Connected to {}:{}", ip_, port_);
  }

  ssize_t send(const std::span<const std::byte> data, int flags = 0) {
    ssize_t total_sent = 0;
    while (total_sent < (ssize_t)data.size()) {
      ssize_t n = ::send(socket_.getFd(), data.data() + total_sent,
                         data.size() - total_sent, flags);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return total_sent;
        }
        SPDLOG_ERROR("ERROR on sending: {}", strerror(errno));
        throw std::runtime_error(strerror(errno));
      }
      total_sent += n;
    }
    return total_sent;
  }

  int receive(std::vector<std::byte> &data) {
    return ::recv(socket_.getFd(), data.data(), data.size(), 0);
  }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }

  std::string getIp() const { return ip_; }
  uint16_t getPort() const { return port_; }

  int getFd() const { return socket_.getFd(); }

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
