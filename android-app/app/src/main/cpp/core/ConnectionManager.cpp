#include "ConnectionManager.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <cstdio>

namespace wiremic::android {

namespace {

std::string GenerateUuidV4() {
  unsigned char bytes[16];
  RAND_bytes(bytes, sizeof(bytes));
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

  char buffer[37];
  std::snprintf(
      buffer, sizeof(buffer),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12],
      bytes[13], bytes[14], bytes[15]);
  return std::string(buffer);
}

}

ConnectionManager::ConnectionManager(protocol::DeviceInfo localDevice,
                                      std::filesystem::path appDataDir,
                                      ConnectionManagerSettings settings)
    : localDevice_(std::move(localDevice)),
      appDataDir_(std::move(appDataDir)),
      settings_(settings),
      certificateManager_(appDataDir_ / "certs"),
      trustedDevices_(appDataDir_ / "trusted_devices.json") {}

ConnectionManager::~ConnectionManager() { stop(); }

bool ConnectionManager::start() {
  discovery_ = std::make_unique<DiscoveryService>(localDevice_);
  discovery_->setCallback([this](std::vector<DiscoveredDevice> list) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      devices_.clear();
      for (auto& device : list) devices_[device.info.id] = device;
    }
    if (deviceListCallback_) deviceListCallback_(std::move(list));
  });

  discovery_->setInviteCallback(
      [this](protocol::ConnectInvite invite) { onInvite(std::move(invite)); });

  return discovery_->start();
}

void ConnectionManager::stop() {
  disconnectActive();
  if (discovery_) {
    discovery_->stop();
    discovery_.reset();
  }
}

std::vector<DiscoveredDevice> ConnectionManager::discoveredDevices() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<DiscoveredDevice> result;
  result.reserve(devices_.size());
  for (const auto& [id, device] : devices_) result.push_back(device);
  return result;
}

std::optional<PeerState> ConnectionManager::activeConnection() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!hasActiveConnection_) return std::nullopt;
  return activeConnection_;
}

void ConnectionManager::setDeviceListCallback(DeviceListCallback callback) {
  deviceListCallback_ = std::move(callback);
}
void ConnectionManager::setStateCallback(StateCallback callback) {
  stateCallback_ = std::move(callback);
}
void ConnectionManager::setEstablishedCallback(EstablishedCallback callback) {
  establishedCallback_ = std::move(callback);
}
void ConnectionManager::setClosedCallback(ClosedCallback callback) {
  closedCallback_ = std::move(callback);
}
void ConnectionManager::setFailedCallback(FailedCallback callback) {
  failedCallback_ = std::move(callback);
}
void ConnectionManager::setErrorCallback(ErrorCallback callback) {
  errorCallback_ = std::move(callback);
}

protocol::AudioCapabilities ConnectionManager::localCapabilities() const {
  protocol::AudioCapabilities capabilities;
  capabilities.sampleRates = {48000, 44100};
  capabilities.codec = protocol::AudioCodec::Opus;
  capabilities.maxBitrateKbps = 128;
  return capabilities;
}

void ConnectionManager::setState(protocol::ConnectionState state) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    activeConnection_.state = state;
  }
  if (stateCallback_) stateCallback_(state);
}

void ConnectionManager::onInvite(protocol::ConnectInvite invite) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hasActiveConnection_) return;
    if (invite.inviteId == lastInviteId_) return;
    lastInviteId_ = invite.inviteId;

    auto it = devices_.find(invite.device.id);
    if (it == devices_.end()) {
      DiscoveredDevice discovered;
      discovered.info = invite.device;
      discovered.status = DeviceStatus::Online;
      discovered.lastSeen = std::chrono::steady_clock::now();
      devices_[invite.device.id] = discovered;
    } else if (it->second.info.ip.empty()) {
      it->second.info.ip = invite.device.ip;
    }
  }

  requestConnection(invite.device.id);
}

void ConnectionManager::requestConnection(const std::string& deviceId) {
  bool alreadyConnected = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    alreadyConnected = hasActiveConnection_;
  }
  if (alreadyConnected) {
    if (failedCallback_) failedCallback_("ALREADY_CONNECTED");
    return;
  }

  DiscoveredDevice target;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(deviceId);
    if (it == devices_.end()) {
      if (failedCallback_) failedCallback_("DEVICE_NOT_FOUND");
      return;
    }
    target = it->second;
  }

  protocol::ConnectRequest request;
  request.requestId = GenerateUuidV4();
  request.device = localDevice_;
  request.certFingerprint = certificateManager_.localCertificate().fingerprintSha256;
  request.capabilities = localCapabilities();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    activeConnection_.device = target.info;
    activeConnection_.activeRequestId = request.requestId;
    hasActiveConnection_ = true;
  }
  setState(protocol::ConnectionState::RequestSent);

  controlClient_ = std::make_unique<ControlClient>(certificateManager_);

  controlClient_->setResponseCallback([this,
                                        host = target.info.ip](
                                           protocol::ConnectResponse response) {
    std::string activeId;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      activeId = activeConnection_.activeRequestId;
    }
    if (response.requestId != activeId) return;

    if (!response.accepted) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        hasActiveConnection_ = false;
      }
      setState(protocol::ConnectionState::Idle);
      if (failedCallback_) {
        failedCallback_(std::string(protocol::ToString(response.reason)));
      }
      return;
    }

    if (settings_.rememberTrustedDevices && controlClient_) {
      std::lock_guard<std::mutex> lock(mutex_);
      trustedDevices_.Trust(activeConnection_.device.id,
                             activeConnection_.device.name,
                             controlClient_->peerCertificateFingerprint());
    }

    if (response.session) {
      audioSender_ = std::make_unique<AudioSender>();
      audioSender_->setErrorCallback([this](std::string message) {
        if (errorCallback_) errorCallback_(std::move(message));
      });
      audioSender_->start(*response.session, host, response.session->udpPort);
    }

    setState(protocol::ConnectionState::Streaming);
    protocol::DeviceInfo device;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      device = activeConnection_.device;
    }
    if (establishedCallback_) establishedCallback_(device);
  });

  controlClient_->setTimeoutCallback([this]() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hasActiveConnection_ = false;
    }
    setState(protocol::ConnectionState::Idle);
    if (failedCallback_) failedCallback_("TIMEOUT");
  });

  controlClient_->setConnectionLostCallback([this]() {
    if (audioSender_) audioSender_->stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      hasActiveConnection_ = false;
    }
    setState(protocol::ConnectionState::Idle);
    if (failedCallback_) failedCallback_("CONNECTION_LOST");
  });

  controlClient_->setRemoteDisconnectCallback(
      [this](protocol::DisconnectReason reason) {
        if (audioSender_) audioSender_->stop();
        {
          std::lock_guard<std::mutex> lock(mutex_);
          hasActiveConnection_ = false;
        }
        setState(protocol::ConnectionState::Idle);
        if (closedCallback_) closedCallback_(reason);
      });

  controlClient_->setErrorCallback([this](std::string message) {
    if (errorCallback_) errorCallback_(std::move(message));
  });

  controlClient_->connectToDevice(target.info.ip, target.info.controlPort,
                                   request);
}

void ConnectionManager::disconnectActive() {
  if (audioSender_) {
    audioSender_->stop();
    audioSender_.reset();
  }
  if (controlClient_) {
    controlClient_->disconnectFromDevice(protocol::DisconnectReason::UserRequested);
    controlClient_.reset();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hasActiveConnection_ = false;
  }
  setState(protocol::ConnectionState::Idle);
}

void ConnectionManager::refreshDiscovery() {}

}
