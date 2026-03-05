#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <sys/types.h>
#include <vector>

enum class HandshakeResult { DONE, WANT_READ, WANT_WRITE, ERROR, NO_TLS };

struct ReceiveResult {
  enum class Status { DATA, CLOSED, WOULD_BLOCK, ERROR } status;
  ssize_t bytes = 0;

  static ReceiveResult data(ssize_t n) { return {Status::DATA, n}; }
  static ReceiveResult closed() { return {Status::CLOSED, 0}; }
  static ReceiveResult wouldBlock() { return {Status::WOULD_BLOCK, 0}; }
  static ReceiveResult error() { return {Status::ERROR, 0}; }
};

class IStream {
public:
  virtual ReceiveResult receive(std::vector<unsigned char> &data) const = 0;
  virtual ssize_t send(const std::span<const unsigned char> &data) const = 0;
  virtual std::string getIp() const = 0;
  virtual uint16_t getPort() const = 0;
  virtual int getFd() const = 0;
  virtual HandshakeResult handshake() const = 0;
  virtual ~IStream() = default;
};
