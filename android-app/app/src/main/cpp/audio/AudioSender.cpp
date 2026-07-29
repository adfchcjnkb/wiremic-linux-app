#include "AudioSender.hpp"

#include "OpenSLESCapture.hpp"

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

  std::string chosen;
  capture_ = CreateAudioCapture(
      [this](const void* frames, int32_t frameCount) {
        handleAudioData(frames, frameCount);
      },
      &chosen);

  if (!capture_) {
    if (errorCallback_) errorCallback_("no audio capture backend is available");
    close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (!capture_->start(static_cast<int32_t>(sampleRate_), channels_,
                       frameSamples_)) {
    const std::string firstError = capture_->lastError();

    // AAudio can refuse to open even where it exists -- another application
    // holding the microphone exclusively, or a vendor bug. OpenSL ES is
    // present on every release, so fall back to it rather than giving up.
    capture_.reset();
    capture_ = std::make_unique<OpenSLESCapture>(
        [this](const void* frames, int32_t frameCount) {
          handleAudioData(frames, frameCount);
        });
    chosen = "OpenSL ES";

    if (!capture_->start(static_cast<int32_t>(sampleRate_), channels_,
                         frameSamples_)) {
      if (errorCallback_) {
        errorCallback_("could not open the microphone (" + firstError +
                        "; then " + capture_->lastError() + ")");
      }
      capture_.reset();
      close(socketFd_);
      socketFd_ = -1;
      return false;
    }
  }

  streamSampleRate_ = capture_->sampleRate();
  streamChannels_ = capture_->channels();
  streamIsFloat_ = capture_->isFloat();
  if (streamSampleRate_ <= 0) streamSampleRate_ = static_cast<int32_t>(sampleRate_);
  if (streamChannels_ <= 0) streamChannels_ = channels_;

  resampler_.Reset(streamSampleRate_, static_cast<int32_t>(sampleRate_));

  if (errorCallback_ &&
      (streamSampleRate_ != static_cast<int32_t>(sampleRate_) ||
       streamChannels_ != channels_ || streamIsFloat_)) {
    errorCallback_(std::string("microphone via ") + chosen + " opened at " +
                    std::to_string(streamSampleRate_) + " Hz / " +
                    std::to_string(streamChannels_) + " ch; converting to " +
                    std::to_string(sampleRate_) + " Hz / " +
                    std::to_string(channels_) + " ch");
  }

  running_ = true;
  return true;
}

void AudioSender::stop() {
  if (capture_) {
    capture_->stop();
    capture_.reset();
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

void AudioSender::appendConverted(const void* frames, int32_t numFrames) {
  const size_t inFrames = static_cast<size_t>(numFrames);
  const size_t inChannels = static_cast<size_t>(streamChannels_);
  if (inChannels == 0) return;

  monoScratch_.clear();
  monoScratch_.reserve(inFrames);

  if (streamIsFloat_) {
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

void AudioSender::handleAudioData(const void* frames, int32_t numFrames) {
  if (!running_ || socketFd_ < 0 || !encoder_) return;
  if (frames == nullptr || numFrames <= 0) return;

  const size_t chunk = static_cast<size_t>(frameSamples_) * channels_;
  if (chunk == 0) return;

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

}

}
