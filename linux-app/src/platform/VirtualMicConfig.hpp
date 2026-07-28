#pragma once

#include <string>
#include <cstdint>

namespace wiremic::platform {

struct VirtualMicConfig {
  std::string nodeName{"WireMic Virtual Microphone"};
  std::string nodeDescription{"Wireless Mic (from Android)"};
  uint32_t sampleRate{48000};
  uint8_t channels{1};
};

}
