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
    if (!ssl_)
      throw std::runtime_error("Failed to create SSL object");
    SSL_set_fd(ssl_, fd);
    auto [ip, port] = resolvePeerAddress(socket_.getFd());
    ip_ = ip;
    port_ = port;
  }

  // move constructor — transfer ownership, null out source
  TlsStream(TlsStream &&other) noexcept
      : socket_(std::move(other.socket_)), ssl_(other.ssl_),
        ip_(std::move(other.ip_)), port_(other.port_) {
    other.ssl_ = nullptr; // prevent double-free in destructor
  }

  TlsStream &operator=(TlsStream &&other) noexcept {
    if (this == &other)
      return *this;
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
    }
    socket_ = std::move(other.socket_);
    ssl_ = other.ssl_;
    ip_ = std::move(other.ip_);
    port_ = other.port_;
    other.ssl_ = nullptr;
    return *this;
  }

  TlsStream(TlsStream const &) = delete;
  TlsStream &operator=(TlsStream const &) = delete;

  ~TlsStream() {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
    }
  }

  HandshakeResult handshake() const override {
    int ret = SSL_accept(ssl_);
    if (ret == 1)
      return HandshakeResult::DONE;

    int err = SSL_get_error(ssl_, ret);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      return HandshakeResult::WANT_READ;
    case SSL_ERROR_WANT_WRITE:
      return HandshakeResult::WANT_WRITE;
    default:
      SPDLOG_ERROR("TLS handshake error for {}:{} — SSL error code {}", ip_,
                   port_, err);
      return HandshakeResult::ERROR;
    }
  }

  ssize_t receive(std::vector<std::byte> &buf) const override {
    return SSL_read(ssl_, buf.data(), buf.size());
  }

  ssize_t send(const std::span<const std::byte> &data) const override {
    int n = SSL_write(ssl_, data.data(), data.size());
    if (n <= 0) {
      int err = SSL_get_error(ssl_, n);
      if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
        return 0;
      return -1;
    }
    return n;
  }

  std::string getIp() const override { return ip_; }
  uint16_t getPort() const override { return port_; }

  int getFd() const override { return socket_.getFd(); }

  int setSocketNonBlocking() { return socket_.setNonBlocking(); }

private:
  Socket socket_; // your existing class, just manages the fd
  SSL *ssl_ = nullptr;
  std::string ip_;
  uint16_t port_;
};
