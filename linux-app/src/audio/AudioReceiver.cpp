#include "AudioReceiver.hpp"

#include <QNetworkDatagram>

#include <algorithm>

#include "UdpSocketBinder.hpp"

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
  playoutTimer_.setTimerType(Qt::PreciseTimer);
}

bool AudioReceiver::start() {
  QString bindError;
  quint16 boundPort = 0;

  if (!BindUdpSocket(socket_, localPort_, boundPort, bindError)) {
    emit errorOccurred(
        QStringLiteral("Failed to bind the audio receiver socket: %1. A VPN, "
                       "firewall, or sandbox may be restricting UDP.")
            .arg(bindError));
    return false;
  }

  localPort_ = boundPort;
  playoutClockRunning_ = false;
  framesEmitted_ = 0;
  playoutTimer_.start(frameSizeMs_);
  return true;
}

void AudioReceiver::stop() {
  playoutTimer_.stop();
  playoutClockRunning_ = false;
  framesEmitted_ = 0;
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
  const auto now = std::chrono::steady_clock::now();

  if (!playoutClockRunning_) {
    if (jitterBuffer_.bufferedCount() == 0) return;
    playoutClockRunning_ = true;
    playoutEpoch_ = now;
    framesEmitted_ = 0;
  }

  const auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - playoutEpoch_)
          .count();
  uint64_t framesDue =
      static_cast<uint64_t>(elapsedMs) / std::max<uint8_t>(1, frameSizeMs_) + 1;
  if (framesDue <= framesEmitted_) framesDue = framesEmitted_ + 1;

  constexpr uint64_t kMaxFramesPerTick = 8;
  uint64_t budget = framesDue - framesEmitted_;
  if (budget > kMaxFramesPerTick) {
    framesEmitted_ = framesDue - kMaxFramesPerTick;
    budget = kMaxFramesPerTick;
  }

  for (uint64_t i = 0; i < budget; ++i) {
    if (!emitOneFrame()) {
      playoutClockRunning_ = false;
      return;
    }
    ++framesEmitted_;
  }
}

bool AudioReceiver::emitOneFrame() {
  const auto outcome = jitterBuffer_.Pop();

  switch (outcome.result) {
    case JitterPopResult::NotReady:
      return false;

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
      return true;
    }

    case JitterPopResult::Loss: {
      try {
        if (!outcome.fecPayload.empty()) {
          auto pcm = decoder_->DecodeWithFec(outcome.fecPayload.data(),
                                              outcome.fecPayload.size(),
                                              frameSamples_);
          emit pcmFrameReady(std::move(pcm), true);
          return true;
        }
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
      return true;
    }

    case JitterPopResult::Silence: {
      std::vector<int16_t> silence(static_cast<size_t>(frameSamples_) *
                                        static_cast<size_t>(channels_),
                                    0);
      emit pcmFrameReady(std::move(silence), false);
      return true;
    }
  }
  return false;
}

}
