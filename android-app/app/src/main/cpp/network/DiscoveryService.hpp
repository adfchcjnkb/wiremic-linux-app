#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Protocol.hpp"

namespace wiremic::android {

enum class DeviceStatus { Online, Offline };

struct DiscoveredDevice {
  protocol::DeviceInfo info;
  DeviceStatus status{DeviceStatus::Online};
  std::chrono::steady_clock::time_point lastSeen;
};

class DiscoveryService {
 public:
  using DeviceListCallback = std::function<void(std::vector<DiscoveredDevice>)>;
  using InviteCallback = std::function<void(protocol::ConnectInvite)>;

  explicit DiscoveryService(protocol::DeviceInfo localDevice);
  ~DiscoveryService();

  bool start();
  void stop();
  void setCallback(DeviceListCallback callback);
  void setInviteCallback(InviteCallback callback);

  [[nodiscard]] std::vector<DiscoveredDevice> devices() const;

 private:
  struct LanInterface {
    std::string name;
    uint32_t address{0};
    uint32_t broadcast{0};
  };

  void run();
  void sendAnnounce(int socketFd) const;
  void sendDatagram(int socketFd, const std::string& payload) const;
  void handlePacket(const char* data, size_t length, const std::string& senderIp);
  void notify();

  [[nodiscard]] static std::vector<LanInterface> LanInterfaces();
  void refreshMulticastMemberships();

  protocol::DeviceInfo localDevice_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  int socketFd_{-1};
  std::vector<std::string> joinedInterfaces_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  DeviceListCallback callback_;
  InviteCallback inviteCallback_;
};

}
