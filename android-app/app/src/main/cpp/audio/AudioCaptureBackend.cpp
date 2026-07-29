#include "AudioCaptureBackend.hpp"

#include "AAudioCapture.hpp"
#include "OpenSLESCapture.hpp"

namespace wiremic::android {

std::unique_ptr<AudioCaptureBackend> CreateAudioCapture(
    CaptureFramesCallback callback, std::string* chosenBackend) {
  if (AAudioCapture::Available()) {
    if (chosenBackend) *chosenBackend = "AAudio";
    return std::make_unique<AAudioCapture>(std::move(callback));
  }

  if (chosenBackend) *chosenBackend = "OpenSL ES";
  return std::make_unique<OpenSLESCapture>(std::move(callback));
}

}
