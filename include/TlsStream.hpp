#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>

#include "IStream.hpp"
#include "Socket.hpp"
#include "utils.hpp"

class TlsStream : public IStream {
public:
  TlsStream(int fd, SSL_CTX *ctx) : socket_(fd) {
    ssl_ = SSL_new(ctx);
    SSL_set_fd(ssl_, fd);
    auto [ip, port] = resolvePeerAddress(socket_.getFd());
    ip_ = ip;
    port_ = port;
  }

  // called once after construction, before any read/write
  bool handshake() { return SSL_accept(ssl_) == 1; }

  ssize_t receive(std::vector<std::byte> &buf) override {
    return SSL_read(ssl_, buf.data(), buf.size());
  }

  ssize_t send(const std::span<const std::byte> buf) override {
    return SSL_write(ssl_, buf.data(), buf.size());
  }

  std::string getIp() const override { return ip_; }
  uint16_t getPort() const override { return port_; }

  ~TlsStream() {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }

private:
  Socket socket_; // your existing class, just manages the fd
  SSL *ssl_ = nullptr;
  std::string ip_;
  uint16_t port_;
};
