#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "HttpServer.hpp"
#include "logUtils.hpp"

using json = nlohmann::json;

int main() {
  configureLog();
  SPDLOG_DEBUG("C++ standard: {}", __cplusplus);

  Router router;

  router.setCorsOrigins({"http://localhost:8080", "https://localhost:8443"});

  router.get("/", [](const HttpRequest &request) {
    auto nameIt = request.params.find("name");
    std::string name;
    if (nameIt == request.params.end())
      name = "World!";
    else
      name = std::string(nameIt->second);

    return HttpResponse(200, "Hello " + name + "!");
  });

  router.post("/", [](const HttpRequest &request) {
    json data = json::parse(request.body);
    auto res = HttpResponse(200, "Hello, " + std::string(data["name"]) + "!");
    res.setHeader("Content-Type", "text/plain");
    return res;
  });

  router.put("/", [](const HttpRequest &request) { return HttpResponse(200); });

  HttpServer server;
  server.setTlsContext("cert.pem", "key.pem");
  server.setRouter(router);
  server.addListener("localhost", "8080");
  server.addTlsListener("localhost", "8443");
  server.run();

  return 0;
}
