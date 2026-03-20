#pragma once

#include <csignal>
#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <vector>

#include "ErrorFactory.hpp"
#include "HttpConnection.hpp"
#include "ListenerSocket.hpp"
#include "Router.hpp"

class HttpServer {
public:
  HttpServer(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

  void setTlsContext(std::string certPath, std::string keyPath) {
    // takes the cert.pem and key.pem files and sets up the context needed
    // for TLS
    tlsContext_ = std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()),
                                           SSL_CTX_free);
    if (!tlsContext_)
      throw std::runtime_error("Failed to create SSL_CTX");

    if (SSL_CTX_use_certificate_file(tlsContext_.get(), certPath.c_str(),
                                     SSL_FILETYPE_PEM) <= 0)
      throw std::runtime_error("Failed to load certificate");

    if (SSL_CTX_use_PrivateKey_file(tlsContext_.get(), keyPath.c_str(),
                                    SSL_FILETYPE_PEM) <= 0)
      throw std::runtime_error("Failed to load private key");
  }

  // Add listeners, either TCP or TLS
  void addListener(const std::string &host, const std::string &port) {
    auto listener = std::make_unique<ListenerSocket>(host, port);
    listener->setSocketNonBlocking();
    tcpListeners_.push_back(std::move(listener));
  }

  void addTlsListener(const std::string &host, const std::string &port) {
    auto listener = std::make_unique<ListenerSocket>(host, port);
    listener->setSocketNonBlocking();
    tlsListeners_.push_back(std::move(listener));
  };

  void setRouter(Router &router) { router_ = &router; };

  void run() {
    signal(SIGPIPE, SIG_IGN);
    if (!router_)
      throw std::runtime_error("Call setRouter() before run()");

    for (auto &listener : tcpListeners_) {
      listener->listen(BACKLOG);
      executor_.spawn(tcpAcceptLoop(*listener));
    }

    for (auto &listener : tlsListeners_) {
      listener->listen(BACKLOG);
      executor_.spawn(tlsAcceptLoop(*listener));
    }

    executor_.run();
  }

  ErrorFactory &getErrorFactory() { return errorFactory_; }

private:
  static constexpr int BACKLOG = 20;

  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners_, tlsListeners_;

  Executor executor_;

  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

  Task<void> tcpAcceptLoop(ListenerSocket &listener) {
    for (;;) {
      co_await ReadAwaitable{listener.getFd(),
                             std::chrono::steady_clock::time_point::max()};
      auto stream = std::make_shared<TcpStream>(listener.accept());
      stream->setSocketNonBlocking();
      executor_.spawn(handleConnection(std::move(stream)));
    }
  }

  Task<void> tlsAcceptLoop(ListenerSocket &listener) {
    for (;;) {
      co_await ReadAwaitable{listener.getFd(),
                             std::chrono::steady_clock::time_point::max()};
      auto stream =
          std::make_shared<TlsStream>(listener.acceptTls(tlsContext_.get()));
      stream->setSocketNonBlocking();
      executor_.spawn(handleConnection(std::move(stream)));
    }
  }

  Task<void> handleConnection(std::shared_ptr<IStream> stream) {
    int fd = stream->getFd();
    HttpConnection conn(stream, *router_, errorFactory_);
    co_await conn.run();
    tl_executor->unregister(fd);
  }
};
