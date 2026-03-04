#pragma once

#include <cstdint>
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

  // duh
  void setRouter(Router &router) { router_ = &router; };

  void run() {

    // Go through every TCP listener and set up the socket, then add to epoll
    for (auto &listener : tcpListeners_) {
      listener->listen(BACKLOG);
      epoll_.add(listener->getFd(), EPOLLIN, listener->getFd());
    }

    // TLS listeners added without tls context = bad
    if (!tlsListeners_.empty() && !tlsContext_) {
      throw std::runtime_error("TLS context not set");
    }

    // Same thing as above for Tls listeners
    for (auto &listener : tlsListeners_) {
      listener->listen(BACKLOG);
      epoll_.add(listener->getFd(), EPOLLIN, listener->getFd());
    }

    for (;;) {
      // event queue
      std::vector<epoll_event> events = epoll_.wait(64);

      for (auto &event : events) {

        // Timer Event, close connection
        auto timerIt = timerConnections_.find(event.data.fd);
        if (timerIt != timerConnections_.end()) {
          uint64_t val;
          // must read because timer is level triggered
          // would fire every iteration otherwise
          ::read(event.data.fd, &val, sizeof(val));
          timerIt->second->onTimeout();
          closeConnection(timerIt->second->getFd());
          continue;
        }

        // Check whether event is a TCP or TLS listener,
        // if neither, it's a connection event
        auto tcpIt = std::find_if(
            tcpListeners_.begin(), tcpListeners_.end(),
            [&](auto &listener) { return listener->getFd() == event.data.fd; });

        if (tcpIt != tcpListeners_.end()) {
          auto stream = std::make_shared<TcpStream>((*tcpIt)->accept());
          stream->setSocketNonBlocking();
          int fd = stream->getFd();
          auto conn =
              std::make_shared<HttpConnection>(std::move(stream), *router_);
          connections_[fd] = conn;
          timerConnections_[conn->getTimerFd()] = conn;

          epoll_.add(fd, EPOLLIN | EPOLLET, fd);
          epoll_.add(conn->getTimerFd(), EPOLLIN, conn->getTimerFd());
          continue;
        }

        auto tlsIt = std::find_if(
            tlsListeners_.begin(), tlsListeners_.end(),
            [&](auto &listener) { return listener->getFd() == event.data.fd; });

        if (tlsIt != tlsListeners_.end()) {
          auto stream = std::make_shared<TlsStream>(
              (*tlsIt)->acceptTls(tlsContext_.get()));
          stream->setSocketNonBlocking();
          int fd = stream->getFd();
          auto conn =
              std::make_shared<HttpConnection>(std::move(stream), *router_);
          connections_[fd] = conn;
          timerConnections_[conn->getTimerFd()] = conn;

          epoll_.add(fd, EPOLLIN | EPOLLET, fd);
          epoll_.add(conn->getTimerFd(), EPOLLIN, conn->getTimerFd());
          continue;
        }

        // Connection event
        auto it = connections_.find(event.data.fd);
        if (it == connections_.end())
          continue;

        auto &conn = it->second;

        if (event.events & EPOLLIN)
          conn->onReadable();

        if (event.events & EPOLLOUT)
          conn->onWriteable();

        if (conn->isClosing()) {
          closeConnection(conn->getFd());
          continue;
        }

        uint32_t newEvents = EPOLLIN | EPOLLET;
        // if there's data left to write, add EPOLLOUT
        if (conn->wantsWrite())
          newEvents |= EPOLLOUT;
        epoll_.modify(event.data.fd, newEvents, event.data.fd);
      }
    }
  }

private:
  static constexpr int BACKLOG = 20;

  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners_;
  std::vector<std::unique_ptr<ListenerSocket>> tlsListeners_;

  std::unordered_map<int, std::shared_ptr<HttpConnection>> connections_;
  std::unordered_map<int, std::shared_ptr<HttpConnection>> timerConnections_;

  EpollInstance epoll_;
  Router *router_ = nullptr;

  void closeConnection(int connFd) {
    auto it = connections_.find(connFd);
    if (it == connections_.end())
      return;
    int tfd = it->second->getTimerFd();
    epoll_.remove(connFd);
    epoll_.remove(tfd);
    timerConnections_.erase(tfd);
    connections_.erase(it);
  }
};
