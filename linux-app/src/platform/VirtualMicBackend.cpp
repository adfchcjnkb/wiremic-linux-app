#include "VirtualMicBackend.hpp"

#include "PulseAudioVirtualMic.hpp"
#include "PipeWireVirtualMic.hpp"

namespace wiremic::platform {

const char* AudioServerDisplayName(AudioServerKind kind) {
  switch (kind) {
    case AudioServerKind::PipeWire:
      return "PipeWire";
    case AudioServerKind::PulseAudio:
      return "PulseAudio";
    case AudioServerKind::None:
      break;
  }
  return "none";
}

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

  // How many servers to publish on is a question about how many *graphs* there
  // are, not how many daemons are running.
  //
  // When pipewire-pulse answers the pulse socket there is one graph behind two
  // doors: publishing through the pulse door puts the device in the PipeWire
  // graph as well, and publishing twice would put two identically named
  // microphones in front of someone who has no way to tell them apart.
  //
  // A real PulseAudio daemon with a separate PipeWire daemon beside it is a
  // different machine entirely: two graphs that cannot see each other, with
  // applications living in both. libpulse clients reach PulseAudio, while
  // anything going through ALSA reaches PipeWire, since pipewire-alsa claims
  // the default PCM. Publishing on one server there means half the programs on
  // the machine cannot find the microphone at all -- and because each
  // application only ever sees one of the two graphs, publishing on both is
  // still exactly one microphone from where anybody is standing.
  const auto flavour = PulseAudioVirtualMic::QueryServerFlavour();
  const bool pipeWireRunning = PipeWireVirtualMic::IsPipeWireAvailable();

  switch (flavour) {
    case PulseAudioVirtualMic::ServerFlavour::PipeWirePulse:
      kinds.push_back(AudioServerKind::PulseAudio);
      break;
    case PulseAudioVirtualMic::ServerFlavour::PulseAudio:
      kinds.push_back(AudioServerKind::PulseAudio);
      if (pipeWireRunning) kinds.push_back(AudioServerKind::PipeWire);
      break;
    case PulseAudioVirtualMic::ServerFlavour::None:
      if (pipeWireRunning) {
        kinds.push_back(AudioServerKind::PipeWire);
      } else if (PulseAudioVirtualMic::IsPulseAudioAvailable()) {
        kinds.push_back(AudioServerKind::PulseAudio);
      }
      break;
  }

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
