#pragma once

#include <string>
#include <cstdint>

namespace wiremic::platform {

// The PulseAudio object names the virtual microphone is published under. They
// are fixed rather than generated: applications and scripts refer to a
// microphone by name, and a name that changed between runs would be worse than
// useless to them.
inline constexpr const char* kPulseSinkName = "wiremic_null_sink";
inline constexpr const char* kPulseSourceName = "wiremic_virtual_mic";

// The node name used when publishing natively on PipeWire.
inline constexpr const char* kPipeWireNodeName = "WireMic Virtual Microphone";

struct VirtualMicConfig {
  std::string nodeName{kPipeWireNodeName};
  std::string nodeDescription{"Wireless Mic (from Android)"};
  uint32_t sampleRate{48000};
  uint8_t channels{1};
  uint8_t frameSizeMs{10};
};

}
