#pragma once

#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "EpollInstance.hpp"
#include "HttpConnection.hpp"
#include "ListenerSocket.hpp"
#include "Router.hpp"

class HttpServer {
public:
  void setTlsContext(std::string certPath, std::string keyPath) {
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
  void setWorkerCount(int n) { workerCount_ = n; };

  void run() {
    for (auto &listener : tcpListeners_) {
      epoll_.add(listener->getFd(), EPOLLIN, listener->getFd());
      listener->listen(BACKLOG);
    }

    if (!tlsListeners_.empty() && !tlsContext_) {
      throw std::runtime_error("TLS context not set");
    }
    for (auto &listener : tlsListeners_) {
      epoll_.add(listener->getFd(), EPOLLIN, listener->getFd());
      listener->listen(BACKLOG);
    }

    for (;;) {
      std::vector<epoll_event> events = epoll_.wait(64);

      for (auto &event : events) {

        auto tcpIt = std::find_if(
            tcpListeners_.begin(), tcpListeners_.end(),
            [&](auto &listener) { return listener->getFd() == event.data.fd; });
        auto tlsIt = std::find_if(
            tlsListeners_.begin(), tlsListeners_.end(),
            [&](auto &listener) { return listener->getFd() == event.data.fd; });

        if (tcpIt != tcpListeners_.end()) {
          auto stream = std::make_shared<TcpStream>((*tcpIt)->accept());
          stream->setSocketNonBlocking();
          int fd = stream->getFd();
          connections_[fd] =
              std::make_shared<HttpConnection>(std::move(stream), *router_);
          epoll_.add(fd, EPOLLIN | EPOLLET, fd);

        } else if (tlsIt != tlsListeners_.end()) {
          auto stream = std::make_shared<TlsStream>(
              (*tlsIt)->acceptTls(tlsContext_.get()));
          stream->setSocketNonBlocking();
          int fd = stream->getFd();
          connections_[fd] =
              std::make_shared<HttpConnection>(std::move(stream), *router_);
          epoll_.add(fd, EPOLLIN | EPOLLET, fd);

        } else {
          auto it = connections_.find(event.data.fd);
          if (it == connections_.end())
            continue;

          auto &conn = it->second;

          if (event.events & EPOLLIN)
            conn->onReadable();

          if (event.events & EPOLLOUT)
            conn->onWriteable();

          if (conn->isClosing()) {
            epoll_.remove(event.data.fd);
            connections_.erase(it);
            continue;
          }

          uint32_t newEvents = EPOLLIN | EPOLLET;
          if (conn->wantsWrite()) {
            newEvents |= EPOLLOUT;
          }
          epoll_.modify(event.data.fd, newEvents, event.data.fd);
        }
      }
    }
  }

private:
  static constexpr int BACKLOG = 20;

  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners_;
  std::vector<std::unique_ptr<ListenerSocket>> tlsListeners_;

  std::unordered_map<int, std::shared_ptr<HttpConnection>> connections_;
  EpollInstance epoll_;
  Router *router_ = nullptr;
  int workerCount_ = 1;
};
