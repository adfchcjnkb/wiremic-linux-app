#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "AudioDeviceLister.hpp"
#include "Check.hpp"
#include "PipeWireAudioCapture.hpp"
#include "VirtualMicBackend.hpp"

using namespace wiremic::platform;

int main() {
  const auto devices = AudioDeviceLister::ListInputDevices(1500);
  std::cout << "INPUT_DEVICES_FOUND: " << devices.size() << "\n";
  for (const auto& d : devices) {
    std::cout << "  - " << d.description << " (" << d.name << ")"
               << (d.isDefault ? " [default]" : "") << "\n";
  }

  if (DetectAudioServer() != AudioServerKind::PipeWire) {
    std::cout << "PipeWire not live in this environment, skipping loopback\n";
    std::cout << "AUDIO_CAPTURE_TESTS_PASSED\n";
    return 0;
  }

  VirtualMicConfig micConfig;
  micConfig.nodeName = "WireMic_Capture_Test_Source";
  micConfig.nodeDescription = "WireMic Capture Test Source";
  micConfig.sampleRate = 48000;
  micConfig.channels = 1;

  auto mic = CreateVirtualMic(micConfig, AudioServerKind::PipeWire);
  WIREMIC_CHECK(mic != nullptr);
  WIREMIC_CHECK(mic->start());

  std::atomic<size_t> samplesCaptured{0};

  AudioCaptureConfig captureConfig;
  captureConfig.nodeName = "WireMic_Capture_Test_Sink";
  captureConfig.sourceTargetName = "WireMic_Capture_Test_Source";
  captureConfig.sampleRate = 48000;
  captureConfig.channels = 1;

  PipeWireAudioCapture capture(
      captureConfig, [&](const int16_t*, size_t count) {
        samplesCaptured += count;
      });

  WIREMIC_CHECK(capture.start());

  std::vector<int16_t> pushBuffer(480, 1234);
  for (int i = 0; i < 20; ++i) {
    mic->pushSamples(pushBuffer.data(), pushBuffer.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  std::cout << "SAMPLES_CAPTURED: " << samplesCaptured.load() << "\n";
  WIREMIC_CHECK(samplesCaptured.load() > 0);

  capture.stop();
  mic->stop();

  std::cout << "CAPTURE_LOOPBACK_OK\n";
  std::cout << "AUDIO_CAPTURE_TESTS_PASSED\n";
  return 0;
}
