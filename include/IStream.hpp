#include <cstdint>
#include <span>
#include <string>
#include <sys/types.h>
#include <vector>

class IStream {
public:
  virtual ssize_t receive(std::vector<std::byte> &data) = 0;
  virtual ssize_t send(const std::span<const std::byte> data) = 0;
  virtual std::string getIp() const = 0;
  virtual uint16_t getPort() const = 0;
  virtual ~IStream() = default;
};
