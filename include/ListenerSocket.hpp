#pragma once

#include <netdb.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h> // socket, bind, listen, accept, setsockopt, SOCK_STREAM, SOL_SOCKET, SO_REUSEADDR

#include "Socket.hpp"
#include "TcpStream.hpp"
#include "TlsStream.hpp"

class ListenerSocket {
public:
  ListenerSocket(std::string const &host, std::string port) {
    host_ = host;
    port_ = port;

    addrinfo hints = {}, *raw = nullptr;
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res(nullptr,
                                                           freeaddrinfo);

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai = getaddrinfo(host_.c_str(), port_.c_str(), &hints, &raw);
    if (gai != 0) {
      SPDLOG_ERROR("ERROR on getaddrinfo {}", gai_strerror(gai));
      throw std::runtime_error("Failed to locate address");
    }
    res.reset(raw);

    socket_ =
        Socket(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (!socket_) {
      SPDLOG_ERROR("Failed to create socket {}", strerror(errno));
      throw std::runtime_error("Failed to create socket");
    }

    bind_socket(res);
  }

  void listen(int backlog) {
    if (::listen(socket_.getFd(), backlog) < 0) {
      SPDLOG_ERROR("ERROR on listening {}", strerror(errno));
      throw std::runtime_error("Failed to listen on socket");
    }
    SPDLOG_INFO("Listening on {}:{}", host_, port_);
  }

  TlsStream acceptTls(SSL_CTX *ctx) {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    int newSocket_fd = ::accept(socket_.getFd(), (sockaddr *)&addr, &len);
    if (newSocket_fd < 0) {
      SPDLOG_ERROR("ERROR on accepting: {}", strerror(errno));
      throw std::runtime_error("Failed to accept connection");
    }
    return TlsStream(newSocket_fd, ctx, addr, len);
  }

  TcpStream accept() {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    int newSocket_fd = ::accept(socket_.getFd(), (sockaddr *)&addr, &len);
    if (newSocket_fd < 0) {
      SPDLOG_ERROR("ERROR on accepting: {}", strerror(errno));
      throw std::runtime_error("Failed to accept connection");
    }
    return TcpStream(newSocket_fd, addr, len);
  }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }
  int getFd() { return socket_.getFd(); }

private:
  Socket socket_;
  std::string host_, port_;

  void bind_socket(std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> &res) {
    int reuse = 1;
    if (setsockopt(socket_.getFd(), SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(int)) < 0) {
      SPDLOG_ERROR("ERROR on setsockopt {}", strerror(errno));
      throw std::runtime_error("Failed to set socket options");
    }

    if (bind(socket_.getFd(), res->ai_addr, res->ai_addrlen) < 0) {
      SPDLOG_ERROR("ERROR on binding {}", strerror(errno));
      throw std::runtime_error("Failed to bind socket");
    }
  }
};
