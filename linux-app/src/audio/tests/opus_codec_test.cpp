#include "Check.hpp"

#include <cmath>
#include <iostream>
#include <vector>

#include "OpusCodec.hpp"

using namespace wiremic::audio;

namespace {

std::vector<int16_t> GenerateSineWave(int sampleRate, int frameSamples,
                                       double frequencyHz, double amplitude) {
  std::vector<int16_t> samples(static_cast<size_t>(frameSamples));
  for (int i = 0; i < frameSamples; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const double value = amplitude * std::sin(2.0 * M_PI * frequencyHz * t);
    samples[static_cast<size_t>(i)] = static_cast<int16_t>(value);
  }
  return samples;
}

double RmsOf(const std::vector<int16_t>& samples) {
  double sumSquares = 0.0;
  for (auto sample : samples) {
    sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
  }
  return std::sqrt(sumSquares / static_cast<double>(samples.size()));
}

}  // namespace

int main() {
  constexpr uint32_t kSampleRate = 48000;
  constexpr int kChannels = 1;
  constexpr int kFrameSamples = 480;

  OpusFrameEncoder encoder(kSampleRate, kChannels, 64);
  OpusFrameDecoder decoder(kSampleRate, kChannels);

  const auto tone = GenerateSineWave(kSampleRate, kFrameSamples, 1000.0, 12000.0);
  const double inputRms = RmsOf(tone);
  WIREMIC_CHECK(inputRms > 1000.0);

  const auto encoded = encoder.Encode(tone.data(), kFrameSamples);
  WIREMIC_CHECK(!encoded.empty());
  WIREMIC_CHECK(encoded.size() < tone.size() * sizeof(int16_t));
  std::cout << "OPUS_ENCODE_OK bytes=" << encoded.size() << "\n";

  const auto decoded = decoder.Decode(encoded.data(), encoded.size(), kFrameSamples);
  WIREMIC_CHECK(decoded.size() == static_cast<size_t>(kFrameSamples));

  const double outputRms = RmsOf(decoded);
  WIREMIC_CHECK(outputRms > inputRms * 0.5);
  WIREMIC_CHECK(outputRms < inputRms * 1.5);
  std::cout << "OPUS_DECODE_ENERGY_PRESERVED_OK input_rms=" << inputRms
            << " output_rms=" << outputRms << "\n";

  bool anyNonZero = false;
  for (auto sample : decoded) {
    if (sample != 0) {
      anyNonZero = true;
      break;
    }
  }
  WIREMIC_CHECK(anyNonZero);
  std::cout << "OPUS_DECODE_NONSILENT_OK\n";

  const auto concealed = decoder.DecodePacketLoss(kFrameSamples);
  WIREMIC_CHECK(concealed.size() == static_cast<size_t>(kFrameSamples));
  std::cout << "OPUS_PLC_PRODUCES_FRAME_OK\n";

  for (int i = 0; i < 20; ++i) {
    const auto nextTone =
        GenerateSineWave(kSampleRate, kFrameSamples, 440.0, 8000.0);
    const auto enc = encoder.Encode(nextTone.data(), kFrameSamples);
    const auto dec = decoder.Decode(enc.data(), enc.size(), kFrameSamples);
    WIREMIC_CHECK(dec.size() == static_cast<size_t>(kFrameSamples));
  }
  std::cout << "OPUS_STREAM_MULTIPLE_FRAMES_OK\n";

  std::cout << "OPUS_CODEC_TESTS_PASSED\n";
  return 0;
}
