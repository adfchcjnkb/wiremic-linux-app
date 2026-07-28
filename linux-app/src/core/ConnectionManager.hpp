#pragma once

#include <QObject>
#include <QTimer>
#include <QUuid>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AudioReceiver.hpp"
#include "AudioSender.hpp"
#include "CertificateManager.hpp"
#include "ControlClient.hpp"
#include "ControlServer.hpp"
#include "DiscoveryService.hpp"
#include "PipeWireAudioCapture.hpp"
#include "Protocol.hpp"
#include "TrustedDeviceStore.hpp"
#include "VirtualMicBackend.hpp"

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
  [[nodiscard]] protocol::ConnectionState connectionState() const;
  [[nodiscard]] protocol::DeviceInfo peerDevice() const;
  [[nodiscard]] std::vector<security::TrustedDevice> trustedDevices() const;
  [[nodiscard]] quint16 controlPort() const;
  void revokeTrust(const std::string& deviceId);

  [[nodiscard]] ConnectionManagerSettings settings() const;
  void updateSettings(const ConnectionManagerSettings& settings);

  [[nodiscard]] bool virtualMicActive() const;
  [[nodiscard]] QString audioBackendName() const;

 signals:
  void deviceListChanged();
  void incomingRequestPending(protocol::ConnectRequest request,
                               QString peerFingerprint);
  void incomingRequestCancelled(QString requestId);
  void connectionStateChanged(protocol::ConnectionState state);
  void connectionEstablished(protocol::DeviceInfo peer);
  void connectionClosed(protocol::DisconnectReason reason);
  void connectionFailed(QString reason);
  void errorOccurred(QString message);
  void audioStateChanged(bool micActive, QString backendName);

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
  void onInviteReceived(const protocol::ConnectInvite& invite);
  void onInviteTimeout();
  void createControlClient();
  bool inviteDevice(const network::DiscoveredDevice& device);
  void dialDevice(const protocol::DeviceInfo& device);

  void setState(protocol::ConnectionState state);
  protocol::AudioCapabilities localCapabilities() const;
  protocol::AudioSession negotiateSession(
      const protocol::AudioCapabilities& remoteCapabilities) const;

  bool startAudioReceive(protocol::AudioSession& session);
  bool startAudioSend(const protocol::AudioSession& session,
                       const QString& remoteHost);
  void stopAudio();
  void drainCapturedAudio();

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

  QTimer inviteTimer_;
  std::string invitedDeviceId_;

  QTimer reconnectTimer_;
  bool reconnectInFlight_{false};
  std::chrono::steady_clock::time_point reconnectDeadline_;
  QString lastRemoteHost_;
  quint16 lastRemotePort_{0};

  std::unique_ptr<audio::AudioReceiver> audioReceiver_;
  std::unique_ptr<platform::VirtualMicBackend> virtualMic_;
  std::unique_ptr<audio::AudioSender> audioSender_;
  std::unique_ptr<platform::PipeWireAudioCapture> audioCapture_;
  platform::AudioServerKind audioServerKind_{platform::AudioServerKind::None};

  std::mutex captureMutex_;
  std::vector<int16_t> capturedSamples_;
  QTimer captureDrainTimer_;
};

}
