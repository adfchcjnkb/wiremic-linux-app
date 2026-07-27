#include "AudioSender.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

namespace wiremic::android {

AudioSender::AudioSender() : encoder_(48000, 1, 96) {}

AudioSender::~AudioSender() { stop(); }

void AudioSender::setErrorCallback(ErrorCallback callback) {
  errorCallback_ = std::move(callback);
}

bool AudioSender::isRunning() const { return running_; }

bool AudioSender::openUdpSocket(const std::string& host, uint16_t port) {
  socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFd_ < 0) return false;

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&address),
              sizeof(address)) < 0) {
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }
  return true;
}

bool AudioSender::start(const protocol::AudioSession& session,
                         const std::string& host, uint16_t udpPort) {
  if (running_) return true;

  sessionKey_ = session.sessionKey;
  sampleRate_ = session.sampleRate;
  channels_ = session.channels;
  sequence_ = 0;
  frameSamples_ = static_cast<int>(sampleRate_) * session.frameSizeMs / 1000;

  encoder_.~OpusFrameEncoder();
  new (&encoder_) audio::OpusFrameEncoder(sampleRate_, channels_,
                                           static_cast<int>(session.bitrateKbps));

  if (!openUdpSocket(host, udpPort)) {
    if (errorCallback_) errorCallback_("failed to open audio UDP socket");
    return false;
  }

  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
    if (errorCallback_) errorCallback_("failed to create AAudio stream builder");
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
  AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sampleRate_));
  AAudioStreamBuilder_setChannelCount(builder, channels_);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setPerformanceMode(builder,
                                         AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
#if __ANDROID_API__ >= 28
  AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
#endif
  AAudioStreamBuilder_setDataCallback(builder, &AudioSender::OnAudioData, this);
  AAudioStreamBuilder_setErrorCallback(builder, &AudioSender::OnStreamError, this);

  const aaudio_result_t openResult =
      AAudioStreamBuilder_openStream(builder, &stream_);
  AAudioStreamBuilder_delete(builder);

  if (openResult != AAUDIO_OK || !stream_) {
    if (errorCallback_) errorCallback_("failed to open AAudio input stream");
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (AAudioStream_requestStart(stream_) != AAUDIO_OK) {
    if (errorCallback_) errorCallback_("failed to start AAudio input stream");
    AAudioStream_close(stream_);
    stream_ = nullptr;
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  running_ = true;
  return true;
}

void AudioSender::stop() {
  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
  if (socketFd_ >= 0) {
    close(socketFd_);
    socketFd_ = -1;
  }
  running_ = false;
}

aaudio_data_callback_result_t AudioSender::OnAudioData(AAudioStream* /*stream*/,
                                                         void* userdata,
                                                         void* audioData,
                                                         int32_t numFrames) {
  auto* self = static_cast<AudioSender*>(userdata);
  return self->handleAudioData(static_cast<const int16_t*>(audioData), numFrames);
}

void AudioSender::OnStreamError(AAudioStream* /*stream*/, void* userdata,
                                 aaudio_result_t error) {
  auto* self = static_cast<AudioSender*>(userdata);
  if (self->errorCallback_) {
    self->errorCallback_(std::string("AAudio stream error: ") +
                          AAudio_convertResultToText(error));
  }
  self->running_ = false;
}

aaudio_data_callback_result_t AudioSender::handleAudioData(
    const int16_t* frames, int32_t numFrames) {
  if (!running_ || socketFd_ < 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;

  if (frames == nullptr || numFrames <= 0) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  if (numFrames != frameSamples_) {
    if (errorCallback_) {
      errorCallback_("AAudio frame count mismatch: expected " + 
                      std::to_string(frameSamples_) + 
                      " got " + std::to_string(numFrames));
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  std::vector<uint8_t> opusPayload;
  try {
    opusPayload = encoder_.Encode(frames, numFrames);
  } catch (const std::exception& e) {
    if (errorCallback_) {
      errorCallback_(std::string("Opus encode error: ") + e.what());
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  if (opusPayload.empty()) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  const auto nowMs = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());

  const auto packet = audio::AudioPacketCodec::Encrypt(
      sessionKey_, sequence_++, nowMs, false, false, opusPayload);

  send(socketFd_, packet.data(), packet.size(), 0);

  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

}  // namespace wiremic::android