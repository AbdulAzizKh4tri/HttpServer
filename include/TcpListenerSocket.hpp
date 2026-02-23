#pragma once

#include <netdb.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h> // socket, bind, listen, accept, setsockopt, SOCK_STREAM, SOL_SOCKET, SO_REUSEADDR

#include "Socket.hpp"
#include "TcpConnectionSocket.hpp"
#include "utils.hpp"

class TcpListenerSocket {
public:
  TcpListenerSocket(std::string host, std::string port) {
    host_ = host;
    port_ = port;

    addrinfo hints = {}, *res;
    ScopeGuard cleanup([&] { freeaddrinfo(res); });

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res) != 0) {
      SPDLOG_ERROR("ERROR on getaddrinfo %s", strerror(errno));
      throw std::runtime_error("Failed to locate address");
    }

    socket_ =
        Socket(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (!socket_) {
      SPDLOG_ERROR("Failed to create socket %s", strerror(errno));
      throw std::runtime_error("Failed to create socket");
    }

    bindSocket(res);
  }

  void listen(int backlog) {
    if (::listen(socket_.get_fd(), backlog) < 0) {
      SPDLOG_ERROR("ERROR on listening %s", strerror(errno));
      throw std::runtime_error("Failed to listen on socket");
    }
    SPDLOG_INFO("Listening on {}:{}", host_, port_);
  }

  TcpConnectionSocket accept() {
    struct sockaddr_storage clientAddress;
    socklen_t clientAddressLength = sizeof(clientAddress);
    int newSocket_fd =
        ::accept(socket_.get_fd(), (struct sockaddr *)&clientAddress, &clientAddressLength);

    if (newSocket_fd < 0) {
      SPDLOG_ERROR("ERROR on accepting %s", strerror(errno));
      throw std::runtime_error("Failed to accept connection");
    }
    return TcpConnectionSocket(newSocket_fd, clientAddress);
  }

private:
  Socket socket_;
  std::string host_, port_;

  void bindSocket(addrinfo *res) {
    int reuse = 1;
    setsockopt(socket_.get_fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

    if (bind(socket_.get_fd(), res->ai_addr, res->ai_addrlen) < 0) {
      SPDLOG_ERROR("ERROR on binding %s", strerror(errno));
      throw std::runtime_error("Failed to bind socket");
    }
    SPDLOG_INFO("Socket Bound for {}:{}", host_, port_);
  }
};
