#pragma once

#include "HttpRequest.hpp"
#include "HttpTypes.hpp"
#include "ISessionStore.hpp"

struct SessionConfig {
  size_t minIdSize = 8;
  size_t maxIdSize = 64;
};

class SessionMiddleware {
public:
  SessionMiddleware(SessionConfig SessionConfig, ISessionStore &sessionStore);
  Task<Response> operator()(HttpRequest &request, Next next);

private:
  SessionConfig sessionConfig_;
  ISessionStore &sessionStore_;

  std::string sanitize(std::string id) const;
};
