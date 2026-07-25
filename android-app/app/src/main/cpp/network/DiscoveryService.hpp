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

  explicit DiscoveryService(protocol::DeviceInfo localDevice);
  ~DiscoveryService();

  bool start();
  void stop();
  void setCallback(DeviceListCallback callback);

  [[nodiscard]] std::vector<DiscoveredDevice> devices() const;

 private:
  void run();
  void sendAnnounce(int socketFd) const;
  void handlePacket(const char* data, size_t length, const std::string& senderIp);
  void notify();

  protocol::DeviceInfo localDevice_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  int socketFd_{-1};

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  DeviceListCallback callback_;
};

}  // namespace wiremic::android
