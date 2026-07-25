#include "ConnectionManager.hpp"

#include <openssl/rand.h>

#include <algorithm>

namespace wiremic::core {

using namespace std::chrono_literals;

namespace {
constexpr int kReconnectRetryIntervalMs = 2000;
}

ConnectionManager::ConnectionManager(protocol::DeviceInfo localDevice,
                                      std::filesystem::path appDataDir,
                                      ConnectionManagerSettings settings,
                                      quint16 controlPort, QObject* parent)
    : QObject(parent),
      localDevice_(std::move(localDevice)),
      appDataDir_(std::move(appDataDir)),
      settings_(settings),
      certificateManager_(appDataDir_ / "certs"),
      trustedDevices_(appDataDir_ / "trusted_devices.json"),
      controlServer_(certificateManager_, controlPort, this) {
  connect(&controlServer_, &network::ControlServer::connectRequestReceived,
          this, &ConnectionManager::onIncomingRequest);
  connect(&controlServer_, &network::ControlServer::clientDisconnected, this,
          [this](QString requestId, protocol::DisconnectReason reason) {
            if (hasActiveConnection_ &&
                activeConnection_.activeRequestId == requestId.toStdString()) {
              hasActiveConnection_ = false;
              setState(protocol::ConnectionState::Idle);
              emit connectionClosed(reason);
            }
            pendingIncoming_.erase(requestId.toStdString());
          });
  connect(&controlServer_, &network::ControlServer::errorOccurred, this,
          &ConnectionManager::errorOccurred);

  reconnectTimer_.setInterval(kReconnectRetryIntervalMs);
  connect(&reconnectTimer_, &QTimer::timeout, this,
          &ConnectionManager::onReconnectTick);
}

bool ConnectionManager::start() {
  if (!controlServer_.start()) {
    return false;
  }
  localDevice_.controlPort = controlServer_.port();

  discovery_ =
      std::make_unique<network::DiscoveryService>(localDevice_, this);
  connect(discovery_.get(), &network::DiscoveryService::deviceDiscovered,
          this, &ConnectionManager::onDeviceDiscovered);
  connect(discovery_.get(), &network::DiscoveryService::deviceUpdated, this,
          &ConnectionManager::onDeviceDiscovered);
  connect(discovery_.get(), &network::DiscoveryService::deviceStatusChanged,
          this, &ConnectionManager::onDeviceStatusChanged);
  connect(discovery_.get(), &network::DiscoveryService::deviceRemoved, this,
          [this](const QString& deviceId) {
            devices_.erase(deviceId.toStdString());
            emit deviceListChanged();
          });
  connect(discovery_.get(), &network::DiscoveryService::errorOccurred, this,
          &ConnectionManager::errorOccurred);

  if (!discovery_->start()) {
    return false;
  }
  return true;
}

void ConnectionManager::stop() {
  reconnectTimer_.stop();
  if (controlClient_) {
    controlClient_->disconnectFromDevice(
        protocol::DisconnectReason::UserRequested);
    controlClient_.reset();
  }
  controlServer_.stop();
  if (discovery_) discovery_->stop();
  hasActiveConnection_ = false;
}

quint16 ConnectionManager::controlPort() const { return controlServer_.port(); }

void ConnectionManager::onDeviceDiscovered(
    const network::DiscoveredDevice& device) {
  devices_[device.info.id] = device;
  emit deviceListChanged();
}

void ConnectionManager::onDeviceStatusChanged(const QString& deviceId,
                                               network::DeviceStatus status) {
  auto it = devices_.find(deviceId.toStdString());
  if (it != devices_.end()) {
    it->second.status = status;
  }
  emit deviceListChanged();
}

std::vector<network::DiscoveredDevice> ConnectionManager::discoveredDevices()
    const {
  std::vector<network::DiscoveredDevice> result;
  result.reserve(devices_.size());
  for (const auto& [id, device] : devices_) result.push_back(device);
  return result;
}

std::optional<PeerConnectionState> ConnectionManager::activeConnection()
    const {
  if (!hasActiveConnection_) return std::nullopt;
  return activeConnection_;
}

std::vector<security::TrustedDevice> ConnectionManager::trustedDevices()
    const {
  return trustedDevices_.All();
}

void ConnectionManager::revokeTrust(const std::string& deviceId) {
  trustedDevices_.Revoke(deviceId);
}

protocol::AudioCapabilities ConnectionManager::localCapabilities() const {
  protocol::AudioCapabilities capabilities;
  capabilities.sampleRates = {48000, 44100};
  capabilities.codec = protocol::AudioCodec::Opus;
  capabilities.maxBitrateKbps = 128;
  return capabilities;
}

protocol::AudioSession ConnectionManager::negotiateSession(
    const protocol::AudioCapabilities& remoteCapabilities) const {
  protocol::AudioSession session;

  const auto local = localCapabilities();
  uint32_t bestRate = 0;
  for (const auto rate : local.sampleRates) {
    const bool remoteSupports =
        std::find(remoteCapabilities.sampleRates.begin(),
                  remoteCapabilities.sampleRates.end(),
                  rate) != remoteCapabilities.sampleRates.end();
    if (remoteSupports && rate > bestRate) bestRate = rate;
  }
  session.sampleRate = bestRate != 0 ? bestRate : 48000;

  session.channels = 1;
  session.codec = protocol::AudioCodec::Opus;
  session.bitrateKbps =
      std::min(settings_.audioBitrateKbps, remoteCapabilities.maxBitrateKbps);

  switch (settings_.latencyMode) {
    case protocol::LatencyMode::UltraLow5Ms:
      session.frameSizeMs = 5;
      break;
    case protocol::LatencyMode::Low10Ms:
      session.frameSizeMs = 10;
      break;
    case protocol::LatencyMode::Balanced20Ms:
      session.frameSizeMs = 20;
      break;
  }

  RAND_bytes(session.sessionKey.data(),
             static_cast<int>(session.sessionKey.size()));

  return session;
}

void ConnectionManager::onIncomingRequest(protocol::ConnectRequest request,
                                           QString peerFingerprint) {
  if (hasActiveConnection_) {
    controlServer_.reject(request.requestId,
                           protocol::RejectReason::AlreadyConnected);
    return;
  }

  const bool trusted = settings_.rememberTrustedDevices &&
                        trustedDevices_.IsTrusted(
                            request.device.id, peerFingerprint.toStdString());

  if (trusted && settings_.autoConnect) {
    const auto session = negotiateSession(request.capabilities);
    controlServer_.accept(request.requestId, session);

    activeConnection_.device = request.device;
    activeConnection_.activeRequestId = request.requestId;
    hasActiveConnection_ = true;
    setState(protocol::ConnectionState::Streaming);
    emit connectionEstablished(request.device);
    return;
  }

  pendingIncoming_[request.requestId] = {request, peerFingerprint};
  activeConnection_.device = request.device;
  activeConnection_.activeRequestId = request.requestId;
  setState(protocol::ConnectionState::AwaitingApproval);
  emit incomingRequestPending(request, peerFingerprint);
}

void ConnectionManager::approveIncoming(const std::string& requestId) {
  auto it = pendingIncoming_.find(requestId);
  if (it == pendingIncoming_.end()) return;

  const auto& [request, peerFingerprint] = it->second;
  const auto session = negotiateSession(request.capabilities);
  controlServer_.accept(requestId, session);

  if (settings_.rememberTrustedDevices) {
    trustedDevices_.Trust(request.device.id, request.device.name,
                           peerFingerprint.toStdString());
  }

  activeConnection_.device = request.device;
  activeConnection_.activeRequestId = requestId;
  hasActiveConnection_ = true;
  setState(protocol::ConnectionState::Streaming);
  emit connectionEstablished(request.device);

  pendingIncoming_.erase(it);
}

void ConnectionManager::rejectIncoming(const std::string& requestId,
                                        protocol::RejectReason reason) {
  auto it = pendingIncoming_.find(requestId);
  if (it == pendingIncoming_.end()) return;

