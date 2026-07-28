#include "AudioSender.hpp"

#include <QString>

#include <exception>

namespace wiremic::audio {

AudioSender::AudioSender(SessionKey key, uint32_t sampleRate, int channels,
                          int bitrateKbps, uint8_t frameSizeMs,
                          QHostAddress remoteHost, quint16 remotePort,
                          QObject* parent)
    : QObject(parent),
      key_(key),
      sampleRate_(sampleRate),
      channels_(channels),
      frameSizeMs_(frameSizeMs),
      remoteHost_(std::move(remoteHost)),
      remotePort_(remotePort),
      encoder_(std::make_unique<OpusFrameEncoder>(sampleRate, channels,
                                                    bitrateKbps)) {}

bool AudioSender::start() {
  if (!socket_.bind(QHostAddress::AnyIPv4, 0)) {
    emit errorOccurred(QStringLiteral("Failed to bind audio sender socket: %1")
                            .arg(socket_.errorString()));
    return false;
  }
  sequence_ = 0;
  startTime_ = std::chrono::steady_clock::now();
  encodeErrorReported_ = false;
  running_ = true;
  return true;
}

void AudioSender::stop() {
  running_ = false;
  socket_.close();
}

int AudioSender::frameSamples() const {
  return static_cast<int>(sampleRate_) * frameSizeMs_ / 1000;
}

void AudioSender::pushPcmFrame(const int16_t* pcm, int frameSamples) {
  if (!running_) return;

  // Encode and encrypt both throw. This runs from a timer slot, so letting an
  // exception escape would unwind through the Qt event loop and abort the whole
  // application over a single bad frame — drop the frame instead.
  try {
    const auto opusPayload = encoder_->Encode(pcm, frameSamples);
    if (opusPayload.empty()) return;

    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_)
            .count();

    const auto packet = AudioPacketCodec::Encrypt(
        key_, sequence_++, static_cast<uint32_t>(elapsedMs), false, false,
        opusPayload);

    socket_.writeDatagram(reinterpret_cast<const char*>(packet.data()),
                           static_cast<qint64>(packet.size()), remoteHost_,
                           remotePort_);
  } catch (const std::exception& e) {
    // One failure usually means every following frame fails too, so report
    // only the first rather than flooding the log at one message per frame.
    if (!encodeErrorReported_) {
      encodeErrorReported_ = true;
      emit errorOccurred(
          QStringLiteral("Dropping outgoing audio frames: %1").arg(e.what()));
    }
  }
}

}  // namespace wiremic::audio
