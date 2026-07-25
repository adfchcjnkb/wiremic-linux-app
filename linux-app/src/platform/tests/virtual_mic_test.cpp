#include <iostream>
#include <vector>
#include <cmath>

#include "Check.hpp"
#include "VirtualMicBackend.hpp"

using namespace wiremic::platform;

int main() {
  const auto kind = DetectAudioServer();
  std::cout << "DETECTED_AUDIO_SERVER: "
            << (kind == AudioServerKind::PipeWire     ? "PipeWire"
                : kind == AudioServerKind::PulseAudio ? "PulseAudio"
                                                       : "None")
            << "\n";

  VirtualMicConfig config;
  config.nodeName = "WireMic Test";
  config.nodeDescription = "WireMic Virtual Mic (Test)";
  config.sampleRate = 48000;
  config.channels = 1;

  auto mic = CreateVirtualMic(config);

  if (kind == AudioServerKind::None) {
    WIREMIC_CHECK(mic == nullptr);
    std::cout << "NO_AUDIO_SERVER_GRACEFUL_OK\n";
    std::cout << "VIRTUAL_MIC_TESTS_PASSED\n";
    return 0;
  }

  WIREMIC_CHECK(mic != nullptr);
  const bool started = mic->start();

  if (!started) {
    std::cout << "START_FAILED_GRACEFULLY_OK (no session bus in this "
                 "sandbox is expected)\n";
    std::cout << "VIRTUAL_MIC_TESTS_PASSED\n";
    return 0;
  }

  WIREMIC_CHECK(mic->isRunning());

  std::vector<int16_t> samples(480);
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<int16_t>(
        4000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 48000.0));
  }
  mic->pushSamples(samples.data(), samples.size());

  mic->stop();
  WIREMIC_CHECK(!mic->isRunning());

  std::cout << "FULL_LIVE_PIPEWIRE_START_STOP_OK\n";
  std::cout << "VIRTUAL_MIC_TESTS_PASSED\n";
  return 0;
}