  controlServer_.reject(requestId, reason);
  pendingIncoming_.erase(it);

  if (activeConnection_.activeRequestId == requestId &&
      !hasActiveConnection_) {
    setState(protocol::ConnectionState::Idle);
  }
}

void ConnectionManager::createControlClient() {
  controlClient_ =
      std::make_unique<network::ControlClient>(certificateManager_, this);
  connect(controlClient_.get(), &network::ControlClient::responseReceived,
          this, &ConnectionManager::onOutgoingResponse);
  connect(controlClient_.get(), &network::ControlClient::timedOut, this,
          &ConnectionManager::onOutgoingTimedOut);
  connect(controlClient_.get(), &network::ControlClient::connectionLost, this,
          &ConnectionManager::onConnectionLost);
  connect(controlClient_.get(), &network::ControlClient::remoteDisconnected,
          this, &ConnectionManager::onRemoteDisconnected);
  connect(controlClient_.get(), &network::ControlClient::errorOccurred, this,
          &ConnectionManager::errorOccurred);
}

void ConnectionManager::requestConnection(const std::string& deviceId) {
  if (hasActiveConnection_) {
    emit connectionFailed(QStringLiteral("ALREADY_CONNECTED"));
    return;
  }

  auto it = devices_.find(deviceId);
  if (it == devices_.end()) {
    emit connectionFailed(QStringLiteral("DEVICE_NOT_FOUND"));
    return;
  }

  const auto& device = it->second.info;

  protocol::ConnectRequest request;
  request.requestId = QUuid::createUuid().toString().toStdString();
  request.device = localDevice_;
  request.certFingerprint =
      certificateManager_.localCertificate().fingerprintSha256;
  request.capabilities = localCapabilities();

  createControlClient();

  lastRemoteHost_ = QString::fromStdString(device.ip);
  lastRemotePort_ = device.controlPort;

  activeConnection_.device = device;
  activeConnection_.activeRequestId = request.requestId;
  hasActiveConnection_ = true;
  setState(protocol::ConnectionState::RequestSent);

  controlClient_->connectToDevice(lastRemoteHost_, lastRemotePort_, request);
}

void ConnectionManager::onOutgoingResponse(protocol::ConnectResponse response) {
  if (response.requestId != activeConnection_.activeRequestId) return;

  if (!response.accepted) {
    hasActiveConnection_ = false;
    setState(protocol::ConnectionState::Idle);
    emit connectionFailed(QString::fromStdString(
        std::string(protocol::ToString(response.reason))));
    return;
  }

  if (settings_.rememberTrustedDevices && controlClient_) {
    trustedDevices_.Trust(
        activeConnection_.device.id, activeConnection_.device.name,
        controlClient_->peerCertificateFingerprint().toStdString());
  }

  setState(protocol::ConnectionState::Streaming);
  emit connectionEstablished(activeConnection_.device);
}

void ConnectionManager::onOutgoingTimedOut(QString requestId) {
  if (requestId.toStdString() != activeConnection_.activeRequestId) return;
  hasActiveConnection_ = false;
  setState(protocol::ConnectionState::Idle);
  emit connectionFailed(QStringLiteral("TIMEOUT"));
}

void ConnectionManager::onConnectionLost(QString requestId) {
  if (requestId.toStdString() != activeConnection_.activeRequestId) return;
  setState(protocol::ConnectionState::Reconnecting);
  reconnectDeadline_ =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(protocol::kReconnectWindowMs);
  reconnectTimer_.start();
}

void ConnectionManager::onReconnectTick() {
  if (std::chrono::steady_clock::now() >= reconnectDeadline_) {
    reconnectTimer_.stop();
    hasActiveConnection_ = false;
    setState(protocol::ConnectionState::Idle);
    emit connectionFailed(QStringLiteral("RECONNECT_TIMEOUT"));
    return;
  }

  if (!controlClient_) return;

  protocol::ConnectRequest request;
  request.requestId = activeConnection_.activeRequestId;
  request.device = localDevice_;
  request.certFingerprint =
      certificateManager_.localCertificate().fingerprintSha256;
  request.capabilities = localCapabilities();

  createControlClient();
  controlClient_->connectToDevice(lastRemoteHost_, lastRemotePort_, request);
}

void ConnectionManager::onRemoteDisconnected(
    protocol::DisconnectReason reason) {
  reconnectTimer_.stop();
  hasActiveConnection_ = false;
  setState(protocol::ConnectionState::Idle);
  emit connectionClosed(reason);
}

void ConnectionManager::disconnectActive() {
  reconnectTimer_.stop();

  if (controlClient_) {
    controlClient_->disconnectFromDevice(
        protocol::DisconnectReason::UserRequested);
    controlClient_.reset();
  } else if (hasActiveConnection_) {
    controlServer_.disconnectClient(
        activeConnection_.activeRequestId,
        protocol::DisconnectReason::UserRequested);
  }

  hasActiveConnection_ = false;
  setState(protocol::ConnectionState::Idle);
}

void ConnectionManager::refreshDiscovery() {
  if (discovery_) discovery_->refreshNow();
}

void ConnectionManager::setState(protocol::ConnectionState state) {
  activeConnection_.state = state;
  emit connectionStateChanged(state);
}

}  // namespace wiremic::core
