#include "ConnectionManager.hpp"

#include <openssl/rand.h>

#include <QHostAddress>

#include <algorithm>

namespace wiremic::core {

using namespace std::chrono_literals;

namespace {
constexpr int kReconnectRetryIntervalMs = 2000;
constexpr int kCaptureDrainIntervalMs = 5;

const char* AudioServerName(platform::AudioServerKind kind) {
  switch (kind) {
    case platform::AudioServerKind::PipeWire:
      return "PipeWire";
    case platform::AudioServerKind::PulseAudio:
      return "PulseAudio";
    case platform::AudioServerKind::None:
      break;
  }
  return "none";
}
}  // namespace

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
            const auto id = requestId.toStdString();
            if (hasActiveConnection_ &&
                activeConnection_.activeRequestId == id) {
              hasActiveConnection_ = false;
              stopAudio();
              setState(protocol::ConnectionState::Idle);
              emit connectionClosed(reason);
            }
            // A peer that disappears while its request is still awaiting
            // approval leaves a prompt on screen that can never be answered.
            if (pendingIncoming_.erase(id) > 0) {
              if (activeConnection_.activeRequestId == id &&
                  !hasActiveConnection_) {
                setState(protocol::ConnectionState::Idle);
              }
              emit incomingRequestCancelled(requestId);
            }
          });
  connect(&controlServer_, &network::ControlServer::errorOccurred, this,
          &ConnectionManager::errorOccurred);

  reconnectTimer_.setInterval(kReconnectRetryIntervalMs);
  connect(&reconnectTimer_, &QTimer::timeout, this,
          &ConnectionManager::onReconnectTick);

  captureDrainTimer_.setInterval(kCaptureDrainIntervalMs);
  connect(&captureDrainTimer_, &QTimer::timeout, this,
          &ConnectionManager::drainCapturedAudio);

  audioServerKind_ = platform::DetectAudioServer();
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
  reconnectInFlight_ = false;
  stopAudio();
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

protocol::ConnectionState ConnectionManager::connectionState() const {
  return activeConnection_.state;
}

