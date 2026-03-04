#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <unordered_map>

#include "EpollInstance.hpp"
#include "HttpConnection.hpp"
#include "ListenerSocket.hpp"
#include "TlsStream.hpp"

void configureLog() {
#ifdef NDEBUG
  spdlog::set_level(spdlog::level::info);
#else
  spdlog::set_level(spdlog::level::debug);
#endif
  spdlog::set_pattern("[%Y-%m-%d %H:%M] [%^%l%$] [thread %t] %v");
}

int main() {
  configureLog();
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  // --- TLS context setup ---
  auto ctx =
      std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  if (!ctx)
    throw std::runtime_error("Failed to create SSL_CTX");

  if (SSL_CTX_use_certificate_file(ctx.get(), "cert.pem", SSL_FILETYPE_PEM) <=
      0)
    throw std::runtime_error("Failed to load certificate");

  if (SSL_CTX_use_PrivateKey_file(ctx.get(), "key.pem", SSL_FILETYPE_PEM) <= 0)
    throw std::runtime_error("Failed to load private key");

  // --- Listener setup ---
  ListenerSocket TlsListener("localhost", "8443");
  TlsListener.setSocketNonBlocking();
  TlsListener.listen(10);

  ListenerSocket TcpListener("localhost", "8080");
  TcpListener.setSocketNonBlocking();
  TcpListener.listen(10);

  EpollInstance epoll;
  epoll.add(TlsListener.getFd(), EPOLLIN, TlsListener.getFd());
  epoll.add(TcpListener.getFd(), EPOLLIN, TcpListener.getFd());

  std::unordered_map<int, std::shared_ptr<HttpConnection>> connections;

  for (;;) {
    std::vector<epoll_event> events = epoll.wait(64);

    for (auto &event : events) {

      if (event.data.fd == TlsListener.getFd()) {
        // New incoming connection. acceptTls() just wraps the fd + SSL_CTX,
        // no handshake yet — that happens non-blocking via HttpConnection.
        auto stream =
            std::make_shared<TlsStream>(TlsListener.acceptTls(ctx.get()));
        stream->setSocketNonBlocking();

        int fd = stream->getFd();
        auto conn = std::make_shared<HttpConnection>(std::move(stream));
        connections[fd] = conn;

        // Start with EPOLLIN so we can drive the handshake forward.
        // The handshake reads first, so EPOLLIN is the right starting event.
        epoll.add(fd, EPOLLIN | EPOLLET, fd);

      } else if (event.data.fd == TcpListener.getFd()) {
        auto stream = std::make_shared<TcpStream>(TcpListener.accept());
        stream->setSocketNonBlocking();

        int fd = stream->getFd();
        auto conn = std::make_shared<HttpConnection>(std::move(stream));
        connections[fd] = conn;

        epoll.add(fd, EPOLLIN | EPOLLET, fd);
      } else {
        auto it = connections.find(event.data.fd);
        if (it == connections.end())
          continue;

        auto &conn = it->second;

        if (event.events & EPOLLIN)
          conn->onReadable();

        if (event.events & EPOLLOUT)
          conn->onWriteable();

        if (conn->isClosing()) {
          SPDLOG_INFO("Removing connection {}:{}", conn->getIp(),
                      conn->getPort());
          epoll.remove(event.data.fd);
          connections.erase(it);
          continue;
        }

        uint32_t newEvents = EPOLLIN | EPOLLET;
        if (conn->wantsWrite()) {
          SPDLOG_DEBUG("wants write");
          newEvents |= EPOLLOUT;
        }
        epoll.modify(event.data.fd, newEvents, event.data.fd);
      }
    }
  }

  return 0;
}
