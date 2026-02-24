#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <TcpConnectionSocket.hpp>
#include <TcpListenerSocket.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>

#include "EpollInstance.hpp"

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

  TcpListenerSocket listener("localhost", "8080");
  listener.setSocketNonBlocking();
  listener.listen(10);

  EpollInstance epoll;
  epoll.add(listener.getFd(), EPOLLIN, listener.getFd());

  std::unordered_map<int, std::shared_ptr<TcpConnectionSocket>> connections;

  for (;;) {
    std::vector<epoll_event> events = epoll.wait(64);
    for (auto &event : events) {

      if (event.data.fd == listener.getFd()) {
        auto newConnection =
            std::make_shared<TcpConnectionSocket>(listener.accept());
        newConnection->setSocketNonBlocking();
        int connFd = newConnection->getFd();
        connections[connFd] = newConnection;
        epoll.add(connFd, EPOLLIN | EPOLLET, connFd);
      } else {
        auto connection = connections[event.data.fd];
        std::vector<std::byte> data(4096);
        int n = connection->receive(data);
        data.resize(n);
        SPDLOG_DEBUG("Received message of size {} : {}", data.size(),
                     reinterpret_cast<const char *>(data.data()));
        if (data.empty()) {
          SPDLOG_INFO("Connection {}:{} disconnected", connection->getIp(),
                      connection->getPort());
          epoll.remove(event.data.fd);
          connections.erase(event.data.fd);
        }
        for (auto &[cfd, c] : connections) {
          if (cfd == connection->getFd())
            continue;

          c->send(data);
        }
      }
    }
  }

  return 0;
}
