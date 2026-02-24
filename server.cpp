#include <memory>
#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <TcpConnectionSocket.hpp>
#include <TcpListenerSocket.hpp>
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

  return 0;
}
