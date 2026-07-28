#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace wiremic::audio {

inline constexpr size_t kSessionKeyBytes = 32;
using SessionKey = std::array<uint8_t, kSessionKeyBytes>;

struct DecryptedAudioPacket {
  uint64_t sequence{};
  uint32_t captureTimestampMs{};
  bool marker{false};
  bool dtx{false};
  std::vector<uint8_t> opusPayload;
};

class AudioPacketCodec {
 public:
  static std::vector<uint8_t> Encrypt(const SessionKey& key,
                                       uint64_t sequence,
                                       uint32_t captureTimestampMs,
                                       bool marker, bool dtx,
                                       const std::vector<uint8_t>& opusPayload);

  static std::optional<DecryptedAudioPacket> Decrypt(const SessionKey& key,
                                                       const uint8_t* data,
                                                       size_t length);
};

}
