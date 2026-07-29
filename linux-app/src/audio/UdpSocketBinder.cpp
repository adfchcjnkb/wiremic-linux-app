#include "UdpSocketBinder.hpp"

#include <QNetworkProxy>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace wiremic::audio {

bool BindUdpSocket(QUdpSocket& socket, quint16 port, quint16& boundPort,
                    QString& error) {
  if (socket.state() != QAbstractSocket::UnconnectedState) socket.close();

  const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }

  const int enable = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    ::close(fd);
    return false;
  }

  sockaddr_in actual{};
  socklen_t actualLength = sizeof(actual);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &actualLength) !=
      0) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    ::close(fd);
    return false;
  }

  const quint16 assignedPort = ntohs(actual.sin_port);
  if (assignedPort == 0) {
    error = QStringLiteral("the OS assigned no local port");
    ::close(fd);
    return false;
  }

  socket.setProxy(QNetworkProxy::NoProxy);

  if (!socket.setSocketDescriptor(fd, QAbstractSocket::BoundState,
                                   QIODevice::ReadWrite)) {
    error = socket.errorString();
    ::close(fd);
    return false;
  }

  boundPort = assignedPort;
  error.clear();
  return true;
}

}
