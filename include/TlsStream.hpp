#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <vector>

#include "IStream.hpp"
#include "Socket.hpp"

class TlsStream : public IStream {
public:
  TlsStream(int fd, SSL_CTX *ctx, sockaddr_storage addr, socklen_t len);

  TlsStream(TlsStream &&other) noexcept;

  TlsStream &operator=(TlsStream &&other) noexcept;

  TlsStream(TlsStream const &) = delete;
  TlsStream &operator=(TlsStream const &) = delete;

  ~TlsStream();

  HandshakeResult handshake() override;

  ReceiveResult receive(std::vector<unsigned char> &buf) const override;

  ssize_t send(const std::span<const unsigned char> &data) const override;

  std::string getIp() const override;
  uint16_t getPort() const override;

  int getFd() const override;

  int setSocketNonBlocking();

private:
  Socket socket_;
  SSL *ssl_ = nullptr;
  std::string ip_;
  uint16_t port_;
};
