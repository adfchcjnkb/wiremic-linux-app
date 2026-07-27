#pragma once

#include <cstdint>
#include <string>
#include <memory>

#include "VirtualMicConfig.hpp"

struct pa_simple;

namespace wiremic::platform {

class PulseAudioVirtualMic {
 public:
  explicit PulseAudioVirtualMic(const VirtualMicConfig& config);
  ~PulseAudioVirtualMic();

  PulseAudioVirtualMic(const PulseAudioVirtualMic&) = delete;
  PulseAudioVirtualMic& operator=(const PulseAudioVirtualMic&) = delete;

  bool start();
  void stop();
  [[nodiscard]] bool isRunning() const;

  void pushSamples(const int16_t* interleaved, size_t sampleCount);

  [[nodiscard]] static bool IsPulseAudioAvailable();

 private:
  bool loadModules();
  void unloadModules();
  bool runPactlCommand(const std::string& cmd, std::string& output);
  int parseModuleId(const std::string& output);

  VirtualMicConfig config_;
  std::string sinkName_;
  std::string sourceName_;
  int nullSinkModuleId_{-1};
  int remapSourceModuleId_{-1};

  pa_simple* playbackStream_{nullptr};
  bool running_{false};
};

}  // namespace wiremic::platform