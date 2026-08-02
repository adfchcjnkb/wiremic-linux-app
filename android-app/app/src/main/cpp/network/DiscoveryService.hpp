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

  // Announces straight to one address instead of waiting to be found. This is
  // the way out when broadcast and multicast are both being swallowed -- the
  // person reads the IP off their computer and types it in, and the computer's
  // reply puts it in the device list like any other discovery.
  bool probeHost(const std::string& host) const;

  // Asks for the socket to be torn down and opened again on the next pass of
  // the discovery loop. A socket keeps the network it was created on for its
  // whole life, so when a VPN comes up or goes down -- or the phone moves
  // between Wi-Fi and tethering -- the existing one is still bound to a network
  // that may no longer reach anything.
  void requestRebind();

 private:
  struct LanInterface {
    std::string name;
    uint32_t address{0};
    uint32_t broadcast{0};
  };

  void run();
  bool openSocket();
  void closeSocket();
  void sendAnnounce(int socketFd) const;
  void sendDirectedAnnounce(int socketFd, const std::string& peerIp) const;
  void sendDatagram(int socketFd, const std::string& payload) const;
  void handlePacket(const char* data, size_t length, const std::string& senderIp);
  void notify();

  [[nodiscard]] static std::vector<LanInterface> LanInterfaces();
  void refreshMulticastMemberships();

  protocol::DeviceInfo localDevice_;
  std::atomic<bool> running_{false};
  std::atomic<bool> rebindRequested_{false};
  std::thread thread_;
  int socketFd_{-1};
  std::vector<std::string> joinedInterfaces_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  DeviceListCallback callback_;
  InviteCallback inviteCallback_;
};

}
