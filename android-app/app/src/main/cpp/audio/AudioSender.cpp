#include "AudioSender.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <vector>

namespace wiremic::android {

AudioSender::AudioSender() = default;

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
  pending_.clear();
  pending_.reserve(static_cast<size_t>(frameSamples_) * channels_ * 4);
  encodeErrorReported_ = false;

  try {
    encoder_ = std::make_unique<audio::OpusFrameEncoder>(
        sampleRate_, channels_, static_cast<int>(session.bitrateKbps));
  } catch (const std::exception& e) {
    if (errorCallback_) {
      errorCallback_(std::string("failed to create Opus encoder: ") + e.what());
    }
    return false;
  }

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
  AAudioStreamBuilder_setFramesPerDataCallback(builder, frameSamples_);
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

  streamSampleRate_ = AAudioStream_getSampleRate(stream_);
  streamChannels_ = AAudioStream_getChannelCount(stream_);
  streamFormat_ = AAudioStream_getFormat(stream_);

  if (streamSampleRate_ <= 0) streamSampleRate_ = static_cast<int32_t>(sampleRate_);
  if (streamChannels_ <= 0) streamChannels_ = channels_;

  resampler_.Reset(streamSampleRate_, static_cast<int32_t>(sampleRate_));

  if (streamFormat_ != AAUDIO_FORMAT_PCM_I16 &&
      streamFormat_ != AAUDIO_FORMAT_PCM_FLOAT) {
    if (errorCallback_) {
      errorCallback_("microphone returned an unsupported sample format");
    }
    AAudioStream_close(stream_);
    stream_ = nullptr;
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (streamSampleRate_ != static_cast<int32_t>(sampleRate_) ||
      streamChannels_ != channels_ ||
      streamFormat_ != AAUDIO_FORMAT_PCM_I16) {
    if (errorCallback_) {
      errorCallback_("microphone opened at " + std::to_string(streamSampleRate_) +
                      " Hz / " + std::to_string(streamChannels_) +
                      " ch; converting to " + std::to_string(sampleRate_) +
                      " Hz / " + std::to_string(channels_) + " ch");
    }
  }

  const int32_t framesPerBurst = AAudioStream_getFramesPerBurst(stream_);
  const int32_t callbackFrames =
      static_cast<int32_t>(static_cast<int64_t>(frameSamples_) *
                            streamSampleRate_ / static_cast<int32_t>(sampleRate_));
  int32_t desiredBuffer = callbackFrames * 2;
  if (framesPerBurst > 0) {
    const int32_t burstFloor = framesPerBurst * 2;
    if (desiredBuffer < burstFloor) desiredBuffer = burstFloor;
    const int32_t remainder = desiredBuffer % framesPerBurst;
    if (remainder != 0) desiredBuffer += framesPerBurst - remainder;
  }
  if (desiredBuffer > 0) {
    AAudioStream_setBufferSizeInFrames(stream_, desiredBuffer);
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
  pending_.clear();
  monoScratch_.clear();
  streamSampleRate_ = 0;
  streamChannels_ = 0;
  running_ = false;
}

aaudio_data_callback_result_t AudioSender::OnAudioData(AAudioStream* ,
                                                         void* userdata,
                                                         void* audioData,
                                                         int32_t numFrames) {
  auto* self = static_cast<AudioSender*>(userdata);
  return self->handleAudioData(audioData, numFrames);
}

void AudioSender::OnStreamError(AAudioStream* , void* userdata,
                                 aaudio_result_t error) {
  auto* self = static_cast<AudioSender*>(userdata);
  if (self->errorCallback_) {
    self->errorCallback_(std::string("AAudio stream error: ") +
                          AAudio_convertResultToText(error));
  }
  self->running_ = false;
}

void AudioSender::appendConverted(const void* frames, int32_t numFrames) {
  const size_t inFrames = static_cast<size_t>(numFrames);
  const size_t inChannels = static_cast<size_t>(streamChannels_);
  if (inChannels == 0) return;

  monoScratch_.clear();
  monoScratch_.reserve(inFrames);

  if (streamFormat_ == AAUDIO_FORMAT_PCM_FLOAT) {
    const auto* src = static_cast<const float*>(frames);
    for (size_t f = 0; f < inFrames; ++f) {
      double sum = 0.0;
      for (size_t c = 0; c < inChannels; ++c) sum += src[f * inChannels + c];
      double value = (sum / static_cast<double>(inChannels)) * 32767.0;
      if (value > 32767.0) value = 32767.0;
      if (value < -32768.0) value = -32768.0;
      monoScratch_.push_back(static_cast<int16_t>(value));
    }
  } else {
    const auto* src = static_cast<const int16_t*>(frames);
    for (size_t f = 0; f < inFrames; ++f) {
      int32_t sum = 0;
      for (size_t c = 0; c < inChannels; ++c) sum += src[f * inChannels + c];
      monoScratch_.push_back(
          static_cast<int16_t>(sum / static_cast<int32_t>(inChannels)));
    }
  }

  resampler_.Process(monoScratch_.data(), monoScratch_.size(), pending_);
}

aaudio_data_callback_result_t AudioSender::handleAudioData(
    const void* frames, int32_t numFrames) {
  if (!running_ || socketFd_ < 0 || !encoder_) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  if (frames == nullptr || numFrames <= 0) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  const size_t chunk = static_cast<size_t>(frameSamples_) * channels_;
  if (chunk == 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;

  appendConverted(frames, numFrames);

  const size_t maxPending = chunk * 3;
  if (pending_.size() > maxPending) {
    pending_.erase(pending_.begin(),
                    pending_.begin() +
                        static_cast<long>(pending_.size() - maxPending));
  }

  size_t offset = 0;
  while (pending_.size() - offset >= chunk) {
    const int16_t* frame = pending_.data() + offset;
    offset += chunk;

    try {
      const auto opusPayload = encoder_->Encode(frame, frameSamples_);
      if (opusPayload.empty()) continue;

      const auto nowMs = static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());

      const auto packet = audio::AudioPacketCodec::Encrypt(
          sessionKey_, sequence_++, nowMs, false, false, opusPayload);

      send(socketFd_, packet.data(), packet.size(), 0);
    } catch (const std::exception& e) {
      if (!encodeErrorReported_) {
        encodeErrorReported_ = true;
        if (errorCallback_) {
          errorCallback_(std::string("Dropping outgoing audio frames: ") +
                          e.what());
        }
      }
    }
  }

  if (offset > 0) {
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(offset));
  }

  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

}
