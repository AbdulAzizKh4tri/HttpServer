#pragma once

#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <vector>

#include "ErrorFactory.hpp"
#include "Executor.hpp"
#include "ExecutorContext.hpp"
#include "ListenerSocket.hpp"
#include "Router.hpp"

class HttpServer {
public:
  HttpServer(ErrorFactory &errorFactory);

  void setTlsContext(std::string certPath, std::string keyPath);

  // Add listeners, either TCP or TLS
  void addListener(const std::string &host, const std::string &port);

  void addTlsListener(const std::string &host, const std::string &port);

  void setRouter(Router &router);

  void run();

  ErrorFactory &getErrorFactory();

private:
  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners_, tlsListeners_;

  Executor executor_;

  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

  Task<void> tcpAcceptLoop(ListenerSocket &listener);

  Task<void> tlsAcceptLoop(ListenerSocket &listener);

  Task<void> handleConnection(std::shared_ptr<IStream> stream);
};
