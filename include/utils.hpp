#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdexcept>
#include <string>

struct PeerAddress {
    std::string ip;
    uint16_t port;
};

inline PeerAddress resolvePeerAddress(int fd) {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (sockaddr *)&addr, &len) == -1)
        throw std::runtime_error("Failed to resolve peer address");
    PeerAddress result;
    if (addr.ss_family == AF_INET) {
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((sockaddr_in *)&addr)->sin_addr, ipstr, INET_ADDRSTRLEN);
        result.ip = ipstr;
        result.port = ntohs(((sockaddr_in *)&addr)->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &((sockaddr_in6 *)&addr)->sin6_addr, ipstr, INET6_ADDRSTRLEN);
        result.ip = ipstr;
        result.port = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
    } else {
        throw std::runtime_error("Unknown address family");
    }
    return result;
}
