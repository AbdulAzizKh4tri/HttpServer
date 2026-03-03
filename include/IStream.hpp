#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <sys/types.h>
#include <vector>

enum class HandshakeResult { DONE, WANT_READ, WANT_WRITE, ERROR, NO_TLS };

class IStream {
public:
  virtual ssize_t receive(std::vector<std::byte> &data) const = 0;
  virtual ssize_t send(const std::span<const std::byte> &data) const = 0;
  virtual std::string getIp() const = 0;
  virtual uint16_t getPort() const = 0;
  virtual int getFd() const = 0;
  virtual HandshakeResult handshake() const = 0;
  virtual ~IStream() = default;
};
