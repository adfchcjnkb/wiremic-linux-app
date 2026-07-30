#include "AudioSender.hpp"

#include "OpenSLESCapture.hpp"

#include <arpa/inet.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <vector>

namespace wiremic::android {

namespace {

// Enough slack that a scheduling hiccup on the sender thread costs latency
// rather than samples. This is a second of audio at the negotiated rate, a few
// hundred kilobytes, and it is never filled in normal operation.
constexpr size_t kQueueSeconds = 1;

// If the sender thread ever does fall this far behind, the backlog is thrown
// away instead of played out late. For a live microphone, late audio is worse
// than missing audio.
constexpr int kMaxBacklogMs = 200;

// The largest single callback the mixer converts without slicing.
constexpr size_t kMaxCallbackFrames = 16384;

}

AudioSender::AudioSender() = default;

AudioSender::~AudioSender() { stop(); }

void AudioSender::setErrorCallback(ErrorCallback callback) {
  errorCallback_ = std::move(callback);
}

bool AudioSender::isRunning() const { return running_; }

uint64_t AudioSender::droppedSamples() const {
  return droppedSamples_.load(std::memory_order_relaxed);
}

void AudioSender::reportError(std::string message) {
  if (errorCallback_) errorCallback_(std::move(message));
}

bool AudioSender::openUdpSocket(const std::string& host, uint16_t port) {
  socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFd_ < 0) return false;

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    closeSocket();
    return false;
  }

  if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&address),
              sizeof(address)) < 0) {
    closeSocket();
    return false;
  }
  return true;
}

void AudioSender::closeSocket() {
  if (socketFd_ >= 0) {
    close(socketFd_);
    socketFd_ = -1;
  }
}

void AudioSender::teardown() {
  // Silence the callback first so nothing new enters the queue while the
  // pipeline behind it is being dismantled.
  captureReady_.store(false, std::memory_order_release);

  if (capture_) {
    capture_->stop();
    capture_.reset();
  }

  if (workerRunning_.exchange(false)) {
    if (wakeReady_) sem_post(&wake_);
    if (worker_.joinable()) worker_.join();
  }
  if (wakeReady_) {
    sem_destroy(&wake_);
    wakeReady_ = false;
  }

  closeSocket();

  pending_.clear();
  streamSampleRate_ = 0;
  streamChannels_ = 0;
  running_ = false;
}

bool AudioSender::start(const protocol::AudioSession& session,
                         const std::string& host, uint16_t udpPort) {
  if (running_) return true;

  sessionKey_ = session.sessionKey;
  sampleRate_ = session.sampleRate;
  channels_ = session.channels > 0 ? session.channels : 1;
  sequence_ = 0;
  frameSamples_ = static_cast<int>(sampleRate_) * session.frameSizeMs / 1000;
  if (frameSamples_ <= 0) frameSamples_ = 480;
  encodeErrorReported_ = false;
  droppedSamples_.store(0, std::memory_order_relaxed);
  captureReady_.store(false, std::memory_order_release);

  try {
    encoder_ = std::make_unique<audio::OpusFrameEncoder>(
        sampleRate_, channels_, static_cast<int>(session.bitrateKbps));
  } catch (const std::exception& e) {
    reportError(std::string("failed to create Opus encoder: ") + e.what());
    return false;
  }

  if (!openUdpSocket(host, udpPort)) {
    reportError("failed to open audio UDP socket");
    return false;
  }

  // Everything the callback touches has to exist before capture starts, even
  // though the real device format is only known afterwards. The queue is sized
  // for the worst case and the callback stays inert until captureReady_ is set.
  ring_.Reset(static_cast<size_t>(sampleRate_) * kQueueSeconds * 2);
  captureScratch_.assign(kMaxCallbackFrames, 0);

  std::string chosen;
  capture_ = CreateAudioCapture(
      [this](const void* frames, int32_t frameCount) {
        handleAudioData(frames, frameCount);
      },
      &chosen);

  if (!capture_) {
    reportError("no audio capture backend is available");
    closeSocket();
    return false;
  }

  if (!capture_->start(static_cast<int32_t>(sampleRate_), 1, frameSamples_)) {
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

    if (!capture_->start(static_cast<int32_t>(sampleRate_), 1, frameSamples_)) {
      reportError("could not open the microphone (" + firstError + "; then " +
                  capture_->lastError() + ")");
      capture_.reset();
      closeSocket();
      return false;
    }
  }

  streamSampleRate_ = capture_->sampleRate();
  streamChannels_ = capture_->channels();
  streamIsFloat_ = capture_->isFloat();
  if (streamSampleRate_ <= 0) {
    streamSampleRate_ = static_cast<int32_t>(sampleRate_);
  }
  if (streamChannels_ <= 0) streamChannels_ = 1;

  resampler_.Reset(streamSampleRate_, static_cast<int32_t>(sampleRate_));

  drained_.assign(static_cast<size_t>(streamSampleRate_) / 2, 0);
  pending_.clear();
  pending_.reserve(static_cast<size_t>(frameSamples_) * 8);
  frameScratch_.assign(
      static_cast<size_t>(frameSamples_) * static_cast<size_t>(channels_), 0);

  if (sem_init(&wake_, 0, 0) != 0) {
    reportError("failed to create the audio sender semaphore");
    capture_->stop();
    capture_.reset();
    closeSocket();
    return false;
  }
  wakeReady_ = true;

  workerRunning_.store(true, std::memory_order_release);
  worker_ = std::thread(&AudioSender::senderLoop, this);

  running_ = true;
  captureReady_.store(true, std::memory_order_release);

  if (streamSampleRate_ != static_cast<int32_t>(sampleRate_) ||
      streamChannels_ != 1 || streamIsFloat_) {
    reportError(std::string("microphone via ") + chosen + " opened at " +
                std::to_string(streamSampleRate_) + " Hz / " +
                std::to_string(streamChannels_) + " ch; converting to " +
                std::to_string(sampleRate_) + " Hz / mono");
  }

  return true;
}

