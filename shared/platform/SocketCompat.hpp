#pragma once


#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace wiremic::platform {

using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;

inline int CloseSocket(socket_t fd) { return ::close(fd); }

inline int LastSocketError() { return errno; }

inline bool SetNonBlocking(socket_t fd) {
  return ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0;
}

inline std::string SocketErrorText(int code) {
  return std::string(std::strerror(code));
}

inline void EnsureSocketsReady() {}

using socklen_t = ::socklen_t;
using sockopt_value_t = const void*;

inline sockopt_value_t AsOptionValue(const void* value) { return value; }

}

