#include "HttpServer.hpp"

#include <csignal>
#include <sys/eventfd.h>

#include "Awaitables.hpp"
#include "HttpConnection.hpp"
#include "config.hpp"

HttpServer::HttpServer(ErrorFactory &errorFactory)
    : errorFactory_(errorFactory) {}

void HttpServer::setTlsContext(std::string certPath, std::string keyPath) {
  // takes the cert.pem and key.pem files and sets up the context needed
  // for TLS
  tlsContext_ =
      std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
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
void HttpServer::addListener(const std::string &host, const std::string &port) {
  auto listener = std::make_unique<ListenerSocket>(host, port);
  listener->setSocketNonBlocking();
  tcpListeners_.push_back(std::move(listener));
}

void HttpServer::addTlsListener(const std::string &host,
                                const std::string &port) {
  auto listener = std::make_unique<ListenerSocket>(host, port);
  listener->setSocketNonBlocking();
  tlsListeners_.push_back(std::move(listener));
};

void HttpServer::setRouter(Router &router) { router_ = &router; };

int HttpServer::shutdownEventFd = -1;
void HttpServer::run() {
  signal(SIGPIPE, SIG_IGN);

  shutdownEventFd = eventfd(0, EFD_NONBLOCK);
  signal(SIGINT, [](int) {
    uint64_t v = 1;
    auto n = ::write(shutdownEventFd, &v, 8);
  });
  signal(SIGTERM, [](int) {
    uint64_t v = 1;
    auto n = ::write(shutdownEventFd, &v, 8);
  });
  executor_.spawn(shutdownWatchdog());

  if (!router_)
    throw std::runtime_error("Call setRouter() before run()");

  for (auto &listener : tcpListeners_) {
    listener->listen(LISTEN_BACKLOG);
    executor_.spawn(tcpAcceptLoop(*listener));
  }

  for (auto &listener : tlsListeners_) {
    listener->listen(LISTEN_BACKLOG);
    executor_.spawn(tlsAcceptLoop(*listener));
  }

  executor_.run();
}

ErrorFactory &HttpServer::getErrorFactory() { return errorFactory_; }

Task<void> HttpServer::tcpAcceptLoop(ListenerSocket &listener) {
  for (;;) {
    co_await ReadAwaitable{listener.getFd(),
                           std::chrono::steady_clock::time_point::max()};
    auto stream = std::make_shared<TcpStream>(listener.accept());
    stream->setSocketNonBlocking();
    executor_.spawn(handleConnection(std::move(stream)));
  }
}

Task<void> HttpServer::tlsAcceptLoop(ListenerSocket &listener) {
  for (;;) {
    co_await ReadAwaitable{listener.getFd(),
                           std::chrono::steady_clock::time_point::max()};
    auto stream =
        std::make_shared<TlsStream>(listener.acceptTls(tlsContext_.get()));
    stream->setSocketNonBlocking();
    executor_.spawn(handleConnection(std::move(stream)));
  }
}

Task<void> HttpServer::handleConnection(std::shared_ptr<IStream> stream) {
  int fd = stream->getFd();
  HttpConnection conn(stream, *router_, errorFactory_, shutdown_);
  co_await conn.run();
  tl_executor->unregister(fd);
}

Task<void> HttpServer::shutdownWatchdog() {
  co_await ReadAwaitable{shutdownEventFd,
                         std::chrono::steady_clock::time_point::max()};

  SPDLOG_INFO("Shutdown signal received, draining connections...");
  shutdown_ = true;

  for (auto &listener : tcpListeners_)
    executor_.unregister(listener->getFd());
  for (auto &listener : tlsListeners_)
    executor_.unregister(listener->getFd());

  auto deadline = now() + std::chrono::seconds(GRACEFUL_SHUTDOWN_TIMEOUT_S);
  int acceptLoopCount = tcpListeners_.size() + tlsListeners_.size();

  while (now() < deadline) {
    // + 1 for the watchdog itself
    if (executor_.getOwnedTaskCount() <= acceptLoopCount + 1) {
      SPDLOG_INFO("Graceful Shutdown");
      spdlog::shutdown();
      exit(0);
    }
    auto pollDeadline = now() + std::chrono::seconds(1);
    co_await ReadAwaitable{shutdownEventFd, pollDeadline};
    tl_timed_out = false;
  }

  SPDLOG_INFO("Timeout, Shutting down");
  spdlog::shutdown();
  exit(0);
}
