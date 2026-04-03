#include "HttpServer.hpp"

#include <csignal>
#include <thread>

#include "Awaitables.hpp"
#include "Executor.hpp"
#include "ExecutorContext.hpp"
#include "HttpConnection.hpp"

HttpServer::HttpServer(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

void HttpServer::setTlsContext(std::string certPath, std::string keyPath) {
  // takes the cert.pem and key.pem files and sets up the context needed
  // for TLS
  tlsContext_ = std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  if (!tlsContext_)
    throw std::runtime_error("Failed to create SSL_CTX");

  if (SSL_CTX_use_certificate_file(tlsContext_.get(), certPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw std::runtime_error("Failed to load certificate");

  if (SSL_CTX_use_PrivateKey_file(tlsContext_.get(), keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw std::runtime_error("Failed to load private key");
}

// Add listeners, either TCP or TLS
void HttpServer::addListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, false};
  listenersConfigs_.push_back(config);
}

void HttpServer::addTlsListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, true};
  listenersConfigs_.push_back(config);
};

void HttpServer::setRouter(Router &router) { router_ = &router; };

std::atomic<bool> HttpServer::shutdown_ = false;

void HttpServer::run(int N) {
  signal(SIGPIPE, SIG_IGN);

  signal(SIGINT, [](int) {
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown_ = true;
  });
  signal(SIGTERM, [](int) {
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown_ = true;
  });

  if (!router_)
    throw std::runtime_error("Call setRouter() before run()");

  if (N <= 0)
    N = std::thread::hardware_concurrency();

  std::vector<std::thread> executorThreads;

  if (N > 1)
    SPDLOG_INFO("KAGE BUNSHIN NO JUTSU");
  for (int i = 0; i < N; i++)
    executorThreads.emplace_back([this] { workerMain(); });
  for (auto &t : executorThreads)
    t.join();

  spdlog::shutdown();
}

void HttpServer::workerMain() {
  Executor executor;
  tl_executor = &executor;
  std::vector<std::unique_ptr<ListenerSocket>> tcpListeners, tlsListeners;

  for (auto &config : listenersConfigs_) {
    if (config.isTls) {
      auto listener = std::make_unique<ListenerSocket>(config.host, config.port);
      listener->setSocketNonBlocking();
      tlsListeners.push_back(std::move(listener));
    } else {
      auto listener = std::make_unique<ListenerSocket>(config.host, config.port);
      listener->setSocketNonBlocking();
      tcpListeners.push_back(std::move(listener));
    }
  }

  for (auto &listener : tcpListeners) {
    listener->listen(listenBacklog_);
    tl_executor->spawn(tcpAcceptLoop(*listener));
  }

  for (auto &listener : tlsListeners) {
    listener->listen(listenBacklog_);
    tl_executor->spawn(tlsAcceptLoop(*listener));
  }

  tl_executor->run(shutdown_);
}

ErrorFactory &HttpServer::getErrorFactory() { return errorFactory_; }

Task<void> HttpServer::tcpAcceptLoop(ListenerSocket &listener) {
  tl_executor->registerReadOnlyFd(listener.getFd());
  for (;;) {
    co_await ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(2)};
    if (tl_timed_out) {
      tl_timed_out = false;
      if (shutdown_)
        co_return;
      continue;
    }
    for (;;) {
      auto streamOpt = listener.accept();
      if (!streamOpt)
        break;
      auto stream = std::make_shared<TcpStream>(std::move(*streamOpt));
      tl_executor->spawn(handleConnection(std::move(stream)));
    }
  }
}

Task<void> HttpServer::tlsAcceptLoop(ListenerSocket &listener) {
  tl_executor->registerReadOnlyFd(listener.getFd());
  for (;;) {
    co_await ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(2)};
    if (tl_timed_out) {
      tl_timed_out = false;
      if (shutdown_)
        co_return;
      continue;
    }
    for (;;) {
      auto streamOpt = listener.acceptTls(tlsContext_.get());
      if (!streamOpt)
        break;
      auto stream = std::make_shared<TlsStream>(std::move(*streamOpt));
      tl_executor->spawn(handleConnection(std::move(stream)));
    }
  }
}

Task<void> HttpServer::handleConnection(std::shared_ptr<IStream> stream) {
  int fd = stream->getFd();
  tl_executor->registerFd(fd);

  HttpConnection conn(stream, *router_, errorFactory_, shutdown_);
  co_await conn.run();
  tl_executor->unregister(fd);
}
