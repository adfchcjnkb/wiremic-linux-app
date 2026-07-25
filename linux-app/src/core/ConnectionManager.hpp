#pragma once

#include <QObject>
#include <QTimer>
#include <QUuid>

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "CertificateManager.hpp"
#include "ControlClient.hpp"
#include "ControlServer.hpp"
#include "DiscoveryService.hpp"
#include "Protocol.hpp"
#include "TrustedDeviceStore.hpp"

namespace wiremic::core {

struct ConnectionManagerSettings {
  bool autoConnect{false};
  bool rememberTrustedDevices{true};
  protocol::LatencyMode latencyMode{protocol::LatencyMode::Low10Ms};
  uint32_t audioBitrateKbps{96};
};

struct PeerConnectionState {
  protocol::DeviceInfo device;
  protocol::ConnectionState state{protocol::ConnectionState::Idle};
  std::string activeRequestId;
};

class ConnectionManager : public QObject {
  Q_OBJECT

 public:
  ConnectionManager(protocol::DeviceInfo localDevice,
                     std::filesystem::path appDataDir,
                     ConnectionManagerSettings settings,
                     quint16 controlPort = protocol::kDefaultControlPort,
                     QObject* parent = nullptr);

  bool start();
  void stop();

  void requestConnection(const std::string& deviceId);
  void approveIncoming(const std::string& requestId);
  void rejectIncoming(const std::string& requestId,
                       protocol::RejectReason reason);
  void disconnectActive();
  void refreshDiscovery();

  [[nodiscard]] std::vector<network::DiscoveredDevice> discoveredDevices()
      const;
  [[nodiscard]] std::optional<PeerConnectionState> activeConnection() const;
  [[nodiscard]] std::vector<security::TrustedDevice> trustedDevices() const;
  [[nodiscard]] quint16 controlPort() const;
  void revokeTrust(const std::string& deviceId);

 signals:
  void deviceListChanged();
  void incomingRequestPending(protocol::ConnectRequest request,
                               QString peerFingerprint);
  void connectionStateChanged(protocol::ConnectionState state);
  void connectionEstablished(protocol::DeviceInfo peer);
  void connectionClosed(protocol::DisconnectReason reason);
  void connectionFailed(QString reason);
  void errorOccurred(QString message);

 private:
  void onDeviceDiscovered(const network::DiscoveredDevice& device);
  void onDeviceStatusChanged(const QString& deviceId,
                              network::DeviceStatus status);
  void onIncomingRequest(protocol::ConnectRequest request,
                          QString peerFingerprint);
  void onOutgoingResponse(protocol::ConnectResponse response);
  void onOutgoingTimedOut(QString requestId);
  void onConnectionLost(QString requestId);
  void onRemoteDisconnected(protocol::DisconnectReason reason);
  void onReconnectTick();
  void createControlClient();

  void setState(protocol::ConnectionState state);
  protocol::AudioCapabilities localCapabilities() const;
  protocol::AudioSession negotiateSession(
      const protocol::AudioCapabilities& remoteCapabilities) const;

  protocol::DeviceInfo localDevice_;
  std::filesystem::path appDataDir_;
  ConnectionManagerSettings settings_;

  security::CertificateManager certificateManager_;
  security::TrustedDeviceStore trustedDevices_;
  std::unique_ptr<network::DiscoveryService> discovery_;
  network::ControlServer controlServer_;
  std::unique_ptr<network::ControlClient> controlClient_;

  std::unordered_map<std::string, network::DiscoveredDevice> devices_;
  std::unordered_map<std::string,
                      std::pair<protocol::ConnectRequest, QString>>
      pendingIncoming_;
  PeerConnectionState activeConnection_;
  bool hasActiveConnection_{false};

  QTimer reconnectTimer_;
  std::chrono::steady_clock::time_point reconnectDeadline_;
  QString lastRemoteHost_;
  quint16 lastRemotePort_{0};
};

}  // namespace wiremic::core
