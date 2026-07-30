#include "DiscoveryService.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace wiremic::android {

namespace {
constexpr int kSocketTimeoutMs = 200;

bool IsTunnelInterface(const char* name) {
  static const char* const kTunnelPrefixes[] = {"tun",  "tap",       "ppp",
                                                 "ipsec", "nordlynx", "wg",
                                                 "rmnet_ims", "clat"};
  for (const char* prefix : kTunnelPrefixes) {
    if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) return true;
  }
  return false;
}
}

std::vector<DiscoveryService::LanInterface> DiscoveryService::LanInterfaces() {
  std::vector<LanInterface> result;

  struct ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0 || addresses == nullptr) return result;

  for (struct ifaddrs* it = addresses; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    if ((it->ifa_flags & IFF_UP) == 0) continue;
    if ((it->ifa_flags & IFF_RUNNING) == 0) continue;
    if ((it->ifa_flags & IFF_LOOPBACK) != 0) continue;
    if (it->ifa_name == nullptr || IsTunnelInterface(it->ifa_name)) continue;

    LanInterface lan;
    lan.name = it->ifa_name;
    lan.address =
        reinterpret_cast<struct sockaddr_in*>(it->ifa_addr)->sin_addr.s_addr;

    if ((it->ifa_flags & IFF_BROADCAST) != 0 && it->ifa_broadaddr != nullptr &&
        it->ifa_broadaddr->sa_family == AF_INET) {
      lan.broadcast = reinterpret_cast<struct sockaddr_in*>(it->ifa_broadaddr)
                          ->sin_addr.s_addr;
    }

    result.push_back(lan);
  }

  freeifaddrs(addresses);
  return result;
}

void DiscoveryService::refreshMulticastMemberships() {
  if (socketFd_ < 0) return;

  const auto interfaces = LanInterfaces();

  std::vector<std::string> current;
  current.reserve(interfaces.size());
  for (const auto& lan : interfaces) current.push_back(lan.name);
  std::sort(current.begin(), current.end());

  if (current == joinedInterfaces_) return;
  joinedInterfaces_ = current;

  for (const auto& lan : interfaces) {
    struct ip_mreq request {};
    request.imr_multiaddr.s_addr = inet_addr(protocol::kDiscoveryMulticastGroup);
    request.imr_interface.s_addr = lan.address;
    setsockopt(socketFd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request,
               sizeof(request));
  }
}

DiscoveryService::DiscoveryService(protocol::DeviceInfo localDevice)
    : localDevice_(std::move(localDevice)) {}

DiscoveryService::~DiscoveryService() { stop(); }

void DiscoveryService::setCallback(DeviceListCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
}

void DiscoveryService::setInviteCallback(InviteCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  inviteCallback_ = std::move(callback);
}

bool DiscoveryService::start() {
  if (running_) return true;

  socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFd_ < 0) return false;

  int broadcastEnable = 1;
  setsockopt(socketFd_, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
             sizeof(broadcastEnable));
  int reuseEnable = 1;
  setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &reuseEnable,
             sizeof(reuseEnable));
  setsockopt(socketFd_, SOL_SOCKET, SO_REUSEPORT, &reuseEnable,
             sizeof(reuseEnable));

  int multicastTtl = 1;
  setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_TTL, &multicastTtl,
             sizeof(multicastTtl));
  int multicastLoop = 1;
  setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_LOOP, &multicastLoop,
             sizeof(multicastLoop));

  struct timeval timeout {};
  timeout.tv_sec = 0;
  timeout.tv_usec = kSocketTimeoutMs * 1000;
  setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(protocol::kDiscoveryBroadcastPort);

  if (bind(socketFd_, reinterpret_cast<struct sockaddr*>(&address),
           sizeof(address)) < 0) {
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  joinedInterfaces_.clear();
  refreshMulticastMemberships();

  running_ = true;
  thread_ = std::thread(&DiscoveryService::run, this);
  return true;
}

void DiscoveryService::stop() {
  if (!running_) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (socketFd_ >= 0) {
    close(socketFd_);
    socketFd_ = -1;
  }
  joinedInterfaces_.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  devices_.clear();
}

void DiscoveryService::sendDatagram(int socketFd,
                                     const std::string& payload) const {
  const auto interfaces = LanInterfaces();

  for (const auto& lan : interfaces) {
    struct sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(protocol::kDiscoveryBroadcastPort);

    if (lan.broadcast != 0) {
      destination.sin_addr.s_addr = lan.broadcast;
      sendto(socketFd, payload.data(), payload.size(), 0,
             reinterpret_cast<struct sockaddr*>(&destination),
             sizeof(destination));
    }

    struct in_addr egress {};
    egress.s_addr = lan.address;
    if (setsockopt(socketFd, IPPROTO_IP, IP_MULTICAST_IF, &egress,
                   sizeof(egress)) == 0) {
      destination.sin_addr.s_addr =
          inet_addr(protocol::kDiscoveryMulticastGroup);
      sendto(socketFd, payload.data(), payload.size(), 0,
             reinterpret_cast<struct sockaddr*>(&destination),
             sizeof(destination));
    }
  }

  struct sockaddr_in fallback {};
  fallback.sin_family = AF_INET;
  fallback.sin_port = htons(protocol::kDiscoveryBroadcastPort);
  fallback.sin_addr.s_addr = INADDR_BROADCAST;
  sendto(socketFd, payload.data(), payload.size(), 0,
         reinterpret_cast<struct sockaddr*>(&fallback), sizeof(fallback));
}

