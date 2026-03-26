#pragma once

#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <vector>
#include <atomic>

#include "ErrorFactory.hpp"
#include "ListenerSocket.hpp"
#include "Router.hpp"

struct ListenerConfig {
  std::string host;
  std::string port;
  bool isTls;
};

class HttpServer {
public:
  static std::atomic<bool> shutdown_;

  HttpServer(ErrorFactory &errorFactory);

  void setTlsContext(std::string certPath, std::string keyPath);

  // Add listeners, either TCP or TLS
  void addListener(const std::string &host, const std::string &port);

  void addTlsListener(const std::string &host, const std::string &port);

  void setRouter(Router &router);

  void run(int N);

  ErrorFactory &getErrorFactory();

private:
  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<ListenerConfig> listenersConfigs_;

  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

  void workerMain();

  Task<void> tcpAcceptLoop(ListenerSocket &listener);

  Task<void> tlsAcceptLoop(ListenerSocket &listener);

  Task<void> handleConnection(std::shared_ptr<IStream> stream);
};
