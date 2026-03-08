#pragma once

#include <cstdint>
#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "EpollInstance.hpp"
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

  // duh
  void setRouter(Router &router) { router_ = &router; };

  void run() {
    if (!router_)
      throw std::runtime_error("Call setRouter() before run()");

    for (auto &listener : tcpListeners_) {
      listener->listen(BACKLOG);
      int fd = listener->getFd();
      epoll_.add(fd, EPOLLIN, fd);
      tcpListenerFds_.insert(fd);
    }

    if (!tlsListeners_.empty() && !tlsContext_)
      throw std::runtime_error("TLS listeners added but no TLS context set");

    for (auto &listener : tlsListeners_) {
      listener->listen(BACKLOG);
      int fd = listener->getFd();
      epoll_.add(fd, EPOLLIN, fd);
      tlsListenerFds_.insert(fd);
    }

    for (;;) {
      for (auto &event : epoll_.wait(64)) {
        int fd = event.data.fd;

        if (timerConnections_.contains(fd)) {
          handleTimerEvent(fd);
          continue;
        }
        if (tcpListenerFds_.contains(fd)) {
          handleNewTcpConnection(fd);
          continue;
        }
        if (tlsListenerFds_.contains(fd)) {
          handleNewTlsConnection(fd);
          continue;
        }
        handleConnectionEvent(event);
      }
    }
  }

  ErrorFactory &getErrorFactory() { return errorFactory_; }
  void setErrorFactory(ErrorFactory errorFactory) {
    errorFactory_ = errorFactory;
  }

private:
  static constexpr int BACKLOG = 20;

  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners_, tlsListeners_;

  std::unordered_set<int> tcpListenerFds_, tlsListenerFds_;

  std::unordered_map<int, std::shared_ptr<HttpConnection>> connections_;
  std::unordered_map<int, std::shared_ptr<HttpConnection>> timerConnections_;

  EpollInstance epoll_;
  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

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

  void registerConnection(std::shared_ptr<HttpConnection> conn) {
    int fd = conn->getFd();
    connections_[fd] = conn;
    timerConnections_[conn->getTimerFd()] = conn;
    epoll_.add(fd, EPOLLIN | EPOLLET, fd);
    epoll_.add(conn->getTimerFd(), EPOLLIN, conn->getTimerFd());
  }

  void handleTimerEvent(int timerFd) {
    uint64_t val;
    ::read(timerFd, &val, sizeof(val)); // must drain — level triggered
    auto &conn = timerConnections_.at(timerFd);
    conn->onTimeout();
    closeConnection(conn->getFd());
  }

  void handleNewTcpConnection(int listenerFd) {
    auto it = std::find_if(tcpListeners_.begin(), tcpListeners_.end(),
                           [&](auto &l) { return l->getFd() == listenerFd; });
    auto stream = std::make_shared<TcpStream>((*it)->accept());
    stream->setSocketNonBlocking();
    registerConnection(std::make_shared<HttpConnection>(
        std::move(stream), *router_, errorFactory_));
  }

  void handleNewTlsConnection(int listenerFd) {
    auto it = std::find_if(tlsListeners_.begin(), tlsListeners_.end(),
                           [&](auto &l) { return l->getFd() == listenerFd; });
    auto stream =
        std::make_shared<TlsStream>((*it)->acceptTls(tlsContext_.get()));
    stream->setSocketNonBlocking();
    registerConnection(std::make_shared<HttpConnection>(
        std::move(stream), *router_, errorFactory_));
  }

  void handleConnectionEvent(const epoll_event &event) {
    auto it = connections_.find(event.data.fd);
    if (it == connections_.end())
      return;

    auto &conn = it->second;
    if (event.events & EPOLLIN)
      conn->onReadable();
    if (event.events & EPOLLOUT)
      conn->onWriteable();

    if (conn->isClosing()) {
      closeConnection(conn->getFd());
      return;
    }

    uint32_t newEvents = EPOLLIN | EPOLLET;
    if (conn->wantsWrite())
      newEvents |= EPOLLOUT;
    epoll_.modify(event.data.fd, newEvents, event.data.fd);
  }
};
