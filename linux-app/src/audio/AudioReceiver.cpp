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
  constexpr int kMaxBindAttempts = 5;
  const quint16 requestedPort = localPort_;
  QString lastError;

  for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
      socket_.close();
    }

    if (!socket_.bind(QHostAddress::AnyIPv4, requestedPort)) {
      lastError = socket_.errorString();
      continue;
    }

    localPort_ = socket_.localPort();
    if (localPort_ != 0) {
      playoutTimer_.start(frameSizeMs_);
      return true;
    }

    // Rare but real: the bind() syscall can succeed while the OS
    // momentarily fails to hand back the assigned ephemeral port (seen with
    // some VPN/firewall setups and sandboxed environments). Retrying with a
    // fresh bind almost always clears it, since it is a transient allocation
    // hiccup rather than a persistent condition.
    lastError = QStringLiteral(
        "socket bound but the OS reported no local port");
    socket_.close();
  }

  emit errorOccurred(
      QStringLiteral(
          "Failed to bind a usable audio receiver socket after %1 "
          "attempts: %2. This can happen if a VPN, firewall, or sandboxed "
          "environment is restricting UDP port allocation.")
          .arg(kMaxBindAttempts)
          .arg(lastError));
  return false;
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

    try {
      auto decrypted = AudioPacketCodec::Decrypt(
          key_, reinterpret_cast<const uint8_t*>(data.constData()),
          static_cast<size_t>(data.size()));
      if (!decrypted) continue;

      jitterBuffer_.Push(decrypted->sequence,
                          std::move(decrypted->opusPayload), decrypted->dtx,
                          std::chrono::steady_clock::now());
    } catch (const std::exception& e) {
      emit errorOccurred(
          QStringLiteral("Dropped malformed audio packet: %1").arg(e.what()));
    }
  }
}

void AudioReceiver::onPlayoutTick() {
  const auto outcome = jitterBuffer_.Pop();

  switch (outcome.result) {
    case JitterPopResult::NotReady:
      return;

    case JitterPopResult::Ready: {
      try {
        auto pcm = decoder_->Decode(outcome.payload.data(),
                                     outcome.payload.size(), frameSamples_);
        emit pcmFrameReady(std::move(pcm), false);
      } catch (const std::exception& e) {
        emit errorOccurred(
            QStringLiteral("Opus decode failed, concealing frame: %1")
                .arg(e.what()));
        try {
          auto pcm = decoder_->DecodePacketLoss(frameSamples_);
          emit pcmFrameReady(std::move(pcm), true);
        } catch (const std::exception&) {
          std::vector<int16_t> silence(
              static_cast<size_t>(frameSamples_) *
                  static_cast<size_t>(channels_),
              0);
          emit pcmFrameReady(std::move(silence), true);
        }
      }
      return;
    }

    case JitterPopResult::Loss: {
      try {
        auto pcm = decoder_->DecodePacketLoss(frameSamples_);
        emit pcmFrameReady(std::move(pcm), true);
      } catch (const std::exception& e) {
        emit errorOccurred(
            QStringLiteral("Opus PLC failed, emitting silence: %1")
                .arg(e.what()));
        std::vector<int16_t> silence(static_cast<size_t>(frameSamples_) *
                                          static_cast<size_t>(channels_),
                                      0);
        emit pcmFrameReady(std::move(silence), true);
      }
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

}
