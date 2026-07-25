#include "DiscoveryService.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace wiremic::android {

namespace {
constexpr int kSocketTimeoutMs = 200;
}

DiscoveryService::DiscoveryService(protocol::DeviceInfo localDevice)
    : localDevice_(std::move(localDevice)) {}

DiscoveryService::~DiscoveryService() { stop(); }

void DiscoveryService::setCallback(DeviceListCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
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
  std::lock_guard<std::mutex> lock(mutex_);
  devices_.clear();
}

void DiscoveryService::sendAnnounce(int socketFd) const {
  protocol::AnnouncePacket packet;
  packet.device = localDevice_;
  packet.protoVersion = protocol::kProtocolVersion;
  const auto json = protocol::ToJson(packet);

  struct sockaddr_in broadcastAddress {};
  broadcastAddress.sin_family = AF_INET;
  broadcastAddress.sin_port = htons(protocol::kDiscoveryBroadcastPort);
  broadcastAddress.sin_addr.s_addr = INADDR_BROADCAST;

  sendto(socketFd, json.data(), json.size(), 0,
         reinterpret_cast<struct sockaddr*>(&broadcastAddress),
         sizeof(broadcastAddress));
}

void DiscoveryService::handlePacket(const char* data, size_t length,
                                     const std::string& senderIp) {
  auto parsed = protocol::ParseAnnounce(std::string(data, length));
  if (!parsed) return;
  if (parsed->device.id == localDevice_.id) return;

  parsed->device.ip = senderIp;
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

}  // namespace wiremic::android
