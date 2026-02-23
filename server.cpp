#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <TcpListenerSocket.hpp>
#include <spdlog/spdlog.h>

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

  TcpListenerSocket listener("0.0.0.0", "8080");
  listener.listen(10);

  TcpConnectionSocket conn = listener.accept();
  while (1) {
    conn.send(conn.recv());
  }

  return 0;
}
