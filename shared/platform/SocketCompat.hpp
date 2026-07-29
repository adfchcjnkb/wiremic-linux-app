#pragma once

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <cstdint>
#include <string>

namespace wiremic::platform {

using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;

inline int CloseSocket(socket_t fd) { return ::closesocket(fd); }

inline int LastSocketError() { return ::WSAGetLastError(); }

inline bool SetNonBlocking(socket_t fd) {
  u_long mode = 1;
  return ::ioctlsocket(fd, FIONBIO, &mode) == 0;
}

inline std::string SocketErrorText(int code) {
  char* buffer = nullptr;
  const DWORD length = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(code),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&buffer), 0, nullptr);
  std::string text = length && buffer ? std::string(buffer, length)
                                       : std::string("socket error");
  if (buffer) ::LocalFree(buffer);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

// Winsock needs explicit startup. Constructing this once before any socket is
// created is enough; repeated construction is reference counted by Winsock.
class WinsockScope {
 public:
  WinsockScope() {
    WSADATA data{};
    ok_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WinsockScope() {
    if (ok_) ::WSACleanup();
  }
  WinsockScope(const WinsockScope&) = delete;
  WinsockScope& operator=(const WinsockScope&) = delete;
  [[nodiscard]] bool ok() const { return ok_; }

 private:
  bool ok_{false};
};

inline void EnsureSocketsReady() { static WinsockScope scope; }

using socklen_t = int;
using sockopt_value_t = const char*;

inline sockopt_value_t AsOptionValue(const void* value) {
  return static_cast<sockopt_value_t>(value);
}

}

#else

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

#endif
