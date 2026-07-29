#include "UdpSocketBinder.hpp"

#include <QNetworkProxy>

#include "SocketCompat.hpp"

#include <cerrno>
#include <cstring>

namespace wiremic::audio {

bool BindUdpSocket(QUdpSocket& socket, quint16 port, quint16& boundPort,
                    QString& error) {
  if (socket.state() != QAbstractSocket::UnconnectedState) socket.close();

  platform::EnsureSocketsReady();

  const platform::socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd == platform::kInvalidSocket) {
    error = QString::fromStdString(
        platform::SocketErrorText(platform::LastSocketError()));
    return false;
  }

  platform::SetNonBlocking(fd);

  const int enable = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               platform::AsOptionValue(&enable), sizeof(enable));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    error = QString::fromStdString(
        platform::SocketErrorText(platform::LastSocketError()));
    platform::CloseSocket(fd);
    return false;
  }

  sockaddr_in actual{};
  platform::socklen_t actualLength = sizeof(actual);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &actualLength) !=
      0) {
    error = QString::fromStdString(
        platform::SocketErrorText(platform::LastSocketError()));
    platform::CloseSocket(fd);
    return false;
  }

  const quint16 assignedPort = ntohs(actual.sin_port);
  if (assignedPort == 0) {
    error = QStringLiteral("the OS assigned no local port");
    platform::CloseSocket(fd);
    return false;
  }

  socket.setProxy(QNetworkProxy::NoProxy);

  if (!socket.setSocketDescriptor(static_cast<qintptr>(fd), QAbstractSocket::BoundState,
                                   QIODevice::ReadWrite)) {
    error = socket.errorString();
    platform::CloseSocket(fd);
    return false;
  }

  boundPort = assignedPort;
  error.clear();
  return true;
}

}
