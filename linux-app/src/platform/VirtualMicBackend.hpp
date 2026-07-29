#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "VirtualMicConfig.hpp"

namespace wiremic::platform {

enum class AudioServerKind { None, PipeWire, PulseAudio, WindowsCable };

class VirtualMicBackend {
 public:
  virtual ~VirtualMicBackend() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool isRunning() const = 0;
  virtual void pushSamples(const int16_t* interleaved, size_t sampleCount) = 0;
};

[[nodiscard]] const char* AudioServerDisplayName(AudioServerKind kind);
[[nodiscard]] AudioServerKind DetectAudioServer();
[[nodiscard]] std::vector<AudioServerKind> DetectAllAudioServers();
[[nodiscard]] std::unique_ptr<VirtualMicBackend> CreateVirtualMic(
    const VirtualMicConfig& config, AudioServerKind preferredKind = AudioServerKind::None);
[[nodiscard]] std::unique_ptr<VirtualMicBackend> CreateVirtualMicOnAllServers(
    const VirtualMicConfig& config, std::string* publishedOn = nullptr);

}
