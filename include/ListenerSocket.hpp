#pragma once

#include <memory>
#include <netdb.h>
#include <optional>

#include "Socket.hpp"
#include "TcpStream.hpp"
#include "TlsStream.hpp"

class ListenerSocket {
public:
  ListenerSocket(std::string const &host, std::string port);

  void listen(int backlog);

  std::optional<TlsStream> acceptTls(SSL_CTX *ctx);

  std::optional<TcpStream> accept();

  int acceptRawFd();

  int setSocketNonBlocking();
  int getFd();

private:
  Socket socket_;
  std::string host_, port_;

  void bind_socket(std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> &res);
};
