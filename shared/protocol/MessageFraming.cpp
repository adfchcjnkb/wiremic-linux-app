#include "MessageFraming.hpp"

#include <stdexcept>

namespace wiremic::protocol {

std::string FrameMessage(const std::string& payload) {
  if (payload.size() > kMaxControlMessageBytes) {
    throw std::length_error("control message exceeds maximum size");
  }
  const uint32_t length = static_cast<uint32_t>(payload.size());
  std::string framed;
  framed.reserve(4 + payload.size());
  framed.push_back(static_cast<char>((length >> 24) & 0xFF));
  framed.push_back(static_cast<char>((length >> 16) & 0xFF));
  framed.push_back(static_cast<char>((length >> 8) & 0xFF));
  framed.push_back(static_cast<char>(length & 0xFF));
  framed.append(payload);
  return framed;
}

void MessageFramer::Feed(const char* data, size_t length) {
  buffer_.append(data, length);
}

std::optional<std::string> MessageFramer::NextMessage() {
  if (buffer_.size() < 4) return std::nullopt;

  const auto u8 = [this](size_t index) {
    return static_cast<uint32_t>(static_cast<unsigned char>(buffer_[index]));
  };
  const uint32_t length =
      (u8(0) << 24) | (u8(1) << 16) | (u8(2) << 8) | u8(3);

  if (length > kMaxControlMessageBytes) {
    throw std::length_error("incoming control message exceeds maximum size");
  }
  if (buffer_.size() < 4 + static_cast<size_t>(length)) return std::nullopt;

  std::string message = buffer_.substr(4, length);
  buffer_.erase(0, 4 + length);
  return message;
}

}  // namespace wiremic::protocol
