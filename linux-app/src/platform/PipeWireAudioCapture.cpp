#include "PipeWireAudioCapture.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <algorithm>

namespace wiremic::platform {

PipeWireAudioCapture::PipeWireAudioCapture(AudioCaptureConfig config,
                                            AudioCaptureCallback onSamples)
    : config_(std::move(config)), onSamples_(std::move(onSamples)) {
  pw_init(nullptr, nullptr);
}

PipeWireAudioCapture::~PipeWireAudioCapture() { stop(); }

bool PipeWireAudioCapture::isRunning() const { return running_; }

void PipeWireAudioCapture::OnProcess(void* userdata) {
  auto* self = static_cast<PipeWireAudioCapture*>(userdata);

  struct pw_buffer* pwBuffer = pw_stream_dequeue_buffer(self->stream_);
  if (!pwBuffer) return;

  struct spa_buffer* spaBuffer = pwBuffer->buffer;
  if (spaBuffer->datas[0].data == nullptr) {
    pw_stream_queue_buffer(self->stream_, pwBuffer);
    return;
  }

  const uint32_t stride =
      static_cast<uint32_t>(sizeof(int16_t)) * self->config_.channels;
  const uint32_t chunkSize = spaBuffer->datas[0].chunk->size;
  const uint32_t frameCount = stride > 0 ? chunkSize / stride : 0;

  if (frameCount > 0 && self->onSamples_) {
    const auto* src =
        static_cast<const int16_t*>(spaBuffer->datas[0].data);
    self->onSamples_(src, static_cast<size_t>(frameCount) *
                               self->config_.channels);
  }

  pw_stream_queue_buffer(self->stream_, pwBuffer);
}

bool PipeWireAudioCapture::start() {
  if (running_) return true;

  loop_ = pw_thread_loop_new("wiremic-audio-capture", nullptr);
  if (!loop_) return false;

  pw_thread_loop_lock(loop_);

  context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
  if (!context_) {
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
    return false;
  }

  core_ = pw_context_connect(context_, nullptr, 0);
  if (!core_) {
    pw_thread_loop_unlock(loop_);
    pw_context_destroy(context_);
    pw_thread_loop_destroy(loop_);
    context_ = nullptr;
    loop_ = nullptr;
    return false;
  }

  struct pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_NODE_NAME,
      config_.nodeName.c_str(), nullptr);

  if (!config_.sourceTargetName.empty()) {
    pw_properties_set(props, PW_KEY_TARGET_OBJECT,
                       config_.sourceTargetName.c_str());
  }

  static const struct pw_stream_events streamEvents = [] {
    struct pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = &PipeWireAudioCapture::OnProcess;
    return events;
  }();

  stream_ = pw_stream_new(core_, config_.nodeName.c_str(), props);
  if (!stream_) {
    pw_thread_loop_unlock(loop_);
    stop();
    return false;
  }

  pw_stream_add_listener(stream_, &streamListener_, &streamEvents, this);

  uint8_t buffer[1024];
  struct spa_pod_builder builder =
      SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  struct spa_audio_info_raw audioInfo{};
  audioInfo.format = SPA_AUDIO_FORMAT_S16;
  audioInfo.rate = config_.sampleRate;
  audioInfo.channels = config_.channels;
  if (config_.channels == 1) {
    audioInfo.position[0] = SPA_AUDIO_CHANNEL_MONO;
  } else {
    audioInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
    audioInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
  }

  const struct spa_pod* params[1];
  params[0] =
      spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audioInfo);

  const int connectResult = pw_stream_connect(
      stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                    PW_STREAM_FLAG_MAP_BUFFERS |
                                    PW_STREAM_FLAG_RT_PROCESS),
      params, 1);

  pw_thread_loop_unlock(loop_);

  if (connectResult < 0) {
    stop();
    return false;
  }

  if (pw_thread_loop_start(loop_) < 0) {
    stop();
    return false;
  }

  running_ = true;
  return true;
}

bool PipeWireAudioCapture::switchTarget(const std::string& deviceName) {
  const bool wasRunning = running_;
  if (wasRunning) stop();
  config_.sourceTargetName = deviceName;
  if (wasRunning) return start();
  return true;
}

void PipeWireAudioCapture::stop() {
  if (loop_) {
    pw_thread_loop_lock(loop_);
    if (stream_) {
      spa_hook_remove(&streamListener_);
      pw_stream_destroy(stream_);
      stream_ = nullptr;
    }
    if (core_) {
      pw_core_disconnect(core_);
      core_ = nullptr;
    }
    if (context_) {
      pw_context_destroy(context_);
      context_ = nullptr;
    }
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
  }
  running_ = false;
}

}  // namespace wiremic::platform
