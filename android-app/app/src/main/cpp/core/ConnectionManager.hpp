#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioSender.hpp"
#include "CertificateManager.hpp"
#include "ControlClient.hpp"
#include "DiscoveryService.hpp"
#include "Protocol.hpp"
#include "TrustedDeviceStore.hpp"

namespace wiremic::android {

struct ConnectionManagerSettings {
  bool rememberTrustedDevices{true};
  protocol::LatencyMode latencyMode{protocol::LatencyMode::Low10Ms};
  uint32_t audioBitrateKbps{96};
};

struct PeerState {
  protocol::DeviceInfo device;
  protocol::ConnectionState state{protocol::ConnectionState::Idle};
  std::string activeRequestId;
};

class ConnectionManager {
 public:
  using DeviceListCallback = std::function<void(std::vector<DiscoveredDevice>)>;
  using StateCallback = std::function<void(protocol::ConnectionState)>;
  using EstablishedCallback = std::function<void(protocol::DeviceInfo)>;
  using ClosedCallback = std::function<void(protocol::DisconnectReason)>;
  using FailedCallback = std::function<void(std::string)>;
  using ErrorCallback = std::function<void(std::string)>;

  ConnectionManager(protocol::DeviceInfo localDevice,
                     std::filesystem::path appDataDir,
                     ConnectionManagerSettings settings);
  ~ConnectionManager();

  bool start();
  void stop();

  void requestConnection(const std::string& deviceId);
  void disconnectActive();
  void refreshDiscovery();
  bool probeHost(const std::string& host);

  [[nodiscard]] std::vector<DiscoveredDevice> discoveredDevices() const;
  [[nodiscard]] std::optional<PeerState> activeConnection() const;

  void setDeviceListCallback(DeviceListCallback callback);
  void setStateCallback(StateCallback callback);
  void setEstablishedCallback(EstablishedCallback callback);
  void setClosedCallback(ClosedCallback callback);
  void setFailedCallback(FailedCallback callback);
  void setErrorCallback(ErrorCallback callback);

 private:
  protocol::AudioCapabilities localCapabilities() const;
  void setState(protocol::ConnectionState state);
  void onInvite(protocol::ConnectInvite invite);

  protocol::DeviceInfo localDevice_;
  std::filesystem::path appDataDir_;
  ConnectionManagerSettings settings_;

  security::CertificateManager certificateManager_;
  security::TrustedDeviceStore trustedDevices_;
  std::unique_ptr<DiscoveryService> discovery_;
  std::unique_ptr<ControlClient> controlClient_;
  std::unique_ptr<AudioSender> audioSender_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DiscoveredDevice> devices_;
  PeerState activeConnection_;
  bool hasActiveConnection_{false};
  std::string lastInviteId_;

  DeviceListCallback deviceListCallback_;
  StateCallback stateCallback_;
  EstablishedCallback establishedCallback_;
  ClosedCallback closedCallback_;
  FailedCallback failedCallback_;
  ErrorCallback errorCallback_;
};

}
