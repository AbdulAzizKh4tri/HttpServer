#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>

#include "Socket.hpp"
#include "StreamResults.hpp"

class TlsStream {
public:
  TlsStream(int fd, SSL_CTX *ctx, sockaddr_storage addr, socklen_t len);

  TlsStream(TlsStream &&other) noexcept;

  TlsStream &operator=(TlsStream &&other) noexcept;

  TlsStream(TlsStream const &) = delete;
  TlsStream &operator=(TlsStream const &) = delete;

  ~TlsStream();

  HandshakeResult handshake();

  ReceiveResult receive(std::span<unsigned char> buf) const;

  ssize_t send(const std::span<const unsigned char> data) const;

  void resetConnection();

  int setSocketNonBlocking();

  std::string getIp() const;
  uint16_t getPort() const;

  int getFd() const;

private:
  Socket socket_;
  SSL *ssl_ = nullptr;
  std::string ip_;
  uint16_t port_;
};
