#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace wiremic::protocol {

inline constexpr uint32_t kMaxControlMessageBytes = 1u << 20;

class MessageFramer {
 public:
  void Feed(const char* data, size_t length);
  std::optional<std::string> NextMessage();
  // Drops any half-received frame. Must be called when the underlying socket
  // is reconnected, or the leftover bytes corrupt the next message boundary.
  void Reset();

 private:
  std::string buffer_;
};

std::string FrameMessage(const std::string& payload);

}  // namespace wiremic::protocol
