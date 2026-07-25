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

 private:
  std::string buffer_;
};

std::string FrameMessage(const std::string& payload);

}  // namespace wiremic::protocol
