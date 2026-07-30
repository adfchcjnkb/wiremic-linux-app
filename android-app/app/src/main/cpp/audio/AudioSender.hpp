#pragma once

#include <semaphore.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "AudioCaptureBackend.hpp"
#include "AudioPacketCodec.hpp"
#include "MonoResampler.hpp"
#include "OpusCodec.hpp"
#include "PcmRingBuffer.hpp"
#include "Protocol.hpp"

namespace wiremic::android {

// Captures the microphone and streams it to the desktop.
//
// The device hands audio over on a thread with a hard deadline. Everything
// expensive -- resampling, Opus, encryption, the socket write -- happens on a
// separate thread, and the two are joined by a lock-free queue. Doing that work
// inside the callback makes the platform drop microphone blocks whenever a
// frame runs long, and every dropped block is a step in the waveform that is
// heard as a click.
class AudioSender {
 public:
  using ErrorCallback = std::function<void(std::string)>;

  AudioSender();
  ~AudioSender();

  AudioSender(const AudioSender&) = delete;
  AudioSender& operator=(const AudioSender&) = delete;

  bool start(const protocol::AudioSession& session, const std::string& host,
             uint16_t udpPort);
  void stop();
  [[nodiscard]] bool isRunning() const;

  void setErrorCallback(ErrorCallback callback);

  // Microphone samples the queue could not accept because the sender thread
  // stalled. Zero over a whole session is the goal; anything else is audible.
  [[nodiscard]] uint64_t droppedSamples() const;

 private:
  // Runs on the device audio callback thread. Allocation, locks, syscalls and
  // exceptions are all forbidden here.
  void handleAudioData(const void* frames, int32_t numFrames);

  void senderLoop();
  void drainQueue();
  void encodePending();
  void reportError(std::string message);

  bool openUdpSocket(const std::string& host, uint16_t port);
  void closeSocket();
  void teardown();

  std::unique_ptr<AudioCaptureBackend> capture_;
  int socketFd_{-1};
  std::atomic<bool> running_{false};

  std::unique_ptr<audio::OpusFrameEncoder> encoder_;
  audio::SessionKey sessionKey_{};
  uint64_t sequence_{0};
  uint32_t sampleRate_{48000};
  uint8_t channels_{1};
  int frameSamples_{480};

  int32_t streamSampleRate_{0};
  int32_t streamChannels_{0};
  bool streamIsFloat_{false};

  // The callback must see a fully configured pipeline or nothing at all.
  std::atomic<bool> captureReady_{false};

  audio::PcmRingBuffer ring_;
  std::vector<int16_t> captureScratch_;
  // The callback cannot wait for room, so anything the queue refuses is gone.
  std::atomic<uint64_t> droppedSamples_{0};

  std::thread worker_;
  std::atomic<bool> workerRunning_{false};
  sem_t wake_{};
  bool wakeReady_{false};

  audio::MonoResampler resampler_;
  std::vector<int16_t> drained_;
  std::vector<int16_t> pending_;
  std::vector<int16_t> frameScratch_;
  bool encodeErrorReported_{false};

  ErrorCallback errorCallback_;
};

}
