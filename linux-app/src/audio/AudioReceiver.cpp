#include "AudioReceiver.hpp"

#include <QNetworkDatagram>

namespace wiremic::audio {

AudioReceiver::AudioReceiver(SessionKey key, uint32_t sampleRate,
                              int channels, uint8_t frameSizeMs,
                              quint16 localPort, QObject* parent)
    : QObject(parent),
      key_(key),
      sampleRate_(sampleRate),
      channels_(channels),
      frameSizeMs_(frameSizeMs),
      localPort_(localPort),
      decoder_(std::make_unique<OpusFrameDecoder>(sampleRate, channels)),
      jitterBuffer_(frameSizeMs),
      frameSamples_(static_cast<int>(sampleRate) * frameSizeMs / 1000) {
  connect(&socket_, &QUdpSocket::readyRead, this,
          &AudioReceiver::onReadyRead);
  connect(&playoutTimer_, &QTimer::timeout, this,
          &AudioReceiver::onPlayoutTick);
}

bool AudioReceiver::start() {
  if (!socket_.bind(QHostAddress::AnyIPv4, localPort_)) {
    emit errorOccurred(
        QStringLiteral("Failed to bind audio receiver socket: %1")
            .arg(socket_.errorString()));
    return false;
  }
  localPort_ = socket_.localPort();
  playoutTimer_.start(frameSizeMs_);
  return true;
}

void AudioReceiver::stop() {
  playoutTimer_.stop();
  socket_.close();
}

quint16 AudioReceiver::port() const { return localPort_; }

int AudioReceiver::currentJitterDepthFrames() const {
  return jitterBuffer_.currentTargetDepthFrames();
}

void AudioReceiver::onReadyRead() {
  while (socket_.hasPendingDatagrams()) {
    const auto datagram = socket_.receiveDatagram();
    const auto data = datagram.data();

    auto decrypted = AudioPacketCodec::Decrypt(
        key_, reinterpret_cast<const uint8_t*>(data.constData()),
        static_cast<size_t>(data.size()));
    if (!decrypted) continue;

    jitterBuffer_.Push(decrypted->sequence, std::move(decrypted->opusPayload),
                        decrypted->dtx, std::chrono::steady_clock::now());
  }
}

void AudioReceiver::onPlayoutTick() {
  const auto outcome = jitterBuffer_.Pop();

  switch (outcome.result) {
    case JitterPopResult::NotReady:
      return;

    case JitterPopResult::Ready: {
      auto pcm = decoder_->Decode(outcome.payload.data(),
                                   outcome.payload.size(), frameSamples_);
      emit pcmFrameReady(std::move(pcm), false);
      return;
    }

    case JitterPopResult::Loss: {
      auto pcm = decoder_->DecodePacketLoss(frameSamples_);
      emit pcmFrameReady(std::move(pcm), true);
      return;
    }

    case JitterPopResult::Silence: {
      std::vector<int16_t> silence(static_cast<size_t>(frameSamples_) *
                                        static_cast<size_t>(channels_),
                                    0);
      emit pcmFrameReady(std::move(silence), false);
      return;
    }
  }
}

}  // namespace wiremic::audio
