#include "VirtualMicBackend.hpp"

#ifdef _WIN32
#include "WindowsVirtualMic.hpp"
#else
#include "PulseAudioVirtualMic.hpp"
#include "PipeWireVirtualMic.hpp"
#endif

namespace wiremic::platform {

const char* AudioServerDisplayName(AudioServerKind kind) {
  switch (kind) {
    case AudioServerKind::PipeWire:
      return "PipeWire";
    case AudioServerKind::PulseAudio:
      return "PulseAudio";
    case AudioServerKind::WindowsCable:
      return "VB-CABLE";
    case AudioServerKind::None:
      break;
  }
  return "none";
}

namespace {

#ifndef _WIN32
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
#else
class WindowsCableBackend final : public VirtualMicBackend {
 public:
  explicit WindowsCableBackend(const VirtualMicConfig& config) : impl_(config) {}
  bool start() override { return impl_.start(); }
  void stop() override { return impl_.stop(); }
  [[nodiscard]] bool isRunning() const override { return impl_.isRunning(); }
  void pushSamples(const int16_t* interleaved, size_t sampleCount) override {
    impl_.pushSamples(interleaved, sampleCount);
  }

 private:
  WindowsVirtualMic impl_;
};
#endif

class CompositeBackend final : public VirtualMicBackend {
 public:
  void add(std::unique_ptr<VirtualMicBackend> backend) {
    backends_.push_back(std::move(backend));
  }

  [[nodiscard]] bool empty() const { return backends_.empty(); }

  bool start() override {
    bool anyStarted = false;
    for (auto& backend : backends_) {
      if (backend->start()) anyStarted = true;
    }
    return anyStarted;
  }

  void stop() override {
    for (auto& backend : backends_) backend->stop();
  }

  [[nodiscard]] bool isRunning() const override {
    for (const auto& backend : backends_) {
      if (backend->isRunning()) return true;
    }
    return false;
  }

  void pushSamples(const int16_t* interleaved, size_t sampleCount) override {
    for (auto& backend : backends_) {
      if (backend->isRunning()) backend->pushSamples(interleaved, sampleCount);
    }
  }

 private:
  std::vector<std::unique_ptr<VirtualMicBackend>> backends_;
};

}

std::vector<AudioServerKind> DetectAllAudioServers() {
  std::vector<AudioServerKind> kinds;

#ifdef _WIN32
  if (WindowsVirtualMic::IsCableInstalled()) {
    kinds.push_back(AudioServerKind::WindowsCable);
  }
#else
  const auto flavour = PulseAudioVirtualMic::QueryServerFlavour();

  if (flavour == PulseAudioVirtualMic::ServerFlavour::PulseAudio) {
    kinds.push_back(AudioServerKind::PulseAudio);
  }

  if (PipeWireVirtualMic::IsPipeWireAvailable()) {
    kinds.push_back(AudioServerKind::PipeWire);
  }

  if (kinds.empty() &&
      (flavour == PulseAudioVirtualMic::ServerFlavour::PipeWirePulse ||
       PulseAudioVirtualMic::IsPulseAudioAvailable())) {
    kinds.push_back(AudioServerKind::PulseAudio);
  }
#endif

  return kinds;
}

std::unique_ptr<VirtualMicBackend> CreateVirtualMicOnAllServers(
    const VirtualMicConfig& config, std::string* publishedOn) {
  const auto kinds = DetectAllAudioServers();
  if (kinds.empty()) {
    if (publishedOn) publishedOn->clear();
    return nullptr;
  }

  auto composite = std::make_unique<CompositeBackend>();
  std::string names;

  for (const auto kind : kinds) {
    auto backend = CreateVirtualMic(config, kind);
    if (!backend) continue;
    composite->add(std::move(backend));
    if (!names.empty()) names += " + ";
    names += AudioServerDisplayName(kind);
  }

  if (composite->empty()) {
    if (publishedOn) publishedOn->clear();
    return nullptr;
  }

  if (publishedOn) *publishedOn = names;
  return composite;
}

AudioServerKind DetectAudioServer() {
  const auto kinds = DetectAllAudioServers();
  return kinds.empty() ? AudioServerKind::None : kinds.front();
}

std::unique_ptr<VirtualMicBackend> CreateVirtualMic(
    const VirtualMicConfig& config, AudioServerKind preferredKind) {
  const auto kind = preferredKind == AudioServerKind::None
                         ? DetectAudioServer()
                         : preferredKind;

  switch (kind) {
#ifdef _WIN32
    case AudioServerKind::WindowsCable:
      return std::make_unique<WindowsCableBackend>(config);
    case AudioServerKind::PipeWire:
    case AudioServerKind::PulseAudio:
      return nullptr;
#else
    case AudioServerKind::PipeWire:
      return std::make_unique<PipeWireBackend>(config);
    case AudioServerKind::PulseAudio:
      return std::make_unique<PulseAudioBackend>(config);
    case AudioServerKind::WindowsCable:
      return nullptr;
#endif
    case AudioServerKind::None:
      return nullptr;
  }
  return nullptr;
}

}