void AudioSender::stop() {
  if (!running_ && !workerRunning_ && socketFd_ < 0 && !capture_) return;
  teardown();
}

void AudioSender::handleAudioData(const void* frames, int32_t numFrames) {
  if (!captureReady_.load(std::memory_order_acquire)) return;
  if (frames == nullptr || numFrames <= 0) return;

  const size_t inChannels = static_cast<size_t>(streamChannels_);
  if (inChannels == 0) return;

  size_t remaining = static_cast<size_t>(numFrames);
  size_t offset = 0;

  // Mixing to mono and copying into the queue is the entire budget of the
  // audio callback. Anything more and the platform starts dropping blocks.
  while (remaining > 0) {
    const size_t slice =
        remaining < kMaxCallbackFrames ? remaining : kMaxCallbackFrames;

    if (streamIsFloat_) {
      const auto* src = static_cast<const float*>(frames) + offset * inChannels;
      for (size_t f = 0; f < slice; ++f) {
        float sum = 0.0f;
        for (size_t c = 0; c < inChannels; ++c) sum += src[f * inChannels + c];
        float value = (sum / static_cast<float>(inChannels)) * 32767.0f;
        if (value > 32767.0f) value = 32767.0f;
        if (value < -32768.0f) value = -32768.0f;
        captureScratch_[f] = static_cast<int16_t>(value);
      }
    } else {
      const auto* src =
          static_cast<const int16_t*>(frames) + offset * inChannels;
      if (inChannels == 1) {
        std::memcpy(captureScratch_.data(), src, slice * sizeof(int16_t));
      } else {
        for (size_t f = 0; f < slice; ++f) {
          int32_t sum = 0;
          for (size_t c = 0; c < inChannels; ++c) sum += src[f * inChannels + c];
          captureScratch_[f] =
              static_cast<int16_t>(sum / static_cast<int32_t>(inChannels));
        }
      }
    }

    // Retrying would mean blocking the audio thread, so a full queue costs
    // samples. That is only reachable if the sender thread has stalled for
    // the best part of a second.
    const size_t accepted = ring_.Write(captureScratch_.data(), slice);
    if (accepted < slice) {
      droppedSamples_.fetch_add(slice - accepted, std::memory_order_relaxed);
    }

    offset += slice;
    remaining -= slice;
  }

  if (wakeReady_) sem_post(&wake_);
}

void AudioSender::senderLoop() {
  // Best effort: ask to be scheduled ahead of ordinary work so a busy phone
  // does not let the queue build up. Being refused is harmless.
  setpriority(PRIO_PROCESS, 0, -16);

  while (workerRunning_.load(std::memory_order_acquire)) {
    while (sem_wait(&wake_) != 0 && errno == EINTR) {
    }
    if (!workerRunning_.load(std::memory_order_acquire)) break;
    drainQueue();
  }

  // Flush whatever the microphone delivered just before the stop request.
  drainQueue();
}

void AudioSender::drainQueue() {
  if (drained_.empty()) return;

  // A backlog is dropped rather than transmitted behind the talker.
  const size_t backlogLimit = static_cast<size_t>(streamSampleRate_) *
                              static_cast<size_t>(kMaxBacklogMs) / 1000;
  const size_t waiting = ring_.available();
  if (backlogLimit > 0 && waiting > backlogLimit) {
    const size_t trimmed = ring_.Discard(waiting - backlogLimit);
    droppedSamples_.fetch_add(trimmed, std::memory_order_relaxed);
  }

  while (true) {
    const size_t got = ring_.Read(drained_.data(), drained_.size());
    if (got == 0) break;
    resampler_.Process(drained_.data(), got, pending_);
    encodePending();
    if (got < drained_.size()) break;
  }
}

void AudioSender::encodePending() {
  const size_t chunk = static_cast<size_t>(frameSamples_);
  if (chunk == 0 || socketFd_ < 0 || !encoder_) return;

  size_t offset = 0;
  while (pending_.size() - offset >= chunk) {
    const int16_t* mono = pending_.data() + offset;
    offset += chunk;

    const int16_t* toEncode = mono;
    if (channels_ > 1) {
      for (size_t f = 0; f < chunk; ++f) {
        for (uint8_t c = 0; c < channels_; ++c) {
          frameScratch_[f * channels_ + c] = mono[f];
        }
      }
      toEncode = frameScratch_.data();
    }

    try {
      const auto opusPayload = encoder_->Encode(toEncode, frameSamples_);
      if (opusPayload.empty()) continue;

      const auto nowMs = static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());

      const auto packet = audio::AudioPacketCodec::Encrypt(
          sessionKey_, sequence_++, nowMs, false, false, opusPayload);

      ::send(socketFd_, packet.data(), packet.size(), 0);
    } catch (const std::exception& e) {
      if (!encodeErrorReported_) {
        encodeErrorReported_ = true;
        reportError(std::string("Dropping outgoing audio frames: ") + e.what());
      }
    }
  }

  if (offset > 0) {
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<long>(offset));
  }
}

}
