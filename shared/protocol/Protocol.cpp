#include "Protocol.hpp"

#include <nlohmann/json.hpp>

namespace wiremic::protocol {

using json = nlohmann::json;

const char* ToString(Platform value) {
  switch (value) {
    case Platform::Linux:
      return "linux";
    case Platform::Android:
      return "android";
  }
  return "unknown";
}

const char* ToString(ConnectionType value) {
  switch (value) {
    case ConnectionType::Wifi:
      return "wifi";
    case ConnectionType::Ethernet:
      return "ethernet";
    case ConnectionType::Hotspot:
      return "hotspot";
    case ConnectionType::Unknown:
      return "unknown";
  }
  return "unknown";
}

const char* ToString(RejectReason value) {
  switch (value) {
    case RejectReason::None:
      return "NONE";
    case RejectReason::RejectedByUser:
      return "REJECTED_BY_USER";
    case RejectReason::AlreadyConnected:
      return "ALREADY_CONNECTED";
    case RejectReason::UnsupportedCodec:
      return "UNSUPPORTED_CODEC";
    case RejectReason::Timeout:
      return "TIMEOUT";
    case RejectReason::UnsupportedProtocol:
      return "UNSUPPORTED_PROTOCOL";
  }
  return "NONE";
}

const char* ToString(AudioRole value) {
  switch (value) {
    case AudioRole::Sender:
      return "sender";
    case AudioRole::Receiver:
      return "receiver";
  }
  return "sender";
}

std::optional<AudioRole> ParseAudioRole(const std::string& value) {
  if (value == "sender") return AudioRole::Sender;
  if (value == "receiver") return AudioRole::Receiver;
  return std::nullopt;
}

std::optional<Platform> ParsePlatform(const std::string& value) {
  if (value == "linux") return Platform::Linux;
  if (value == "android") return Platform::Android;
  return std::nullopt;
}

std::optional<ConnectionType> ParseConnectionType(const std::string& value) {
  if (value == "wifi") return ConnectionType::Wifi;
  if (value == "ethernet") return ConnectionType::Ethernet;
  if (value == "hotspot") return ConnectionType::Hotspot;
  return ConnectionType::Unknown;
}

namespace {

RejectReason ParseRejectReason(const std::string& value) {
  if (value == "REJECTED_BY_USER") return RejectReason::RejectedByUser;
  if (value == "ALREADY_CONNECTED") return RejectReason::AlreadyConnected;
  if (value == "UNSUPPORTED_CODEC") return RejectReason::UnsupportedCodec;
  if (value == "TIMEOUT") return RejectReason::Timeout;
  if (value == "UNSUPPORTED_PROTOCOL") return RejectReason::UnsupportedProtocol;
  return RejectReason::None;
}

json DeviceToJson(const DeviceInfo& device) {
  return json{
      {"id", device.id},
      {"name", device.name},
      {"model", device.model},
      {"platform", ToString(device.platform)},
      {"ip", device.ip},
      {"connectionType", ToString(device.connectionType)},
      {"controlPort", device.controlPort},
  };
}

std::optional<DeviceInfo> DeviceFromJson(const json& node) {
  if (!node.is_object()) return std::nullopt;
  DeviceInfo device;
  device.id = node.value("id", "");
  device.name = node.value("name", "");
  device.model = node.value("model", "");
  auto platform = ParsePlatform(node.value("platform", ""));
  if (!platform || device.id.empty()) return std::nullopt;
  device.platform = *platform;
  device.ip = node.value("ip", "");
  device.connectionType =
      ParseConnectionType(node.value("connectionType", "unknown"))
          .value_or(ConnectionType::Unknown);
  device.controlPort = node.value("controlPort", kDefaultControlPort);
  return device;
}

const char* CodecName(AudioCodec codec) {
  return codec == AudioCodec::Opus ? "opus" : "pcm";
}

AudioCodec ParseCodec(const std::string& value) {
  return value == "pcm" ? AudioCodec::PcmFallback : AudioCodec::Opus;
}

std::string ToBase64(const std::array<uint8_t, kSessionKeyBytes>& bytes) {
  static constexpr char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(44);
  size_t i = 0;
  while (i + 3 <= bytes.size()) {
    uint32_t chunk = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    out.push_back(table[(chunk >> 18) & 0x3F]);
    out.push_back(table[(chunk >> 12) & 0x3F]);
    out.push_back(table[(chunk >> 6) & 0x3F]);
    out.push_back(table[chunk & 0x3F]);
    i += 3;
  }
  if (i + 1 == bytes.size()) {
    uint32_t chunk = bytes[i] << 16;
    out.push_back(table[(chunk >> 18) & 0x3F]);
    out.push_back(table[(chunk >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (i + 2 == bytes.size()) {
    uint32_t chunk = (bytes[i] << 16) | (bytes[i + 1] << 8);
    out.push_back(table[(chunk >> 18) & 0x3F]);
    out.push_back(table[(chunk >> 12) & 0x3F]);
    out.push_back(table[(chunk >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

int DecodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::array<uint8_t, kSessionKeyBytes> FromBase64(const std::string& text) {
  std::array<uint8_t, kSessionKeyBytes> out{};
  size_t outPos = 0;
  int buffer = 0;
  int bits = 0;
  for (char c : text) {
    int value = DecodeChar(c);
    if (value < 0) continue;
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (outPos < out.size()) {
        out[outPos++] = static_cast<uint8_t>((buffer >> bits) & 0xFF);
      }
    }
  }
  return out;
}

json AudioSessionToJson(const AudioSession& session) {
  return json{
      {"udpPort", session.udpPort},
      {"sampleRate", session.sampleRate},
      {"channels", session.channels},
      {"codec", CodecName(session.codec)},
      {"bitrateKbps", session.bitrateKbps},
      {"frameSizeMs", session.frameSizeMs},
      {"sessionKey", ToBase64(session.sessionKey)},
  };
}

AudioSession AudioSessionFromJson(const json& node) {
  AudioSession session;
  session.udpPort = node.value("udpPort", uint16_t{0});
  session.sampleRate = node.value("sampleRate", 48000u);
  session.channels = static_cast<uint8_t>(node.value("channels", 1));
  session.codec = ParseCodec(node.value("codec", "opus"));
  session.bitrateKbps = node.value("bitrateKbps", 96u);
  session.frameSizeMs = static_cast<uint8_t>(node.value("frameSizeMs", 10));
  session.sessionKey = FromBase64(node.value("sessionKey", ""));
  return session;
}

}  // namespace

std::string ToJson(const AnnouncePacket& packet) {
  json node{
      {"type", "ANNOUNCE"},
      {"protoVersion", packet.protoVersion},
  };
  node.merge_patch(DeviceToJson(packet.device));
  node["controlPort"] = packet.device.controlPort;
  return node.dump();
}

std::optional<AnnouncePacket> ParseAnnounce(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "ANNOUNCE") {
      return std::nullopt;
    }
    auto device = DeviceFromJson(node);
    if (!device) return std::nullopt;
    AnnouncePacket packet;
    packet.device = *device;
    packet.protoVersion = node.value("protoVersion", kProtocolVersion);
    return packet;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::string ToJson(const ConnectRequest& request) {
  json capabilities{
      {"sampleRates", request.capabilities.sampleRates},
      {"codec", CodecName(request.capabilities.codec)},
      {"maxBitrateKbps", request.capabilities.maxBitrateKbps},
  };
  json node{
      {"type", "CONNECT_REQUEST"},
      {"requestId", request.requestId},
      {"device", DeviceToJson(request.device)},
      {"certFingerprint", request.certFingerprint},
      {"audioCapabilities", capabilities},
      {"protoVersion", request.protoVersion},
      {"audioRole", ToString(request.audioRole)},
  };
  if (request.offeredSession) {
    node["audioSession"] = AudioSessionToJson(*request.offeredSession);
  }
  return node.dump();
}

std::optional<ConnectRequest> ParseConnectRequest(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "CONNECT_REQUEST") {
      return std::nullopt;
    }
    auto device = DeviceFromJson(node.value("device", json::object()));
    if (!device) return std::nullopt;
    ConnectRequest request;
    request.requestId = node.value("requestId", "");
    request.device = *device;
    request.certFingerprint = node.value("certFingerprint", "");
    auto capabilities = node.value("audioCapabilities", json::object());
    request.capabilities.sampleRates =
        capabilities.value("sampleRates", std::vector<uint32_t>{48000});
    request.capabilities.codec =
        ParseCodec(capabilities.value("codec", "opus"));
    request.capabilities.maxBitrateKbps =
        capabilities.value("maxBitrateKbps", 128u);
    // Absent on peers predating the field, which only ever acted as senders.
    request.protoVersion = node.value("protoVersion", kProtocolVersion);
    request.audioRole = ParseAudioRole(node.value("audioRole", "sender"))
                            .value_or(AudioRole::Sender);
    if (request.audioRole == AudioRole::Receiver &&
        node.contains("audioSession")) {
      request.offeredSession = AudioSessionFromJson(node["audioSession"]);
    }
    return request;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::string ToJson(const ConnectResponse& response) {
  json node{
      {"type", "CONNECT_RESPONSE"},
      {"requestId", response.requestId},
      {"accepted", response.accepted},
      {"reason", ToString(response.reason)},
  };
  if (response.session) {
    node["audioSession"] = AudioSessionToJson(*response.session);
  }
  return node.dump();
}

std::optional<ConnectResponse> ParseConnectResponse(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "CONNECT_RESPONSE") {
      return std::nullopt;
    }
    ConnectResponse response;
    response.requestId = node.value("requestId", "");
    response.accepted = node.value("accepted", false);
    response.reason = ParseRejectReason(node.value("reason", "NONE"));
    if (node.contains("audioSession")) {
      response.session = AudioSessionFromJson(node["audioSession"]);
    }
    return response;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

namespace {

const char* DisconnectReasonName(DisconnectReason reason) {
  switch (reason) {
    case DisconnectReason::UserRequested:
      return "USER_REQUESTED";
    case DisconnectReason::Timeout:
      return "TIMEOUT";
    case DisconnectReason::ProtocolError:
      return "PROTOCOL_ERROR";
    case DisconnectReason::RemoteShutdown:
      return "REMOTE_SHUTDOWN";
  }
  return "USER_REQUESTED";
}

DisconnectReason ParseDisconnectReason(const std::string& value) {
  if (value == "TIMEOUT") return DisconnectReason::Timeout;
  if (value == "PROTOCOL_ERROR") return DisconnectReason::ProtocolError;
  if (value == "REMOTE_SHUTDOWN") return DisconnectReason::RemoteShutdown;
  return DisconnectReason::UserRequested;
}

}  // namespace

ControlMessageType PeekMessageType(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded()) return ControlMessageType::Unknown;
    const auto type = node.value("type", "");
    if (type == "CONNECT_REQUEST") return ControlMessageType::ConnectRequest;
    if (type == "CONNECT_RESPONSE") return ControlMessageType::ConnectResponse;
    if (type == "KEEPALIVE") return ControlMessageType::KeepAlive;
    if (type == "KEEPALIVE_ACK") return ControlMessageType::KeepAliveAck;
    if (type == "DISCONNECT") return ControlMessageType::Disconnect;
    return ControlMessageType::Unknown;
  } catch (const json::exception&) {
    return ControlMessageType::Unknown;
  }
}

std::string ToJson(const KeepAlive& message) {
  return json{{"type", "KEEPALIVE"}, {"seq", message.sequence}}.dump();
}

std::string ToJson(const KeepAliveAck& message) {
  return json{{"type", "KEEPALIVE_ACK"}, {"seq", message.sequence}}.dump();
}

std::string ToJson(const DisconnectMessage& message) {
  return json{{"type", "DISCONNECT"},
              {"reason", DisconnectReasonName(message.reason)}}
      .dump();
}

std::optional<KeepAlive> ParseKeepAlive(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "KEEPALIVE") {
      return std::nullopt;
    }
    KeepAlive message;
    message.sequence = node.value("seq", uint64_t{0});
    return message;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::optional<KeepAliveAck> ParseKeepAliveAck(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "KEEPALIVE_ACK") {
      return std::nullopt;
    }
    KeepAliveAck message;
    message.sequence = node.value("seq", uint64_t{0});
    return message;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

std::optional<DisconnectMessage> ParseDisconnect(const std::string& text) {
  try {
    json node = json::parse(text, nullptr, false);
    if (node.is_discarded() || node.value("type", "") != "DISCONNECT") {
      return std::nullopt;
    }
    DisconnectMessage message;
    message.reason = ParseDisconnectReason(node.value("reason", ""));
    return message;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

}  // namespace wiremic::protocol
