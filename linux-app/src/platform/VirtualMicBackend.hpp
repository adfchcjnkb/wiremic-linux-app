#pragma once

#include <cstdint>
#include <memory>

#include "VirtualMicConfig.hpp"

namespace wiremic::platform {

enum class AudioServerKind { None, PipeWire, PulseAudio };

class VirtualMicBackend {
 public:
  virtual ~VirtualMicBackend() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool isRunning() const = 0;
  virtual void pushSamples(const int16_t* interleaved, size_t sampleCount) = 0;
};

[[nodiscard]] AudioServerKind DetectAudioServer();
[[nodiscard]] std::unique_ptr<VirtualMicBackend> CreateVirtualMic(
    const VirtualMicConfig& config, AudioServerKind preferredKind = AudioServerKind::None);

}  // namespace wiremic::platform