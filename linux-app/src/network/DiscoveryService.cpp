#include "DiscoveryService.hpp"

#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>

#include "SocketCompat.hpp"

#include <cerrno>
#include <cstring>

namespace wiremic::network {

using namespace std::chrono_literals;

namespace {
constexpr int kSweepIntervalMs = 1000;
constexpr int kRebindIntervalMs = 1000;

bool IsTunnelInterface(const QString& name) {
  static const char* const kTunnelPrefixes[] = {
      "tun", "tap", "wg", "ppp", "ipsec", "utun", "gpd",
      "nordlynx", "proton", "tailscale", "zt", "vboxnet", "docker"};
  for (const char* prefix : kTunnelPrefixes) {
    if (name.startsWith(QLatin1String(prefix), Qt::CaseInsensitive)) return true;
  }
  return false;
}
}

std::vector<DiscoveryService::LanInterface> DiscoveryService::LanInterfaces() {
  std::vector<LanInterface> result;

  for (const auto& interface : QNetworkInterface::allInterfaces()) {
    const auto flags = interface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) ||
        !flags.testFlag(QNetworkInterface::IsRunning) ||
        flags.testFlag(QNetworkInterface::IsLoopBack)) {
      continue;
    }
    if (IsTunnelInterface(interface.name())) continue;

    for (const auto& entry : interface.addressEntries()) {
      if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
      if (entry.ip().isLoopback()) continue;

      LanInterface lan;
      lan.name = interface.name();
      lan.address = entry.ip();
      lan.broadcast = entry.broadcast();
      result.push_back(lan);
    }
  }

  return result;
}

void DiscoveryService::refreshMulticastMemberships() {
  if (!bound_) return;

  const auto interfaces = LanInterfaces();

  QStringList current;
  current.reserve(static_cast<qsizetype>(interfaces.size()));
  for (const auto& lan : interfaces) current << lan.name;
  current.sort();

  if (current == joinedGroups_) return;
  joinedGroups_ = current;

  const auto fd = static_cast<platform::socket_t>(socket_.socketDescriptor());
  if (fd == platform::kInvalidSocket) return;

  const QHostAddress group(
      QString::fromLatin1(protocol::kDiscoveryMulticastGroup));

  for (const auto& lan : interfaces) {
    ip_mreq request{};
    request.imr_multiaddr.s_addr = htonl(group.toIPv4Address());
    request.imr_interface.s_addr = htonl(lan.address.toIPv4Address());

    ::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                 platform::AsOptionValue(&request), sizeof(request));
  }
}

DiscoveryService::DiscoveryService(protocol::DeviceInfo localDevice,
                                    QObject* parent)
    : QObject(parent), localDevice_(std::move(localDevice)) {
  connect(&socket_, &QUdpSocket::readyRead, this,
          &DiscoveryService::onReadyRead);
  connect(&announceTimer_, &QTimer::timeout, this,
          &DiscoveryService::onAnnounceTimer);
  connect(&sweepTimer_, &QTimer::timeout, this,
          &DiscoveryService::onSweepTimer);
  connect(&rebindTimer_, &QTimer::timeout, this,
          &DiscoveryService::onRebindTimer);
}

DiscoveryService::~DiscoveryService() { stop(); }

bool DiscoveryService::bindSocket() {
  if (socket_.state() != QAbstractSocket::UnconnectedState) socket_.close();

  platform::EnsureSocketsReady();

  const platform::socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd == platform::kInvalidSocket) {
    bindError_ = QString::fromStdString(
        platform::SocketErrorText(platform::LastSocketError()));
    bound_ = false;
    return false;
  }

  const int enable = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               platform::AsOptionValue(&enable), sizeof(enable));
#ifdef SO_REUSEPORT
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
               platform::AsOptionValue(&enable), sizeof(enable));
