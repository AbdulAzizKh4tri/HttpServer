#pragma once

#include <rukh/Task.hpp>
#include <rukh/middleware/Session.hpp>

namespace rukh {

class ISessionStore {
public:
  virtual Task<std::optional<Session>> load(const std::string &id) = 0;
  virtual Task<void> save(const std::string &id, const Session &session) = 0;
  virtual Task<void> destroy(const std::string &id) = 0;
  virtual std::string generateId() = 0;
  virtual ~ISessionStore() = default;
};
} // namespace rukh
