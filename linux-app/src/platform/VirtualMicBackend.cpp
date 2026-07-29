#include "VirtualMicBackend.hpp"

#include "PulseAudioVirtualMic.hpp"
#include "PipeWireVirtualMic.hpp"

namespace wiremic::platform {

namespace {

class PipeWireBackend final : public VirtualMicBackend {
 public:
  explicit PipeWireBackend(const VirtualMicConfig& config) : impl_(config) {}
  bool start() override { return impl_.start(); }
  void stop() override { return impl_.stop(); }
  [[nodiscard]] bool isRunning() const override { return impl_.isRunning(); }
  void pushSamples(const int16_t* interleaved, size_t sampleCount) override {
    impl_.pushSamples(interleaved, sampleCount);
  }

 private:
  PipeWireVirtualMic impl_;
};

class PulseAudioBackend final : public VirtualMicBackend {
 public:
  explicit PulseAudioBackend(const VirtualMicConfig& config) : impl_(config) {}
  bool start() override { return impl_.start(); }
  void stop() override { return impl_.stop(); }
  [[nodiscard]] bool isRunning() const override { return impl_.isRunning(); }
  void pushSamples(const int16_t* interleaved, size_t sampleCount) override {
    impl_.pushSamples(interleaved, sampleCount);
  }

 private:
  PulseAudioVirtualMic impl_;
};

}

AudioServerKind DetectAudioServer() {
  const auto flavour = PulseAudioVirtualMic::QueryServerFlavour();

  if (flavour == PulseAudioVirtualMic::ServerFlavour::PulseAudio) {
    return AudioServerKind::PulseAudio;
  }

  if (PipeWireVirtualMic::IsPipeWireAvailable()) {
    return AudioServerKind::PipeWire;
  }

  if (flavour == PulseAudioVirtualMic::ServerFlavour::PipeWirePulse ||
      PulseAudioVirtualMic::IsPulseAudioAvailable()) {
    return AudioServerKind::PulseAudio;
  }

  return AudioServerKind::None;
}

std::unique_ptr<VirtualMicBackend> CreateVirtualMic(
    const VirtualMicConfig& config, AudioServerKind preferredKind) {
  const auto kind = preferredKind == AudioServerKind::None
                         ? DetectAudioServer()
                         : preferredKind;

  switch (kind) {
    case AudioServerKind::PipeWire:
      return std::make_unique<PipeWireBackend>(config);
    case AudioServerKind::PulseAudio:
      return std::make_unique<PulseAudioBackend>(config);
    case AudioServerKind::None:
      return nullptr;
  }
  return nullptr;
}

}
