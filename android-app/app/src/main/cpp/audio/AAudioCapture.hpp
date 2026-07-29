#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <string>

#include "AudioCaptureBackend.hpp"

namespace wiremic::android {

// AAudio exists from Android 8.0 (API 26). Every entry point is guarded by a
// runtime availability check so this file still links and loads on Android 7,
// where the symbols are weak and absent.
class AAudioCapture final : public AudioCaptureBackend {
 public:
  explicit AAudioCapture(CaptureFramesCallback callback);
  ~AAudioCapture() override;

  bool start(int32_t requestedRate, int32_t requestedChannels,
             int32_t framesPerCallback) override;
  void stop() override;

  [[nodiscard]] int32_t sampleRate() const override { return sampleRate_; }
  [[nodiscard]] int32_t channels() const override { return channels_; }
  [[nodiscard]] bool isFloat() const override { return isFloat_; }
  [[nodiscard]] const char* name() const override { return "AAudio"; }
  [[nodiscard]] const std::string& lastError() const override {
    return lastError_;
  }

  [[nodiscard]] static bool Available();

 private:
  static aaudio_data_callback_result_t OnAudioData(AAudioStream* stream,
                                                     void* userdata,
                                                     void* audioData,
                                                     int32_t numFrames);
  static void OnStreamError(AAudioStream* stream, void* userdata,
                             aaudio_result_t error);

  CaptureFramesCallback callback_;
  std::string lastError_;

  AAudioStream* stream_{nullptr};
  int32_t sampleRate_{48000};
  int32_t channels_{1};
  bool isFloat_{false};
  std::atomic<bool> running_{false};
};

}