#endif
  ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
               platform::AsOptionValue(&enable), sizeof(enable));

  const int multicastTtl = 1;
  ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
               platform::AsOptionValue(&multicastTtl), sizeof(multicastTtl));
  const int multicastLoop = 1;
  ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
               platform::AsOptionValue(&multicastLoop), sizeof(multicastLoop));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(protocol::kDiscoveryBroadcastPort);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    bindError_ = QString::fromStdString(
        platform::SocketErrorText(platform::LastSocketError()));
    platform::CloseSocket(fd);
    bound_ = false;
    return false;
  }

  if (!socket_.setSocketDescriptor(static_cast<qintptr>(fd), QAbstractSocket::BoundState,
                                    QIODevice::ReadWrite)) {
    bindError_ = socket_.errorString();
    platform::CloseSocket(fd);
    bound_ = false;
    return false;
  }

  bindError_.clear();
  bound_ = true;
  joinedGroups_.clear();
  refreshMulticastMemberships();
  return true;
}

bool DiscoveryService::start() {
  if (running_) return true;

  if (!bindSocket()) {
    emit errorOccurred(
        QStringLiteral("Failed to bind discovery socket on port %1: %2")
            .arg(protocol::kDiscoveryBroadcastPort)
            .arg(bindError_));
    return false;
  }

  running_ = true;
  announceTimer_.start(protocol::kAnnounceIntervalMs);
  sweepTimer_.start(kSweepIntervalMs);
  QTimer::singleShot(250, this, &DiscoveryService::sendAnnounce);
  return true;
}

void DiscoveryService::stop() {
  if (!running_) return;
  running_ = false;
  bound_ = false;
  joinedGroups_.clear();
  announceTimer_.stop();
  sweepTimer_.stop();
  rebindTimer_.stop();
  socket_.close();
  devices_.clear();
}

void DiscoveryService::refreshNow() {
  if (!running_) return;
  sendAnnounce();
}

std::vector<DiscoveredDevice> DiscoveryService::devices() const {
  std::vector<DiscoveredDevice> result;
  result.reserve(devices_.size());
  for (const auto& [id, device] : devices_) result.push_back(device);
  return result;
}

void DiscoveryService::scheduleRebind() {
  bound_ = false;
  if (running_ && !rebindTimer_.isActive()) {
    rebindTimer_.start(kRebindIntervalMs);
  }
}

void DiscoveryService::onRebindTimer() {
  if (!running_) {
    rebindTimer_.stop();
    return;
  }
  if (bindSocket()) {
    rebindTimer_.stop();
    sendAnnounce();
  }
}

void DiscoveryService::sendAnnounce() {
  if (!running_) return;

  if (!bound_ || socket_.state() != QAbstractSocket::BoundState) {
    scheduleRebind();
    return;
  }

  protocol::AnnouncePacket packet;
  packet.device = localDevice_;
  packet.protoVersion = protocol::kProtocolVersion;

  if (!broadcast(QByteArray::fromStdString(protocol::ToJson(packet)))) {
    emit errorOccurred(QStringLiteral("Failed to broadcast announce: %1")
                            .arg(socket_.errorString()));
    scheduleRebind();
  }
}

bool DiscoveryService::sendToInterface(const QByteArray& bytes,
                                        const LanInterface& lan) {
  bool sentAny = false;

  if (!lan.broadcast.isNull()) {
    sentAny = socket_.writeDatagram(bytes, lan.broadcast,
                                     protocol::kDiscoveryBroadcastPort) >= 0;
  }

  const auto fd = static_cast<platform::socket_t>(socket_.socketDescriptor());
  if (fd != platform::kInvalidSocket) {
    in_addr egress{};
    egress.s_addr = htonl(lan.address.toIPv4Address());
    if (::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                     platform::AsOptionValue(&egress), sizeof(egress)) == 0) {
      const QHostAddress group(
          QString::fromLatin1(protocol::kDiscoveryMulticastGroup));
      sentAny = socket_.writeDatagram(bytes, group,
                                       protocol::kDiscoveryBroadcastPort) >= 0 ||
                 sentAny;
    }
  }

  return sentAny;
}

bool DiscoveryService::broadcast(const QByteArray& bytes) {
  const auto interfaces = LanInterfaces();

  bool sentAny = false;
  for (const auto& lan : interfaces) {
    sentAny = sendToInterface(bytes, lan) || sentAny;
  }

  sentAny = socket_.writeDatagram(bytes, QHostAddress::Broadcast,
                                   protocol::kDiscoveryBroadcastPort) >= 0 ||
             sentAny;

  return sentAny;
}

