#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <atomic>
#include <string>
#include <vector>

#include "AudioCaptureBackend.hpp"

namespace wiremic::android {

// Capture path for Android 7, where AAudio does not exist. OpenSL ES is
// deprecated but still present on every release, so it also serves as the
// fallback if AAudio refuses to open on a newer device.
class OpenSLESCapture final : public AudioCaptureBackend {
 public:
  explicit OpenSLESCapture(CaptureFramesCallback callback);
  ~OpenSLESCapture() override;

  bool start(int32_t requestedRate, int32_t requestedChannels,
             int32_t framesPerCallback) override;
  void stop() override;

  [[nodiscard]] int32_t sampleRate() const override { return sampleRate_; }
  [[nodiscard]] int32_t channels() const override { return channels_; }
  [[nodiscard]] bool isFloat() const override { return false; }
  [[nodiscard]] const char* name() const override { return "OpenSL ES"; }
  [[nodiscard]] const std::string& lastError() const override {
    return lastError_;
  }

 private:
  static void BufferQueueCallback(SLAndroidSimpleBufferQueueItf queue,
                                   void* context);
  void onBufferReady(SLAndroidSimpleBufferQueueItf queue);

  CaptureFramesCallback callback_;
  std::string lastError_;

  SLObjectItf engineObject_{nullptr};
  SLEngineItf engine_{nullptr};
  SLObjectItf recorderObject_{nullptr};
  SLRecordItf record_{nullptr};
  SLAndroidSimpleBufferQueueItf queue_{nullptr};

  std::vector<std::vector<int16_t>> buffers_;
  int readIndex_{0};
  int32_t framesPerBuffer_{480};
  int32_t sampleRate_{48000};
  int32_t channels_{1};
  std::atomic<bool> running_{false};
};

}
