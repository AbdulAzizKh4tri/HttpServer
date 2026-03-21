#include "TcpStream.hpp"

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <span>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include "utils.hpp"

TcpStream::TcpStream(int fd, sockaddr_storage addr, socklen_t len)
    : socket_(fd) {
  auto [ip, port] = resolvePeerAddress(addr, len);
  ip_ = ip;
  port_ = port;
}

ssize_t TcpStream::send(const std::span<const unsigned char> &data) const {
  ssize_t n = ::send(socket_.getFd(), data.data(), data.size(), 0);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return 0;
  return n;
}

ReceiveResult TcpStream::receive(std::vector<unsigned char> &data) const {
  ssize_t n = ::recv(socket_.getFd(), data.data(), data.size(), 0);
  if (n > 0)
    return ReceiveResult::data(n);
  if (n == 0)
    return ReceiveResult::closed();
  if (errno == EAGAIN || errno == EWOULDBLOCK)
    return ReceiveResult::wouldBlock();
  if (errno == ECONNRESET)
    return ReceiveResult::closed();
  return ReceiveResult::error();
}

HandshakeResult TcpStream::handshake() { return HandshakeResult::NO_TLS; }

int TcpStream::setSocketNonBlocking() { return socket_.setNonBlocking(); }

std::string TcpStream::getIp() const { return ip_; }
uint16_t TcpStream::getPort() const { return port_; }

int TcpStream::getFd() const { return socket_.getFd(); }
