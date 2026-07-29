#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "AudioReceiver.hpp"
#include "AudioSender.hpp"
#include "Check.hpp"

using namespace wiremic::audio;

namespace {

constexpr int kBudgetMs = 250;

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
  poll.start(1);

  if (!predicate()) loop.exec();
  return predicate();
}

int16_t PeakOf(const std::vector<int16_t>& samples) {
  int16_t peak = 0;
  for (const auto sample : samples) {
    const int16_t magnitude =
        static_cast<int16_t>(sample < 0 ? -sample : sample);
    if (magnitude > peak) peak = magnitude;
  }
  return peak;
}

}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

  SessionKey key{};
  for (size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<uint8_t>(i * 7 + 3);
  }

  constexpr uint32_t sampleRate = 48000;
  constexpr int channels = 1;
  constexpr uint8_t frameSizeMs = 10;
  const int frameSamples = static_cast<int>(sampleRate) * frameSizeMs / 1000;

  AudioReceiver receiver(key, sampleRate, channels, frameSizeMs, 0);
  WIREMIC_CHECK(receiver.start());

  AudioSender sender(key, sampleRate, channels, 96, frameSizeMs,
                      QHostAddress::LocalHost, receiver.port());
  WIREMIC_CHECK(sender.start());

  std::chrono::steady_clock::time_point burstSentAt;
  std::chrono::steady_clock::time_point burstHeardAt;
  bool heardBurst = false;

  QObject::connect(&receiver, &AudioReceiver::pcmFrameReady,
                    [&](std::vector<int16_t> samples, bool) {
                      if (heardBurst) return;
                      if (PeakOf(samples) > 4000) {
                        burstHeardAt = std::chrono::steady_clock::now();
                        heardBurst = true;
                      }
                    });

  std::vector<int16_t> silence(frameSamples, 0);
  std::vector<int16_t> burst(frameSamples);
  for (int i = 0; i < frameSamples; ++i) {
    burst[i] = static_cast<int16_t>(
        12000.0 * std::sin(2.0 * M_PI * 440.0 * i / sampleRate));
  }

  // Prime the pipeline with silence so the measurement reflects steady-state
  // latency rather than the initial jitter-buffer fill.
  for (int i = 0; i < 40; ++i) {
    sender.pushPcmFrame(silence.data(), frameSamples);
    WaitFor([] { return false; }, frameSizeMs);
  }

  WIREMIC_CHECK(!heardBurst);

  burstSentAt = std::chrono::steady_clock::now();
  for (int i = 0; i < 5; ++i) {
    sender.pushPcmFrame(burst.data(), frameSamples);
  }

  const bool arrived = WaitFor([&] { return heardBurst; }, 3000);
  WIREMIC_CHECK(arrived);

  const auto latencyMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(burstHeardAt -
                                                             burstSentAt)
          .count();

  std::cout << "END_TO_END_LATENCY_MS=" << latencyMs << " (budget "
            << kBudgetMs << ")\n";
  WIREMIC_CHECK(latencyMs < kBudgetMs);

  sender.stop();
  receiver.stop();

  std::cout << "AUDIO_LATENCY_TESTS_PASSED\n";
  return 0;
}
