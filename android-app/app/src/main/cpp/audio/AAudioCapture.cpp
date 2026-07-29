#include "AAudioCapture.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <mutex>

namespace wiremic::android {

namespace {

// AAudio only exists from Android 8.0. Linking against libaaudio.so would make
// the whole library fail to load on Android 7, and clang's availability guards
// do not cover a negated early return, so the symbols are resolved by hand at
// runtime instead. On Android 7 the handle simply stays null and the caller
// falls back to OpenSL ES.
struct AAudioApi {
  void* handle{nullptr};

  aaudio_result_t (*createStreamBuilder)(AAudioStreamBuilder**){nullptr};
  void (*setDirection)(AAudioStreamBuilder*, aaudio_direction_t){nullptr};
  void (*setSampleRate)(AAudioStreamBuilder*, int32_t){nullptr};
  void (*setChannelCount)(AAudioStreamBuilder*, int32_t){nullptr};
  void (*setFormat)(AAudioStreamBuilder*, aaudio_format_t){nullptr};
  void (*setPerformanceMode)(AAudioStreamBuilder*, aaudio_performance_mode_t){nullptr};
  void (*setSharingMode)(AAudioStreamBuilder*, aaudio_sharing_mode_t){nullptr};
  void (*setFramesPerDataCallback)(AAudioStreamBuilder*, int32_t){nullptr};
  void (*setDataCallback)(AAudioStreamBuilder*, AAudioStream_dataCallback, void*){nullptr};
  void (*setErrorCallback)(AAudioStreamBuilder*, AAudioStream_errorCallback, void*){nullptr};
  void (*setInputPreset)(AAudioStreamBuilder*, aaudio_input_preset_t){nullptr};
  aaudio_result_t (*openStream)(AAudioStreamBuilder*, AAudioStream**){nullptr};
  aaudio_result_t (*deleteBuilder)(AAudioStreamBuilder*){nullptr};

  int32_t (*getSampleRate)(AAudioStream*){nullptr};
  int32_t (*getChannelCount)(AAudioStream*){nullptr};
  aaudio_format_t (*getFormat)(AAudioStream*){nullptr};
  int32_t (*getFramesPerBurst)(AAudioStream*){nullptr};
  aaudio_result_t (*setBufferSizeInFrames)(AAudioStream*, int32_t){nullptr};
  aaudio_result_t (*requestStart)(AAudioStream*){nullptr};
  aaudio_result_t (*requestStop)(AAudioStream*){nullptr};
  aaudio_result_t (*closeStream)(AAudioStream*){nullptr};
  const char* (*resultToText)(aaudio_result_t){nullptr};

  [[nodiscard]] bool usable() const {
    return handle && createStreamBuilder && openStream && requestStart &&
           closeStream && getSampleRate && getChannelCount && getFormat;
  }
};

const AAudioApi& Api() {
  static AAudioApi api = [] {
    AAudioApi loaded;
    loaded.handle = ::dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!loaded.handle) return loaded;

    auto sym = [&loaded](const char* name) {
      return ::dlsym(loaded.handle, name);
    };

#define WIREMIC_BIND(field, name) \
  loaded.field = reinterpret_cast<decltype(loaded.field)>(sym(name))

    WIREMIC_BIND(createStreamBuilder, "AAudio_createStreamBuilder");
    WIREMIC_BIND(setDirection, "AAudioStreamBuilder_setDirection");
    WIREMIC_BIND(setSampleRate, "AAudioStreamBuilder_setSampleRate");
    WIREMIC_BIND(setChannelCount, "AAudioStreamBuilder_setChannelCount");
    WIREMIC_BIND(setFormat, "AAudioStreamBuilder_setFormat");
    WIREMIC_BIND(setPerformanceMode, "AAudioStreamBuilder_setPerformanceMode");
    WIREMIC_BIND(setSharingMode, "AAudioStreamBuilder_setSharingMode");
    WIREMIC_BIND(setFramesPerDataCallback,
                 "AAudioStreamBuilder_setFramesPerDataCallback");
    WIREMIC_BIND(setDataCallback, "AAudioStreamBuilder_setDataCallback");
    WIREMIC_BIND(setErrorCallback, "AAudioStreamBuilder_setErrorCallback");
    WIREMIC_BIND(setInputPreset, "AAudioStreamBuilder_setInputPreset");
    WIREMIC_BIND(openStream, "AAudioStreamBuilder_openStream");
    WIREMIC_BIND(deleteBuilder, "AAudioStreamBuilder_delete");
    WIREMIC_BIND(getSampleRate, "AAudioStream_getSampleRate");
    WIREMIC_BIND(getChannelCount, "AAudioStream_getChannelCount");
    WIREMIC_BIND(getFormat, "AAudioStream_getFormat");
    WIREMIC_BIND(getFramesPerBurst, "AAudioStream_getFramesPerBurst");
    WIREMIC_BIND(setBufferSizeInFrames, "AAudioStream_setBufferSizeInFrames");
    WIREMIC_BIND(requestStart, "AAudioStream_requestStart");
    WIREMIC_BIND(requestStop, "AAudioStream_requestStop");
    WIREMIC_BIND(closeStream, "AAudioStream_close");
    WIREMIC_BIND(resultToText, "AAudio_convertResultToText");

#undef WIREMIC_BIND

    return loaded;
  }();
  return api;
}

std::string ResultText(aaudio_result_t result) {
  const auto& api = Api();
  if (api.resultToText) {
    const char* text = api.resultToText(result);
    if (text) return text;
  }
  return std::to_string(static_cast<int>(result));
}

}