protocol::DeviceInfo ConnectionManager::peerDevice() const {
  if (activeConnection_.state == protocol::ConnectionState::Idle) return {};
  return activeConnection_.device;
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

bool ConnectionManager::virtualMicActive() const {
  return virtualMic_ && virtualMic_->isRunning();
}

QString ConnectionManager::audioBackendName() const {
  return QString::fromLatin1(AudioServerName(audioServerKind_));
}

ConnectionManagerSettings ConnectionManager::settings() const {
  return settings_;
}

void ConnectionManager::updateSettings(
    const ConnectionManagerSettings& settings) {
  settings_ = settings;
}

bool ConnectionManager::startAudioReceive(protocol::AudioSession& session) {
  stopAudio();

  audio::SessionKey key{};
  std::copy(session.sessionKey.begin(), session.sessionKey.end(), key.begin());

  // Port 0 asks the OS for a free port; the bound port is read back below and
  // handed to the peer in the CONNECT_RESPONSE. This has to succeed — without
  // a port there is nothing to tell the peer.
  audioReceiver_ = std::make_unique<audio::AudioReceiver>(
      key, session.sampleRate, session.channels, session.frameSizeMs,
      /*localPort=*/0, this);

  connect(audioReceiver_.get(), &audio::AudioReceiver::errorOccurred, this,
          &ConnectionManager::errorOccurred);
  connect(audioReceiver_.get(), &audio::AudioReceiver::pcmFrameReady, this,
          [this](std::vector<int16_t> samples, bool) {
            if (virtualMic_ && !samples.empty()) {
              virtualMic_->pushSamples(samples.data(), samples.size());
            }
          });

  if (!audioReceiver_->start()) {
    audioReceiver_.reset();
    return false;
  }

  // The virtual microphone is best-effort: if no audio server is running we
  // still complete the pairing and keep the session up, and say so loudly,
  // rather than rejecting a peer for a locally recoverable problem. Audio is
  // simply discarded until a mic exists.
  platform::VirtualMicConfig micConfig;
  micConfig.sampleRate = session.sampleRate;
  micConfig.channels = session.channels;

  virtualMic_ = platform::CreateVirtualMic(micConfig, audioServerKind_);
  if (!virtualMic_) {
    emit errorOccurred(QStringLiteral(
        "No PipeWire or PulseAudio server is running — connected, but no "
        "virtual microphone could be created."));
  } else if (!virtualMic_->start()) {
    virtualMic_.reset();
    emit errorOccurred(
        QStringLiteral("Connected, but creating the virtual microphone via %1 "
                       "failed.")
            .arg(audioBackendName()));
  }

  session.udpPort = audioReceiver_->port();
  emit audioStateChanged(virtualMicActive(), audioBackendName());
  return true;
}

bool ConnectionManager::startAudioSend(const protocol::AudioSession& session,
                                        const QString& remoteHost) {
  stopAudio();

  const QHostAddress host(remoteHost);
  if (host.isNull() || session.udpPort == 0) {
    emit errorOccurred(
        QStringLiteral("Peer did not provide a usable audio endpoint."));
    return false;
  }

  audio::SessionKey key{};
  std::copy(session.sessionKey.begin(), session.sessionKey.end(), key.begin());

  audioSender_ = std::make_unique<audio::AudioSender>(
      key, session.sampleRate, session.channels,
      static_cast<int>(session.bitrateKbps), session.frameSizeMs, host,
      session.udpPort, this);
  connect(audioSender_.get(), &audio::AudioSender::errorOccurred, this,
          &ConnectionManager::errorOccurred);

  if (!audioSender_->start()) {
    audioSender_.reset();
    return false;
  }

  platform::AudioCaptureConfig captureConfig;
  captureConfig.sampleRate = session.sampleRate;
  captureConfig.channels = session.channels;

  audioCapture_ = std::make_unique<platform::PipeWireAudioCapture>(
      captureConfig, [this](const int16_t* samples, size_t count) {
        // Realtime PipeWire thread: only append under the lock, never touch
        // Qt objects from here.
        std::lock_guard<std::mutex> lock(captureMutex_);
        capturedSamples_.insert(capturedSamples_.end(), samples,
                                 samples + count);
      });

  if (!audioCapture_->start()) {
    audioCapture_.reset();
    audioSender_->stop();
    audioSender_.reset();
    emit errorOccurred(
        QStringLiteral("Failed to open a local microphone for streaming."));
    return false;
  }

  captureDrainTimer_.start();
  emit audioStateChanged(false, audioBackendName());
  return true;
}

void ConnectionManager::drainCapturedAudio() {
  if (!audioSender_) return;

  const int frameSamples = audioSender_->frameSamples();
  if (frameSamples <= 0) return;
  const size_t chunk = static_cast<size_t>(frameSamples);

  while (true) {
    std::vector<int16_t> frame;
    {
      std::lock_guard<std::mutex> lock(captureMutex_);
      if (capturedSamples_.size() < chunk) break;
      frame.assign(capturedSamples_.begin(),
                    capturedSamples_.begin() + static_cast<long>(chunk));
      capturedSamples_.erase(capturedSamples_.begin(),
                              capturedSamples_.begin() +
                                  static_cast<long>(chunk));
    }
    audioSender_->pushPcmFrame(frame.data(), frameSamples);
  }
}

void ConnectionManager::stopAudio() {
  const bool wasActive =
      audioReceiver_ != nullptr || audioSender_ != nullptr;

  captureDrainTimer_.stop();

  if (audioCapture_) {
    audioCapture_->stop();
    audioCapture_.reset();
  }
  if (audioSender_) {
    audioSender_->stop();
    audioSender_.reset();
  }
  if (audioReceiver_) {
    audioReceiver_->stop();
    audioReceiver_.reset();
  }
  if (virtualMic_) {
    virtualMic_->stop();
    virtualMic_.reset();
  }
  {
    std::lock_guard<std::mutex> lock(captureMutex_);
    capturedSamples_.clear();
  }

  if (wasActive) emit audioStateChanged(false, audioBackendName());
}

void ConnectionManager::onIncomingRequest(protocol::ConnectRequest request,
                                           QString peerFingerprint) {
  if (hasActiveConnection_) {
    controlServer_.reject(request.requestId,
                           protocol::RejectReason::AlreadyConnected);
    return;
  }

  // A prompt is already on screen for someone else. Answer this one now rather
  // than silently replacing the visible request with a different device.
  if (!pendingIncoming_.empty()) {
    controlServer_.reject(request.requestId,
                           protocol::RejectReason::AlreadyConnected);
    return;
  }

  if (request.protoVersion != protocol::kProtocolVersion) {
    controlServer_.reject(request.requestId,
                           protocol::RejectReason::UnsupportedProtocol);
    emit errorOccurred(
        QStringLiteral("Rejected %1: it speaks protocol version %2, this "
                       "build speaks %3.")
            .arg(QString::fromStdString(request.device.name))
            .arg(request.protoVersion)
            .arg(protocol::kProtocolVersion));
    return;
  }

  const bool trusted = settings_.rememberTrustedDevices &&
                        trustedDevices_.IsTrusted(
                            request.device.id, peerFingerprint.toStdString());

  if (trusted && settings_.autoConnect) {
    auto session = negotiateSession(request.capabilities);
    if (!startAudioReceive(session)) {
      controlServer_.reject(request.requestId,
                             protocol::RejectReason::UnsupportedCodec);
      return;
    }
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

  // Copied, not bound by reference: the entry is erased below, and the signals
  // emitted before that point can re-enter and rehash the map.
  const protocol::ConnectRequest request = it->second.first;
  const QString peerFingerprint = it->second.second;
  pendingIncoming_.erase(it);

  auto session = negotiateSession(request.capabilities);
  if (!startAudioReceive(session)) {
    controlServer_.reject(requestId, protocol::RejectReason::UnsupportedCodec);
    setState(protocol::ConnectionState::Idle);
    emit connectionFailed(QStringLiteral("AUDIO_UNAVAILABLE"));
    return;
  }
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
  // Close the previous attempt's socket before dropping it, so a stale TLS
  // connection isn't left half-open on the peer while we dial again.
  if (controlClient_) {
    controlClient_->disconnect(this);
    controlClient_->disconnectFromDevice(
        protocol::DisconnectReason::UserRequested);
    controlClient_.reset();
  }

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
  reconnectInFlight_ = false;
  setState(protocol::ConnectionState::RequestSent);

  controlClient_->connectToDevice(lastRemoteHost_, lastRemotePort_, request);
}

void ConnectionManager::onOutgoingResponse(protocol::ConnectResponse response) {
  if (response.requestId != activeConnection_.activeRequestId) return;

  reconnectInFlight_ = false;

  if (!response.accepted) {
    hasActiveConnection_ = false;
    reconnectTimer_.stop();
    stopAudio();
    setState(protocol::ConnectionState::Idle);
    emit connectionFailed(QString::fromStdString(
        std::string(protocol::ToString(response.reason))));
    return;
  }

  reconnectTimer_.stop();

  if (settings_.rememberTrustedDevices && controlClient_) {
    trustedDevices_.Trust(
        activeConnection_.device.id, activeConnection_.device.name,
        controlClient_->peerCertificateFingerprint().toStdString());
  }

  // We are the initiator, so the peer is the receiver: capture locally and
  // stream to the endpoint it just handed us. The control session stays up if
  // this fails — the peer accepted us — but say so, because a connection that
  // silently carries no audio looks like the app is broken.
  if (!response.session) {
    emit errorOccurred(QStringLiteral(
        "Peer accepted the connection but offered no audio endpoint."));
  } else if (!startAudioSend(*response.session, lastRemoteHost_)) {
    emit errorOccurred(QStringLiteral(
        "Connected, but the microphone stream could not be started."));
  }

  setState(protocol::ConnectionState::Streaming);
  emit connectionEstablished(activeConnection_.device);
}

void ConnectionManager::onOutgoingTimedOut(QString requestId) {
  if (requestId.toStdString() != activeConnection_.activeRequestId) return;
  reconnectInFlight_ = false;

  // A timeout during the reconnect window is just a failed retry; leave the
  // reconnect timer running until the window itself expires.
  if (reconnectTimer_.isActive()) return;

  hasActiveConnection_ = false;
  stopAudio();
  setState(protocol::ConnectionState::Idle);
  emit connectionFailed(QStringLiteral("TIMEOUT"));
}

void ConnectionManager::onConnectionLost(QString requestId) {
  if (requestId.toStdString() != activeConnection_.activeRequestId) return;
  stopAudio();
  setState(protocol::ConnectionState::Reconnecting);
  reconnectDeadline_ =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(protocol::kReconnectWindowMs);
  reconnectInFlight_ = false;
  reconnectTimer_.start();
}

void ConnectionManager::onReconnectTick() {
  if (std::chrono::steady_clock::now() >= reconnectDeadline_) {
    reconnectTimer_.stop();
    reconnectInFlight_ = false;
    hasActiveConnection_ = false;
    stopAudio();
    setState(protocol::ConnectionState::Idle);
    emit connectionFailed(QStringLiteral("RECONNECT_TIMEOUT"));
    return;
  }

  // Don't stack a fresh TLS handshake on top of one that is still in flight —
  // the previous attempt gets its full 20 s request timeout to land.
  if (reconnectInFlight_) return;

  protocol::ConnectRequest request;
  request.requestId = activeConnection_.activeRequestId;
  request.device = localDevice_;
  request.certFingerprint =
      certificateManager_.localCertificate().fingerprintSha256;
  request.capabilities = localCapabilities();

  createControlClient();
  reconnectInFlight_ = true;
  controlClient_->connectToDevice(lastRemoteHost_, lastRemotePort_, request);
}

void ConnectionManager::onRemoteDisconnected(
    protocol::DisconnectReason reason) {
  reconnectTimer_.stop();
  reconnectInFlight_ = false;
  hasActiveConnection_ = false;
  stopAudio();
  setState(protocol::ConnectionState::Idle);
  emit connectionClosed(reason);
}

void ConnectionManager::disconnectActive() {
  reconnectTimer_.stop();
  reconnectInFlight_ = false;
  stopAudio();

  if (controlClient_) {
    controlClient_->disconnectFromDevice(
        protocol::DisconnectReason::UserRequested);
    controlClient_.reset();
  } else if (hasActiveConnection_) {
    controlServer_.disconnectClient(
        activeConnection_.activeRequestId,
        protocol::DisconnectReason::UserRequested);
  }

  const bool wasConnected = hasActiveConnection_;
  hasActiveConnection_ = false;
  setState(protocol::ConnectionState::Idle);
  if (wasConnected) {
    emit connectionClosed(protocol::DisconnectReason::UserRequested);
  }
}

void ConnectionManager::refreshDiscovery() {
  if (discovery_) discovery_->refreshNow();
}

void ConnectionManager::setState(protocol::ConnectionState state) {
  activeConnection_.state = state;
  emit connectionStateChanged(state);
}

}  // namespace wiremic::core
