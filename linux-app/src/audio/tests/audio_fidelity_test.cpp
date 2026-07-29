#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "AudioPacketCodec.hpp"
#include "Check.hpp"
#include "OpusCodec.hpp"

using namespace wiremic::audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr double kToneHz = 440.0;
constexpr double kAmplitude = 10000.0;

std::vector<int16_t> MakeTone(size_t samples) {
  std::vector<int16_t> pcm(samples);
  for (size_t i = 0; i < samples; ++i) {
    pcm[i] = static_cast<int16_t>(
        kAmplitude * std::sin(2.0 * M_PI * kToneHz * static_cast<double>(i) /
                               kSampleRate));
  }
  return pcm;
}

double Rms(const std::vector<int16_t>& pcm, size_t from) {
  if (pcm.size() <= from) return 0.0;
  double sum = 0.0;
  for (size_t i = from; i < pcm.size(); ++i) {
    sum += static_cast<double>(pcm[i]) * pcm[i];
  }
  return std::sqrt(sum / static_cast<double>(pcm.size() - from));
}

double DominantHz(const std::vector<int16_t>& pcm, size_t from) {
  size_t crossings = 0;
  for (size_t i = from + 1; i < pcm.size(); ++i) {
    if ((pcm[i - 1] < 0) != (pcm[i] < 0)) ++crossings;
  }
  const double seconds =
      static_cast<double>(pcm.size() - from) / kSampleRate;
  if (seconds <= 0.0) return 0.0;
  return static_cast<double>(crossings) / (2.0 * seconds);
}

bool RunMode(uint8_t frameSizeMs, int bitrateKbps) {
  const int frameSamples =
      static_cast<int>(kSampleRate) * frameSizeMs / 1000;

  OpusFrameEncoder encoder(kSampleRate, 1, bitrateKbps);
  OpusFrameDecoder decoder(kSampleRate, 1);

  const size_t totalFrames = 100;
  const auto input = MakeTone(static_cast<size_t>(frameSamples) * totalFrames);

  SessionKey key{};
  for (size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<uint8_t>(i * 5 + 1);
  }

  std::vector<int16_t> output;
  output.reserve(input.size());

  std::chrono::nanoseconds cryptoTime{0};
  uint64_t sequence = 0;

  for (size_t f = 0; f < totalFrames; ++f) {
    const int16_t* frame = input.data() + f * frameSamples;

    const auto payload = encoder.Encode(frame, frameSamples);
    if (payload.empty()) continue;

    const auto cryptoStart = std::chrono::steady_clock::now();
    const auto packet = AudioPacketCodec::Encrypt(key, sequence++, 0, false,
                                                   false, payload);
    const auto decrypted = AudioPacketCodec::Decrypt(key, packet.data(),
                                                      packet.size());
    cryptoTime += std::chrono::steady_clock::now() - cryptoStart;

    if (!decrypted) return false;

    auto pcm = decoder.Decode(decrypted->opusPayload.data(),
                               decrypted->opusPayload.size(), frameSamples);
    output.insert(output.end(), pcm.begin(), pcm.end());
  }

  // Opus needs a few frames to converge; ignore the leading transient.
  const size_t skip = static_cast<size_t>(frameSamples) * 10;
  const double inputRms = Rms(input, skip);
  const double outputRms = Rms(output, skip);
  const double outputHz = DominantHz(output, skip);
  const double ratio = inputRms > 0.0 ? outputRms / inputRms : 0.0;

  const double cryptoUsPerFrame =
      std::chrono::duration<double, std::micro>(cryptoTime).count() /
      static_cast<double>(totalFrames);

  std::cout << "  frame=" << static_cast<int>(frameSizeMs) << "ms"
            << " bitrate=" << bitrateKbps << "k"
            << " outHz=" << static_cast<int>(outputHz + 0.5)
            << " rmsRatio=" << ratio
            << " crypto=" << cryptoUsPerFrame << "us/frame\n";

  if (output.size() != input.size()) {
    std::cout << "    SIZE MISMATCH in=" << input.size()
              << " out=" << output.size() << "\n";
    return false;
  }
  if (std::abs(outputHz - kToneHz) > 15.0) return false;
  if (ratio < 0.7 || ratio > 1.4) return false;

  // Encryption must not be a meaningful share of a frame's time budget.
  if (cryptoUsPerFrame > 200.0) return false;

  return true;
}

}

int main() {
  std::cout << "AUDIO FIDELITY ACROSS LATENCY MODES\n";

  bool allOk = true;
  for (const uint8_t frameSizeMs : {5, 10, 20}) {
    for (const int bitrate : {32, 64, 96, 128}) {
      if (!RunMode(frameSizeMs, bitrate)) {
        std::cout << "    FAILED at frame=" << static_cast<int>(frameSizeMs)
                  << "ms bitrate=" << bitrate << "k\n";
        allOk = false;
      }
    }
  }

  WIREMIC_CHECK(allOk);
  std::cout << "AUDIO_FIDELITY_TESTS_PASSED\n";
  return 0;
}
