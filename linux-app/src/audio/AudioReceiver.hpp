#pragma once

#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <memory>
#include <vector>

#include "AudioPacketCodec.hpp"
#include "JitterBuffer.hpp"
#include "OpusCodec.hpp"

namespace wiremic::audio {

class AudioReceiver : public QObject {
  Q_OBJECT

 public:
  AudioReceiver(SessionKey key, uint32_t sampleRate, int channels,
                uint8_t frameSizeMs, quint16 localPort,
                QObject* parent = nullptr);

  bool start();
  void stop();

  [[nodiscard]] quint16 port() const;
  [[nodiscard]] int currentJitterDepthFrames() const;

 signals:
  void pcmFrameReady(std::vector<int16_t> samples, bool wasConcealed);
  void errorOccurred(QString message);

 private slots:
  void onReadyRead();
  void onPlayoutTick();

 private:
  SessionKey key_;
  uint32_t sampleRate_;
  int channels_;
  uint8_t frameSizeMs_;
  quint16 localPort_;

  QUdpSocket socket_;
  std::unique_ptr<OpusFrameDecoder> decoder_;
  JitterBuffer jitterBuffer_;
  QTimer playoutTimer_;
  int frameSamples_;
};

}