AAudioCapture::AAudioCapture(CaptureFramesCallback callback)
    : callback_(std::move(callback)) {}

AAudioCapture::~AAudioCapture() { stop(); }

bool AAudioCapture::Available() { return Api().usable(); }

aaudio_data_callback_result_t AAudioCapture::OnAudioData(AAudioStream*,
                                                           void* userdata,
                                                           void* audioData,
                                                           int32_t numFrames) {
  auto* self = static_cast<AAudioCapture*>(userdata);
  if (self->running_ && self->callback_ && audioData && numFrames > 0) {
    self->callback_(audioData, numFrames);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AAudioCapture::OnStreamError(AAudioStream*, void* userdata,
                                   aaudio_result_t error) {
  auto* self = static_cast<AAudioCapture*>(userdata);
  self->running_ = false;
  self->lastError_ = "AAudio stream error: " + ResultText(error);
}

bool AAudioCapture::start(int32_t requestedRate, int32_t requestedChannels,
                           int32_t framesPerCallback) {
  if (running_) return true;

  const auto& api = Api();
  if (!api.usable()) {
    lastError_ = "AAudio needs Android 8.0 or newer";
    return false;
  }

  AAudioStreamBuilder* builder = nullptr;
  if (api.createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
    lastError_ = "failed to create AAudio stream builder";
    return false;
  }

  api.setDirection(builder, AAUDIO_DIRECTION_INPUT);
  api.setSampleRate(builder, requestedRate);
  api.setChannelCount(builder, requestedChannels);
  api.setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  api.setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  api.setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  api.setFramesPerDataCallback(builder, framesPerCallback);
  api.setDataCallback(builder, &AAudioCapture::OnAudioData, this);
  api.setErrorCallback(builder, &AAudioCapture::OnStreamError, this);

  // The voice-communication preset arrived in Android 9, so the symbol is
  // absent on 8.x; resolving it separately keeps 8.x working.
  if (api.setInputPreset) {
    api.setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
  }

  const aaudio_result_t opened = api.openStream(builder, &stream_);
  if (api.deleteBuilder) api.deleteBuilder(builder);

  if (opened != AAUDIO_OK || !stream_) {
    lastError_ = "failed to open AAudio input stream: " + ResultText(opened);
    stream_ = nullptr;
    return false;
  }

  sampleRate_ = api.getSampleRate(stream_);
  channels_ = api.getChannelCount(stream_);
  isFloat_ = api.getFormat(stream_) == AAUDIO_FORMAT_PCM_FLOAT;
  if (sampleRate_ <= 0) sampleRate_ = requestedRate;
  if (channels_ <= 0) channels_ = requestedChannels;

  const int32_t burst =
      api.getFramesPerBurst ? api.getFramesPerBurst(stream_) : 0;
  const int32_t callbackFrames = static_cast<int32_t>(
      static_cast<int64_t>(framesPerCallback) * sampleRate_ /
      std::max(1, requestedRate));
  int32_t desired = callbackFrames * 2;
  if (burst > 0) {
    desired = std::max(desired, burst * 2);
    const int32_t remainder = desired % burst;
    if (remainder != 0) desired += burst - remainder;
  }
  if (desired > 0 && api.setBufferSizeInFrames) {
    api.setBufferSizeInFrames(stream_, desired);
  }

  running_ = true;
  if (api.requestStart(stream_) != AAUDIO_OK) {
    lastError_ = "failed to start the AAudio input stream";
    running_ = false;
    stop();
    return false;
  }

  lastError_.clear();
  return true;
}

void AAudioCapture::stop() {
  running_ = false;
  if (!stream_) return;

  const auto& api = Api();
  if (api.requestStop) api.requestStop(stream_);
  if (api.closeStream) api.closeStream(stream_);
  stream_ = nullptr;
}

}