void DiscoveryService::sendAnnounce(int socketFd) const {
  protocol::AnnouncePacket packet;
  packet.device = localDevice_;
  packet.protoVersion = protocol::kProtocolVersion;
  sendDatagram(socketFd, protocol::ToJson(packet));
}

// Answers whoever was just heard from directly instead of waiting for the next
// broadcast round. Broadcast is the part of discovery that networks interfere
// with -- access points drop it, Windows Firewall drops unsolicited inbound
// datagrams -- and a directed reply means it only has to work in one of the two
// directions for the two devices to find each other.
void DiscoveryService::sendDirectedAnnounce(int socketFd,
                                             const std::string& peerIp) const {
  protocol::AnnouncePacket packet;
  packet.device = localDevice_;
  packet.protoVersion = protocol::kProtocolVersion;
  packet.reply = true;

  const std::string payload = protocol::ToJson(packet);

  struct sockaddr_in destination {};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(protocol::kDiscoveryBroadcastPort);
  if (inet_pton(AF_INET, peerIp.c_str(), &destination.sin_addr) != 1) return;

  sendto(socketFd, payload.data(), payload.size(), 0,
         reinterpret_cast<struct sockaddr*>(&destination), sizeof(destination));
}

void DiscoveryService::handlePacket(const char* data, size_t length,
                                     const std::string& senderIp) {
  const std::string text(data, length);

  if (auto invite = protocol::ParseInvite(text)) {
    if (invite->device.id == localDevice_.id) return;
    if (invite->targetDeviceId != localDevice_.id) return;
    if (invite->protoVersion != protocol::kProtocolVersion) return;

    invite->device.ip = senderIp;
    InviteCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = inviteCallback_;
    }
    if (callback) callback(*invite);
    return;
  }

  auto parsed = protocol::ParseAnnounce(text);
  if (!parsed) return;
  if (parsed->device.id == localDevice_.id) return;

  parsed->device.ip = senderIp;

  // A reply is never answered, so the two devices exchange at most one extra
  // datagram per announce and cannot talk each other into a loop.
  if (!parsed->reply && socketFd_ >= 0) {
    sendDirectedAnnounce(socketFd_, senderIp);
  }

  const auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(parsed->device.id);
    if (it == devices_.end()) {
      DiscoveredDevice discovered;
      discovered.info = parsed->device;
      discovered.status = DeviceStatus::Online;
      discovered.lastSeen = now;
      devices_.emplace(parsed->device.id, std::move(discovered));
    } else {
      it->second.info = parsed->device;
      it->second.lastSeen = now;
      it->second.status = DeviceStatus::Online;
    }
  }
  notify();
}

void DiscoveryService::notify() {
  DeviceListCallback callback;
  std::vector<DiscoveredDevice> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = callback_;
    snapshot.reserve(devices_.size());
    for (const auto& [id, device] : devices_) snapshot.push_back(device);
  }
  if (callback) callback(std::move(snapshot));
}

void DiscoveryService::run() {
  char buffer[protocol::kMaxDiscoveryPacketBytes];
  auto lastAnnounce = std::chrono::steady_clock::now() -
                       std::chrono::milliseconds(protocol::kAnnounceIntervalMs);
  auto lastSweep = std::chrono::steady_clock::now();

  while (running_) {
    const auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAnnounce)
            .count() >= protocol::kAnnounceIntervalMs) {
      sendAnnounce(socketFd_);
      lastAnnounce = now;
    }

    struct sockaddr_in senderAddress {};
    socklen_t senderLength = sizeof(senderAddress);
    const ssize_t received =
        recvfrom(socketFd_, buffer, sizeof(buffer), 0,
                 reinterpret_cast<struct sockaddr*>(&senderAddress),
                 &senderLength);

    if (received > 0) {
      char ipBuffer[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &senderAddress.sin_addr, ipBuffer, sizeof(ipBuffer));
      handlePacket(buffer, static_cast<size_t>(received), ipBuffer);
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSweep)
            .count() >= 1000) {
      refreshMulticastMemberships();

      bool changed = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> toRemove;
        for (auto& [id, device] : devices_) {
          const auto silenceMs =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - device.lastSeen)
                  .count();
          if (silenceMs >= protocol::kRemoveAfterSilenceMs) {
            toRemove.push_back(id);
          } else if (silenceMs >= protocol::kAnnounceIntervalMs *
                                      protocol::kOfflineAfterMissedAnnounces &&
                     device.status == DeviceStatus::Online) {
            device.status = DeviceStatus::Offline;
            changed = true;
          }
        }
        for (const auto& id : toRemove) {
          devices_.erase(id);
          changed = true;
        }
      }
      if (changed) notify();
      lastSweep = now;
    }
  }
}

}
