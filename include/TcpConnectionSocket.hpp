#pragma once

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <vector>

#include "Socket.hpp"

class TcpConnectionSocket {
  static constexpr std::size_t READ_BUFFER_SIZE = 4096;
  static constexpr std::size_t WRITE_BUFFER_SIZE = 4096;

public:
  std::vector<std::byte> writeBuffer;

  TcpConnectionSocket(int fd, sockaddr_storage cli_addr)
      : socket_(fd), client_address_(cli_addr),
        writeBuffer(std::vector<std::byte>(WRITE_BUFFER_SIZE)) {

    if (client_address_.ss_family == AF_INET) {
      char ipstr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &((sockaddr_in *)&client_address_)->sin_addr, ipstr,
                INET_ADDRSTRLEN);

      ip_ = ipstr;
      port_ = ntohs(((sockaddr_in *)&client_address_)->sin_port);

      SPDLOG_INFO("Connected to {}:{}", ip_, ntohs(port_));

    } else if (client_address_.ss_family == AF_INET6) {
      char ipstr[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &((sockaddr_in6 *)&client_address_)->sin6_addr, ipstr,
                INET6_ADDRSTRLEN);

      ip_ = ipstr;
      port_ = ntohs(((sockaddr_in6 *)&client_address_)->sin6_port);

      SPDLOG_INFO("Connected to {}:{}", ip_, ntohs(port_));
    }
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

  std::vector<std::byte> receiveAll() {
    std::vector<std::byte> all;
    std::vector<std::byte> chunk(READ_BUFFER_SIZE);

    for (;;) {
      ssize_t n = ::recv(socket_.getFd(), chunk.data(), chunk.size(), 0);
      if (n > 0) {
        all.insert(all.end(), chunk.begin(), chunk.begin() + n);
      } else if (n == 0) {
        return {};
      } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        if (errno == ECONNRESET || errno == ENOTCONN) {
          return {};
        }
        throw std::runtime_error("Failed to receive data");
      }
    }

    return all;
  }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }

  std::string getIp() const { return ip_; }
  uint16_t getPort() const { return port_; }

  int getFd() const { return socket_.getFd(); }

private:
  Socket socket_;
  const sockaddr_storage client_address_;
  std::string ip_;
  uint16_t port_;
};
