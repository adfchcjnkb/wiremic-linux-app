#pragma once

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

#include <chrono>
#include <cstdint>
#include <memory>

#include "AudioPacketCodec.hpp"
#include "OpusCodec.hpp"

namespace wiremic::audio {

class AudioSender : public QObject {
  Q_OBJECT

 public:
  AudioSender(SessionKey key, uint32_t sampleRate, int channels,
              int bitrateKbps, uint8_t frameSizeMs, QHostAddress remoteHost,
              quint16 remotePort, QObject* parent = nullptr);

  bool start();
  void stop();

  void pushPcmFrame(const int16_t* pcm, int frameSamples);

  [[nodiscard]] int frameSamples() const;

 signals:
  void errorOccurred(QString message);

 private:
  SessionKey key_;
  uint32_t sampleRate_;
  int channels_;
  uint8_t frameSizeMs_;
  QHostAddress remoteHost_;
  quint16 remotePort_;

  QUdpSocket socket_;
  std::unique_ptr<OpusFrameEncoder> encoder_;
  uint64_t sequence_{0};
  std::chrono::steady_clock::time_point startTime_;
  bool running_{false};
  bool encodeErrorReported_{false};
};

}  // namespace wiremic::audio
