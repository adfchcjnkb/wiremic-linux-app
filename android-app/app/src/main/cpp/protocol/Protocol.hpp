#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wiremic::protocol {

inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr uint16_t kDiscoveryBroadcastPort = 47500;
inline constexpr uint16_t kDefaultControlPort = 47600;
inline constexpr int kAnnounceIntervalMs = 2000;
inline constexpr int kOfflineAfterMissedAnnounces = 3;
inline constexpr int kRemoveAfterSilenceMs = 15000;
inline constexpr int kConnectRequestTimeoutMs = 20000;
inline constexpr int kKeepaliveIntervalMs = 3000;
inline constexpr int kKeepaliveMissedLimit = 3;
inline constexpr int kReconnectWindowMs = 30000;
inline constexpr size_t kMaxDiscoveryPacketBytes = 512;
inline constexpr size_t kMaxAudioPacketBytes = 1200;
inline constexpr size_t kAudioHeaderBytes = 34;
inline constexpr size_t kPolyTagBytes = 16;
inline constexpr size_t kSessionKeyBytes = 32;

enum class Platform : uint8_t { Linux, Android };

enum class ConnectionType : uint8_t { Wifi, Ethernet, Hotspot, Unknown };

enum class DeviceStatus : uint8_t { Online, Offline, Connected };

enum class ConnectionState : uint8_t {
  Idle,
  Discovering,
  RequestSent,
  AwaitingApproval,
  Accepted,
  Streaming,
  Disconnected,
  Reconnecting
};

enum class RejectReason : uint8_t {
  None,
  RejectedByUser,
  AlreadyConnected,
  UnsupportedCodec,
  Timeout,
  UnsupportedProtocol
};

enum class AudioCodec : uint8_t { Opus, PcmFallback };

enum class LatencyMode : uint8_t { UltraLow5Ms, Low10Ms, Balanced20Ms };

struct DeviceInfo {
  std::string id;
  std::string name;
  std::string model;
  Platform platform{};
  std::string ip;
  ConnectionType connectionType{ConnectionType::Unknown};
  uint16_t controlPort{kDefaultControlPort};
};

struct AudioCapabilities {
  std::vector<uint32_t> sampleRates;
  AudioCodec codec{AudioCodec::Opus};
  uint32_t maxBitrateKbps{128};
};

struct AudioSession {
  uint16_t udpPort{};
  uint32_t sampleRate{48000};
  uint8_t channels{1};
  AudioCodec codec{AudioCodec::Opus};
  uint32_t bitrateKbps{96};
  uint8_t frameSizeMs{10};
  std::array<uint8_t, kSessionKeyBytes> sessionKey{};
};

struct ConnectRequest {
  std::string requestId;
  DeviceInfo device;
  std::string certFingerprint;
  AudioCapabilities capabilities;
};

struct ConnectResponse {
  std::string requestId;
  bool accepted{false};
  RejectReason reason{RejectReason::None};
  std::optional<AudioSession> session;
};

struct KeepAlive {
  uint64_t sequence{};
};

struct KeepAliveAck {
  uint64_t sequence{};
};

enum class DisconnectReason : uint8_t {
  UserRequested,
  Timeout,
  ProtocolError,
  RemoteShutdown
};

struct DisconnectMessage {
  DisconnectReason reason{DisconnectReason::UserRequested};
};

enum class ControlMessageType {
  Unknown,
  ConnectRequest,
  ConnectResponse,
  KeepAlive,
  KeepAliveAck,
  Disconnect
};

ControlMessageType PeekMessageType(const std::string& json);

std::string ToJson(const KeepAlive& message);
std::string ToJson(const KeepAliveAck& message);
std::string ToJson(const DisconnectMessage& message);
std::optional<KeepAlive> ParseKeepAlive(const std::string& json);
std::optional<KeepAliveAck> ParseKeepAliveAck(const std::string& json);
std::optional<DisconnectMessage> ParseDisconnect(const std::string& json);

struct AnnouncePacket {
  DeviceInfo device;
  uint16_t protoVersion{kProtocolVersion};
};

std::string ToJson(const AnnouncePacket& packet);
std::optional<AnnouncePacket> ParseAnnounce(const std::string& json);

std::string ToJson(const ConnectRequest& request);
std::optional<ConnectRequest> ParseConnectRequest(const std::string& json);

std::string ToJson(const ConnectResponse& response);
std::optional<ConnectResponse> ParseConnectResponse(const std::string& json);

const char* ToString(Platform value);
const char* ToString(ConnectionType value);
const char* ToString(RejectReason value);
std::optional<Platform> ParsePlatform(const std::string& value);
std::optional<ConnectionType> ParseConnectionType(const std::string& value);

}  // namespace wiremic::protocol