bool DiscoveryService::sendInvite(const protocol::ConnectInvite& invite) {
  if (!running_) return false;
  if (!bound_ || socket_.state() != QAbstractSocket::BoundState) {
    scheduleRebind();
    return false;
  }
  return broadcast(QByteArray::fromStdString(protocol::ToJson(invite)));
}

void DiscoveryService::onReadyRead() {
  if (!running_) return;
  while (socket_.hasPendingDatagrams()) {
    QNetworkDatagram datagram = socket_.receiveDatagram(
        static_cast<qint64>(protocol::kMaxDiscoveryPacketBytes));
    if (datagram.isValid()) {
      handlePacket(datagram.data(), datagram.senderAddress());
    }
  }
}

void DiscoveryService::handlePacket(const QByteArray& data,
                                     const QHostAddress& sender) {
  if (!running_) return;
  const auto text = data.toStdString();

  if (auto invite = protocol::ParseInvite(text)) {
    if (invite->device.id == localDevice_.id) return;
    if (invite->targetDeviceId != localDevice_.id) return;
    if (invite->protoVersion != protocol::kProtocolVersion) return;

    QHostAddress inviterAddress(sender);
    if (inviterAddress.protocol() == QAbstractSocket::IPv6Protocol) {
      bool convertible = false;
      const quint32 ipv4 = inviterAddress.toIPv4Address(&convertible);
      if (convertible) inviterAddress = QHostAddress(ipv4);
    }
    invite->device.ip = inviterAddress.toString().toStdString();
    emit inviteReceived(*invite);
    return;
  }

  auto parsed = protocol::ParseAnnounce(text);
  if (!parsed) return;
  if (parsed->device.id == localDevice_.id) return;

  if (parsed->protoVersion != protocol::kProtocolVersion) return;

  QHostAddress senderAddress(sender);
  if (senderAddress.protocol() == QAbstractSocket::IPv6Protocol) {
    bool convertible = false;
    const quint32 ipv4 = senderAddress.toIPv4Address(&convertible);
    if (convertible) senderAddress = QHostAddress(ipv4);
  }
  parsed->device.ip = senderAddress.toString().toStdString();

  const auto now = std::chrono::steady_clock::now();
  auto it = devices_.find(parsed->device.id);
  if (it == devices_.end()) {
    DiscoveredDevice discovered;
    discovered.info = parsed->device;
    discovered.status = DeviceStatus::Online;
    discovered.lastSeen = now;
    discovered.missedAnnounces = 0;
    devices_.emplace(parsed->device.id, discovered);
    emit deviceDiscovered(devices_.at(parsed->device.id));
    return;
  }

  it->second.info = parsed->device;
  it->second.lastSeen = now;
  it->second.missedAnnounces = 0;
  const bool wasOffline = it->second.status == DeviceStatus::Offline;
  it->second.status = DeviceStatus::Online;
  emit deviceUpdated(it->second);
  if (wasOffline) {
    emit deviceStatusChanged(QString::fromStdString(parsed->device.id),
                              DeviceStatus::Online);
  }
}

void DiscoveryService::onAnnounceTimer() { sendAnnounce(); }

void DiscoveryService::onSweepTimer() {
  if (!running_) return;

  refreshMulticastMemberships();

  const auto now = std::chrono::steady_clock::now();
  std::vector<std::string> toRemove;

  for (auto& [id, device] : devices_) {
    const auto silenceMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              device.lastSeen)
            .count();
    if (silenceMs >= protocol::kRemoveAfterSilenceMs) {
      toRemove.push_back(id);
      continue;
    }
    const auto expectedMissed = silenceMs / protocol::kAnnounceIntervalMs;
    device.missedAnnounces = static_cast<int>(expectedMissed);
    if (expectedMissed >= protocol::kOfflineAfterMissedAnnounces &&
        device.status == DeviceStatus::Online) {
      device.status = DeviceStatus::Offline;
      emit deviceStatusChanged(QString::fromStdString(id),
                                DeviceStatus::Offline);
    }
  }

  for (const auto& id : toRemove) {
    devices_.erase(id);
    emit deviceRemoved(QString::fromStdString(id));
  }
}

}
