#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace wiremic::android {

// Raw frames exactly as the device produced them. The caller is responsible
// for converting to the negotiated format, because what the device grants is
// not what was asked for.
using CaptureFramesCallback =
    std::function<void(const void* frames, int32_t frameCount)>;

class AudioCaptureBackend {
 public:
  virtual ~AudioCaptureBackend() = default;

  virtual bool start(int32_t requestedRate, int32_t requestedChannels,
                     int32_t framesPerCallback) = 0;
  virtual void stop() = 0;

  [[nodiscard]] virtual int32_t sampleRate() const = 0;
  [[nodiscard]] virtual int32_t channels() const = 0;
  [[nodiscard]] virtual bool isFloat() const = 0;
  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual const std::string& lastError() const = 0;
};

// AAudio arrived in Android 8.0 and gives the lowest latency, so it is used
// whenever the device is new enough; OpenSL ES covers Android 7.
std::unique_ptr<AudioCaptureBackend> CreateAudioCapture(
    CaptureFramesCallback callback, std::string* chosenBackend);

}
