#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AudioPacketCodec.hpp"
#include "OpusCodec.hpp"
#include "Protocol.hpp"

namespace wiremic::android {

class AudioSender {
 public:
  using ErrorCallback = std::function<void(std::string)>;

  AudioSender();
  ~AudioSender();

  bool start(const protocol::AudioSession& session, const std::string& host,
             uint16_t udpPort);
  void stop();
  [[nodiscard]] bool isRunning() const;

  void setErrorCallback(ErrorCallback callback);

 private:
  static aaudio_data_callback_result_t OnAudioData(AAudioStream* stream,
                                                     void* userdata,
                                                     void* audioData,
                                                     int32_t numFrames);
  static void OnStreamError(AAudioStream* stream, void* userdata,
                             aaudio_result_t error);

  aaudio_data_callback_result_t handleAudioData(const int16_t* frames,
                                                 int32_t numFrames);

  bool openUdpSocket(const std::string& host, uint16_t port);

  AAudioStream* stream_{nullptr};
  int socketFd_{-1};
  std::atomic<bool> running_{false};

  std::unique_ptr<audio::OpusFrameEncoder> encoder_;
  audio::SessionKey sessionKey_{};
  uint64_t sequence_{0};
  uint32_t sampleRate_{48000};
  uint8_t channels_{1};
  int frameSamples_{480};

  std::vector<int16_t> pending_;
  bool encodeErrorReported_{false};

  ErrorCallback errorCallback_;
};

}
