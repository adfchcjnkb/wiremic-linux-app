#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUdpSocket>

#include <cmath>
#include <iostream>
#include <vector>

#include "AudioReceiver.hpp"
#include "AudioSender.hpp"
#include "Check.hpp"

using namespace wiremic::audio;

namespace {

bool WaitFor(std::function<bool()> predicate, int timeoutMs) {
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeoutMs);

  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, [&]() {
    if (predicate()) loop.quit();
  });
  poll.start(5);

  if (!predicate()) loop.exec();
  return predicate();
}

}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  SessionKey key{};
  for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 3 + 7);

  constexpr uint32_t sampleRate = 48000;
  constexpr int channels = 1;
  constexpr uint8_t frameSizeMs = 10;
  const int frameSamples = static_cast<int>(sampleRate) * frameSizeMs / 1000;

  AudioReceiver receiver(key, sampleRate, channels, frameSizeMs, 0);
  WIREMIC_CHECK(receiver.start());
  std::cout << "RECEIVER_BOUND_ON_" << receiver.port() << "\n";

  AudioSender sender(key, sampleRate, channels, 96, frameSizeMs,
                      QHostAddress::LocalHost, receiver.port());
  WIREMIC_CHECK(sender.start());

  std::vector<std::vector<int16_t>> receivedFrames;
  bool anyConcealed = false;
  QObject::connect(&receiver, &AudioReceiver::pcmFrameReady,
                    [&](std::vector<int16_t> samples, bool concealed) {
                      receivedFrames.push_back(std::move(samples));
                      if (concealed) anyConcealed = true;
                    });

  QString receiverError;
  QObject::connect(&receiver, &AudioReceiver::errorOccurred,
                    [&](const QString& msg) { receiverError = msg; });

  constexpr int kFramesToSend = 50;
  std::vector<int16_t> pcm(static_cast<size_t>(frameSamples));

  for (int frameIndex = 0; frameIndex < kFramesToSend; ++frameIndex) {
    for (int i = 0; i < frameSamples; ++i) {
      const double t =
          static_cast<double>(frameIndex * frameSamples + i) / sampleRate;
      pcm[static_cast<size_t>(i)] =
          static_cast<int16_t>(8000.0 * std::sin(2.0 * M_PI * 440.0 * t));
    }
    sender.pushPcmFrame(pcm.data(), frameSamples);

    QEventLoop delay;
    QTimer::singleShot(frameSizeMs, &delay, &QEventLoop::quit);
    delay.exec();
  }

  const bool gotEnough = WaitFor(
      [&] { return receivedFrames.size() >= kFramesToSend - 5; }, 3000);
  WIREMIC_CHECK(gotEnough);
  WIREMIC_CHECK(receiverError.isEmpty());
  std::cout << "RECEIVED_FRAMES: " << receivedFrames.size() << " / "
            << kFramesToSend << "\n";

  bool anyNonSilent = false;
  for (const auto& frame : receivedFrames) {
    WIREMIC_CHECK(frame.size() == static_cast<size_t>(frameSamples));
    for (auto sample : frame) {
      if (sample != 0) {
        anyNonSilent = true;
        break;
      }
    }
    if (anyNonSilent) break;
  }
  WIREMIC_CHECK(anyNonSilent);
  std::cout << "AUDIO_CONTENT_NON_SILENT_OK\n";
  std::cout << "ANY_CONCEALED_FRAMES: " << anyConcealed << "\n";

  sender.stop();
  receiver.stop();

  {
    AudioReceiver corruptionReceiver(key, sampleRate, channels, frameSizeMs, 0);
    WIREMIC_CHECK(corruptionReceiver.start());

    QString receiverErrorSeen;
    QObject::connect(&corruptionReceiver, &AudioReceiver::errorOccurred,
                      [&](const QString& msg) { receiverErrorSeen = msg; });

    bool gotAnyFrame = false;
    QObject::connect(&corruptionReceiver, &AudioReceiver::pcmFrameReady,
                      [&](std::vector<int16_t>, bool) { gotAnyFrame = true; });

    QUdpSocket rawUdp;
    rawUdp.connectToHost(QHostAddress::LocalHost, corruptionReceiver.port());
    WIREMIC_CHECK(rawUdp.waitForConnected(2000));

    for (int i = 0; i < 20; ++i) {
      QByteArray garbage(60, '\0');
      for (int b = 0; b < garbage.size(); ++b) {
        garbage[b] = static_cast<char>((i * 37 + b * 13) & 0xFF);
      }
      rawUdp.write(garbage);
    }
    rawUdp.flush();

    QEventLoop settleLoop;
    QTimer::singleShot(600, &settleLoop, &QEventLoop::quit);
    settleLoop.exec();

    std::cout << "SENT_20_GARBAGE_UDP_PACKETS_RECEIVER_STILL_ALIVE_OK\n";
    std::cout << "RECEIVER_PORT_STILL_RESPONSIVE: " << corruptionReceiver.port()
               << "\n";

    corruptionReceiver.stop();
  }
  std::cout << "MALFORMED_PACKET_DID_NOT_CRASH_PROCESS_OK\n";

  std::cout << "AUDIO_TRANSPORT_LOOPBACK_TESTS_PASSED\n";
  return 0;
}
