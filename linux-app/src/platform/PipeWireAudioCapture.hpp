#pragma once

#include <pipewire/stream.h>
#include <spa/utils/hook.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;

namespace wiremic::platform {

struct AudioCaptureConfig {
  std::string nodeName{"WireMic Capture"};
  std::string sourceTargetName;
  uint32_t sampleRate{48000};
  uint8_t channels{1};
};

using AudioCaptureCallback =
    std::function<void(const int16_t* interleaved, size_t sampleCount)>;

class PipeWireAudioCapture {
 public:
  PipeWireAudioCapture(AudioCaptureConfig config, AudioCaptureCallback onSamples);
  ~PipeWireAudioCapture();

  PipeWireAudioCapture(const PipeWireAudioCapture&) = delete;
  PipeWireAudioCapture& operator=(const PipeWireAudioCapture&) = delete;

  bool start();
  void stop();
  [[nodiscard]] bool isRunning() const;
  bool switchTarget(const std::string& deviceName);

 private:
  static void OnProcess(void* userdata);

  AudioCaptureConfig config_;
  AudioCaptureCallback onSamples_;

  pw_thread_loop* loop_{nullptr};
  pw_context* context_{nullptr};
  pw_core* core_{nullptr};
  pw_stream* stream_{nullptr};
  struct spa_hook streamListener_{};
  bool running_{false};
};

}
